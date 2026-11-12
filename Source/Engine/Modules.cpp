#include "Modules.h"

#include <cassert>
#include <cstring>

#include "Catalog.h"
#include "Dsp.h"

namespace mm
{
using namespace dsp;

namespace
{
// Índice de um parâmetro pelo nome, para o código não depender da ordem do catálogo.
int ix (const char* type, const char* name)
{
    const auto* def = findModule (type);
    const int index = def != nullptr ? def->paramIndex (name) : -1;
    assert (index >= 0);
    return index;
}

inline float linked (float* const* ch, int numCh, int i)
{
    return numCh == 2 ? std::max (std::abs (ch[0][i]), std::abs (ch[1][i])) : std::abs (ch[0][i]);
}

// Saturador: tanh com o volume compensado em 75% do drive, como no protótipo.
struct Saturator
{
    float drive = 1.0f, compensation = 1.0f;

    void set (float driveDb)
    {
        drive = dbToLin (driveDb);
        compensation = dbToLin (-0.75f * driveDb);
    }

    float process (float x) const { return std::tanh (x * drive) * compensation; }
};

// ---------------------------------------------------------------- filtros
class FilterModule : public Module
{
public:
    enum class Kind { highpass, lowpass, lowShelf, highShelf, peak };

    FilterModule (Kind k, const char* type) : kind (k), freq (ix (type, "freq_hz"))
    {
        if (k != Kind::highpass && k != Kind::lowpass)
        {
            gain = ix (type, "gain_db");
            q = ix (type, "q");
        }
    }

    void prepare (double sr, int) override { sampleRate = sr; filter.reset(); }
    void reset() override { filter.reset(); }

    void setParams (const float* p) override
    {
        switch (kind)
        {
            case Kind::highpass:  filter.highpass (sampleRate, p[freq]); break;
            case Kind::lowpass:   filter.lowpass (sampleRate, p[freq]); break;
            case Kind::lowShelf:  filter.lowShelf (sampleRate, p[freq], p[gain], p[q]); break;
            case Kind::highShelf: filter.highShelf (sampleRate, p[freq], p[gain], p[q]); break;
            case Kind::peak:      filter.peak (sampleRate, p[freq], p[gain], p[q]); break;
        }
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
                ch[c][i] = filter.process (ch[c][i], c);
    }

private:
    Kind kind;
    int freq, gain = 0, q = 0;
    double sampleRate = 44100.0;
    Biquad filter;
};

// ---------------------------------------------------------------- compressor
class CompressorModule : public Module
{
public:
    void prepare (double sr, int) override { sampleRate = sr; envelope.value = 0.0f; makeup.prepare (sr, 20.0f); mix.prepare (sr, 20.0f); }
    void reset() override { envelope.value = 0.0f; }

    void setParams (const float* p) override
    {
        threshold = dbToLin (p[iThreshold]);
        slope = 1.0f / p[iRatio] - 1.0f;
        envelope.set (sampleRate, p[iAttack], p[iRelease]);
        makeup.set (dbToLin (p[iMakeup]));
        mix.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float level = envelope.process (linked (ch, numCh, i));
            const float gain = (level > threshold ? std::pow (level / threshold, slope) : 1.0f) * makeup.next();
            const float wet = mix.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = ch[c][i] * (1.0f - wet) + ch[c][i] * gain * wet;
        }
    }

private:
    const int iThreshold = ix ("compressor", "threshold_db"), iRatio = ix ("compressor", "ratio"),
              iAttack = ix ("compressor", "attack_ms"), iRelease = ix ("compressor", "release_ms"),
              iMakeup = ix ("compressor", "makeup_db"), iMix = ix ("compressor", "mix");
    double sampleRate = 44100.0;
    float threshold = 1.0f, slope = 0.0f;
    Envelope envelope;
    Smoothed makeup, mix;
};

// ---------------------------------------------------------------- saturação
class SaturationModule : public Module
{
public:
    void prepare (double sr, int) override { mix.prepare (sr, 20.0f); }

    void setParams (const float* p) override
    {
        saturator.set (p[iDrive]);
        mix.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float wet = mix.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = ch[c][i] * (1.0f - wet) + saturator.process (ch[c][i]) * wet;
        }
    }

private:
    const int iDrive = ix ("saturation", "drive_db"), iMix = ix ("saturation", "mix");
    Saturator saturator;
    Smoothed mix;
};

