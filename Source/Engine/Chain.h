// Cadeia compilada: os módulos de um ChainSpec prontos para rodar em tempo real.
#pragma once

#include <memory>
#include <vector>

#include "Catalog.h"
#include "ChainSpec.h"
#include "Dsp.h"
#include "Modules.h"

namespace mm
{
class Chain
{
public:
    // Monta a partir de um spec JÁ validado. Aloca: chame fora da thread de áudio.
    explicit Chain (const ChainSpec& validated);

    // Aloca os buffers. Chame fora da thread de áudio, antes de process().
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Processa no lugar. `macros` tem kMaxMacros posições de 0 a 1. `sidechain` (opcional) é o sinal que
    // controla os compressores com a opção sidechain ligada, alinhado com `channels`.
    // Seguro para a thread de áudio: não aloca nem bloqueia.
    void process (float* const* channels, int numChannels, int numSamples,
                  const float* macros, float mix, float outputGainDb,
                  const float* const* sidechain = nullptr, int sidechainChannels = 0);

    void reset();
    bool empty() const { return slots.empty(); }
    // Atraso total da cadeia em amostras (o som seco da mistura é atrasado igual). Válido depois de prepare().
    int latencySamples() const { return latencia; }

    static constexpr float kSafetyCeilingDb = -1.0f;

private:
    struct Slot
    {
        std::unique_ptr<Module> module;
        float params[kMaxParams] = {};
        bool dirty = true;
    };

    struct Binding
    {
        int macro = 0, slot = 0, param = 0;
        float valueAt0 = 0.0f, valueAt1 = 0.0f;
        bool log = false;
    };

    void applyMacros (const float* macros);

    std::vector<Slot> slots;
    std::vector<Binding> bindings;
    std::vector<float> dry[2];
    dsp::DelayLine dryAtraso[2];
    int latencia = 0;
    float macroNow[kMaxMacros] = {};
    float macroApplied[kMaxMacros] = {};
    bool macrosPrimed = false;
    float macroCoefficient = 0.0f;
    dsp::Smoothed mixSmoothed, gainSmoothed;
    float limiterEnvelope = 0.0f, limiterRelease = 0.0f;
    int maxBlock = 0;
    static constexpr int kChunk = 32;  // os macros atualizam os módulos a cada 32 amostras
};
}
