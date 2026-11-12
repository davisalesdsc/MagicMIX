// Módulos DSP em tempo real. Um por tipo do catálogo.
#pragma once

#include <atomic>
#include <memory>
#include <string_view>

namespace mm
{
class Module
{
public:
    virtual ~Module() = default;

    // Aloca o que precisar. Chamado fora da thread de áudio.
    virtual void prepare (double sampleRate, int numChannels) = 0;
    // Parâmetros na ordem do catálogo. Seguro para a thread de áudio.
    virtual void setParams (const float* params) = 0;
    // Processa no lugar. numChannels é 1 ou 2.
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;
    virtual void reset() {}
    // Atraso que o módulo introduz, em amostras (válido depois de prepare()).
    virtual int latencySamples() const { return 0; }

    // Sinal de sidechain alinhado com o próximo process() (nullptr = nenhum). A cadeia define antes de cada pedaço.
    void setSidechain (const float* const* channels, int numChannels)
    {
        sidechain = channels;
        sidechainChannels = numChannels;
    }

    // ---- medidores para a interface (escritos pela thread de áudio, lidos por qualquer thread)
    // Quanto o módulo está reduzindo agora, em dB (0 = nada). Compressores e de-esser.
    float gainReductionDb() const { return medidorReducao.load (std::memory_order_relaxed); }
    // Redução por frequência, em dB, para o supressor. Devolve quantos pontos escreveu e a largura de cada um em Hz.
    virtual int reductionCurve (float* /*outDb*/, int /*maxPoints*/, float& binHz) const { binHz = 0.0f; return 0; }

protected:
    void publishReduction (float db) { medidorReducao.store (db, std::memory_order_relaxed); }
    std::atomic<float> medidorReducao { 0.0f };

    const float* const* sidechain = nullptr;
    int sidechainChannels = 0;
};

// nullptr se o tipo não existir no catálogo.
std::unique_ptr<Module> createModule (std::string_view type);
}