// ---------------------------------------------------------------- fita
class TapeModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& line : lines)
            line.prepare ((int) (sr * 0.01));
        tone.reset();
        depth.prepare (sr, 50.0f);
        phase = 0.0f;
    }

    void reset() override
    {
        for (auto& line : lines) line.reset();
        tone.reset();
    }

    void setParams (const float* p) override
    {
        saturator.set (p[iDrive]);
        tone.lowpass (sampleRate, p[iTone]);
        depth.set (p[iWow] * 0.0015f * (float) sampleRate);  // até 1,5 ms de oscilação
    }

    void process (float* const* ch, int numCh, int n) override
    {
        const float increment = 2.0f * kPi * 0.7f / (float) sampleRate;  // oscilação lenta, 0,7 Hz
        for (int i = 0; i < n; ++i)
        {
            const float d = depth.next();
            const float delay = 1.0f + d * (1.0f + std::sin (phase));
            phase += increment;
            if (phase > 2.0f * kPi) phase -= 2.0f * kPi;

            for (int c = 0; c < numCh; ++c)
            {
                float x = tone.process (saturator.process (ch[c][i]), c);
                if (d > 0.01f)
                {
                    const float delayed = lines[c].read (delay);
                    lines[c].write (x);
                    x = delayed;
                }
                else
                    lines[c].write (x);
                ch[c][i] = x;
            }
        }
    }

private:
    const int iDrive = ix ("tape", "drive_db"), iTone = ix ("tape", "tone_hz"), iWow = ix ("tape", "wow");
    double sampleRate = 44100.0;
    Saturator saturator;
    Biquad tone;
    DelayLine lines[2];
    Smoothed depth;
    float phase = 0.0f;
};

// ---------------------------------------------------------------- delay
class DelayModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& line : lines)
            line.prepare ((int) (sr * 1.6));
        time.prepare (sr, 80.0f);
        mix.prepare (sr, 20.0f);
        reset();
    }

    void reset() override
    {
        for (auto& line : lines) line.reset();
        highpass.reset();
        lowpass.reset();
    }

    void setParams (const float* p) override
    {
        time.set (p[iTime] / 1000.0f * (float) sampleRate);
        feedback = p[iFeedback];
        highpass.highpass (sampleRate, p[iHighpass]);
        lowpass.lowpass (sampleRate, p[iLowpass]);
        pingPong = p[iPingPong] >= 0.5f;
        mix.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float delay = time.next(), wet = mix.next();

            if (pingPong && numCh == 2)
            {
                // A entrada vai para a esquerda; cada repetição cruza para o outro lado.
                const float left = lowpass.process (highpass.process (lines[0].read (delay), 0), 0);
                const float right = lowpass.process (highpass.process (lines[1].read (delay), 1), 1);
                lines[0].write ((ch[0][i] + ch[1][i]) * 0.5f + right * feedback);
                lines[1].write (left * feedback);
                ch[0][i] = ch[0][i] * (1.0f - wet) + left * wet;
                ch[1][i] = ch[1][i] * (1.0f - wet) + right * wet;
                continue;
            }

            for (int c = 0; c < numCh; ++c)
            {
                const float echo = lowpass.process (highpass.process (lines[c].read (delay), c), c);
                lines[c].write (ch[c][i] + echo * feedback);
                ch[c][i] = ch[c][i] * (1.0f - wet) + echo * wet;
            }
        }
    }

private:
    const int iTime = ix ("delay", "time_ms"), iFeedback = ix ("delay", "feedback"), iHighpass = ix ("delay", "highpass_hz"),
              iLowpass = ix ("delay", "lowpass_hz"), iPingPong = ix ("delay", "ping_pong"), iMix = ix ("delay", "mix");
    double sampleRate = 44100.0;
    DelayLine lines[2];
    Biquad highpass, lowpass;
    Smoothed time, mix;
    float feedback = 0.0f;
    bool pingPong = false;
};

