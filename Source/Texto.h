// Textos com acento precisam ser convertidos de UTF-8 de forma explícita:
// juce::String ("...") com caracteres fora do ASCII gera texto corrompido.
#pragma once

#include <juce_core/juce_core.h>

namespace mm
{
inline juce::String tx (const char* utf8) { return juce::String::fromUTF8 (utf8); }
inline juce::String tx (const std::string& utf8) { return juce::String::fromUTF8 (utf8.c_str()); }
}
