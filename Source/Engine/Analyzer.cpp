#include "Analyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace mm
{
namespace
{
constexpr double kEps = 1.0e-12;
constexpr double kPi = 3.14159265358979323846;

double db (double x) { return 20.0 * std::log10 (std::max (x, kEps)); }

// FFT radix-2 no lugar. O tamanho precisa ser potência de 2.
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> step = std::polar (1.0, -2.0 * kPi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

std::vector<double> smooth (const std::vector<double>& x, int points)
{
    // Média móvel centrada, repetindo as pontas.
    const int n = (int) x.size();
    std::vector<double> out ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        for (int k = i - points / 2; k < i - points / 2 + points; ++k)
            sum += x[(size_t) std::min (std::max (k, 0), n - 1)];
        out[(size_t) i] = sum / points;
    }
    return out;
}

// Picos estreitos que sobram acima do contorno geral do espectro (heurística).
std::vector<Resonance> findResonances (const std::vector<double>& freqs, const std::vector<double>& power)
{
    const int perOctave = 48;
    const int count = (int) (std::log2 (12000.0 / 80.0) * perOctave);
    std::vector<double> grid ((size_t) count), level ((size_t) count);
    for (int i = 0; i < count; ++i)
    {
        grid[(size_t) i] = 80.0 * std::pow (2.0, (double) i / perOctave);
        // Interpolação linear do espectro na grade logarítmica.
        const auto upper = std::lower_bound (freqs.begin(), freqs.end(), grid[(size_t) i]);
        double value = power.back();
        if (upper == freqs.begin())
            value = power.front();
        else if (upper != freqs.end())
        {
            const size_t hi = (size_t) (upper - freqs.begin()), lo = hi - 1;
            const double t = (grid[(size_t) i] - freqs[lo]) / (freqs[hi] - freqs[lo]);
            value = power[lo] + t * (power[hi] - power[lo]);
        }
        level[(size_t) i] = 10.0 * std::log10 (value + kEps);
    }

    const auto detail = smooth (level, perOctave / 12);  // 1/12 de oitava
    const auto contour = smooth (level, perOctave);      // 1 oitava
    const double loudest = *std::max_element (level.begin(), level.end());

    std::vector<int> peaks;
    for (int i = 1; i < count - 1; ++i)
    {
        const double p = detail[(size_t) i] - contour[(size_t) i];
        const double before = detail[(size_t) i - 1] - contour[(size_t) i - 1];
        const double after = detail[(size_t) i + 1] - contour[(size_t) i + 1];
        if (level[(size_t) i] > loudest - 40.0 && p >= 6.0 && p >= before && p > after)
            peaks.push_back (i);
    }
    std::sort (peaks.begin(), peaks.end(), [&] (int a, int b) {
        return detail[(size_t) a] - contour[(size_t) a] > detail[(size_t) b] - contour[(size_t) b];
    });
    if (peaks.size() > 3)
        peaks.resize (3);
    std::sort (peaks.begin(), peaks.end());

    std::vector<Resonance> out;
    for (int i : peaks)
        out.push_back ({ (int) std::lround (grid[(size_t) i]),
                         (float) (std::round ((detail[(size_t) i] - contour[(size_t) i]) * 10.0) / 10.0) });
    return out;
}
}

Features analyze (const float* const* channels, int numChannels, int numSamples, double sampleRate)
{
    Features f;
    numChannels = std::min (numChannels, 2);
    if (numChannels < 1 || numSamples < 1 || sampleRate <= 0.0)
        return f;

    f.valid = true;
    f.durationS = std::round ((double) numSamples / sampleRate * 100.0) / 100.0;
    f.sampleRate = (int) sampleRate;
    f.channels = numChannels;

    std::vector<double> mono ((size_t) numSamples);
    double peak = 0.0, sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int c = 0; c < numChannels; ++c)
        {
            sum += channels[c][i];
            peak = std::max (peak, (double) std::abs (channels[c][i]));
        }
        mono[(size_t) i] = sum / numChannels;
        sumSquares += mono[(size_t) i] * mono[(size_t) i];
    }
    const double rms = std::sqrt (sumSquares / numSamples);
    auto round1 = [] (double v) { return (float) (std::round (v * 10.0) / 10.0); };
    f.peakDbfs = round1 (db (peak));
    f.rmsDbfs = round1 (db (rms));
    f.crestFactorDb = round1 (db (peak) - db (rms));

    // Espectro médio de potência: janelas de Hann de 4096 com 50% de sobreposição.
    const int size = 4096, hop = size / 2;
    if (numSamples < size)
        mono.resize ((size_t) size, 0.0);
    std::vector<double> window ((size_t) size), power ((size_t) size / 2 + 1, 0.0), freqs ((size_t) size / 2 + 1);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (size - 1));
    std::vector<std::complex<double>> frame ((size_t) size);
    for (int start = 0; start + size <= (int) mono.size(); start += hop)
    {
        for (int i = 0; i < size; ++i)
            frame[(size_t) i] = mono[(size_t) (start + i)] * window[(size_t) i];
        fft (frame);
        for (int k = 0; k <= size / 2; ++k)
            power[(size_t) k] += std::norm (frame[(size_t) k]);
    }
    double total = 0.0, weighted = 0.0;
    for (int k = 0; k <= size / 2; ++k)
    {
        freqs[(size_t) k] = k * sampleRate / size;
        total += power[(size_t) k];
        weighted += freqs[(size_t) k] * power[(size_t) k];
    }
    f.spectralCentroidHz = total > kEps ? (int) std::lround (weighted / total) : 0;

    static const double bands[6][2] = { { 20, 80 }, { 80, 250 }, { 250, 800 }, { 800, 2500 }, { 2500, 6000 }, { 6000, 20000 } };
    for (int b = 0; b < 6; ++b)
    {
        double share = 0.0;
        for (int k = 0; k <= size / 2; ++k)
            if (freqs[(size_t) k] >= bands[b][0] && freqs[(size_t) k] < bands[b][1])
                share += power[(size_t) k];
        f.bandEnergyDb[b] = round1 (10.0 * std::log10 (std::max (total > kEps ? share / total : 0.0, kEps)));
    }

    if (total > kEps)
        f.resonances = findResonances (freqs, power);

    if (numChannels == 2)
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            lr += (double) channels[0][i] * channels[1][i];
            ll += (double) channels[0][i] * channels[0][i];
            rr += (double) channels[1][i] * channels[1][i];
        }
        const double denom = std::sqrt (ll * rr);
        f.hasCorrelation = true;
        f.stereoCorrelation = denom > kEps ? (float) (std::round (lr / denom * 100.0) / 100.0) : 1.0f;
    }
    return f;
}
}