// ---------------------------------------------------------------- reverb
class ReverbModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        reverb.prepare (sr);
        for (auto& line : preDelay)
            line.prepare ((int) (sr * 0.25));
        mix.prepare (sr, 20.0f);
    }

    void reset() override
    {
        reverb.reset();
        for (auto& line : preDelay) line.reset();
    }

    void setParams (const float* p) override
    {
        reverb.set (p[iSize], p[iDamping]);
        width = p[iWidth];
        preDelaySamples = p[iPreDelay] / 1000.0f * (float) sampleRate;
        mix.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        const float wetScale = 2.0f;
        const float same = wetScale * (width * 0.5f + 0.5f), cross = wetScale * (1.0f - width) * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            float inL = ch[0][i], inR = numCh == 2 ? ch[1][i] : ch[0][i];
            if (preDelaySamples >= 1.0f)
            {
                const float dl = preDelay[0].read (preDelaySamples), dr = preDelay[1].read (preDelaySamples);
                preDelay[0].write (inL);
                preDelay[1].write (inR);
                inL = dl;
                inR = dr;
            }
            float wetL, wetR;
            reverb.process (inL, inR, wetL, wetR);
            const float wet = mix.next();
            if (numCh == 2)
            {
                ch[0][i] = ch[0][i] * (1.0f - wet) + (wetL * same + wetR * cross) * wet;
                ch[1][i] = ch[1][i] * (1.0f - wet) + (wetR * same + wetL * cross) * wet;
            }
            else
                ch[0][i] = ch[0][i] * (1.0f - wet) + (wetL + wetR) * 0.5f * wetScale * wet;
        }
    }

private:
    const int iSize = ix ("reverb", "size"), iDamping = ix ("reverb", "damping"), iWidth = ix ("reverb", "width"),
              iPreDelay = ix ("reverb", "pre_delay_ms"), iMix = ix ("reverb", "mix");
    double sampleRate = 44100.0;
    Freeverb reverb;
    DelayLine preDelay[2];
    Smoothed mix;
    float width = 1.0f, preDelaySamples = 0.0f;
};

// ---------------------------------------------------------------- chorus
class ChorusModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& line : lines)
            line.prepare ((int) (sr * 0.03));
        mix.prepare (sr, 20.0f);
        phase = 0.0f;
    }

    void reset() override { for (auto& line : lines) line.reset(); }

    void setParams (const float* p) override
    {
        increment = 2.0f * kPi * p[iRate] / (float) sampleRate;
        depthSamples = p[iDepth] * 0.004f * (float) sampleRate;  // até ±4 ms em torno de 7 ms
        mix.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        const float centre = 0.007f * (float) sampleRate;
        for (int i = 0; i < n; ++i)
        {
            const float wet = mix.next();
            for (int c = 0; c < numCh; ++c)
            {
                // O canal direito modula 90 graus adiantado, o que abre a imagem.
                const float delay = centre + depthSamples * std::sin (phase + (c == 1 ? kPi * 0.5f : 0.0f));
                const float delayed = lines[c].read (delay);
                lines[c].write (ch[c][i]);
                ch[c][i] = ch[c][i] * (1.0f - wet) + delayed * wet;
            }
            phase += increment;
            if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
        }
    }

private:
    const int iRate = ix ("chorus", "rate_hz"), iDepth = ix ("chorus", "depth"), iMix = ix ("chorus", "mix");
    double sampleRate = 44100.0;
    DelayLine lines[2];
    Smoothed mix;
    float phase = 0.0f, increment = 0.0f, depthSamples = 0.0f;
};

// ---------------------------------------------------------------- de-esser
class DeEsserModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        envelope.set (sr, 1.0f, 50.0f);
        gain.prepare (sr, 5.0f);
        gain.set (1.0f);
        reset();
    }

    void reset() override { crossover.reset(); envelope.value = 0.0f; }

    void setParams (const float* p) override
    {
        crossover.set (sampleRate, p[iFreq]);
        thresholdDb = p[iThreshold];
        maxReductionDb = p[iMaxReduction];
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            float low[2] = {}, high[2] = {};
            for (int c = 0; c < numCh; ++c)
                crossover.split (ch[c][i], c, low[c], high[c]);

            const float level = envelope.process (std::max (std::abs (high[0]), std::abs (high[1])));
            const float overDb = linToDb (level) - thresholdDb;
            gain.set (dbToLin (-std::min (std::max (overDb, 0.0f) * 0.75f, maxReductionDb)));  // taxa 4:1
            const float g = gain.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = low[c] + high[c] * g;
        }
    }

private:
    const int iFreq = ix ("de_esser", "freq_hz"), iThreshold = ix ("de_esser", "threshold_db"),
              iMaxReduction = ix ("de_esser", "max_reduction_db");
    double sampleRate = 44100.0;
    Crossover crossover;
    Envelope envelope;
    Smoothed gain;
    float thresholdDb = -30.0f, maxReductionDb = 6.0f;
};

