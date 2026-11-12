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
    CHECK (catalog().size() == 18);
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

static void testNoAllocationWhileProcessing()
{
    gTest = "process nao aloca memoria";
    std::vector<ModuleSpec> modules;
    std::vector<MacroSpec> macros;
    int index = 0;
    for (const char* type : { "highpass", "de_esser", "compressor", "peak", "tape", "exciter", "band_compressor",
                              "transient_shaper", "chorus", "delay", "reverb", "clipper" })
        modules.push_back ({ "m" + std::to_string (index++), type, {} });
    macros.push_back ({ "A", "", 0.5f, { { "m3", "gain_db", -6, 6 }, { "m9", "time_ms", 100, 600 }, { "m2", "threshold_db", -10, -40 } } });
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
        float* ch[2] = { audio.left.data() + start, audio.right.data() + start };
        chain.process (ch, 2, 512, macroValues, 0.8f, -1.0f);
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
    testNoAllocationWhileProcessing();

    std::printf ("%d verificações, %d falha(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
