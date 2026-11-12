// Cadeia compilada: os módulos de um ChainSpec prontos para rodar em tempo real.
#pragma once

#include <array>
#include <atomic>
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

    // ---- edição ao vivo pela interface (sem remontar a cadeia)
    // `param` é o índice no catálogo; kParamLigado liga (1) ou desliga (0) o módulo inteiro.
    // Com `rebind`, os extremos do macro que controla o parâmetro passam a ser `at0` e `at1`.
    struct Edit
    {
        int slot = 0, param = 0;
        float value = 0.0f;
        bool rebind = false;
        float at0 = 0.0f, at1 = 0.0f;
    };
    static constexpr int kParamLigado = -1;
    // Só uma thread pode enfileirar (a de mensagens). Devolve false se a fila estiver cheia.
    bool queueEdit (const Edit& edit);

    // ---- medidores (escritos pela thread de áudio, lidos pela interface)
    int numSlots() const { return (int) slots.size(); }
    const Module* moduleAt (int slot) const { return slot >= 0 && slot < (int) slots.size() ? slots[(size_t) slot].module.get() : nullptr; }
    float slotInputPeak (int slot) const { return slot >= 0 && slot < kMaxModules ? picoEntrada[(size_t) slot].load (std::memory_order_relaxed) : 0.0f; }
    float slotOutputPeak (int slot) const { return slot >= 0 && slot < kMaxModules ? picoSaida[(size_t) slot].load (std::memory_order_relaxed) : 0.0f; }
    // Atraso total da cadeia em amostras (o som seco da mistura é atrasado igual). Válido depois de prepare().
    int latencySamples() const { return latencia; }

    static constexpr float kSafetyCeilingDb = -1.0f;

private:
    struct Slot
    {
        std::unique_ptr<Module> module;
        float params[kMaxParams] = {};
        bool dirty = true;
        bool enabled = true;
        int atraso = 0;              // atraso do módulo: desligado, o som passa por este desvio para não sair do alinhamento
        dsp::DelayLine desvio[2];
    };

    struct Binding
    {
        int macro = 0, slot = 0, param = 0;
        float valueAt0 = 0.0f, valueAt1 = 0.0f;
        bool log = false;
    };

    void applyMacros (const float* macros);
    void applyEdits();

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
    std::array<Edit, 256> fila {};
    std::atomic<int> filaEscrita { 0 }, filaLeitura { 0 };
    std::array<std::atomic<float>, kMaxModules> picoEntrada {}, picoSaida {};
    float medidorQueda = 0.0f;
    static constexpr int kChunk = 32;  // os macros atualizam os módulos a cada 32 amostras
};
}