// ---------------------------------------------------------------- transient shaper
class TransientShaperModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        // Ataque: duas envoltórias que só diferem no tempo de subida (iguais na queda).
        attackFast.set (sr, 0.5f, 60.0f);
        attackSlow.set (sr, 10.0f, 60.0f);
        // Sustentação: duas envoltórias que só diferem no tempo de queda (iguais na subida).
        sustainSlow.set (sr, 0.5f, 400.0f);
        sustainFast.set (sr, 0.5f, 60.0f);
        gain.prepare (sr, 0.5f);  // rápido, para o ganho acompanhar o ataque
        gain.set (1.0f);
        reset();
    }

    void reset() override { attackFast.value = attackSlow.value = sustainSlow.value = sustainFast.value = 0.0f; }

    void setParams (const float* p) override
    {
        attackDb = p[iAttack];
        sustainDb = p[iSustain];
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float level = linked (ch, numCh, i);
            const float onsetDb = linToDb (attackFast.process (level) + 1.0e-6f) - linToDb (attackSlow.process (level) + 1.0e-6f);
            const float tailDb = linToDb (sustainSlow.process (level) + 1.0e-6f) - linToDb (sustainFast.process (level) + 1.0e-6f);
            const float gainDb = attackDb * std::min (std::max (onsetDb / 6.0f, 0.0f), 1.0f)
                                 + sustainDb * std::min (std::max (tailDb / 6.0f, 0.0f), 1.0f);
            gain.set (dbToLin (gainDb));
            const float g = gain.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] *= g;
        }
    }

private:
    const int iAttack = ix ("transient_shaper", "attack_db"), iSustain = ix ("transient_shaper", "sustain_db");
    Envelope attackFast, attackSlow, sustainSlow, sustainFast;
    Smoothed gain;
    float attackDb = 0.0f, sustainDb = 0.0f;
};

// ---------------------------------------------------------------- compressor de uma banda
class BandCompressorModule : public Module
{
public:
    void prepare (double sr, int) override { sampleRate = sr; reset(); }

    void reset() override
    {
        lowSplit.reset(); highSplit.reset(); lowAlign.reset();
        envelope.value = 0.0f;
    }

    void setParams (const float* p) override
    {
        lowSplit.set (sampleRate, p[iLow]);
        highSplit.set (sampleRate, p[iHigh]);
        lowAlign.set (sampleRate, p[iHigh]);
        threshold = dbToLin (p[iThreshold]);
        slope = 1.0f / p[iRatio] - 1.0f;
        envelope.set (sampleRate, p[iAttack], p[iRelease]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            float below[2] = {}, band[2] = {}, above[2] = {};
            for (int c = 0; c < numCh; ++c)
            {
                float rest, a, b;
                lowSplit.split (ch[c][i], c, below[c], rest);
                highSplit.split (rest, c, band[c], above[c]);
                // Os graves passam pelo mesmo desvio de fase do segundo corte, para a soma ficar plana.
                lowAlign.split (below[c], c, a, b);
                below[c] = a + b;
            }
            const float level = envelope.process (std::max (std::abs (band[0]), std::abs (band[1])));
            const float gain = level > threshold ? std::pow (level / threshold, slope) : 1.0f;
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = below[c] + band[c] * gain + above[c];
        }
    }

private:
    const int iLow = ix ("band_compressor", "low_hz"), iHigh = ix ("band_compressor", "high_hz"),
              iThreshold = ix ("band_compressor", "threshold_db"), iRatio = ix ("band_compressor", "ratio"),
              iAttack = ix ("band_compressor", "attack_ms"), iRelease = ix ("band_compressor", "release_ms");
    double sampleRate = 44100.0;
    Crossover lowSplit, highSplit, lowAlign;
    Envelope envelope;
    float threshold = 1.0f, slope = 0.0f;
};

// ---------------------------------------------------------------- exciter
class ExciterModule : public Module
{
public:
    void prepare (double sr, int) override { sampleRate = sr; amount.prepare (sr, 20.0f); reset(); }
    void reset() override { input1.reset(); input2.reset(); output.reset(); }

