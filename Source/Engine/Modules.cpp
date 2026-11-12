#include "Modules.h"

#include <cassert>
#include <cstring>

#include "Catalog.h"
#include "Dsp.h"
#include "Fft.h"

#include <algorithm>
#include <string>

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

// ---------------------------------------------------------------- dinâmica: peças comuns
// Curva estática de um compressor com joelho suave. Devolve a redução em dB (zero ou negativa).
inline float curvaCompressao (float nivelDb, float limiteDb, float taxa, float joelhoDb)
{
    const float acima = nivelDb - limiteDb, inclinacao = 1.0f / taxa - 1.0f;
    if (joelhoDb > 0.0f && 2.0f * std::abs (acima) <= joelhoDb)
        return inclinacao * (acima + joelhoDb * 0.5f) * (acima + joelhoDb * 0.5f) / (2.0f * joelhoDb);
    return acima > 0.0f ? inclinacao * acima : 0.0f;
}

inline float coefTempo (double sr, float ms) { return ms <= 0.0f ? 0.0f : std::exp (-1.0f / ((float) sr * ms / 1000.0f)); }

// Detector compartilhado pelos compressores: filtro do detector, sidechain e média RMS opcional.
struct Detector
{
    Biquad filtro;
    bool filtrar = false;
    float rms = 0.0f, coefRms = 0.0f;

    void prepare() { filtro.reset(); rms = 0.0f; }
    void ajustar (double sr, float filtroHz, float rmsMs)
    {
        filtrar = filtroHz > 21.0f;
        if (filtrar)
            filtro.highpass (sr, filtroHz);
        coefRms = coefTempo (sr, rmsMs);
    }
    // Nível do detector em dB para a amostra i.
    float nivel (float* const* ch, int numCh, const float* const* sc, int scCh, int i)
    {
        float pico = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            float s = sc != nullptr ? sc[std::min (c, scCh - 1)][i] : ch[c][i];
            if (filtrar)
                s = filtro.process (s, c);
            pico = std::max (pico, std::abs (s));
        }
        if (coefRms > 0.0f)
        {
            rms = pico * pico + coefRms * (rms - pico * pico);
            return linToDb (std::sqrt (rms) * 1.41421356f);  // escala igual à do pico numa senoide
        }
        return linToDb (pico);
    }
};

// ---------------------------------------------------------------- compressor com caracteres
class CompressorModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        detector.prepare();
        reducao = 0.0f;
        ganho.prepare (sr, 20.0f);
        mistura.prepare (sr, 20.0f);
    }
    void reset() override { detector.prepare(); reducao = 0.0f; }

    void setParams (const float* p) override
    {
        static const float joelho[5] = { 6.0f, 12.0f, 2.0f, 4.0f, 14.0f };
        static const float rmsMs[5] = { 8.0f, 12.0f, 0.0f, 3.0f, 25.0f };
        static const float ataqueMin[5] = { 0.1f, 10.0f, 0.02f, 0.1f, 20.0f };
        modelo = std::min (std::max ((int) p[iModelo], 0), 4);
        limite = p[iLimite];
        taxa = p[iTaxa];
        joelhoDb = joelho[modelo];
        ataque = coefTempo (sampleRate, std::max (p[iAtaque], ataqueMin[modelo]));
        liberacao = coefTempo (sampleRate, p[iLiberacao]);
        liberacaoLenta = coefTempo (sampleRate, p[iLiberacao] * 3.0f);
        usarSidechain = p[iSidechain] >= 0.5f;
        detector.ajustar (sampleRate, p[iFiltro], rmsMs[modelo]);
        ganho.set (dbToLin (p[iGanho]));
        mistura.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        const float* const* sc = usarSidechain && sidechain != nullptr ? sidechain : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float nivel = detector.nivel (ch, numCh, sc, sidechainChannels, i);
            float taxaEfetiva = taxa;
            if (modelo == 4)  // vari-mu: a taxa cresce conforme o sinal passa do limite
                taxaEfetiva = 1.0f + (taxa - 1.0f) * std::min (std::max (0.3f + (nivel - limite) / 12.0f, 0.3f), 1.0f);
            const float alvo = curvaCompressao (nivel, limite, taxaEfetiva, joelhoDb);
            float c = alvo < reducao ? ataque : liberacao;
            if (modelo == 1 && alvo >= reducao)  // óptico: quanto mais fundo comprimiu, mais devagar volta
            {
                const float fundo = std::min (-reducao / 12.0f, 1.0f);
                c = liberacao + (liberacaoLenta - liberacao) * fundo;
            }
            reducao = alvo + c * (reducao - alvo);

            const float g = dbToLin (reducao) * ganho.next(), m = mistura.next();
            const float cor = modelo == 2 ? std::min (-reducao / 15.0f, 1.0f) * 0.25f
                            : modelo == 4 ? std::min (-reducao / 15.0f, 1.0f) * 0.12f : 0.0f;
            for (int k = 0; k < numCh; ++k)
            {
                const float x = ch[k][i];
                float y = x * g;
                if (cor > 0.0f)
                {
                    // FET: harmônicos ímpares; vari-mu: um pouco de pares. Só aparecem quando comprime.
                    const float colorido = modelo == 2 ? std::tanh (1.6f * y) / 1.6f : (std::tanh (1.3f * (y + 0.15f)) - std::tanh (1.3f * 0.15f)) / 1.3f;
                    y += cor * (colorido - y);
                }
                ch[k][i] = x + m * (y - x);
            }
        }
    }

