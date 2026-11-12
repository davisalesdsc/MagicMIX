// Conversão entre ChainSpec e JSON.
#pragma once

#include <juce_core/juce_core.h>

#include "../Engine/ChainSpec.h"

namespace mm::json
{
// Formato canônico (o mesmo que o protótipo grava em disco).
juce::var toVar (const ChainSpec& spec);
juce::String toString (const ChainSpec& spec, bool singleLine = false);

// Aceita o formato canônico (versões 1 e 2) e a resposta crua do LLM (parâmetros em lista).
bool fromVar (const juce::var& data, ChainSpec& out, juce::String& error);
bool fromString (const juce::String& text, ChainSpec& out, juce::String& error);

// Cadeia no formato que o LLM lê e devolve, usada nas revisões.
juce::var toLlmVar (const ChainSpec& spec);

// Medidas da pista como o LLM as recebe. `bpm` <= 0 e `trackName` vazio ficam de fora.
juce::var featuresToVar (const Features& features, double bpm, const juce::String& trackName);
}
