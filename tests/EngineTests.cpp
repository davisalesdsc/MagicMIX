// Testes do motor de áudio. C++ puro, sem JUCE: compila e roda em segundos.
//
//   g++ -std=c++17 -O2 -ISource/Engine tests/EngineTests.cpp Source/Engine/*.cpp -o EngineTests && ./EngineTests

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "Analyzer.h"
#include "Catalog.h"
#include "Chain.h"
#include "Edicao.h"
#include "Validate.h"

using namespace mm;

// ---------------------------------------------------------------------------
// Conta alocações de memória para provar que process() não aloca.
static std::atomic<int> gAllocations { 0 };
static std::atomic<bool> gCounting { false };

void* operator new (std::size_t size)
{
    if (gCounting.load())
        ++gAllocations;
    if (void* p = std::malloc (size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

// ---------------------------------------------------------------------------
static int gFailures = 0, gChecks = 0;
static const char* gTest = "";

#define CHECK(condition) \
    do { ++gChecks; if (! (condition)) { ++gFailures; std::printf ("  FALHOU [%s] linha %d: %s\n", gTest, __LINE__, #condition); } } while (false)

static bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

constexpr double kSampleRate = 44100.0;
constexpr int kSecond = 44100;
constexpr float kTwoPi = 6.28318530717958647692f;

struct Audio
{
    std::vector<float> left, right;
    bool stereo = false;

    int size() const { return (int) left.size(); }
    float peak() const
    {
        float p = 0.0f;
        for (float v : left) p = std::max (p, std::abs (v));
        if (stereo) for (float v : right) p = std::max (p, std::abs (v));
        return p;
    }
    float peakDb() const { return 20.0f * std::log10 (std::max (peak(), 1.0e-9f)); }
    Features features() const
    {
        const float* ch[2] = { left.data(), right.data() };
        return analyze (ch, stereo ? 2 : 1, size(), kSampleRate);
    }
    Audio slice (double fromSeconds, double toSeconds) const
    {
        Audio out;
        out.stereo = stereo;
        const int a = (int) (fromSeconds * kSampleRate), b = (int) (toSeconds * kSampleRate);
        out.left.assign (left.begin() + a, left.begin() + b);
        if (stereo) out.right.assign (right.begin() + a, right.begin() + b);
        return out;
    }
};

static Audio sine (float freq, float peakDbfs = -12.0f, double seconds = 1.0, bool stereo = false)
{
    Audio a;
    a.stereo = stereo;
    const float amplitude = std::pow (10.0f, peakDbfs / 20.0f);
    a.left.resize ((size_t) (seconds * kSampleRate));
    for (size_t i = 0; i < a.left.size(); ++i)
        a.left[i] = amplitude * std::sin (kTwoPi * freq * (float) i / (float) kSampleRate);
    if (stereo) a.right = a.left;
    return a;
}

static Audio noise (double seconds = 1.0, float peakDbfs = -12.0f, unsigned seed = 1, bool stereo = false)
{
    Audio a;
    a.stereo = stereo;
    const float amplitude = std::pow (10.0f, peakDbfs / 20.0f);
    auto fill = [&] (std::vector<float>& v, unsigned s) {
        v.resize ((size_t) (seconds * kSampleRate));
        for (auto& x : v)
        {
            s = s * 1664525u + 1013904223u;
            x = amplitude * ((float) (s >> 8) / 8388608.0f - 1.0f);
        }
    };
    fill (a.left, seed);
    if (stereo) fill (a.right, seed + 77u);
    return a;
}

static Audio add (Audio a, const Audio& b)
{
    for (size_t i = 0; i < a.left.size(); ++i)
        a.left[i] += b.left[i];
    return a;
}

static Audio click (bool stereo = false)
{
    Audio a;
    a.stereo = stereo;
    a.left.assign ((size_t) kSecond, 0.0f);
    for (int i = 0; i < 50; ++i) a.left[(size_t) i] = 0.5f;
    if (stereo) a.right = a.left;
    return a;
}

static Audio drumHits()
{
    Audio a;
    a.left.assign ((size_t) kSecond, 0.0f);
    for (double start : { 0.1, 0.4, 0.7 })
        for (int i = (int) (start * kSampleRate), k = 0; i < kSecond; ++i, ++k)
        {
            const float t = (float) k / (float) kSampleRate;
            a.left[(size_t) i] += 0.25f * std::sin (kTwoPi * 120.0f * t) * std::exp (-8.0f * t);
        }
    return a;
}

static float bandDb (const Audio& a, int band)
{
    const auto f = a.features();
    return f.rmsDbfs + f.bandEnergyDb[band];
}
enum { kSub, kLow, kLowMid, kMid, kPresence, kAir };

static ChainSpec specOf (std::vector<ModuleSpec> modules, std::vector<MacroSpec> macros = {})
{
    ChainSpec spec;
    spec.modules = std::move (modules);
    spec.macros = std::move (macros);
    return spec;
}

static ChainSpec clean (const ChainSpec& spec, const Features* features = nullptr, std::vector<std::string>* reportOut = nullptr)
{
    std::vector<std::string> report;
    auto out = validate (spec, features, report);
    if (reportOut != nullptr) *reportOut = report;
    return out;
}

// Roda um spec validado sobre o áudio, em blocos, como um host faria.
static Audio run (const ChainSpec& spec, Audio audio, const float* macroOverride = nullptr, int blockSize = 512)
{
    Chain chain (spec);
    chain.prepare (kSampleRate, blockSize, audio.stereo ? 2 : 1);
    float macros[kMaxMacros] = {};
    for (size_t i = 0; i < spec.macros.size() && i < (size_t) kMaxMacros; ++i)
        macros[i] = spec.macros[i].value;
    if (macroOverride != nullptr)
        for (int i = 0; i < kMaxMacros; ++i)
            macros[i] = macroOverride[i];

    for (int start = 0; start < audio.size(); start += blockSize)
    {
        const int n = std::min (blockSize, audio.size() - start);
        float* ch[2] = { audio.left.data() + start, audio.stereo ? audio.right.data() + start : nullptr };
        chain.process (ch, audio.stereo ? 2 : 1, n, macros, spec.mix, spec.outputGainDb);
    }
    return audio;
}

static Audio runOne (const char* type, std::map<std::string, float> params, const Audio& audio)
{
    std::vector<std::string> report;
    const auto spec = clean (specOf ({ { "m", type, std::move (params) } }), nullptr, &report);
    if (! report.empty())
    {
        ++gFailures;
        std::printf ("  FALHOU [%s]: parâmetros de '%s' foram corrigidos: %s\n", gTest, type, report[0].c_str());
    }
    return run (spec, audio);
}

static bool allFinite (const Audio& a)
{
    for (float v : a.left) if (! std::isfinite (v)) return false;
    for (float v : a.right) if (! std::isfinite (v)) return false;
    return true;
}

// ---------------------------------------------------------------------------
static void testCatalog()
{
    gTest = "catalogo";
    CHECK (catalog().size() == 21);
    CHECK (findModule ("compressor") != nullptr && findModule ("exciter_neural") == nullptr);
    CHECK (catalogText().find ("- highpass: ") == 0);
    for (auto& m : catalog())
    {
        CHECK (createModule (m.type) != nullptr);
        CHECK ((int) m.params.size() <= kMaxParams);
    }
}

static void testValidator()
{
    gTest = "validador";
    std::vector<std::string> report;
    auto spec = specOf (
        { { "brilho", "high_shelf", { { "freq_hz", 6000 }, { "gain_db", 30 } } },
          { "comp", "compressor", { { "ratio", 20 }, { "bogus", 1 } } },
          { "magia", "exciter_neural", { { "amount", 11 } } },
          { "comp", "gain", {} } },
        { { "Brilho", "", 3.0f, { { "brilho", "gain_db", 0, 40 }, { "magia", "amount", 0, 11 } } },
          { "Outro", "", 0.5f, { { "brilho", "gain_db", 0, 1 } } } });
    auto out = clean (spec, nullptr, &report);
    CHECK (out.modules.size() == 2);                       // tipo desconhecido e id repetido saem
    CHECK (out.modules[0].params["gain_db"] == 12.0f);
    CHECK (out.modules[1].params["ratio"] == 10.0f);
    CHECK (out.modules[1].params.size() == findModule ("compressor")->params.size());  // padrões preenchidos
    CHECK (out.macros.size() == 1 && out.macros[0].value == 1.0f);  // "Outro" só tinha alvo já ocupado
    CHECK (out.macros[0].targets.size() == 1 && out.macros[0].targets[0].valueAt1 == 12.0f);
    CHECK (report.size() >= 7);

    gTest = "validador: limites";
    std::vector<ModuleSpec> many;
    for (int i = 0; i < kMaxModules + 3; ++i)
        many.push_back ({ "g" + std::to_string (i), "gain", {} });
    CHECK ((int) clean (specOf (many)).modules.size() == kMaxModules);
    CHECK (clean (specOf ({ { "g", "gain", { { "gain_db", std::nanf ("") } } } })).modules[0].params["gain_db"] == 0.0f);

    gTest = "validador: regras do sinal";
    Features bright;
    bright.valid = true;
    bright.spectralCentroidHz = 5000;
    bright.peakDbfs = -4.0f;
    bright.crestFactorDb = 5.0f;
    auto ruled = clean (specOf ({ { "hs", "high_shelf", { { "gain_db", 9 } } },
                                  { "g", "gain", { { "gain_db", 10 } } },
                                  { "c", "compressor", { { "ratio", 8 } } } }),
                        &bright);
    CHECK (ruled.modules[0].params["gain_db"] == 6.0f);
    CHECK (near (ruled.modules[1].params["gain_db"], 3.0, 1e-4));
    CHECK (ruled.modules[2].params["ratio"] == 3.0f);

    gTest = "validador: teto preserva o ponto de partida";
    Features headroom;
    headroom.valid = true;
    headroom.peakDbfs = -9.0f;  // 8 dB de folga
    headroom.crestFactorDb = 15.0f;
    auto volume = clean (specOf ({ { "vol", "gain", {} } }, { { "Volume", "", 0.5f, { { "vol", "gain_db", -12, 12 } } } }), &headroom);
    const auto& target = volume.macros[0].targets[0];
    CHECK (near (target.valueAt1, 8.0, 1e-4));
    CHECK (near (interpolate (target.valueAt0, target.valueAt1, 0.5f, false), 0.0, 1e-4));  // continua começando em 0 dB

    gTest = "interpolacao";
    CHECK (interpolate (0, 10, 0.5f, false) == 5.0f);
    CHECK (near (interpolate (100, 10000, 0.5f, true), 1000.0, 0.5));
    CHECK (interpolate (0, 10, 7.0f, false) == 10.0f);
}

static void testEveryModuleRuns()
{
    gTest = "todo modulo roda";
    for (auto& m : catalog())
        for (bool stereo : { false, true })
        {
            const auto out = run (clean (specOf ({ { "m", m.type, {} } })), noise (0.5, -12.0f, 3, stereo));
            CHECK (allFinite (out));
            CHECK (out.peak() > 0.0f);
        }
}

static void testChainBasics()
{
    gTest = "cadeia vazia e transparente";
    const auto x = sine (440.0f);
    const auto same = run (ChainSpec(), x);
    bool identical = true;
    for (size_t i = 0; i < x.left.size(); ++i)
        identical = identical && near (same.left[i], x.left[i], 1e-6);
    CHECK (identical);

    gTest = "mix e ganho da cadeia";
    auto spec = clean (specOf ({ { "g", "gain", { { "gain_db", -20 } } } }));
    spec.mix = 0.0f;
    CHECK (near (run (spec, x).slice (0.5, 1.0).peakDb(), -12.0, 0.1));       // só o som original
    spec.mix = 1.0f;
    spec.outputGainDb = 6.0f;
    CHECK (near (run (spec, x).slice (0.5, 1.0).peakDb(), -12.0 - 20.0 + 6.0, 0.2));

    gTest = "limitador de seguranca";
    auto loud = clean (specOf ({ { "g", "gain", { { "gain_db", 12 } } } }));
    CHECK (run (loud, sine (200.0f, -3.0f)).peakDb() <= Chain::kSafetyCeilingDb + 0.05f);

    gTest = "macro move o parametro";
    auto macro = clean (specOf ({ { "g", "gain", {} } }, { { "Volume", "", 0.5f, { { "g", "gain_db", -12, 12 } } } }));
    float low[kMaxMacros] = { 0.0f }, mid[kMaxMacros] = { 0.5f };
    CHECK (near (run (macro, sine (440.0f, -20.0f), mid).slice (0.5, 1.0).peakDb(), -20.0, 0.1));
    CHECK (near (run (macro, sine (440.0f, -20.0f), low).slice (0.5, 1.0).peakDb(), -32.0, 0.1));

    gTest = "resultado nao depende do tamanho do bloco";
    auto chain = clean (specOf ({ { "c", "compressor", { { "threshold_db", -30 }, { "ratio", 4 } } },
                                  { "d", "delay", { { "time_ms", 120 }, { "feedback", 0.4f } } },
                                  { "r", "reverb", {} } }));
    const auto a = run (chain, noise (0.5, -12.0f, 5), nullptr, 64), b = run (chain, noise (0.5, -12.0f, 5), nullptr, 512);
    double difference = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i)
        difference = std::max (difference, (double) std::abs (a.left[i] - b.left[i]));
    CHECK (difference < 1e-5);
}

static void testModules()
{
    gTest = "filtros mudam o espectro";
    const auto two = add (sine (200.0f), sine (8000.0f));
    CHECK (runOne ("lowpass", { { "freq_hz", 500 } }, two).features().spectralCentroidHz < two.features().spectralCentroidHz / 2);
    CHECK (bandDb (runOne ("highpass", { { "freq_hz", 1000 } }, two), kLow) < bandDb (two, kLow) - 20.0f);
    CHECK (near (runOne ("peak", { { "freq_hz", 1000 }, { "gain_db", 6 }, { "q", 1 } }, sine (1000.0f, -20.0f)).slice (0.5, 1.0).peakDb(), -14.0, 0.3));
    CHECK (near (runOne ("high_shelf", { { "freq_hz", 2000 }, { "gain_db", -6 } }, sine (10000.0f, -20.0f)).slice (0.5, 1.0).peakDb(), -26.0, 0.5));
    CHECK (near (runOne ("low_shelf", { { "freq_hz", 400 }, { "gain_db", 6 } }, sine (60.0f, -20.0f)).slice (0.5, 1.0).peakDb(), -14.0, 0.5));

    gTest = "compressor";
    const auto hits = drumHits();
    std::map<std::string, float> squash = { { "threshold_db", -40 }, { "ratio", 8 }, { "attack_ms", 0.5f }, { "release_ms", 100 } };
    const auto full = runOne ("compressor", squash, hits);
    squash["mix"] = 0.5f;
    const auto parallel = runOne ("compressor", squash, hits);
    CHECK (full.peak() < parallel.peak() && parallel.peak() < hits.peak());
    const auto quiet = sine (440.0f, -50.0f);
    CHECK (near (runOne ("compressor", { { "threshold_db", -20 } }, quiet).peakDb(), -50.0, 0.1));  // abaixo do limite não mexe

    gTest = "clipper";
    CHECK (runOne ("clipper", { { "ceiling_db", -12 }, { "softness", 0.5f } }, sine (200.0f, -3.0f)).peakDb() <= -12.0f + 0.01f);
    CHECK (near (runOne ("clipper", { { "ceiling_db", -12 }, { "softness", 0.0f } }, sine (200.0f, -3.0f)).peakDb(), -12.0, 0.01));
    CHECK (near (runOne ("clipper", { { "ceiling_db", -12 } }, sine (200.0f, -30.0f)).peakDb(), -30.0, 0.01));

    gTest = "saturacao e fita";
    CHECK (bandDb (runOne ("saturation", { { "drive_db", 24 }, { "mix", 1 } }, sine (1000.0f, -6.0f)), kPresence)
           > bandDb (sine (1000.0f, -6.0f), kPresence) + 10.0f);  // harmônicos novos
    const auto hiss = noise();
    CHECK (bandDb (runOne ("tape", { { "drive_db", 0 }, { "tone_hz", 4000 }, { "wow", 0 } }, hiss), kAir)
           < bandDb (runOne ("tape", { { "drive_db", 0 }, { "tone_hz", 20000 }, { "wow", 0 } }, hiss), kAir) - 3.0f);

    gTest = "exciter";
    const auto tone = sine (1500.0f, -6.0f);
    CHECK (bandDb (runOne ("exciter", { { "freq_hz", 1000 }, { "drive_db", 24 }, { "amount", 1 } }, tone), kPresence) > bandDb (tone, kPresence) + 10.0f);
    CHECK (near (runOne ("exciter", { { "amount", 0 } }, tone).peakDb(), -6.0, 0.05));

    gTest = "de-esser";
    const auto sibilant = add (sine (200.0f, -12.0f), noise (1.0, -6.0f));
    const auto tamed = runOne ("de_esser", { { "freq_hz", 6000 }, { "threshold_db", -40 }, { "max_reduction_db", 9 } }, sibilant);
    CHECK (bandDb (tamed, kAir) < bandDb (sibilant, kAir) - 4.0f);
    CHECK (near (bandDb (tamed, kLow), bandDb (sibilant, kLow), 0.5));
    const auto soft = add (sine (200.0f), noise (1.0, -50.0f));
    CHECK (near (bandDb (runOne ("de_esser", { { "threshold_db", -10 } }, soft), kAir), bandDb (soft, kAir), 0.5));

    gTest = "compressor de uma banda";
    const auto lowHeavy = add (sine (150.0f, -6.0f), sine (3000.0f, -20.0f));
    const auto banded = runOne ("band_compressor", { { "low_hz", 80 }, { "high_hz", 400 }, { "threshold_db", -30 }, { "ratio", 8 } }, lowHeavy);
    CHECK (bandDb (banded, kLow) < bandDb (lowHeavy, kLow) - 6.0f);
    CHECK (near (bandDb (banded, kPresence), bandDb (lowHeavy, kPresence), 0.7));
    const auto flat = noise (1.0, -40.0f);  // abaixo do limite: os três caminhos somam de volta
    const auto rebuilt = runOne ("band_compressor", { { "threshold_db", 0 } }, flat);
    for (int band : { kLow, kLowMid, kMid, kPresence })
        CHECK (near (bandDb (rebuilt, band), bandDb (flat, band), 0.5));

    gTest = "transient shaper";
    const float base = hits.features().crestFactorDb;
    CHECK (runOne ("transient_shaper", { { "attack_db", 9 }, { "sustain_db", -6 } }, hits).features().crestFactorDb > base + 1.0f);
    CHECK (runOne ("transient_shaper", { { "attack_db", -9 }, { "sustain_db", 6 } }, hits).features().crestFactorDb < base - 1.0f);
    CHECK (near (runOne ("transient_shaper", {}, hits).peak(), hits.peak(), 1e-4));

    gTest = "largura estereo";
    const auto wide = noise (0.5, -12.0f, 9, true);
    const auto collapsed = runOne ("stereo_width", { { "width", 0 } }, wide).slice (0.2, 0.5);
    bool mono = true;
    for (size_t i = 0; i < collapsed.left.size(); ++i)
        mono = mono && near (collapsed.left[i], collapsed.right[i], 1e-5);
    CHECK (mono);
    CHECK (runOne ("stereo_width", { { "width", 2 } }, wide).features().stereoCorrelation < wide.features().stereoCorrelation - 0.3f);
}

static double firstSound (const std::vector<float>& x, double afterSeconds, float threshold = 1.0e-3f)
{
    for (int i = (int) (afterSeconds * kSampleRate); i < (int) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > threshold)
            return i / kSampleRate;
    return 1.0e9;
}

static void testTimeEffects()
{
    gTest = "delay";
    const auto echo = runOne ("delay", { { "time_ms", 200 }, { "feedback", 0.5f }, { "mix", 0.5f } }, click());
    CHECK (near (firstSound (echo.left, 0.01), 0.2, 0.002));
    CHECK (echo.slice (0.39, 0.42).peak() > 0.02f && echo.slice (0.39, 0.42).peak() < echo.slice (0.19, 0.22).peak());

    gTest = "delay: filtros so nas repeticoes";
    Audio burst = noise (0.2);
    burst.left.resize ((size_t) kSecond, 0.0f);
    std::map<std::string, float> base = { { "time_ms", 300 }, { "feedback", 0 }, { "mix", 0.5f } };
    const auto brightEcho = runOne ("delay", base, burst);
    base["lowpass_hz"] = 1500;
    const auto darkEcho = runOne ("delay", base, burst);
    CHECK (bandDb (darkEcho.slice (0.3, 0.5), kAir) < bandDb (brightEcho.slice (0.3, 0.5), kAir) - 10.0f);
    CHECK (near (bandDb (darkEcho.slice (0.0, 0.2), kAir), bandDb (brightEcho.slice (0.0, 0.2), kAir), 0.5));

    gTest = "delay ping-pong";
    const auto pong = runOne ("delay", { { "time_ms", 200 }, { "feedback", 0.5f }, { "mix", 0.5f }, { "ping_pong", 1 } }, click (true));
    const auto first = pong.slice (0.195, 0.225), second = pong.slice (0.395, 0.425);
    auto peakOf = [] (const std::vector<float>& v) { float p = 0; for (float x : v) p = std::max (p, std::abs (x)); return p; };
    CHECK (peakOf (first.left) > 10.0f * peakOf (first.right));    // 1ª repetição à esquerda
    CHECK (peakOf (second.right) > 10.0f * peakOf (second.left));  // 2ª à direita

    gTest = "reverb";
    const auto dry = click();
    const auto wet = runOne ("reverb", { { "size", 0.8f }, { "mix", 0.5f } }, dry);
    CHECK (wet.slice (0.3, 0.6).peak() > 1.0e-4f);                 // tem cauda
    CHECK (firstSound (runOne ("reverb", { { "mix", 0.5f }, { "pre_delay_ms", 0 } }, dry).left, 0.005) < 0.06);
    CHECK (firstSound (runOne ("reverb", { { "mix", 0.5f }, { "pre_delay_ms", 150 } }, dry).left, 0.005) > 0.14);
    CHECK (near (runOne ("reverb", { { "mix", 0 } }, sine (440.0f)).slice (0.5, 1.0).peakDb(), -12.0, 0.05));

    gTest = "chorus";
    CHECK (runOne ("chorus", { { "mix", 0.5f }, { "depth", 0.5f } }, sine (440.0f, -12.0f, 1.0, true)).features().stereoCorrelation < 0.999f);
}

static void testAnalyzer()
{
    gTest = "analise";
    const auto f = sine (1000.0f).features();
    CHECK (near (f.peakDbfs, -12.0, 0.1));
    CHECK (near (f.crestFactorDb, 3.0, 0.2));
    CHECK (near (f.spectralCentroidHz, 1000, 30));
    CHECK (f.bandEnergyDb[kMid] > -1.0f);
    CHECK (f.channels == 1 && ! f.hasCorrelation);

    Audio silence;
    silence.stereo = true;
    silence.left.assign (100, 0.0f);
    silence.right.assign (100, 0.0f);
    CHECK (silence.features().valid && std::isfinite (silence.features().peakDbfs));

    gTest = "analise: ressonancia";
    CHECK (noise (3.0).features().resonances.empty());
    const auto ringing = runOne ("peak", { { "freq_hz", 1000 }, { "gain_db", 12 }, { "q", 8 } }, noise (3.0)).features();
    bool found = false;
    for (auto& r : ringing.resonances)
        found = found || std::abs (r.freqHz - 1000) < 60;
    CHECK (found);
}

// ---------------------------------------------------------------------------
// Efeitos da versão 2: EQ de 8 bandas, compressor com caracteres, Bus Comp, sidechain, de-esser e supressor.

// Amplitude (em dB) de uma frequência num trecho, pelo algoritmo de Goertzel.
static double tomDb (const std::vector<float>& x, double freq, double deSeg, double ateSeg)
{
    const int a = (int) (deSeg * kSampleRate), b = std::min ((int) (ateSeg * kSampleRate), (int) x.size());
    const double w = 2.0 * 3.14159265358979323846 * freq / kSampleRate, coef = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = a; i < b; ++i) { const double s = x[(size_t) i] + coef * s1 - s2; s2 = s1; s1 = s; }
    const double potencia = s1 * s1 + s2 * s2 - coef * s1 * s2;
    return 20.0 * std::log10 (std::max (2.0 * std::sqrt (std::max (potencia, 0.0)) / (b - a), 1e-12));
}

static double rmsDb (const std::vector<float>& x, double deSeg, double ateSeg)
{
    const int a = (int) (deSeg * kSampleRate), b = std::min ((int) (ateSeg * kSampleRate), (int) x.size());
    double s = 0.0;
    for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return 10.0 * std::log10 (std::max (s / std::max (b - a, 1), 1e-24));
}

static Audio runWithSidechain (const ChainSpec& spec, Audio audio, const Audio* sc)
{
    Chain chain (spec);
    chain.prepare (kSampleRate, 512, audio.stereo ? 2 : 1);
    float macros[kMaxMacros] = {};
    for (int start = 0; start < audio.size(); start += 512)
    {
        const int n = std::min (512, audio.size() - start);
        float* ch[2] = { audio.left.data() + start, audio.stereo ? audio.right.data() + start : nullptr };
        const float* scp[1] = { sc != nullptr ? sc->left.data() + start : nullptr };
        chain.process (ch, audio.stereo ? 2 : 1, n, macros, spec.mix, spec.outputGainDb, sc != nullptr ? scp : nullptr, sc != nullptr ? 1 : 0);
    }
    return audio;
}

static void testCatalogV2()
{
    gTest = "catalogo v2: escolhas e degraus";
    const auto* comp = findModule ("compressor");
    const auto& modelo = comp->params[(size_t) comp->paramIndex ("model")];
    CHECK (modelo.isChoice() && modelo.labelList.size() == 5 && modelo.labelFor (2.0f) == "FET");
    CHECK (modelo.clamp (2.6f) == 3.0f);
    const auto* bus = findModule ("bus_compressor");
    const auto& taxa = bus->params[(size_t) bus->paramIndex ("ratio")];
    CHECK (taxa.steps.size() == 4 && taxa.clamp (3.2f) == 4.0f && taxa.labelFor (4.0f) == "4:1");
    const auto& lib = bus->params[(size_t) bus->paramIndex ("release_s")];
    CHECK (lib.labelFor (0.0f) == "Auto" && lib.clamp (0.5f) == 0.6f);
    CHECK (findModule ("eq")->params.size() == 32 && findModule ("resonance")->params.size() == 20);
    CHECK ((int) findModule ("eq")->params.size() <= kMaxParams);
    CHECK (catalogText().find ("escolha (0 = Limpo, 1 = Óptico") != std::string::npos);
}

static void testEq8()
{
    gTest = "EQ de 8 bandas";
    const auto tom = sine (1000.0f, -20.0f);
    CHECK (near (runOne ("eq", {}, tom).slice (0.5, 1.0).peakDb(), -20.0, 0.01));  // todas desligadas: transparente
    CHECK (near (runOne ("eq", { { "b3_type", 1 }, { "b3_freq_hz", 1000 }, { "b3_gain_db", 6 }, { "b3_q", 1 } }, tom).slice (0.5, 1.0).peakDb(), -14.0, 0.3));
    const auto grave = sine (100.0f, -20.0f);
    CHECK (runOne ("eq", { { "b1_type", 4 }, { "b1_freq_hz", 500 } }, grave).slice (0.5, 1.0).peakDb() < -42.0f);
    CHECK (near (runOne ("eq", { { "b8_type", 3 }, { "b8_freq_hz", 4000 }, { "b8_gain_db", -6 } }, sine (12000.0f, -20.0f)).slice (0.5, 1.0).peakDb(), -26.0, 0.6));
    // duas bandas somam
    CHECK (near (runOne ("eq", { { "b2_type", 1 }, { "b2_freq_hz", 1000 }, { "b2_gain_db", 3 }, { "b4_type", 1 }, { "b4_freq_hz", 1000 }, { "b4_gain_db", 3 } }, tom).slice (0.5, 1.0).peakDb(), -14.0, 0.3));
}

static void testCompressoresV2()
{
    gTest = "compressor: os cinco caracteres comprimem";
    const auto alto = sine (440.0f, -6.0f, 1.5);
    for (int modelo = 0; modelo < 5; ++modelo)
    {
        const auto out = runOne ("compressor", { { "threshold_db", -30 }, { "ratio", 4 }, { "attack_ms", 5 }, { "release_ms", 100 }, { "model", (float) modelo } }, alto);
        CHECK (rmsDb (out.left, 1.0, 1.5) < rmsDb (alto.left, 1.0, 1.5) - 10.0);
        CHECK (allFinite (out));
    }

    gTest = "compressor: FET colore, limpo nao";
    const auto base = sine (200.0f, -6.0f, 1.5);
    std::map<std::string, float> forte = { { "threshold_db", -36 }, { "ratio", 8 }, { "attack_ms", 1 }, { "release_ms", 100 } };
    auto limpo = forte, fet = forte;
    limpo["model"] = 0; fet["model"] = 2;
    const auto a = runOne ("compressor", limpo, base), b = runOne ("compressor", fet, base);
    const double h3Limpo = tomDb (a.left, 600.0, 1.0, 1.5) - tomDb (a.left, 200.0, 1.0, 1.5);
    const double h3Fet = tomDb (b.left, 600.0, 1.0, 1.5) - tomDb (b.left, 200.0, 1.0, 1.5);
    CHECK (h3Fet > h3Limpo + 10.0);

    gTest = "compressor: filtro do detector deixa o grave passar";
    const auto sub = sine (50.0f, -6.0f, 1.5);
    std::map<std::string, float> p = { { "threshold_db", -24 }, { "ratio", 6 } };
    const double semFiltro = rmsDb (runOne ("compressor", p, sub).left, 1.0, 1.5);
    p["sc_hpf_hz"] = 400;
    const double comFiltro = rmsDb (runOne ("compressor", p, sub).left, 1.0, 1.5);
    CHECK (comFiltro > semFiltro + 6.0);

    gTest = "sidechain: o baixo abaixa a cada golpe do bumbo";
    const auto baixo = sine (60.0f, -12.0f, 2.0);
    Audio bumbo;
    bumbo.left.assign (baixo.left.size(), 0.0f);
    for (double inicio : { 0.5, 1.0, 1.5 })
        for (int i = 0; i < (int) (0.08 * kSampleRate); ++i)
            bumbo.left[(size_t) (inicio * kSampleRate) + (size_t) i] = 0.8f * std::sin (kTwoPi * 55.0f * i / (float) kSampleRate) * std::exp (-25.0f * i / (float) kSampleRate);
    const auto spec = clean (specOf ({ { "c", "compressor", { { "threshold_db", -30 }, { "ratio", 8 }, { "attack_ms", 1 }, { "release_ms", 120 }, { "sidechain", 1 } } } }));
    const auto duck = runWithSidechain (spec, baixo, &bumbo);
    const double durante = rmsDb (duck.left, 1.0, 1.05), entre = rmsDb (duck.left, 1.35, 1.48);
    CHECK (durante < entre - 6.0);
    CHECK (near (entre, rmsDb (baixo.left, 1.35, 1.48), 1.5));  // entre os golpes o baixo volta ao normal
    const auto semSc = runWithSidechain (spec, baixo, nullptr);  // sem sinal de sidechain, ouve a própria pista
    CHECK (rmsDb (semSc.left, 1.0, 1.05) < rmsDb (baixo.left, 1.0, 1.05) - 3.0);
}

static void testBusComp()
{
    gTest = "bus comp: comprime e a mistura faz paralelo";
    const auto hits = drumHits();
    std::map<std::string, float> p = { { "threshold_db", -30 }, { "ratio", 4 }, { "attack_ms", 0.1f }, { "release_s", 0.1f } };
    const auto cheio = runOne ("bus_compressor", p, hits);
    p["mix"] = 0.5f;
    const auto paralelo = runOne ("bus_compressor", p, hits);
    CHECK (cheio.peak() < paralelo.peak() && paralelo.peak() < hits.peak());

    gTest = "bus comp: liberacao automatica volta rapido depois de golpe curto";
    // Uma rajada curta e outra longa, seguidas de um tom baixo que mede quanto o ganho já voltou.
    auto ensaio = [] (double duracao) {
        Audio x = sine (300.0f, -40.0f, 3.0);
        const int fim = (int) ((0.5 + duracao) * kSampleRate);
        for (int i = (int) (0.5 * kSampleRate); i < fim; ++i)
            x.left[(size_t) i] = 0.5f * std::sin (kTwoPi * 300.0f * i / (float) kSampleRate);
        const auto out = runOne ("bus_compressor", { { "threshold_db", -30 }, { "ratio", 10 }, { "attack_ms", 1 }, { "release_s", 0 } }, x);
        const double t = 0.5 + duracao + 0.3;
        return rmsDb (out.left, t, t + 0.05) - rmsDb (x.left, t, t + 0.05);
    };
    const double depoisCurto = ensaio (0.02), depoisLongo = ensaio (1.5);
    std::printf ("    bus auto: depois do golpe curto %.1f dB, depois do longo %.1f dB\n", depoisCurto, depoisLongo);
    CHECK (depoisCurto > depoisLongo + 3.0);
    CHECK (depoisCurto > -3.0);
}

static void testDeEsserV2()
{
    gTest = "de-esser: escuta mostra so a faixa detectada";
    const auto sinal = add (sine (200.0f, -12.0f), noise (1.0, -20.0f));
    const auto ouvir = runOne ("de_esser", { { "freq_hz", 6000 }, { "threshold_db", 0 }, { "listen", 1 } }, sinal);
    CHECK (bandDb (ouvir, kLow) < bandDb (sinal, kLow) - 30.0f);
    CHECK (near (bandDb (ouvir, kAir), bandDb (sinal, kAir), 1.0));
    const auto largo = runOne ("de_esser", { { "freq_hz", 6000 }, { "threshold_db", -50 }, { "max_reduction_db", 9 }, { "mode", 1 } }, add (sine (200.0f, -12.0f), noise (1.0, -6.0f)));
    CHECK (bandDb (largo, kLow) < bandDb (add (sine (200.0f, -12.0f), noise (1.0, -6.0f)), kLow) - 4.0f);  // modo largo abaixa tudo
}

static void testRessonancias()
{
    gTest = "supressor: sem profundidade so atrasa o som";
    const auto ruido = noise (1.5, -12.0f, 21);
    const auto spec0 = clean (specOf ({ { "r", "resonance", { { "depth_db", 0 } } } }));
    Chain cadeia (spec0);
    cadeia.prepare (kSampleRate, 512, 1);
    const int L = cadeia.latencySamples();
    CHECK (L == 1024);
    const auto atrasado = run (spec0, ruido);
    double erro = 0.0;
    for (int i = 2048; i < ruido.size(); ++i)
        erro = std::max (erro, (double) std::abs (atrasado.left[(size_t) i] - ruido.left[(size_t) (i - L)]));
    CHECK (erro < 1e-4);

    gTest = "supressor: abaixa a ressonancia e preserva o resto";
    Audio apito = noise (3.0, -24.0f, 22);
    for (int i = 0; i < apito.size(); ++i)
        apito.left[(size_t) i] += 0.2f * std::sin (kTwoPi * 3000.0f * i / (float) kSampleRate);
    const auto tratado = runOne ("resonance", { { "depth_db", 12 }, { "selectivity", 3 } }, apito);
    // compara o mesmo instante: a saída está atrasada pela latência do supressor
    const double t0 = 2.0 + 1024.0 / kSampleRate;
    CHECK (tomDb (tratado.left, 3000.0, t0, t0 + 0.8) < tomDb (apito.left, 3000.0, 2.0, 2.8) - 5.0);
    CHECK (near (tomDb (tratado.left, 500.0, t0, t0 + 0.8), tomDb (apito.left, 500.0, 2.0, 2.8), 6.0));  // ruído em volta continua lá
    CHECK (near (rmsDb (runOne ("resonance", { { "depth_db", 12 } }, ruido).left, 1.0, 1.4), rmsDb (ruido.left, 1.0, 1.4), 1.0));  // ruído plano quase não muda

    gTest = "supressor: curva de sensibilidade";
    Audio fraco = noise (3.0, -24.0f, 22);  // ressonância discreta, que não chega à redução máxima
    for (int i = 0; i < fraco.size(); ++i)
        fraco.left[(size_t) i] += 0.03f * std::sin (kTwoPi * 3000.0f * i / (float) kSampleRate);
    std::map<std::string, float> p = { { "depth_db", 18 }, { "selectivity", 8 } };
    const double semSens = tomDb (runOne ("resonance", p, fraco).left, 3000.0, t0, t0 + 0.8);
    p["s4_freq_hz"] = 3000; p["s4_gain_db"] = 12;
    const double comSens = tomDb (runOne ("resonance", p, fraco).left, 3000.0, t0, t0 + 0.8);
    std::printf ("    supressor: tom de 3 kHz %.1f dB sem sensibilidade, %.1f dB com\n", semSens, comSens);
    CHECK (comSens < semSens - 2.0);

    gTest = "supressor: mistura sem defasagem";
    const auto meio = runOne ("resonance", { { "depth_db", 0 }, { "mix", 0.5f } }, ruido);
    double erroMeio = 0.0;
    for (int i = 2048; i < ruido.size(); ++i)
        erroMeio = std::max (erroMeio, (double) std::abs (meio.left[(size_t) i] - ruido.left[(size_t) (i - L)]));
    CHECK (erroMeio < 1e-4);
    // na cadeia, o som seco da mistura geral também espera a latência
    auto specMix = spec0;
    specMix.mix = 0.5f;
    const auto mixCadeia = run (specMix, ruido);
    double erroCadeia = 0.0;
    for (int i = 2048; i < ruido.size(); ++i)
        erroCadeia = std::max (erroCadeia, (double) std::abs (mixCadeia.left[(size_t) i] - ruido.left[(size_t) (i - L)]));
    CHECK (erroCadeia < 1e-4);
}


// Roda uma cadeia já montada, aplicando `antes` (edições) no meio do caminho.
static Audio runEditing (const ChainSpec& spec, Audio audio, const std::vector<Chain::Edit>& edits, double quandoSeg, const float* macroValues = nullptr)
{
    Chain chain (spec);
    chain.prepare (kSampleRate, 512, audio.stereo ? 2 : 1);
    float macros[kMaxMacros] = {};
    for (size_t i = 0; i < spec.macros.size() && i < (size_t) kMaxMacros; ++i)
        macros[i] = spec.macros[i].value;
    if (macroValues != nullptr)
        for (int i = 0; i < kMaxMacros; ++i)
            macros[i] = macroValues[i];
    bool editado = false;
    for (int start = 0; start < audio.size(); start += 512)
    {
        if (! editado && start >= (int) (quandoSeg * kSampleRate))
        {
            for (auto& e : edits)
                CHECK (chain.queueEdit (e));
            editado = true;
        }
        const int n = std::min (512, audio.size() - start);
        float* ch[2] = { audio.left.data() + start, audio.stereo ? audio.right.data() + start : nullptr };
        chain.process (ch, audio.stereo ? 2 : 1, n, macros, spec.mix, spec.outputGainDb);
    }
    return audio;
}

static double maxDiff (const Audio& a, const Audio& b, double deSeg)
{
    double d = 0.0;
    for (int i = (int) (deSeg * kSampleRate); i < std::min (a.size(), b.size()); ++i)
        d = std::max (d, (double) std::abs (a.left[(size_t) i] - b.left[(size_t) i]));
    return d;
}

static void testEdicaoAoVivo()
{
    gTest = "edicao ao vivo igual a cadeia montada com o valor";
    const auto voz = noise (2.0, -6.0f, 31);
    auto base = clean (specOf ({ { "c", "compressor", { { "threshold_db", -10 }, { "ratio", 2 } } } }));
    auto editado = base;
    float pos[kMaxMacros] = {};
    bool ok = false;
    const auto e = editarParametro (editado, 0, "threshold_db", -30, pos, ok);
    CHECK (ok && ! e.rebind && editado.modules[0].params["threshold_db"] == -30.0f);
    const auto aoVivo = runEditing (base, voz, { e }, 0.5);
    const auto montada = run (editado, voz);
    CHECK (maxDiff (aoVivo, montada, 1.0) < 1e-4);   // depois de assentar, soa igual
    CHECK (maxDiff (aoVivo, run (base, voz), 1.0) > 1e-2);  // e mudou de verdade

    gTest = "edicao: valor fora da faixa e limitado";
    auto limitado = base;
    const auto e2 = editarParametro (limitado, 0, "ratio", 50, pos, ok);
    CHECK (ok && near (e2.value, 10.0, 1e-6));
    editarParametro (limitado, 0, "nao_existe", 1, pos, ok);
    CHECK (! ok);

    gTest = "edicao: ajuste manual re-ancora o macro Intensidade";
    MacroSpec intensidade { "Intensidade", "", 0.4f, { { "q", "b2_gain_db", 2.0f, 8.0f } } };
    auto comMacro = clean (specOf ({ { "q", "eq", { { "b2_type", 1 }, { "b2_freq_hz", 1000 } } } }, { intensidade }));
    pos[0] = 0.4f;
    CHECK (near (valorAtual (comMacro, 0, "b2_gain_db", pos), 4.4, 1e-4));
    const auto e3 = editarParametro (comMacro, 0, "b2_gain_db", 6.0f, pos, ok);
    CHECK (e3.rebind && near (e3.value, 6.0, 1e-4));
    CHECK (near (valorAtual (comMacro, 0, "b2_gain_db", pos), 6.0, 1e-4));  // o ajuste vale na posição atual
    const auto& alvo = comMacro.macros[0].targets[0];
    CHECK (near (alvo.valueAt1 - alvo.valueAt0, 6.0, 1e-4));                // e o macro continua mexendo o mesmo tanto
    pos[0] = 1.0f;
    CHECK (near (valorAtual (comMacro, 0, "b2_gain_db", pos), 9.6, 1e-4));
    // perto do limite da faixa: o extremo para em 18 dB e o valor continua na posição atual
    pos[0] = 0.4f;
    const auto e4 = editarParametro (comMacro, 0, "b2_gain_db", 16.0f, pos, ok);
    CHECK (near (e4.value, 16.0, 1e-3) && comMacro.macros[0].targets[0].valueAt1 <= 18.0f);
    // a cadeia tocando com a edição soa igual à cadeia montada a partir do spec editado
    const auto tom = noise (2.0, -18.0f, 32);
    auto antes = clean (specOf ({ { "q", "eq", { { "b2_type", 1 }, { "b2_freq_hz", 1000 } } } }, { intensidade }));
    auto depois = antes;
    const auto e5 = editarParametro (depois, 0, "b2_gain_db", 6.0f, pos, ok);
    CHECK (maxDiff (runEditing (antes, tom, { e5 }, 0.5, pos), run (depois, tom, pos), 1.0) < 1e-4);
    // e girando o botão depois do ajuste, a cadeia ao vivo acompanha os extremos novos
    float max1[kMaxMacros] = { 1.0f };
    Chain viva (antes);
    viva.prepare (kSampleRate, 512, 1);
    Audio a = tom, b = tom;
    for (int start = 0; start < a.size(); start += 512)
    {
        if (start == 512 * 4) viva.queueEdit (e5);
        float* ch[1] = { a.left.data() + start };
        viva.process (ch, 1, std::min (512, a.size() - start), start < kSecond ? pos : max1, 1.0f, 0.0f);
    }
    b = run (depois, b, max1);
    CHECK (maxDiff (a, b, 1.5) < 1e-4);

    gTest = "liga e desliga modulo";
    const auto ruido = noise (2.0, -12.0f, 33);
    const auto ganho = clean (specOf ({ { "g", "gain", { { "gain_db", -12 } } } }));
    auto ganhoEditado = ganho;
    const auto desl = runEditing (ganho, ruido, { ligarModulo (ganhoEditado, 0, false) }, 0.5);
    CHECK (! ganhoEditado.modules[0].enabled);
    CHECK (maxDiff (desl, ruido, 1.0) < 1e-6);  // desligado, o som passa intacto
    auto specDesl = ganho;
    specDesl.modules[0].enabled = false;
    CHECK (maxDiff (run (specDesl, ruido), ruido, 0.0) < 1e-6);
    CHECK (! clean (specDesl).modules[0].enabled);  // o validador preserva o desligado

    gTest = "desligar o supressor mantem o alinhamento";
    const auto sup = clean (specOf ({ { "r", "resonance", { { "depth_db", 12 } } } }));
    auto supSpec = sup;
    const auto off = runEditing (sup, ruido, { ligarModulo (supSpec, 0, false) }, 0.5);
    double erro = 0.0;
    for (int i = (int) (0.6 * kSampleRate); i < ruido.size(); ++i)
        erro = std::max (erro, (double) std::abs (off.left[(size_t) i] - ruido.left[(size_t) (i - 1024)]));
    CHECK (erro < 1e-5);

    gTest = "medidores para a interface";
    const auto forte = sine (200.0f, -3.0f, 1.0);
    const auto comp = clean (specOf ({ { "c", "compressor", { { "threshold_db", -30 }, { "ratio", 4 } } },
                                       { "r", "resonance", { { "depth_db", 12 } } } }));
    Chain medida (comp);
    medida.prepare (kSampleRate, 512, 1);
    Audio m = add (forte, noise (1.0, -30.0f, 34));
    for (int start = 0; start + 512 <= m.size(); start += 512)
    {
        float* ch[1] = { m.left.data() + start };
        medida.process (ch, 1, 512, pos, 1.0f, 0.0f);
    }
    std::printf ("    medidores: redução do compressor %.1f dB, pico de entrada %.2f, saída %.2f\n",
                 medida.moduleAt (0)->gainReductionDb(), medida.slotInputPeak (0), medida.slotOutputPeak (0));
    CHECK (medida.moduleAt (0)->gainReductionDb() > 10.0f);
    CHECK (medida.slotOutputPeak (0) < medida.slotInputPeak (0));
    float curva[1100];
    float binHz = 0.0f;
    const int pontos = medida.moduleAt (1)->reductionCurve (curva, 1100, binHz);
    CHECK (pontos == 513 && near (binHz, kSampleRate / 1024.0, 1e-3));
    CHECK (medida.moduleAt (0)->reductionCurve (curva, 1100, binHz) == 0);
}

static void testNoAllocationWhileProcessing()
{
    gTest = "process nao aloca memoria";
    std::vector<ModuleSpec> modules;
    std::vector<MacroSpec> macros;
    int index = 0;
    for (const char* type : { "highpass", "de_esser", "resonance", "compressor", "eq", "exciter", "bus_compressor",
                              "transient_shaper", "delay", "reverb", "clipper", "tape" })
        modules.push_back ({ "m" + std::to_string (index++), type, {} });
    modules[3].params["sidechain"] = 1;
    modules[4].params = { { "b2_type", 1 }, { "b7_type", 3 } };
    macros.push_back ({ "A", "", 0.5f, { { "m4", "b2_gain_db", -6, 6 }, { "m8", "time_ms", 100, 600 }, { "m3", "threshold_db", -10, -40 }, { "m2", "depth_db", 0, 12 } } });
    const auto spec = clean (specOf (modules, macros));
    CHECK ((int) spec.modules.size() == 12);

    Chain chain (spec);
    chain.prepare (kSampleRate, 512, 2);
    auto audio = noise (1.0, -12.0f, 4, true);
    float macroValues[kMaxMacros] = { 0.5f };

    gAllocations = 0;
    gCounting = true;
    for (int start = 0; start + 512 <= audio.size(); start += 512)
    {
        macroValues[0] = (float) start / (float) audio.size();  // macro em movimento
        if (start == 512 * 10)  // edições da interface chegando no meio
        {
            chain.queueEdit ({ 3, 0, -20.0f, false, 0.0f, 0.0f });
            chain.queueEdit ({ 5, Chain::kParamLigado, 0.0f, false, 0.0f, 0.0f });
            chain.queueEdit ({ 2, Chain::kParamLigado, 0.0f, false, 0.0f, 0.0f });
        }
        float* ch[2] = { audio.left.data() + start, audio.right.data() + start };
        const float* sc[1] = { audio.left.data() + start };
        chain.process (ch, 2, 512, macroValues, 0.8f, -1.0f, sc, 1);
    }
    gCounting = false;
    CHECK (gAllocations.load() == 0);
    CHECK (allFinite (audio));
}

int main()
{
    testCatalog();
    testValidator();
    testEveryModuleRuns();
    testChainBasics();
    testModules();
    testTimeEffects();
    testAnalyzer();
    testCatalogV2();
    testEq8();
    testCompressoresV2();
    testBusComp();
    testDeEsserV2();
    testRessonancias();
    testEdicaoAoVivo();
    testNoAllocationWhileProcessing();

    std::printf ("%d verificações, %d falha(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