private:
    const int iLimite = ix ("compressor", "threshold_db"), iTaxa = ix ("compressor", "ratio"),
              iAtaque = ix ("compressor", "attack_ms"), iLiberacao = ix ("compressor", "release_ms"),
              iGanho = ix ("compressor", "makeup_db"), iMix = ix ("compressor", "mix"), iModelo = ix ("compressor", "model"),
              iFiltro = ix ("compressor", "sc_hpf_hz"), iSidechain = ix ("compressor", "sidechain");
    double sampleRate = 44100.0;
    Detector detector;
    int modelo = 0;
    float limite = 0.0f, taxa = 1.0f, joelhoDb = 6.0f, ataque = 0.0f, liberacao = 0.0f, liberacaoLenta = 0.0f, reducao = 0.0f;
    bool usarSidechain = false;
    Smoothed ganho, mistura;
};

// ---------------------------------------------------------------- compressor de barramento
class BusCompressorModule : public Module
{
public:
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        detector.prepare();
        rapida = lenta = 0.0f;
        ganho.prepare (sr, 20.0f);
        mistura.prepare (sr, 20.0f);
    }
    void reset() override { detector.prepare(); rapida = lenta = 0.0f; }

    void setParams (const float* p) override
    {
        limite = p[iLimite];
        taxa = p[iTaxa];
        const float ataqueMs = p[iAtaque];
        ataque = coefTempo (sampleRate, ataqueMs);
        automatico = p[iLiberacao] <= 0.0f;
        // Automático: um caminho rápido (100 ms) para os golpes e um lento (1,2 s, que demora uns 300 ms a entrar)
        // para as partes sustentadas. Vale o que reduz mais.
        liberacao = coefTempo (sampleRate, automatico ? 100.0f : p[iLiberacao] * 1000.0f);
        ataqueLento = coefTempo (sampleRate, ataqueMs * 10.0f + 300.0f);  // só entra se a compressão durar
        liberacaoLenta = coefTempo (sampleRate, 1200.0f);
        usarSidechain = p[iSidechain] >= 0.5f;
        detector.ajustar (sampleRate, p[iFiltro], 2.0f);
        ganho.set (dbToLin (p[iGanho]));
        mistura.set (p[iMix]);
    }

    void process (float* const* ch, int numCh, int n) override
    {
        const float* const* sc = usarSidechain && sidechain != nullptr ? sidechain : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float alvo = curvaCompressao (detector.nivel (ch, numCh, sc, sidechainChannels, i), limite, taxa, 4.0f);
            rapida = alvo + (alvo < rapida ? ataque : liberacao) * (rapida - alvo);
            float reducao = rapida;
            if (automatico)
            {
                lenta = alvo + (alvo < lenta ? ataqueLento : liberacaoLenta) * (lenta - alvo);
                reducao = std::min (rapida, lenta);
            }
            const float g = dbToLin (reducao) * ganho.next(), m = mistura.next();
            for (int k = 0; k < numCh; ++k)
                ch[k][i] += m * (ch[k][i] * g - ch[k][i]);
        }
    }

