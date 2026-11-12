// Host de teste: carrega o plugin compilado (VST3 ou AU) como um DAW faria.
//
//   HostTest <caminho do .vst3 ou .component>
//
// Confere: carga, parâmetros, áudio em várias taxas e tamanhos de bloco,
// estado (salvar e restaurar uma cadeia) e várias instâncias ao mesmo tempo.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

static int gFailures = 0, gChecks = 0;

#define CHECK(condition) \
    do { ++gChecks; if (! (condition)) { ++gFailures; std::printf ("  FALHOU linha %d: %s\n", __LINE__, #condition); } } while (false)

// Uma cadeia pequena. O plugin a carrega ao abrir quando a variável de ambiente
// MAGICMIX_CADEIA_INICIAL aponta para um arquivo com ela.
static const char* kSpec = R"({"version": 2, "title": "Teste",
  "modules": [
    {"id": "hp", "type": "highpass", "params": {"freq_hz": 200}},
    {"id": "comp", "type": "compressor", "params": {"threshold_db": -30, "ratio": 4}},
    {"id": "eco", "type": "delay", "params": {"time_ms": 120, "feedback": 0.3, "mix": 0.3}},
    {"id": "vol", "type": "gain", "params": {"gain_db": -6}}],
  "macros": [{"name": "Volume", "description": "", "value": 0.5,
              "targets": [{"module_id": "vol", "param": "gain_db", "value_at_0": -12, "value_at_1": 0}]}],
  "mix": 1.0, "output_gain_db": 0.0})";

static void setInitialChain (const juce::String& path)
{
   #if JUCE_WINDOWS
    _putenv_s ("MAGICMIX_CADEIA_INICIAL", path.toRawUTF8());
   #else
    if (path.isEmpty()) unsetenv ("MAGICMIX_CADEIA_INICIAL"); else setenv ("MAGICMIX_CADEIA_INICIAL", path.toRawUTF8(), 1);
   #endif
}

static std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& formats, const juce::String& path,
                                                        double sampleRate, int blockSize)
{
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        if (format->fileMightContainThisPluginType (path))
            format->findAllTypesForFile (found, path);
    if (found.isEmpty())
    {
        std::printf ("nenhum plugin encontrado em %s\n", path.toRawUTF8());
        return nullptr;
    }
    juce::String error;
    auto instance = formats.createPluginInstance (*found[0], sampleRate, blockSize, error);
    if (instance == nullptr)
        std::printf ("falha ao criar a instância: %s\n", error.toRawUTF8());
    return instance;
}

// Passa ruído pelo plugin. Devolve o pico da saída, ou -1 se sair valor inválido.
static float run (juce::AudioPluginInstance& plugin, int blocks, int blockSize, float* difference = nullptr)
{
    const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    juce::Random random (11);
    juce::MidiBuffer midi;
    float peak = 0.0f, diff = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (channels, blockSize), original;
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (c, i, random.nextFloat() * 0.5f - 0.25f);
        original.makeCopyOf (buffer);
        plugin.processBlock (buffer, midi);
        for (int c = 0; c < plugin.getTotalNumOutputChannels(); ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buffer.getSample (c, i);
                if (! std::isfinite (v))
                    return -1.0f;
                peak = juce::jmax (peak, std::abs (v));
                diff = juce::jmax (diff, std::abs (v - original.getSample (c, i)));
            }
    }
    if (difference != nullptr)
        *difference = diff;
    return peak;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("uso: HostTest <caminho do plugin>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])).getFullPathName();
    std::printf ("plugin: %s\n", path.toRawUTF8());

    const auto specFile = juce::File::createTempFile ("magicmix-cadeia.json");
    specFile.replaceWithText (kSpec);

    // carga e parâmetros, sem cadeia
    setInitialChain ({});
    auto plugin = load (formats, path, 48000.0, 512);
    CHECK (plugin != nullptr);
    if (plugin == nullptr)
        return 1;
    CHECK (plugin->getName() == "MagicMIX");
    int macros = 0;
    bool hasMix = false, hasGain = false;
    for (auto* parameter : plugin->getParameters())
    {
        const auto name = parameter->getName (64);
        macros += name.startsWith ("Macro ") ? 1 : 0;
        hasMix = hasMix || name == "Mix";
        hasGain = hasGain || name == "Output Gain";
    }
    CHECK (macros == 10 && hasMix && hasGain);
    CHECK (plugin->getTotalNumOutputChannels() >= 1);
    std::printf ("canais: %d entrada(s), %d saída(s), %d parâmetros\n", plugin->getTotalNumInputChannels(),
                 plugin->getTotalNumOutputChannels(), plugin->getParameters().size());

    // sem cadeia, o som passa intacto
    plugin->prepareToPlay (48000.0, 512);
    float difference = 1.0f;
    CHECK (run (*plugin, 20, 512, &difference) > 0.0f);
    CHECK (difference < 1.0e-5f);

    // uma instância que abre com cadeia processa o som e dá nome ao macro
    setInitialChain (specFile.getFullPathName());
    auto chained = load (formats, path, 48000.0, 512);
    setInitialChain ({});
    CHECK (chained != nullptr);
    if (chained == nullptr)
        return 1;
    chained->prepareToPlay (48000.0, 512);
    CHECK (run (*chained, 40, 512, &difference) > 0.0f);
    CHECK (difference > 1.0e-3f);
    bool namedMacro = false;
    for (auto* parameter : chained->getParameters())
        namedMacro = namedMacro || parameter->getName (64) == "Volume";
    CHECK (namedMacro);

    // o estado salvo leva a cadeia para outra instância, que abriu vazia
    juce::MemoryBlock saved;
    chained->getStateInformation (saved);
    CHECK (saved.getSize() > 100);
    auto second = load (formats, path, 44100.0, 256);
    CHECK (second != nullptr);
    if (second != nullptr)
    {
        second->prepareToPlay (44100.0, 256);
        CHECK (run (*second, 10, 256, &difference) > 0.0f);
        CHECK (difference < 1.0e-5f);
        second->setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);  // alguns formatos aplicam o estado de forma assíncrona
        CHECK (run (*second, 40, 256, &difference) > 0.0f);
        CHECK (difference > 1.0e-3f);
    }

    // várias taxas de amostragem e tamanhos de bloco
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 32, 480, 2048 })
        {
            plugin->releaseResources();
            plugin->setRateAndBufferSizeDetails (rate, block);
            plugin->prepareToPlay (rate, block);
            CHECK (run (*plugin, 12, block) > 0.0f);
        }

    // quatro instâncias ao mesmo tempo, todas com cadeia
    setInitialChain (specFile.getFullPathName());
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> many;
    for (int i = 0; i < 4; ++i)
        if (auto instance = load (formats, path, 48000.0, 512))
        {
            instance->prepareToPlay (48000.0, 512);
            many.push_back (std::move (instance));
        }
    setInitialChain ({});
    CHECK (many.size() == 4);
    for (auto& instance : many)
        CHECK (run (*instance, 20, 512) > 0.0f);

    for (auto& instance : many)
        instance->releaseResources();
    many.clear();
    if (second != nullptr)
        second->releaseResources();
    chained->releaseResources();
    plugin->releaseResources();
    second.reset();
    chained.reset();
    plugin.reset();
    specFile.deleteFile();

    std::printf ("%d verificações, %d falha(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