    void setParams (const float* p) override
    {
        input1.highpass (sampleRate, p[iFreq]);
        input2.highpass (sampleRate, p[iFreq]);
        output.highpass (sampleRate, p[iFreq]);
        saturator.set (p[iDrive]);
        amount.set (p[iAmount]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = amount.next();
            for (int c = 0; c < numCh; ++c)
            {
                // Satura só os agudos e soma de volta apenas o que surgiu de novo acima do corte.
                const float high = input2.process (input1.process (ch[c][i], c), c);
                const float harmonics = output.process (saturator.process (high) - high, c);
                ch[c][i] += a * harmonics;
            }
        }
    }

private:
    const int iFreq = ix ("exciter", "freq_hz"), iDrive = ix ("exciter", "drive_db"), iAmount = ix ("exciter", "amount");
    double sampleRate = 44100.0;
    Biquad input1, input2, output;
    Saturator saturator;
    Smoothed amount;
};

// ---------------------------------------------------------------- clipper
class ClipperModule : public Module
{
public:
    void prepare (double, int) override {}

    void setParams (const float* p) override
    {
        ceiling = dbToLin (p[iCeiling]);
        knee = ceiling * (1.0f - p[iSoftness]);  // abaixo disto o sinal passa intacto
    }

    void process (float* const* ch, int numCh, int n) override
    {
        const float range = ceiling - knee;
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i], level = std::abs (x);
                if (level <= knee)
                    continue;
                const float shaped = range < 1.0e-9f ? ceiling : knee + range * std::tanh ((level - knee) / range);
                ch[c][i] = x < 0.0f ? -shaped : shaped;
            }
    }

private:
    const int iCeiling = ix ("clipper", "ceiling_db"), iSoftness = ix ("clipper", "softness");
    float ceiling = 0.5f, knee = 0.25f;
};

// ---------------------------------------------------------------- largura estéreo
class StereoWidthModule : public Module
{
public:
    void prepare (double sr, int) override { width.prepare (sr, 20.0f); }
    void setParams (const float* p) override { width.set (p[iWidth]); }

    void process (float* const* ch, int numCh, int n) override
    {
        if (numCh != 2)
            return;
        for (int i = 0; i < n; ++i)
        {
            const float mid = (ch[0][i] + ch[1][i]) * 0.5f, side = (ch[0][i] - ch[1][i]) * 0.5f * width.next();
            ch[0][i] = mid + side;
            ch[1][i] = mid - side;
        }
    }

private:
    const int iWidth = ix ("stereo_width", "width");
    Smoothed width;
};

// ---------------------------------------------------------------- ganho
class GainModule : public Module
{
public:
    void prepare (double sr, int) override { gain.prepare (sr, 20.0f); }
    void setParams (const float* p) override { gain.set (dbToLin (p[iGain])); }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] *= g;
        }
    }

private:
    const int iGain = ix ("gain", "gain_db");
    Smoothed gain;
};
}

std::unique_ptr<Module> createModule (std::string_view type)
{
    using Kind = FilterModule::Kind;
    if (type == "highpass")         return std::make_unique<FilterModule> (Kind::highpass, "highpass");
    if (type == "lowpass")          return std::make_unique<FilterModule> (Kind::lowpass, "lowpass");
    if (type == "low_shelf")        return std::make_unique<FilterModule> (Kind::lowShelf, "low_shelf");
    if (type == "high_shelf")       return std::make_unique<FilterModule> (Kind::highShelf, "high_shelf");
    if (type == "peak")             return std::make_unique<FilterModule> (Kind::peak, "peak");
    if (type == "compressor")       return std::make_unique<CompressorModule>();
    if (type == "saturation")       return std::make_unique<SaturationModule>();
    if (type == "tape")             return std::make_unique<TapeModule>();
    if (type == "delay")            return std::make_unique<DelayModule>();
    if (type == "reverb")           return std::make_unique<ReverbModule>();
    if (type == "chorus")           return std::make_unique<ChorusModule>();
    if (type == "de_esser")         return std::make_unique<DeEsserModule>();
    if (type == "transient_shaper") return std::make_unique<TransientShaperModule>();
    if (type == "band_compressor")  return std::make_unique<BandCompressorModule>();
    if (type == "exciter")          return std::make_unique<ExciterModule>();
    if (type == "clipper")          return std::make_unique<ClipperModule>();
    if (type == "stereo_width")     return std::make_unique<StereoWidthModule>();
    if (type == "gain")             return std::make_unique<GainModule>();
    return nullptr;
}
}