private:
    const int iLimite = ix ("bus_compressor", "threshold_db"), iTaxa = ix ("bus_compressor", "ratio"),
              iAtaque = ix ("bus_compressor", "attack_ms"), iLiberacao = ix ("bus_compressor", "release_s"),
              iGanho = ix ("bus_compressor", "makeup_db"), iMix = ix ("bus_compressor", "mix"),
              iFiltro = ix ("bus_compressor", "sc_hpf_hz"), iSidechain = ix ("bus_compressor", "sidechain");
    double sampleRate = 44100.0;
    Detector detector;
    float limite = 0.0f, taxa = 4.0f, ataque = 0.0f, liberacao = 0.0f, ataqueLento = 0.0f, liberacaoLenta = 0.0f, rapida = 0.0f, lenta = 0.0f;
    bool automatico = true, usarSidechain = false;
    Smoothed ganho, mistura;
};

// ---------------------------------------------------------------- EQ paramétrico de 8 bandas
class EqModule : public Module
{
public:
    EqModule()
    {
        for (int b = 0; b < kBandas; ++b)
        {
            const std::string prefixo = "b" + std::to_string (b + 1) + "_";
            iTipo[b] = ix ("eq", (prefixo + "type").c_str());
            iFreq[b] = ix ("eq", (prefixo + "freq_hz").c_str());
            iGanho[b] = ix ("eq", (prefixo + "gain_db").c_str());
            iQ[b] = ix ("eq", (prefixo + "q").c_str());
        }
    }

    void prepare (double sr, int) override { sampleRate = sr; reset(); }
    void reset() override { for (auto& f : filtros) f.reset(); }

    void setParams (const float* p) override
    {
        for (int b = 0; b < kBandas; ++b)
        {
            const int tipo = (int) p[iTipo[b]];
            const float f = p[iFreq[b]], g = p[iGanho[b]], q = p[iQ[b]];
            ativa[b] = tipo != 0;
            switch (tipo)
            {
                case 1: filtros[b].peak (sampleRate, f, g, q); break;
                case 2: filtros[b].lowShelf (sampleRate, f, g, q); break;
                case 3: filtros[b].highShelf (sampleRate, f, g, q); break;
                case 4: filtros[b].highpass (sampleRate, f, q); break;
                case 5: filtros[b].lowpass (sampleRate, f, q); break;
                default: break;
            }
        }
    }

    void process (float* const* ch, int numCh, int n) override
    {
        for (int b = 0; b < kBandas; ++b)
            if (ativa[b])
                for (int c = 0; c < numCh; ++c)
                    for (int i = 0; i < n; ++i)
                        ch[c][i] = filtros[b].process (ch[c][i], c);
    }

private:
    static constexpr int kBandas = 8;
    int iTipo[kBandas] = {}, iFreq[kBandas] = {}, iGanho[kBandas] = {}, iQ[kBandas] = {};
    bool ativa[kBandas] = {};
    Biquad filtros[kBandas];
    double sampleRate = 44100.0;
};

