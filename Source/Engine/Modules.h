// Módulos DSP em tempo real. Um por tipo do catálogo.
#pragma once

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
};

// nullptr se o tipo não existir no catálogo.
std::unique_ptr<Module> createModule (std::string_view type);
}
