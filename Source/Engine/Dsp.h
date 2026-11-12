// Blocos básicos de DSP em tempo real. Nada aqui aloca memória fora de prepare().
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace mm::dsp
{
constexpr float kPi = 3.14159265358979323846f;

inline float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }
inline float linToDb (float lin) { return 20.0f * std::log10 (std::max (lin, 1.0e-9f)); }

// Filtro de segunda ordem (fórmulas do "Audio EQ Cookbook" de Robert Bristow-Johnson).
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1[2] = {}, z2[2] = {};

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    float process (float x, int ch)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void set (float nb0, float nb1, float nb2, float a0, float na1, float na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    static float safeFreq (double sr, float f) { return std::min (std::max (f, 10.0f), (float) sr * 0.49f); }

    void lowpass (double sr, float f, float q = 0.7071f)
    {
        const float w = 2.0f * kPi * safeFreq (sr, f) / (float) sr, c = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        set ((1.0f - c) / 2.0f, 1.0f - c, (1.0f - c) / 2.0f, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void highpass (double sr, float f, float q = 0.7071f)
    {
        const float w = 2.0f * kPi * safeFreq (sr, f) / (float) sr, c = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        set ((1.0f + c) / 2.0f, -(1.0f + c), (1.0f + c) / 2.0f, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void peak (double sr, float f, float gainDb, float q)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w = 2.0f * kPi * safeFreq (sr, f) / (float) sr, c = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        set (1.0f + alpha * A, -2.0f * c, 1.0f - alpha * A, 1.0f + alpha / A, -2.0f * c, 1.0f - alpha / A);
    }

    void lowShelf (double sr, float f, float gainDb, float q)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w = 2.0f * kPi * safeFreq (sr, f) / (float) sr, c = std::cos (w);
        const float s = 2.0f * std::sqrt (A) * std::sin (w) / (2.0f * q);
        set (A * ((A + 1.0f) - (A - 1.0f) * c + s), 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c), A * ((A + 1.0f) - (A - 1.0f) * c - s),
             (A + 1.0f) + (A - 1.0f) * c + s, -2.0f * ((A - 1.0f) + (A + 1.0f) * c), (A + 1.0f) + (A - 1.0f) * c - s);
    }

    void highShelf (double sr, float f, float gainDb, float q)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w = 2.0f * kPi * safeFreq (sr, f) / (float) sr, c = std::cos (w);
        const float s = 2.0f * std::sqrt (A) * std::sin (w) / (2.0f * q);
        set (A * ((A + 1.0f) + (A - 1.0f) * c + s), -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c), A * ((A + 1.0f) + (A - 1.0f) * c - s),
             (A + 1.0f) - (A - 1.0f) * c + s, 2.0f * ((A - 1.0f) - (A + 1.0f) * c), (A + 1.0f) - (A - 1.0f) * c - s);
    }
};

// Crossover Linkwitz-Riley de 4ª ordem: graves + agudos somam com resposta plana.
struct Crossover
{
    Biquad lp1, lp2, hp1, hp2;

    void set (double sr, float f)
    {
        lp1.lowpass (sr, f); lp2.lowpass (sr, f);
        hp1.highpass (sr, f); hp2.highpass (sr, f);
    }

    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

    void split (float x, int ch, float& low, float& high)
    {
        low = lp2.process (lp1.process (x, ch), ch);
        high = hp2.process (hp1.process (x, ch), ch);
    }
};

// Seguidor de envoltória com tempos de ataque e liberação.
struct Envelope
{
    float attack = 0.0f, release = 0.0f, value = 0.0f;

    void set (double sr, float attackMs, float releaseMs)
    {
        attack = attackMs <= 0.0f ? 0.0f : std::exp (-1.0f / ((float) sr * attackMs / 1000.0f));
        release = releaseMs <= 0.0f ? 0.0f : std::exp (-1.0f / ((float) sr * releaseMs / 1000.0f));
    }

    float process (float x)
    {
        const float c = x > value ? attack : release;
        value = x + c * (value - x);
        return value;
    }
};

