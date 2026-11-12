#include "Validate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "Catalog.h"

namespace mm
{
static std::string fmt (float v)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%g", (double) v);
    return buffer;
}

float interpolate (float valueAt0, float valueAt1, float position, bool log)
{
    position = std::min (std::max (position, 0.0f), 1.0f);
    if (log && valueAt0 > 0.0f && valueAt1 > 0.0f)
        return std::exp (std::log (valueAt0) + position * (std::log (valueAt1) - std::log (valueAt0)));
    return valueAt0 + position * (valueAt1 - valueAt0);
}

static ModuleSpec* findSpecModule (ChainSpec& spec, const std::string& id)
{
    for (auto& m : spec.modules)
        if (m.id == id)
            return &m;
    return nullptr;
}

// Move a ponta que não foi limitada para o macro continuar começando em `start`.
static void reanchor (MacroTarget& t, float position, float start, const ParamDef& param, int cappedEnd)
{
    const bool log = param.log && start > 0.0f && t.valueAt0 > 0.0f && t.valueAt1 > 0.0f;
    auto f = [log] (float v) { return log ? std::log (v) : v; };
    auto inverse = [log] (float v) { return log ? std::exp (v) : v; };

    if (cappedEnd == 1 && position < 1.0f)
        t.valueAt0 = param.clamp (inverse ((f (start) - position * f (t.valueAt1)) / (1.0f - position)));
    else if (cappedEnd == 0 && position > 0.0f)
        t.valueAt1 = param.clamp (inverse ((f (start) - (1.0f - position) * f (t.valueAt0)) / position));
}

// Limita por cima o valor do parâmetro e a faixa de qualquer macro que o controle,
// preservando o som de partida sempre que ele já estava abaixo do teto.
static void cap (ChainSpec& spec, ModuleSpec& module, const char* paramName, float ceiling,
                 const std::string& why, std::vector<std::string>& report)
{
    const auto* def = findModule (module.type);
    const auto& param = def->params[(size_t) def->paramIndex (paramName)];
    auto& value = module.params[paramName];

    if (value > ceiling)
    {
        report.push_back (module.id + "." + paramName + ": " + fmt (value) + " -> " + fmt (ceiling) + " (" + why + ")");
        value = ceiling;
    }

    for (auto& macro : spec.macros)
        for (auto& t : macro.targets)
        {
            if (t.moduleId != module.id || t.param != paramName || std::max (t.valueAt0, t.valueAt1) <= ceiling)
                continue;

            report.push_back ("macro '" + macro.name + "' -> " + module.id + "." + paramName + ": teto " + fmt (ceiling) + " (" + why + ")");
            const float start = std::min (interpolate (t.valueAt0, t.valueAt1, macro.value, param.log), ceiling);
            const bool both = t.valueAt0 > ceiling && t.valueAt1 > ceiling;
            const int cappedEnd = t.valueAt1 > ceiling ? 1 : 0;
            t.valueAt0 = std::min (t.valueAt0, ceiling);
            t.valueAt1 = std::min (t.valueAt1, ceiling);
            if (! both)
                reanchor (t, macro.value, start, param, cappedEnd);
        }
}

static void applySignalRules (ChainSpec& spec, const Features& f, std::vector<std::string>& report)
{
    // Fonte que já é brilhante recebe menos realce de agudos.
    if (f.spectralCentroidHz >= 3500)
        for (auto& m : spec.modules)
            if (m.type == "high_shelf" || (m.type == "peak" && m.params["freq_hz"] >= 2500.0f))
                cap (spec, m, "gain_db", MM_BRIGHT_SOURCE_MAX_BOOST_DB, "fonte já brilhante", report);

    // O ganho estático não pode empurrar o pico de entrada acima de -1 dBFS.
    const float available = std::max (0.0f, -1.0f - f.peakDbfs);
    for (auto& m : spec.modules)
        if (m.type == "gain")
            cap (spec, m, "gain_db", available, "headroom de " + fmt (std::round (available * 10.0f) / 10.0f) + " dB", report);

    // Material já muito comprimido não aguenta taxa alta.
    if (f.crestFactorDb < 8.0f)
        for (auto& m : spec.modules)
            if (m.type == "compressor")
                cap (spec, m, "ratio", 3.0f, "fonte já comprimida", report);
}

