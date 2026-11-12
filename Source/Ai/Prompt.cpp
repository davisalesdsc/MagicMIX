#include "Prompt.h"

#include <BinaryData.h>

#include "../Engine/Catalog.h"
#include "../Texto.h"
#include "SpecJson.h"

namespace mm::prompt
{
namespace
{
struct Style { const char* name; const char* text; };
const Style kStyles[] = {
#include "Estilos.inc"
};

juce::var parse (const juce::String& jsonText)
{
    juce::var out;
    juce::JSON::parse (jsonText, out);
    return out;
}

// Esquema da resposta: o mesmo LLMChain do protótipo.
juce::var responseSchema()
{
    juce::StringArray types;
    for (auto& m : catalog())
        types.add (juce::String (m.type).quoted());

    const juce::String schema = juce::String (R"({
      "type": "object", "additionalProperties": false,
      "required": ["title", "summary", "details", "suggestions", "modules", "macros"],
      "properties": {
        "title": {"type": "string"}, "summary": {"type": "string"}, "details": {"type": "string"},
        "suggestions": {"type": "array", "items": {"type": "string"}},
        "modules": {"type": "array", "items": {
          "type": "object", "additionalProperties": false, "required": ["id", "type", "params"],
          "properties": {
            "id": {"type": "string"},
            "type": {"enum": [@@TYPES@@]},
            "params": {"type": "array", "items": {
              "type": "object", "additionalProperties": false, "required": ["name", "value"],
              "properties": {"name": {"type": "string"}, "value": {"type": "number"}}}}}}},
        "macros": {"type": "array", "items": {
          "type": "object", "additionalProperties": false, "required": ["name", "description", "value", "targets"],
          "properties": {
            "name": {"type": "string"}, "description": {"type": "string"}, "value": {"type": "number"},
            "targets": {"type": "array", "items": {
              "type": "object", "additionalProperties": false,
              "required": ["module_id", "param", "value_at_0", "value_at_1"],
              "properties": {"module_id": {"type": "string"}, "param": {"type": "string"},
                             "value_at_0": {"type": "number"}, "value_at_1": {"type": "number"}}}}}}}
      }
    })").replace ("@@TYPES@@", types.joinIntoString (", "));
    return parse (schema);
}
}

juce::StringArray styleNames()
{
    juce::StringArray names;
    for (auto& s : kStyles)
        names.add (s.name);
    return names;
}

juce::String systemPrompt()
{
    const auto text = juce::String::fromUTF8 (BinaryData::PromptSistema_txt, BinaryData::PromptSistema_txtSize);
    return text.replace ("@@CATALOGO@@", tx (catalogText()));
}

juce::String userMessage (const juce::String& request, const juce::var& features, bool hasFeatures,
                          const ChainSpec* current, const juce::String& style)
{
    juce::StringArray parts;
    const auto analysis = hasFeatures ? juce::JSON::toString (features) : tx ("indisponível");
    parts.add ("<analise_da_pista>\n" + analysis + "\n</analise_da_pista>");

    if (current != nullptr)
    {
        juce::StringArray previous;
        for (auto& p : current->prompts)
            previous.add ("- " + tx (p));
        if (previous.isEmpty())
            previous.add (tx ("- (não registrados)"));
        parts.add ("<pedidos_anteriores>\n" + previous.joinIntoString ("\n") + "\n</pedidos_anteriores>");
        parts.add ("<cadeia_atual>\n" + juce::JSON::toString (json::toLlmVar (*current)) + "\n</cadeia_atual>");
    }

    const Style* chosen = &kStyles[0];
    for (auto& s : kStyles)
        if (style == s.name)
            chosen = &s;
    parts.add ("<estilo>\n" + juce::String (chosen->name) + ": " + tx (chosen->text) + "\n</estilo>");
    parts.add ("<pedido>\n" + request.trim() + "\n</pedido>");
    return parts.joinIntoString ("\n\n");
}

juce::String requestBody (const juce::String& model, const juce::String& effort,
                          const juce::String& system, const juce::String& user)
{
    auto format = new juce::DynamicObject();
    format->setProperty ("type", "json_schema");
    format->setProperty ("schema", responseSchema());

    auto outputConfig = new juce::DynamicObject();
    outputConfig->setProperty ("effort", effort);
    outputConfig->setProperty ("format", format);

    auto cache = new juce::DynamicObject();
    cache->setProperty ("type", "ephemeral");
    auto systemBlock = new juce::DynamicObject();
    systemBlock->setProperty ("type", "text");
    systemBlock->setProperty ("text", system);
    systemBlock->setProperty ("cache_control", cache);

    auto message = new juce::DynamicObject();
    message->setProperty ("role", "user");
    message->setProperty ("content", user);

    auto body = new juce::DynamicObject();
    body->setProperty ("model", model);
    body->setProperty ("max_tokens", 16000);
    // Se um classificador de segurança recusar o pedido, a API tenta de novo num modelo alternativo.
    body->setProperty ("fallbacks", "default");
    body->setProperty ("output_config", outputConfig);
    body->setProperty ("system", juce::Array<juce::var> { juce::var (systemBlock) });
    body->setProperty ("messages", juce::Array<juce::var> { juce::var (message) });
    return juce::JSON::toString (juce::var (body), true);
}

bool parseResponse (const juce::String& responseText, ChainSpec& out, juce::String& error)
{
    const auto response = parse (responseText);
    if (! response.isObject())
    {
        error = tx ("a resposta do serviço não é JSON");
        return false;
    }
    if (response["type"].toString() == "error")
    {
        error = tx ("o serviço respondeu com erro: ") + response["error"]["message"].toString();
        return false;
    }
    const auto stopReason = response["stop_reason"].toString();
    if (stopReason == "refusal")
    {
        error = tx ("o modelo recusou o pedido");
        return false;
    }
    if (stopReason == "max_tokens")
    {
        error = tx ("a resposta foi cortada antes de terminar");
        return false;
    }
    if (auto* content = response["content"].getArray())
        for (auto& block : *content)
            if (block["type"].toString() == "text")
                return json::fromString (block["text"].toString(), out, error);

    error = tx ("a resposta não veio no formato esperado");
    return false;
}
}
