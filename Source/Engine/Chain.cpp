#include "Chain.h"

#include <cmath>
#include <cstring>

#include "Validate.h"

namespace mm
{
Chain::Chain (const ChainSpec& spec)
{
    for (auto& m : spec.modules)
    {
        const auto* def = findModule (m.type);
        auto module = createModule (m.type);
        if (def == nullptr || module == nullptr)
            continue;

        Slot slot;
        slot.module = std::move (module);
        for (size_t i = 0; i < def->params.size() && i < (size_t) kMaxParams; ++i)
        {
            const auto found = m.params.find (def->params[i].name);
            slot.params[i] = found != m.params.end() ? found->second : def->params[i].def;
        }
        slots.push_back (std::move (slot));

        // Liga os macros a este módulo.
        for (size_t macro = 0; macro < spec.macros.size() && macro < (size_t) kMaxMacros; ++macro)
            for (auto& t : spec.macros[macro].targets)
            {
                const int param = t.moduleId == m.id ? def->paramIndex (t.param) : -1;
                if (param >= 0)
                    bindings.push_back ({ (int) macro, (int) slots.size() - 1, param, t.valueAt0, t.valueAt1,
                                          def->params[(size_t) param].log });
            }
    }
}

void Chain::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    maxBlock = std::max (maxBlockSize, kChunk);
    for (auto& d : dry)
        d.assign ((size_t) maxBlock, 0.0f);
    latencia = 0;
    for (auto& slot : slots)
    {
        slot.module->prepare (sampleRate, numChannels);
        slot.module->setParams (slot.params);  // alguns módulos só sabem o atraso depois dos parâmetros
        slot.dirty = true;
        latencia += slot.module->latencySamples();
    }
    for (auto& d : dryAtraso)
        d.prepare (latencia + 4);
    macroCoefficient = std::exp (-(float) kChunk / ((float) sampleRate * 0.03f));  // macros levam ~30 ms
    mixSmoothed.prepare (sampleRate, 20.0f);
    gainSmoothed.prepare (sampleRate, 20.0f);
    limiterRelease = std::exp (-1.0f / ((float) sampleRate * 0.08f));
    limiterEnvelope = 0.0f;
    macrosPrimed = false;
}

void Chain::reset()
{
    for (auto& slot : slots)
        slot.module->reset();
    limiterEnvelope = 0.0f;
}

void Chain::applyMacros (const float* macros)
{
    for (int m = 0; m < kMaxMacros; ++m)
        macroNow[m] = macrosPrimed ? macros[m] + macroCoefficient * (macroNow[m] - macros[m]) : macros[m];

    for (auto& b : bindings)
    {
        if (macrosPrimed && std::abs (macroNow[b.macro] - macroApplied[b.macro]) < 1.0e-5f)
            continue;
        slots[(size_t) b.slot].params[b.param] = interpolate (b.valueAt0, b.valueAt1, macroNow[b.macro], b.log);
        slots[(size_t) b.slot].dirty = true;
    }
    std::memcpy (macroApplied, macroNow, sizeof (macroApplied));
    macrosPrimed = true;

    for (auto& slot : slots)
        if (slot.dirty)
        {
            slot.module->setParams (slot.params);
            slot.dirty = false;
        }
}

void Chain::process (float* const* channels, int numChannels, int numSamples,
                     const float* macros, float mix, float outputGainDb,
                     const float* const* sidechain, int sidechainChannels)
{
    if (maxBlock <= 0)
        return;  // prepare() ainda não foi chamado
    numChannels = std::min (numChannels, 2);
    mixSmoothed.set (mix);
    gainSmoothed.set (dsp::dbToLin (outputGainDb));
    const float ceiling = dsp::dbToLin (kSafetyCeilingDb);

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int block = std::min (maxBlock, numSamples - start);
        for (int c = 0; c < numChannels; ++c)
        {
            std::memcpy (dry[c].data(), channels[c] + start, (size_t) block * sizeof (float));
            if (latencia > 0)  // o som seco espera o mesmo tempo que o processado, para a mistura não defasar
                for (int i = 0; i < block; ++i)
                {
                    const float atrasado = dryAtraso[c].read ((float) latencia);
                    dryAtraso[c].write (dry[c][(size_t) i]);
                    dry[c][(size_t) i] = atrasado;
                }
        }

        for (int offset = 0; offset < block; offset += kChunk)
        {
            const int n = std::min (kChunk, block - offset);
            float* chunk[2] = { channels[0] + start + offset, numChannels == 2 ? channels[1] + start + offset : nullptr };
            const int scCh = sidechain != nullptr ? std::min (std::max (sidechainChannels, 1), 2) : 0;
            const float* scChunk[2] = { scCh > 0 ? sidechain[0] + start + offset : nullptr,
                                        scCh > 1 ? sidechain[1] + start + offset : nullptr };
            applyMacros (macros);
            for (auto& slot : slots)
            {
                slot.module->setSidechain (scCh > 0 ? scChunk : nullptr, scCh);
                slot.module->process (chunk, numChannels, n);
            }
        }

        for (int i = 0; i < block; ++i)
        {
            const float wet = mixSmoothed.next(), gain = gainSmoothed.next();
            float peak = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                float& x = channels[c][start + i];
                x = (dry[c][(size_t) i] * (1.0f - wet) + x * wet) * gain;
                if (! std::isfinite (x))
                    x = 0.0f;  // um módulo instável nunca chega à saída
                peak = std::max (peak, std::abs (x));
            }
            // Limitador de segurança: só age quando o pico passa do teto.
            limiterEnvelope = std::max (peak, limiterEnvelope * limiterRelease);
            if (limiterEnvelope > ceiling)
            {
                const float reduction = ceiling / limiterEnvelope;
                for (int c = 0; c < numChannels; ++c)
                    channels[c][start + i] *= reduction;
            }
        }
    }
}
}
