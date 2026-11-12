// Montagem do pedido ao LLM e leitura da resposta. Mesmo conteúdo do protótipo.
#pragma once

#include <juce_core/juce_core.h>

#include "../Engine/ChainSpec.h"

namespace mm::prompt
{
juce::StringArray styleNames();      // artistico, tecnico, didatico
juce::String systemPrompt();

// `current` nulo = pedido novo; senão, revisão da cadeia existente.
juce::String userMessage (const juce::String& request, const juce::var& features, bool hasFeatures,
                          const ChainSpec* current, const juce::String& style);

// Corpo JSON de POST /v1/messages, com saída estruturada no formato da cadeia.
juce::String requestBody (const juce::String& model, const juce::String& effort,
                          const juce::String& system, const juce::String& user);

// Lê a resposta da API. Em caso de erro devolve false e explica em `error`.
bool parseResponse (const juce::String& responseText, ChainSpec& out, juce::String& error);
}
