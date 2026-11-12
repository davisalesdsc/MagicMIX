// Catálogo de módulos DSP: tipos, parâmetros e faixas.
// Os dados vêm de CatalogData.inc, gerado a partir do protótipo em Python.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "CatalogData.inc"

namespace mm
{
constexpr int kMaxModules = MM_MAX_MODULES;
constexpr int kMaxMacros = MM_MAX_MACROS;
constexpr int kMaxSuggestions = MM_MAX_SUGGESTIONS;
constexpr int kMaxParams = MM_MAX_PARAMS;  // maior número de parâmetros de um módulo

struct ParamDef
{
    const char* name;
    float min, max, def;
    const char* unit;
    const char* description;
    bool log;                        // frequências e tempos interpolam em escala logarítmica
    const char* labels = nullptr;    // escolhas nomeadas, separadas por | (o valor é o índice)
    const char* stepsText = nullptr; // degraus permitidos, separados por |

    std::vector<std::string> labelList;  // preenchidos a partir dos textos acima
    std::vector<float> steps;

    bool isChoice() const { return ! labelList.empty() && steps.empty(); }
    // Limita à faixa; em parâmetro de escolha arredonda para o índice, em degraus vai ao mais próximo.
    float clamp (float value) const;
    // Texto do valor para mostrar ao músico ("FET", "4:1", "Auto"), ou vazio se não houver rótulo.
    std::string labelFor (float value) const;
};

struct ModuleDef
{
    const char* type;
    const char* description;
    std::vector<ParamDef> params;

    int paramIndex (std::string_view name) const;  // -1 se não existir
};

const std::vector<ModuleDef>& catalog();
const ModuleDef* findModule (std::string_view type);

// Catálogo em texto, do jeito que é mostrado ao LLM (igual ao do protótipo).
std::string catalogText();
}
