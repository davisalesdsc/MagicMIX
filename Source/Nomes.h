// Nomes em português dos módulos e parâmetros, para a interface e para as respostas do cérebro.
#pragma once

#include <juce_core/juce_core.h>

#include "Engine/ChainSpec.h"

namespace mm
{
juce::String nomeDoModulo (const ModuleSpec& m);           // "Compressor FET", "EQ", "Ressonâncias"...
juce::String rotuloDoParametro (const std::string& nome);  // "Limite", "Taxa"...
juce::String descreverCadeia (const ChainSpec& spec);      // "Volume de trabalho → EQ → Compressor FET"
}