// Suaviza um valor de controle para não dar clique.
struct Smoothed
{
    float value = 0.0f, target = 0.0f, coefficient = 0.0f;
    bool primed = false;

    void prepare (double sr, float ms) { coefficient = std::exp (-1.0f / ((float) sr * ms / 1000.0f)); }

    void set (float newTarget)
    {
        target = newTarget;
        if (! primed) { value = newTarget; primed = true; }
    }

    float next()
    {
        value = target + coefficient * (value - target);
        return value;
    }
};

// Linha de atraso com leitura interpolada.
struct DelayLine
{
    std::vector<float> buffer;
    int writeIndex = 0;

    void prepare (int maxSamples) { buffer.assign ((size_t) std::max (maxSamples + 4, 8), 0.0f); writeIndex = 0; }
    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void write (float x)
    {
        buffer[(size_t) writeIndex] = x;
        if (++writeIndex >= (int) buffer.size())
            writeIndex = 0;
    }

    // Lê `delaySamples` amostras atrás da última escrita (chame antes de write() para atraso exato).
    float read (float delaySamples) const
    {
        const int size = (int) buffer.size();
        delaySamples = std::min (std::max (delaySamples, 1.0f), (float) (size - 3));
        float position = (float) writeIndex - delaySamples;
        while (position < 0.0f)
            position += (float) size;
        const int i0 = (int) position, i1 = (i0 + 1) % size;
        const float frac = position - (float) i0;
        return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
    }
};

// Reverb no desenho clássico Freeverb (8 filtros pente e 4 passa-tudo por canal).
class Freeverb
{
public:
    void prepare (double sr)
    {
        static const int combTuning[kCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int allpassTuning[kAllpasses] = { 556, 441, 341, 225 };
        const double scale = sr / 44100.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < kCombs; ++i)
                comb[ch][i].prepare ((int) ((combTuning[i] + ch * 23) * scale));
            for (int i = 0; i < kAllpasses; ++i)
                allpass[ch][i].prepare ((int) ((allpassTuning[i] + ch * 23) * scale));
        }
    }

    void reset()
    {
        for (auto& channel : comb) for (auto& c : channel) c.reset();
        for (auto& channel : allpass) for (auto& a : channel) a.reset();
    }

    void set (float size, float damping)
    {
        feedback = size * 0.28f + 0.7f;
        damp = damping * 0.4f;
    }

    // Entrada estéreo, saída só com o som reverberado.
    void process (float inL, float inR, float& outL, float& outR)
    {
        const float input = (inL + inR) * 0.015f;
        outL = outR = 0.0f;
        for (int i = 0; i < kCombs; ++i)
        {
            outL += comb[0][i].process (input, feedback, damp);
            outR += comb[1][i].process (input, feedback, damp);
        }
        for (int i = 0; i < kAllpasses; ++i)
        {
            outL = allpass[0][i].process (outL);
            outR = allpass[1][i].process (outR);
        }
    }

private:
    static constexpr int kCombs = 8, kAllpasses = 4;

    struct Comb
    {
        std::vector<float> buffer;
        int index = 0;
        float store = 0.0f;

        void prepare (int size) { buffer.assign ((size_t) std::max (size, 1), 0.0f); index = 0; store = 0.0f; }
        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); store = 0.0f; }

        float process (float input, float feedback, float damp)
        {
            const float output = buffer[(size_t) index];
            store = output * (1.0f - damp) + store * damp;
            buffer[(size_t) index] = input + store * feedback;
            if (++index >= (int) buffer.size()) index = 0;
            return output;
        }
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int index = 0;

        void prepare (int size) { buffer.assign ((size_t) std::max (size, 1), 0.0f); index = 0; }
        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        float process (float input)
        {
            const float delayed = buffer[(size_t) index];
            buffer[(size_t) index] = input + delayed * 0.5f;
            if (++index >= (int) buffer.size()) index = 0;
            return delayed - input;
        }
    };

    Comb comb[2][kCombs];
    Allpass allpass[2][kAllpasses];
    float feedback = 0.84f, damp = 0.2f;
};
}
