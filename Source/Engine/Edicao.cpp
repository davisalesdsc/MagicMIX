#include "Edicao.h"

#include <cmath>

#include "Catalog.h"
#include "Validate.h"

namespace mm
{
static MacroTarget* alvoDoMacro (ChainSpec& spec, const std::string& moduleId, const std::string& param, int& macro)
{
    for (size_t m = 0; m < spec.macros.size() && m < (size_t) kMaxMacros; ++m)
        for (auto& t : spec.macros[m].targets)
            if (t.moduleId == moduleId && t.param == param)
            {
                macro = (int) m;
                return &t;
            }
    return nullptr;
}

Chain::Edit editarParametro (ChainSpec& spec, int moduleIndex, const std::string& param, float value,
                             const float* macroPositions, bool& ok)
{
    Chain::Edit edit;
    ok = false;
    if (moduleIndex < 0 || moduleIndex >= (int) spec.modules.size())
        return edit;
    auto& module = spec.modules[(size_t) moduleIndex];
    const auto* def = findModule (module.type);
    const int index = def != nullptr ? def->paramIndex (param) : -1;
    if (index < 0)
        return edit;
    const auto& p = def->params[(size_t) index];
    value = p.clamp (value);

    edit.slot = moduleIndex;
    edit.param = index;
    edit.value = value;
    module.params[param] = value;
    ok = true;

    int macro = -1;
    if (auto* t = alvoDoMacro (spec, module.id, param, macro))
    {
        const float pos = std::min (std::max (macroPositions[macro], 0.0f), 1.0f);
        const bool log = p.log && t->valueAt0 > 0.0f && t->valueAt1 > 0.0f && value > 0.0f;
        auto f = [log] (float v) { return log ? std::log (v) : v; };
        auto inv = [log] (float v) { return log ? std::exp (v) : v; };

        // Desloca a faixa inteira: o desenho do macro (quanto ele muda do começo ao fim) continua o mesmo.
        const float agora = interpolate (t->valueAt0, t->valueAt1, pos, p.log);
        const float d = f (value) - f (agora);
        float a0 = p.clamp (inv (f (t->valueAt0) + d)), a1 = p.clamp (inv (f (t->valueAt1) + d));
        // Se um extremo bateu no limite da faixa, o outro se ajusta para o valor novo continuar na posição atual.
        if (pos < 1.0f && a1 != inv (f (t->valueAt1) + d))
            a0 = p.clamp (inv ((f (value) - pos * f (a1)) / (1.0f - pos)));
        else if (pos > 0.0f && a0 != inv (f (t->valueAt0) + d))
            a1 = p.clamp (inv ((f (value) - (1.0f - pos) * f (a0)) / pos));
        t->valueAt0 = a0;
        t->valueAt1 = a1;
        edit.rebind = true;
        edit.at0 = a0;
        edit.at1 = a1;
        edit.value = interpolate (a0, a1, pos, p.log);
    }
    return edit;
}

Chain::Edit ligarModulo (ChainSpec& spec, int moduleIndex, bool ligado)
{
    Chain::Edit edit;
    edit.slot = moduleIndex;
    edit.param = Chain::kParamLigado;
    edit.value = ligado ? 1.0f : 0.0f;
    if (moduleIndex >= 0 && moduleIndex < (int) spec.modules.size())
        spec.modules[(size_t) moduleIndex].enabled = ligado;
    return edit;
}

float valorAtual (const ChainSpec& spec, int moduleIndex, const std::string& param, const float* macroPositions)
{
    if (moduleIndex < 0 || moduleIndex >= (int) spec.modules.size())
        return 0.0f;
    const auto& module = spec.modules[(size_t) moduleIndex];
    const auto* def = findModule (module.type);
    const int index = def != nullptr ? def->paramIndex (param) : -1;
    if (index < 0)
        return 0.0f;
    for (size_t m = 0; m < spec.macros.size() && m < (size_t) kMaxMacros; ++m)
        for (auto& t : spec.macros[m].targets)
            if (t.moduleId == module.id && t.param == param)
                return interpolate (t.valueAt0, t.valueAt1, macroPositions[m], def->params[(size_t) index].log);
    const auto found = module.params.find (param);
    return found != module.params.end() ? found->second : def->params[(size_t) index].def;
}
}
