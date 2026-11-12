// Análise da pista com DSP clássico (sem IA). Mesmas medidas do protótipo.
#pragma once

#include "ChainSpec.h"

namespace mm
{
// `channels` aponta para 1 ou 2 canais com `numSamples` amostras cada.
// Aloca memória: chame fora da thread de áudio.
Features analyze (const float* const* channels, int numChannels, int numSamples, double sampleRate);
}