// ---------------------------------------------------------------- supressor de ressonâncias
// Trabalha frequência por frequência (STFT com 75 % de sobreposição). Em cada quadro compara o nível de
// cada frequência com o contorno do espectro à volta dela; o que sobra acima do limite (ajustado pela curva
// de sensibilidade) vira redução, que entra e sai com os tempos de ataque e liberação.
class ResonanceModule : public Module
{
public:
    ResonanceModule()
    {
        for (int k = 0; k < kPontos; ++k)
        {
            iSensF[k] = ix ("resonance", ("s" + std::to_string (k + 1) + "_freq_hz").c_str());
            iSensG[k] = ix ("resonance", ("s" + std::to_string (k + 1) + "_gain_db").c_str());
        }
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        n = sr > 60000.0 ? 2048 : 1024;
        salto = n / 4;
        fifo = n - salto;
        atraso = n;  // o quadro só sai depois de n amostras
        bins = n / 2 + 1;
        fft.prepare (n);
        janela.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            janela[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) n));
        for (int c = 0; c < 2; ++c)
        {
            entrada[c].assign ((size_t) n, 0.0f);
            saida[c].assign ((size_t) n, 0.0f);
            acumulado[c].assign ((size_t) n, 0.0f);
            re[c].assign ((size_t) n, 0.0f);
            im[c].assign ((size_t) n, 0.0f);
            seco[c].prepare (n + 8);
        }
        nivel.assign ((size_t) bins, 0.0f);
        tabela.assign ((size_t) bins * 12, 0.0f);
        soma.assign ((size_t) bins + 1, 0.0);
        alvo.assign ((size_t) bins, 0.0f);
        suave.assign ((size_t) bins, 0.0f);
        reducao.assign ((size_t) bins, 0.0f);
        limiarBin.assign ((size_t) bins, 0.0f);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (entrada[c].begin(), entrada[c].end(), 0.0f);
            std::fill (saida[c].begin(), saida[c].end(), 0.0f);
            std::fill (acumulado[c].begin(), acumulado[c].end(), 0.0f);
            seco[c].reset();
        }
        std::fill (reducao.begin(), reducao.end(), 0.0f);
        posicao = fifo;
    }

    int latencySamples() const override { return atraso; }

    void setParams (const float* p) override
    {
        profundidade = p[iProf];
        nitidez = p[iNit];
        seletividade = p[iSel];
        const float quadroMs = 1000.0f * (float) salto / (float) sampleRate;
        ataque = std::exp (-quadroMs / std::max (p[iAtaque], 0.1f));
        liberacao = std::exp (-quadroMs / std::max (p[iLiberacao], 0.1f));
        mistura = p[iMix];
        duro = p[iModo] >= 0.5f;
        delta = p[iDelta] >= 0.5f;
        // curva de sensibilidade, interpolada em escala log de frequência para cada bin
        float f[kPontos], g[kPontos];
        int ordem[kPontos];
        for (int k = 0; k < kPontos; ++k) { f[k] = p[iSensF[k]]; g[k] = p[iSensG[k]]; ordem[k] = k; }
        std::sort (ordem, ordem + kPontos, [&f] (int a, int b) { return f[a] < f[b]; });
        for (int b = 0; b < bins; ++b)
        {
            const float fb = std::max ((float) b * (float) sampleRate / (float) n, 1.0f);
            float s = g[ordem[0]];
            if (fb >= f[ordem[kPontos - 1]])
                s = g[ordem[kPontos - 1]];
            else
                for (int k = 1; k < kPontos; ++k)
                    if (fb < f[ordem[k]])
                    {
                        const float a = f[ordem[k - 1]], z = f[ordem[k]];
                        const float u = fb <= a ? 0.0f : std::log (fb / a) / std::log (z / a);
                        s = g[ordem[k - 1]] + u * (g[ordem[k]] - g[ordem[k - 1]]);
                        break;
                    }
            // Abaixo de 3 kHz o harmônico mais forte de cada nota se destaca dos vizinhos por natureza (é a
            // nota e o formante, não ressonância). O limite sobe até 24 dB abaixo de 500 Hz para isso não virar
            // corte; para tratar essa região de propósito, o músico empurra a curva de sensibilidade ali.
            const float extra = fb <= 500.0f ? 24.0f : fb >= 3000.0f ? 0.0f : 24.0f * std::log (3000.0f / fb) / std::log (3000.0f / 500.0f);
            limiarBin[(size_t) b] = (seletividade - 1.0f) * 0.8f + extra - 0.75f * s;
        }
    }

    void process (float* const* ch, int numCh, int numSamples) override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                entrada[c][(size_t) posicao] = x;
                const float tratado = saida[c][(size_t) (posicao - fifo)];
                const float original = seco[c].read ((float) atraso);
                seco[c].write (x);
                ch[c][i] = delta ? mistura * tratado : original + mistura * (tratado - original);
            }
            if (++posicao >= n)
            {
                posicao = fifo;
                quadro (numCh);
            }
        }
    }

