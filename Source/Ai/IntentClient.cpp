#include "IntentClient.h"

#include "../Texto.h"
#include "Prompt.h"
#include "SpecJson.h"

namespace mm
{
IntentClient::IntentClient() : juce::Thread ("MagicMIX IA") {}

IntentClient::~IntentClient()
{
    *alive = false;
    stopThread (3000);
}

void IntentClient::start (IntentRequest newRequest, std::function<void (IntentResult)> onDone)
{
    if (isThreadRunning())
        return;
    request = std::move (newRequest);
    callback = std::move (onDone);
    startThread();
}

void IntentClient::run()
{
    IntentResult result;

    const auto features = json::featuresToVar (request.features, request.bpm, request.trackName);
    const bool hasFeatures = request.features.valid || request.bpm > 0.0 || request.trackName.isNotEmpty();
    const auto user = prompt::userMessage (request.prompt, features, hasFeatures,
                                           request.current ? &*request.current : nullptr, request.style);
    const auto body = prompt::requestBody (request.model, request.effort, prompt::systemPrompt(), user);

    juce::String headers = "Content-Type: application/json\r\n"
                           "anthropic-version: 2026-10-21\r\n"
                           "anthropic-beta: server-side-fallback-2026-07-01\r\n";
    if (request.apiKey.isNotEmpty())
        headers += "x-api-key: " + request.apiKey + "\r\n";

    int status = 0;
    const auto url = juce::URL (request.endpoint).withPOSTData (body);
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                             .withExtraHeaders (headers)
                                             .withConnectionTimeoutMs (120000)
                                             .withStatusCode (&status)
                                             .withHttpRequestCmd ("POST"));
    if (stream == nullptr)
    {
        result.error = tx ("não foi possível conectar ao serviço de IA. Confira a internet e o endereço nas configurações.");
    }
    else
    {
        const auto response = stream->readEntireStreamAsString();
        if (threadShouldExit())
            return;
        if (status == 401 || status == 403)
            result.error = tx ("a chave da API foi recusada. Confira a chave nas configurações.");
        else if (prompt::parseResponse (response, result.spec, result.error))
        {
            result.ok = true;
            // O histórico de pedidos é nosso: o que vier na resposta é ignorado.
            result.spec.prompts = request.current ? request.current->prompts : std::vector<std::string>();
            result.spec.prompts.push_back (request.prompt.trim().toStdString());
            if (request.current)
            {
                // Mistura e ganho da cadeia são do músico; a revisão não mexe neles.
                result.spec.mix = request.current->mix;
                result.spec.outputGainDb = request.current->outputGainDb;
            }
        }
        else if (status >= 400 && result.error.isEmpty())
            result.error = tx ("o serviço respondeu com erro ") + juce::String (status);
    }

    juce::MessageManager::callAsync ([flag = alive, done = callback, result]() mutable {
        if (*flag && done)
            done (std::move (result));
    });
}
}