ChainSpec validate (const ChainSpec& spec, const Features* features, std::vector<std::string>& report)
{
    ChainSpec clean;
    clean.version = kSpecVersion;
    clean.title = spec.title;
    clean.summary = spec.summary;
    clean.details = spec.details;
    clean.prompts = spec.prompts;
    clean.suggestions = spec.suggestions;
    if ((int) clean.suggestions.size() > kMaxSuggestions)
        clean.suggestions.resize ((size_t) kMaxSuggestions);
    clean.mix = std::isfinite (spec.mix) ? std::min (std::max (spec.mix, 0.0f), 1.0f) : 1.0f;
    clean.outputGainDb = std::isfinite (spec.outputGainDb) ? std::min (std::max (spec.outputGainDb, -24.0f), 12.0f) : 0.0f;

    std::set<std::string> seen;
    for (auto& raw : spec.modules)
    {
        const auto* def = findModule (raw.type);
        if (def == nullptr)
        {
            report.push_back ("módulo '" + raw.id + "': tipo desconhecido '" + raw.type + "', removido");
            continue;
        }
        if ((int) clean.modules.size() >= kMaxModules)
        {
            report.push_back ("módulo '" + raw.id + "': excede o limite de " + std::to_string (kMaxModules) + " módulos, removido");
            continue;
        }
        if (raw.id.empty() || seen.count (raw.id) != 0)
        {
            report.push_back ("módulo '" + raw.id + "': id vazio ou repetido, removido");
            continue;
        }
        seen.insert (raw.id);

        ModuleSpec module { raw.id, raw.type, {}, raw.enabled };
        for (auto& [name, value] : raw.params)
        {
            (void) value;
            if (def->paramIndex (name) < 0)
                report.push_back (raw.id + "." + name + ": parâmetro inexistente em '" + raw.type + "', ignorado");
        }
        for (auto& p : def->params)
        {
            const auto found = raw.params.find (p.name);
            if (found == raw.params.end())
            {
                module.params[p.name] = p.def;
                continue;
            }
            const float safe = p.clamp (found->second);
            if (safe != found->second)
                report.push_back (raw.id + "." + p.name + ": " + fmt (found->second) + " -> " + fmt (safe)
                                  + " (faixa " + fmt (p.min) + " a " + fmt (p.max) + ")");
            module.params[p.name] = safe;
        }
        clean.modules.push_back (std::move (module));
    }

    std::set<std::pair<std::string, std::string>> taken;  // cada parâmetro obedece a no máximo um macro
    for (auto& rawMacro : spec.macros)
    {
        if ((int) clean.macros.size() >= kMaxMacros)
        {
            report.push_back ("macro '" + rawMacro.name + "': excede o limite de " + std::to_string (kMaxMacros) + " macros, removido");
            continue;
        }
        MacroSpec macro;
        macro.name = rawMacro.name;
        macro.description = rawMacro.description;
        macro.value = std::isfinite (rawMacro.value) ? std::min (std::max (rawMacro.value, 0.0f), 1.0f) : 0.5f;

        for (auto& t : rawMacro.targets)
        {
            const std::string where = "macro '" + rawMacro.name + "' -> " + t.moduleId + "." + t.param;
            const auto* module = findSpecModule (clean, t.moduleId);
            const auto* def = module != nullptr ? findModule (module->type) : nullptr;
            const int index = def != nullptr ? def->paramIndex (t.param) : -1;
            if (index < 0)
            {
                report.push_back (where + ": alvo inexistente, ignorado");
                continue;
            }
            if (taken.count ({ t.moduleId, t.param }) != 0)
            {
                report.push_back (where + ": parâmetro já controlado por outro macro, ignorado");
                continue;
            }
            const auto& param = def->params[(size_t) index];
            const float lo = param.clamp (t.valueAt0), hi = param.clamp (t.valueAt1);
            if (lo != t.valueAt0 || hi != t.valueAt1)
                report.push_back (where + ": faixa " + fmt (t.valueAt0) + ".." + fmt (t.valueAt1) + " -> " + fmt (lo) + ".." + fmt (hi));
            taken.insert ({ t.moduleId, t.param });
            macro.targets.push_back ({ t.moduleId, t.param, lo, hi });
        }
        if (macro.targets.empty())
        {
            report.push_back ("macro '" + rawMacro.name + "': sem alvos válidos, removido");
            continue;
        }
        clean.macros.push_back (std::move (macro));
    }

    for (auto& m : clean.modules)
        if (m.type == "band_compressor" && m.params["low_hz"] > m.params["high_hz"])
        {
            std::swap (m.params["low_hz"], m.params["high_hz"]);
            report.push_back (m.id + ": low_hz e high_hz estavam invertidos, trocados");
        }

    if (features != nullptr && features->valid)
        applySignalRules (clean, *features, report);
    return clean;
}
}
