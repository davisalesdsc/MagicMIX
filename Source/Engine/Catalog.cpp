#include "Catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mm
{
float ParamDef::clamp (float value) const
{
    if (! std::isfinite (value))
        return def;
    return std::min (std::max (value, min), max);
}

int ModuleDef::paramIndex (std::string_view name) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (name == params[i].name)
            return (int) i;
    return -1;
}

const std::vector<ModuleDef>& catalog()
{
    static const std::vector<ModuleDef> modules = {
#define MM_CATALOG_ENTRIES
#include "CatalogData.inc"
#undef MM_CATALOG_ENTRIES
    };
    return modules;
}

const ModuleDef* findModule (std::string_view type)
{
    for (auto& m : catalog())
        if (type == m.type)
            return &m;
    return nullptr;
}

// Mesmo formato de número do "%g" do Python, para o texto bater com o do protótipo.
static std::string number (float v)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%g", (double) v);
    return buffer;
}

std::string catalogText()
{
    std::string text;
    for (auto& m : catalog())
    {
        if (! text.empty())
            text += "\n";
        text += std::string ("- ") + m.type + ": " + m.description;
        for (auto& p : m.params)
        {
            const std::string unit = p.unit[0] != 0 ? std::string (" ") + p.unit : std::string();
            text += std::string ("\n    ") + p.name + ": " + number (p.min) + " a " + number (p.max) + unit
                    + " (padrão " + number (p.def) + (p.log ? ", escala log" : "") + ") - " + p.description;
        }
    }
    return text;
}
}
