// Chamada ao serviço de IA numa thread própria. O resultado volta na thread de mensagens.
#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <juce_events/juce_events.h>

#include "../Engine/ChainSpec.h"

namespace mm
{
struct IntentRequest
{
    juce::String prompt, style, trackName;
    double bpm = 0.0;
    Features features;
    std::optional<ChainSpec> current;  // presente = revisão da cadeia existente
    juce::String apiKey, endpoint, model, effort;
};

struct IntentResult
{
    bool ok = false;
    ChainSpec spec;      // ainda NÃO validado
    juce::String error;
};

class IntentClient : private juce::Thread
{
public:
    IntentClient();
    ~IntentClient() override;

    bool isBusy() const { return isThreadRunning(); }
    // Ignorado se já houver um pedido em andamento.
    void start (IntentRequest request, std::function<void (IntentResult)> onDone);

private:
    void run() override;

    IntentRequest request;
    std::function<void (IntentResult)> callback;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
}