private:
    void mediaLog (const std::vector<float>& origem, std::vector<float>& destino, float oitavas)
    {
        soma[0] = 0.0;
        for (int b = 0; b < bins; ++b)
            soma[(size_t) b + 1] = soma[(size_t) b] + origem[(size_t) b];
        const float r = std::pow (2.0f, oitavas * 0.5f);
        for (int b = 0; b < bins; ++b)
        {
            const int lo = std::max (1, std::min (b - 1, (int) std::floor ((float) b / r)));
            const int hi = std::min (bins - 1, std::max (b + 1, (int) std::ceil ((float) b * r)));
            destino[(size_t) b] = (float) ((soma[(size_t) hi + 1] - soma[(size_t) lo]) / (double) (hi - lo + 1));
        }
    }

    // Tabela para achar o máximo de qualquer trecho de bins em tempo constante.
    void construirMaximos()
    {
        for (int b = 0; b < bins; ++b)
            tabela[(size_t) b] = nivel[(size_t) b];
        for (int k = 1; (1 << k) <= bins; ++k)
        {
            const size_t base = (size_t) k * (size_t) bins, antes = (size_t) (k - 1) * (size_t) bins;
            for (int b = 0; b + (1 << k) <= bins; ++b)
                tabela[base + (size_t) b] = std::max (tabela[antes + (size_t) b], tabela[antes + (size_t) (b + (1 << (k - 1)))]);
        }
    }
    float maximo (int lo, int hi) const
    {
        int k = 0;
        while ((2 << k) <= hi - lo + 1)
            ++k;
        return std::max (tabela[(size_t) k * (size_t) bins + (size_t) lo], tabela[(size_t) k * (size_t) bins + (size_t) (hi - (1 << k) + 1)]);
    }

    void quadro (int numCh)
    {
        for (int c = 0; c < numCh; ++c)
        {
            for (int i = 0; i < n; ++i)
            {
                re[c][(size_t) i] = entrada[c][(size_t) i] * janela[(size_t) i];
                im[c][(size_t) i] = 0.0f;
            }
            fft.transform (re[c].data(), im[c].data(), false);
        }

        // nível de cada frequência, com os canais ligados para não mexer na imagem estéreo
        for (int b = 0; b < bins; ++b)
        {
            float m = 0.0f;
            for (int c = 0; c < numCh; ++c)
                m = std::max (m, re[c][(size_t) b] * re[c][(size_t) b] + im[c][(size_t) b] * im[c][(size_t) b]);
            nivel[(size_t) b] = 10.0f * std::log10 (m + 1.0e-20f);
        }
        // Referência de cada frequência: o pico mais forte entre as vizinhas (uma oitava para cada lado),
        // fora do próprio lóbulo. Uma oitava garante que o harmônico vizinho de uma nota aguda entre na
        // comparação; assim os harmônicos não contam como ressonância, só o que fica acima deles.
        construirMaximos();
        const float r = 2.0f;
        for (int b = 0; b < bins; ++b)
        {
            const int guarda = 3;
            // nos graves a meia oitava tem poucos bins: garante pelo menos três vizinhos de cada lado
            const int lo = std::max (1, std::min ((int) std::floor ((float) b / r), b - guarda - 3));
            const int hi = std::min (bins - 1, std::max ((int) std::ceil ((float) b * r), b + guarda + 3));
            float viz = -400.0f;
            if (b - guarda - 1 >= lo) viz = std::max (viz, maximo (lo, b - guarda - 1));
            if (b + guarda + 1 <= hi) viz = std::max (viz, maximo (b + guarda + 1, hi));
            suave[(size_t) b] = viz > -399.0f ? viz : nivel[(size_t) b];  // sem vizinhos, nada a comparar
        }

        const float inclinacao = duro ? 1.0f : 0.55f;
        const float hzPorBin = (float) sampleRate / (float) n;
        float pico = -400.0f;
        for (int b = 0; b < bins; ++b)
            pico = std::max (pico, nivel[(size_t) b]);
        for (int b = 0; b < bins; ++b)
        {
            const float f = (float) b * hzPorBin;
            const bool audivel = f >= 25.0f && f <= 18000.0f && nivel[(size_t) b] > pico - 70.0f;
            const float sobra = nivel[(size_t) b] - suave[(size_t) b] - limiarBin[(size_t) b];
            alvo[(size_t) b] = audivel ? std::min (std::max (sobra * inclinacao, 0.0f), profundidade) : 0.0f;
        }
        // nitidez: quanto mais baixa, mais a redução se espalha para as frequências vizinhas.
        // O pico em si mantém a redução inteira; o espalhamento só acrescenta em volta.
        mediaLog (alvo, suave, 0.5f / nitidez);
        for (int b = 0; b < bins; ++b)
            suave[(size_t) b] = std::max (suave[(size_t) b], alvo[(size_t) b]);

        for (int b = 0; b < bins; ++b)
        {
            const float a = suave[(size_t) b];
            float& r = reducao[(size_t) b];
            r = a + (a > r ? ataque : liberacao) * (r - a);
            const float g = dbToLin (-r);
            const float aplicar = delta ? 1.0f - g : g;
            for (int c = 0; c < numCh; ++c)
            {
                re[c][(size_t) b] *= aplicar;
                im[c][(size_t) b] *= aplicar;
                if (b > 0 && b < n / 2)  // espelho, para o sinal continuar real
                {
                    re[c][(size_t) (n - b)] *= aplicar;
                    im[c][(size_t) (n - b)] *= aplicar;
                }
            }
        }

        for (int c = 0; c < numCh; ++c)
        {
            fft.transform (re[c].data(), im[c].data(), true);
            auto& acc = acumulado[c];
            for (int i = 0; i < n; ++i)
                acc[(size_t) i] += re[c][(size_t) i] * janela[(size_t) i] * 0.5f;  // janela² com 75 % de sobreposição soma 2
            for (int i = 0; i < salto; ++i)
                saida[c][(size_t) i] = acc[(size_t) i];
            std::copy (acc.begin() + salto, acc.end(), acc.begin());
            std::fill (acc.end() - salto, acc.end(), 0.0f);
            std::copy (entrada[c].begin() + salto, entrada[c].end(), entrada[c].begin());
        }
    }

    static constexpr int kPontos = 6;
    const int iProf = ix ("resonance", "depth_db"), iNit = ix ("resonance", "sharpness"), iSel = ix ("resonance", "selectivity"),
              iAtaque = ix ("resonance", "attack_ms"), iLiberacao = ix ("resonance", "release_ms"), iMix = ix ("resonance", "mix"),
              iModo = ix ("resonance", "mode"), iDelta = ix ("resonance", "delta");
    int iSensF[kPontos] = {}, iSensG[kPontos] = {};
    double sampleRate = 44100.0;
    int n = 1024, salto = 256, fifo = 768, atraso = 1024, bins = 513, posicao = 768;
    Fft fft;
    std::vector<float> janela, nivel, alvo, suave, reducao, limiarBin, tabela;
    std::vector<double> soma;
    std::vector<float> entrada[2], saida[2], acumulado[2], re[2], im[2];
    DelayLine seco[2];
    float profundidade = 6.0f, nitidez = 5.0f, seletividade = 5.0f, ataque = 0.0f, liberacao = 0.0f, mistura = 1.0f;
    bool duro = false, delta = false;
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
        envelope.set (sr, 0.5f, 40.0f);
        gain.prepare (sr, 3.0f);
        gain.set (1.0f);
        reset();
    }

    void reset() override { crossover.reset(); envelope.value = 0.0f; }

    void setParams (const float* p) override
    {
        crossover.set (sampleRate, p[iFreq]);
        thresholdDb = p[iThreshold];
        maxReductionDb = p[iMaxReduction];
        larga = p[iModo] >= 0.5f;
        ouvir = p[iOuvir] >= 0.5f;
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
            // joelho suave de 3 dB e taxa 4:1, limitado à redução máxima
            const float acima = overDb < -1.5f ? 0.0f : overDb > 1.5f ? overDb : (overDb + 1.5f) * (overDb + 1.5f) / 6.0f;
            gain.set (dbToLin (-std::min (acima * 0.75f, maxReductionDb)));
            const float g = gain.next();
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = ouvir ? high[c] * g : larga ? (low[c] + high[c]) * g : low[c] + high[c] * g;
        }
    }

private:
    const int iFreq = ix ("de_esser", "freq_hz"), iThreshold = ix ("de_esser", "threshold_db"),
              iMaxReduction = ix ("de_esser", "max_reduction_db"), iModo = ix ("de_esser", "mode"), iOuvir = ix ("de_esser", "listen");
    double sampleRate = 44100.0;
    Crossover crossover;
    Envelope envelope;
    Smoothed gain;
    float thresholdDb = -30.0f, maxReductionDb = 6.0f;
    bool larga = false, ouvir = false;
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
    if (type == "eq")               return std::make_unique<EqModule>();
    if (type == "compressor")       return std::make_unique<CompressorModule>();
    if (type == "bus_compressor")   return std::make_unique<BusCompressorModule>();
    if (type == "resonance")        return std::make_unique<ResonanceModule>();
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
