// Nomes em português dos módulos e parâmetros, para a interface e para as respostas do cérebro.
#pragma once

#include <juce_core/juce_core.h>

#include "Engine/ChainSpec.h"

namespace mm
{
juce::String nomeDoModulo (const ModuleSpec& m);           // "Compressor FET", "EQ", "Ressonâncias"...
juce::String rotuloDoParametro (const std::string& nome);  // "Limite", "Taxa"...
juce::String descreverCadeia (const ChainSpec& spec);      // "Volume de trabalho → EQ → Compressor FET"

// Grupo de uma pista pelo nome dela na DAW: 0 bateria, 1 vozes, 2 baixo, 3 teclas, 4 harmonia, 5 outros.
int grupoDaPista (const juce::String& nome);
extern const char* const kGrupos[6];
}
