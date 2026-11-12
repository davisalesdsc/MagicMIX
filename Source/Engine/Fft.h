// FFT complexa radix-2, no lugar. Tudo alocado em prepare(); transform() é seguro para tempo real.
#pragma once

#include <cmath>
#include <vector>

namespace mm::dsp
{
class Fft
{
public:
    void prepare (int size)
    {
        n = size;
        bits.assign ((size_t) n, 0);
        int levels = 0;
        while ((1 << levels) < n)
            ++levels;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b)
                r |= ((i >> b) & 1) << (levels - 1 - b);
            bits[(size_t) i] = r;
        }
        cosT.resize ((size_t) n / 2);
        sinT.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (2.0 * 3.14159265358979323846 * i / n);
            sinT[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / n);
        }
    }

    int size() const { return n; }

    // inverse = true calcula a transformada inversa já dividida por n.
    void transform (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bits[(size_t) i];
            if (i < j)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)], wi = inverse ? sinT[(size_t) (k * step)] : -sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
        if (inverse)
        {
            const float scale = 1.0f / (float) n;
            for (int i = 0; i < n; ++i)
            {
                re[i] *= scale;
                im[i] *= scale;
            }
        }
    }

private:
    int n = 0;
    std::vector<int> bits;
    std::vector<float> cosT, sinT;
};
}
