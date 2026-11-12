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
    value = std::min (std::max (value, min), max);
    if (! steps.empty())
    {
        float best = steps.front();
        for (float s : steps)
            if (std::abs (s - value) < std::abs (best - value))
                best = s;
        return best;
    }
    if (! labelList.empty())
        return std::round (value);
    return value;
}

std::string ParamDef::labelFor (float value) const
{
    if (labelList.empty())
        return {};
    const float v = clamp (value);
    size_t index = (size_t) v;
    if (! steps.empty())
        for (size_t i = 0; i < steps.size(); ++i)
            if (steps[i] == v)
                index = i;
    return index < labelList.size() ? labelList[index] : std::string();
}

static std::vector<std::string> split (const char* text)
{
    std::vector<std::string> out;
    if (text == nullptr)
        return out;
    std::string current;
    for (const char* c = text; *c != 0; ++c)
    {
        if (*c == '|') { out.push_back (current); current.clear(); }
        else current += *c;
    }
    out.push_back (current);
    return out;
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
    static const std::vector<ModuleDef> modules = [] {
#if defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wmissing-field-initializers"  // listas e degraus são preenchidos logo abaixo
#endif
        std::vector<ModuleDef> list = {
#define MM_CATALOG_ENTRIES
#include "CatalogData.inc"
#undef MM_CATALOG_ENTRIES
        };
#if defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif
        for (auto& m : list)
            for (auto& p : m.params)
            {
                p.labelList = split (p.labels);
                for (auto& s : split (p.stepsText))
                    p.steps.push_back (std::stof (s));
            }
        return list;
    }();
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
            if (p.isChoice())
            {
                std::string options;
                for (size_t i = 0; i < p.labelList.size(); ++i)
                    options += (i ? ", " : "") + std::to_string (i) + " = " + p.labelList[i];
                text += std::string ("\n    ") + p.name + ": escolha (" + options + "; padrão " + number (p.def) + ") - " + p.description;
                continue;
            }
            std::string range = number (p.min) + " a " + number (p.max) + unit;
            if (! p.steps.empty())
            {
                range = "só os valores";
                for (size_t i = 0; i < p.steps.size(); ++i)
                    range += (i ? ", " : " ") + number (p.steps[i]) + (i < p.labelList.size() ? " (" + p.labelList[i] + ")" : "");
            }
            text += std::string ("\n    ") + p.name + ": " + range
                    + " (padrão " + number (p.def) + (p.log ? ", escala log" : "") + ") - " + p.description;
        }
    }
    return text;
}
}
