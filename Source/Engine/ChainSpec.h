// ChainSpec: o contrato entre a IA e o motor de áudio (versão 2, igual à do protótipo).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace mm
{
constexpr int kSpecVersion = 2;

struct ModuleSpec
{
    std::string id, type;
    std::map<std::string, float> params;
    bool enabled = true;  // o músico pode desligar um módulo sem tirá-lo da cadeia
};

struct MacroTarget
{
    std::string moduleId, param;
    float valueAt0 = 0.0f;  // valor do parâmetro com o macro no mínimo
    float valueAt1 = 0.0f;  // valor do parâmetro com o macro no máximo
};

struct MacroSpec
{
    std::string name, description;
    float value = 0.5f;  // posição do botão, de 0 a 1
    std::vector<MacroTarget> targets;
};

struct ChainSpec
{
    int version = kSpecVersion;
    std::string title, summary, details;
    std::vector<std::string> prompts;      // a conversa: um pedido por revisão
    std::vector<std::string> suggestions;  // próximos pedidos sugeridos
    std::vector<ModuleSpec> modules;
    std::vector<MacroSpec> macros;
    // Controles do músico sobre a cadeia inteira; o LLM não mexe neles.
    float mix = 1.0f;
    float outputGainDb = 0.0f;
};

// Medidas da pista. Vão para o LLM e para os limites que dependem do sinal.
struct Resonance
{
    int freqHz = 0;
    float prominenceDb = 0.0f;
};

struct Features
{
    bool valid = false;
    double durationS = 0.0;
    int sampleRate = 0, channels = 0;
    float peakDbfs = -120.0f, rmsDbfs = -120.0f, crestFactorDb = 0.0f;
    int spectralCentroidHz = 0;
    float bandEnergyDb[6] = {};  // sub, low, low_mid, mid, presence, air
    std::vector<Resonance> resonances;
    bool hasCorrelation = false;
    float stereoCorrelation = 1.0f;
};
}
