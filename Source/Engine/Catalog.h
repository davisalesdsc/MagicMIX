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
constexpr int kMaxParams = 8;  // maior número de parâmetros de um módulo

struct ParamDef
{
    const char* name;
    float min, max, def;
    const char* unit;
    const char* description;
    bool log;  // frequências e tempos interpolam em escala logarítmica

    float clamp (float value) const;
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
