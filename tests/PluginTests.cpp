// Testes da camada do plugin: JSON do ChainSpec, cérebro local, ajustes manuais e o
// processador rodando áudio. Roda sem DAW e sem rede.
//
//   PluginTests [pasta_do_repositorio]
//   PluginTests --retratos <pasta> [audio.wav]   salva imagens da interface

#include <juce_audio_utils/juce_audio_utils.h>

#include "Ai/SpecJson.h"
#include "Engine/Catalog.h"
#include "Engine/Validate.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Texto.h"

using namespace mm;

static int gFailures = 0, gChecks = 0;
static const char* gTest = "";

#define CHECK(condition) \
    do { ++gChecks; if (! (condition)) { ++gFailures; std::printf ("  FALHOU [%s] linha %d: %s\n", gTest, __LINE__, #condition); } } while (false)

static juce::String readExample (const juce::File& repo, const char* relative)
{
    return repo.getChildFile ("prototipo/examples").getChildFile (relative).loadFileAsString();
}

static void testJson (const juce::File& repo)
{
    gTest = "json: exemplos do prototipo";
    const auto examples = repo.getChildFile ("prototipo/examples").findChildFiles (juce::File::findFiles, true, "*.json");
    int loaded = 0;
    for (auto& file : examples)
    {
        if (file.getParentDirectory().getFileName() == "referencia")
            continue;  // casos de referência não são cadeias
        ChainSpec spec;
        juce::String error;
        const bool ok = json::fromString (file.loadFileAsString(), spec, error);
        CHECK (ok);
        if (! ok)
        {
            std::printf ("    %s: %s\n", file.getFileName().toRawUTF8(), error.toRawUTF8());
            continue;
        }
        ++loaded;

        std::vector<std::string> report;
        const auto clean = validate (spec, nullptr, report);

        // ida e volta pelo formato canônico preserva a cadeia
        ChainSpec again;
        CHECK (json::fromString (json::toString (clean), again, error));
        CHECK (again.modules.size() == clean.modules.size() && again.macros.size() == clean.macros.size());
        CHECK (again.title == clean.title && again.prompts == clean.prompts && again.suggestions == clean.suggestions);
        for (size_t i = 0; i < clean.modules.size() && i < again.modules.size(); ++i)
            CHECK (again.modules[i].params == clean.modules[i].params);

        // e pelo formato do LLM também
        ChainSpec fromLlm;
        CHECK (json::fromVar (json::toLlmVar (clean), fromLlm, error));
        CHECK (fromLlm.modules.size() == clean.modules.size());
    }
    CHECK (loaded >= 8);

    gTest = "json: acentos e versao 1";
    ChainSpec old;
    juce::String error;
    CHECK (json::fromString (readExample (repo, "voz_calorosa.json"), old, error));
    CHECK (old.prompts.size() == 1);  // o campo antigo "prompt" vira a lista "prompts"
    CHECK (tx (old.prompts[0]).contains (tx ("compressão")));
    CHECK (! json::fromString ("isto não é json", old, error) && error.isNotEmpty());

    gTest = "json: resposta so de sugestoes";
    ChainSpec ideas;
    CHECK (json::fromString (readExample (repo, "gerados/so_sugestoes.json"), ideas, error));
    CHECK (ideas.modules.empty() && ideas.suggestions.size() >= 2);
}

static float runNoise (MagicMixProcessor& processor, int blocks, int blockSize, float* difference = nullptr)
{
    juce::Random random (7);
    juce::MidiBuffer midi;
    float peak = 0.0f, diff = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, blockSize), original;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (c, i, random.nextFloat() * 0.5f - 0.25f);
        original.makeCopyOf (buffer);
        processor.processBlock (buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buffer.getSample (c, i);
                if (! std::isfinite (v))
                    return -1.0f;
                peak = juce::jmax (peak, std::abs (v));
                if (b == blocks - 1)
                    diff = juce::jmax (diff, std::abs (v - original.getSample (c, i)));
            }
    }
    if (difference != nullptr)
        *difference = diff;
    return peak;
}

static void testProcessor (const juce::File& repo)
{
    gTest = "processador: cadeia vazia";
    MagicMixProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    float difference = 1.0f;
    CHECK (runNoise (processor, 20, 256, &difference) > 0.0f);
    CHECK (difference < 1.0e-6f);  // sem cadeia, o som passa intacto

    gTest = "processador: carrega cadeia";
    juce::String error;
    CHECK (processor.loadSpecJson (readExample (repo, "gerados/conversa/voz_pop_turno2.json"), error));
    CHECK (processor.spec().modules.size() == 9 && processor.spec().macros.size() == 6);
    CHECK (processor.macroParam (0)->getName (64) == tx (processor.spec().macros[0].name));
    CHECK (processor.macroParam (9)->getName (64) == "Macro 10");  // macro sem uso mantém o nome fixo
    CHECK (runNoise (processor, 60, 256, &difference) > 0.0f);     // inclui a troca de cadeia com fade
    CHECK (difference > 1.0e-3f);                                   // agora o som é processado

    gTest = "processador: blocos de tamanhos variados";
    for (int size : { 1, 17, 64, 1024, 4096 })
        CHECK (runNoise (processor, 4, size) >= 0.0f);

    gTest = "processador: estado";
    processor.macroParam (0)->setValueNotifyingHost (0.9f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    MagicMixProcessor restored;
    restored.setPlayConfigDetails (2, 2, 48000.0, 256);
    restored.prepareToPlay (48000.0, 256);
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.spec().modules.size() == 9);
    CHECK (std::abs (restored.macroParam (0)->get() - 0.9f) < 1.0e-4f);
    CHECK (restored.specJson() == processor.specJson());
    CHECK (runNoise (restored, 20, 256) > 0.0f);

    gTest = "processador: desfazer e nova conversa";
    CHECK (processor.canUndo());
    processor.undo();
    CHECK (processor.spec().modules.empty() && ! processor.canUndo());
    CHECK (processor.loadSpecJson (readExample (repo, "voz_calorosa.json"), error));
    processor.newConversation();
    CHECK (processor.spec().modules.empty() && processor.spec().prompts.empty() && processor.canUndo());
    CHECK (! processor.loadSpecJson ("{ quebrado", error) && error.isNotEmpty());

    gTest = "processador: mono para estereo";
    MagicMixProcessor mono;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.inputBuses.add (juce::AudioChannelSet::disabled());  // entrada de sidechain sem uso
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (mono.checkBusesLayoutSupported (layout));
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    CHECK (! mono.checkBusesLayoutSupported (layout));
}

static void testCerebroEAjustes()
{
    gTest = "cerebro: biblioteca embutida";
    MagicMixProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    const auto& lista = processor.biblioteca();
    CHECK (lista.size() == 11);
    for (auto& e : lista)
    {
        std::vector<std::string> report;
        validate (e.cadeia, nullptr, report);
        CHECK (report.empty());                                   // as cadeias aprovadas passam no validador sem correção
        CHECK (e.cadeia.macros.size() == 1 && e.cadeia.macros[0].name == "Intensidade");
        CHECK (std::abs (e.cadeia.macros[0].value - 0.4f) < 1.0e-4f);
        CHECK (e.titulo.isNotEmpty() && e.cadeia.prompts.empty());
    }

    gTest = "cerebro: carregar da biblioteca";
    runNoise (processor, 200, 256);  // a pista tocando, para acertar o volume de trabalho
    processor.carregarDaBiblioteca (0);
    CHECK (! processor.spec().modules.empty() && processor.spec().modules[0].id == "ganho");
    CHECK (processor.macroParam (0)->getName (64) == "Intensidade");
    CHECK (std::abs (processor.macroParam (0)->get() - 0.4f) < 1.0e-3f);
    CHECK (processor.conversa().size() == 1 && ! processor.conversa()[0].doMusico);
    float difference = 0.0f;
    CHECK (runNoise (processor, 80, 256, &difference) > 0.0f && difference > 1.0e-3f);

    gTest = "cerebro: conversa";
    processor.sendPrompt ("  deixa a voz na cara  ");
    CHECK (processor.conversa().size() == 3);
    CHECK (processor.conversa()[1].doMusico && processor.conversa()[1].texto == "deixa a voz na cara");
    CHECK (! processor.conversa()[2].doMusico && processor.conversa()[2].texto.contains (tx ("Biblioteca")));
    CHECK (processor.spec().prompts.size() == 1);
    processor.sendPrompt ("   ");
    CHECK (processor.conversa().size() == 3);

    gTest = "ajuste manual";
    int eq = -1, comp = -1;
    for (size_t i = 0; i < processor.spec().modules.size(); ++i)
    {
        if (processor.spec().modules[i].type == "eq" && eq < 0) eq = (int) i;
        if (processor.spec().modules[i].type == "compressor" && comp < 0) comp = (int) i;
    }
    CHECK (eq >= 0 && comp >= 0);
    const float antes = processor.valorDoParametro (eq, "b2_gain_db");
    processor.comecarAjuste();
    processor.ajustarParametro (eq, "b2_gain_db", antes + 3.0f);
    CHECK (std::abs (processor.valorDoParametro (eq, "b2_gain_db") - (antes + 3.0f)) < 1.0e-3f);
    // o botão Intensidade continua mexendo a partir do ajuste
    processor.macroParam (0)->setValueNotifyingHost (1.0f);
    const float noMaximo = processor.valorDoParametro (eq, "b2_gain_db");
    CHECK (std::abs (noMaximo - (antes + 3.0f)) > 0.5f);
    processor.macroParam (0)->setValueNotifyingHost (0.4f);
    CHECK (std::abs (processor.valorDoParametro (eq, "b2_gain_db") - (antes + 3.0f)) < 1.0e-3f);
    CHECK (processor.canUndo());
    processor.ajustarParametro (comp, "threshold_db", -500.0f);  // fora da faixa: limitado
    CHECK (processor.valorDoParametro (comp, "threshold_db") >= -50.0f);
    CHECK (runNoise (processor, 20, 256) > 0.0f);

    gTest = "liga e desliga";
    processor.ligarModulo (comp, false);
    CHECK (! processor.spec().modules[(size_t) comp].enabled);
    CHECK (runNoise (processor, 20, 256) > 0.0f);
    processor.undo();
    CHECK (processor.spec().modules[(size_t) comp].enabled);

    gTest = "estado guarda conversa e ajustes";
    processor.ligarModulo (comp, false);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    MagicMixProcessor restored;
    restored.setPlayConfigDetails (2, 2, 48000.0, 256);
    restored.prepareToPlay (48000.0, 256);
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.conversa().size() == processor.conversa().size());
    CHECK (! restored.spec().modules[(size_t) comp].enabled);
    CHECK (std::abs (restored.valorDoParametro (eq, "b2_gain_db") - (antes + 3.0f)) < 1.0e-3f);

    gTest = "sidechain";
    MagicMixProcessor sc;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (sc.checkBusesLayoutSupported (layout));
    CHECK (sc.setBusesLayout (layout));
    sc.prepareToPlay (48000.0, 256);
    juce::String error;
    CHECK (sc.loadSpecJson (R"({"version":2,"title":"duck","modules":[{"id":"d","type":"compressor","params":{"threshold_db":-40,"ratio":10,"attack_ms":1,"sidechain":1}}]})", error));
    // pista com ruído baixo e sidechain com ruído forte: a pista abaixa
    juce::Random random (3);
    juce::MidiBuffer midi;
    float ultimoPico = 0.0f, picoSemSc = 0.0f;
    for (int rodada = 0; rodada < 2; ++rodada)
        for (int b = 0; b < 120; ++b)
        {
            juce::AudioBuffer<float> buffer (4, 256);
            for (int i = 0; i < 256; ++i)
            {
                buffer.setSample (0, i, random.nextFloat() * 0.1f - 0.05f);
                buffer.setSample (1, i, random.nextFloat() * 0.1f - 0.05f);
                buffer.setSample (2, i, rodada == 0 ? 0.0f : random.nextFloat() - 0.5f);
                buffer.setSample (3, i, rodada == 0 ? 0.0f : random.nextFloat() - 0.5f);
            }
            sc.processBlock (buffer, midi);
            if (b == 119) (rodada == 0 ? picoSemSc : ultimoPico) = buffer.getMagnitude (0, 0, 256);
        }
    CHECK (ultimoPico < picoSemSc * 0.5f);
}

// Imagens da interface para conferir o visual: Fluxo, conversa e a face de cada efeito.
static juce::AudioBuffer<float> lerAudio (const juce::File& arquivo)
{
    juce::AudioFormatManager formatos;
    formatos.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> leitor (formatos.createReaderFor (arquivo));
    juce::AudioBuffer<float> audio;
    if (leitor != nullptr)
    {
        // 30 s do meio da música (começo e fim costumam ter silêncio)
        const auto tamanho = juce::jmin<juce::int64> (leitor->lengthInSamples, (juce::int64) leitor->sampleRate * 30);
        const auto inicio = juce::jmax<juce::int64> (0, leitor->lengthInSamples / 2 - tamanho / 2);
        audio.setSize (2, (int) tamanho);
        leitor->read (&audio, 0, audio.getNumSamples(), inicio, true, true);
    }
    return audio;
}

static void tocar (MagicMixProcessor& processor, const juce::AudioBuffer<float>& audio, int& posicao, int blocos)
{
    juce::MidiBuffer midi;
    juce::Random random (5);
    for (int b = 0; b < blocos; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (c, i, audio.getNumSamples() > 0 ? audio.getSample (juce::jmin (c, audio.getNumChannels() - 1), (posicao + i) % audio.getNumSamples())
                                                               : (random.nextFloat() - 0.5f) * 0.2f);
        posicao += 512;
        processor.processBlock (buffer, midi);
    }
}

static int retratos (const juce::File& pasta, const juce::File& audioArquivo)
{
    pasta.createDirectory();
    const auto audio = audioArquivo.existsAsFile() ? lerAudio (audioArquivo) : juce::AudioBuffer<float>();
    MagicMixProcessor processor;
    processor.setPlayConfigDetails (2, 2, 44100.0, 512);
    processor.prepareToPlay (44100.0, 512);
    // outras pistas da sessão, como numa DAW, para a coluna de pistas
    std::vector<std::unique_ptr<MagicMixProcessor>> outras;
    auto nomear = [] (MagicMixProcessor& p, const char* nome) {
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String::fromUTF8 (nome);
        p.updateTrackProperties (props);
    };
    nomear (processor, "Voz guia");
    for (auto* nome : { "Bumbo", "Caixa", "Overs", "Baixo", "Sanfona", "Violão" })
    {
        outras.push_back (std::make_unique<MagicMixProcessor>());
        outras.back()->setPlayConfigDetails (2, 2, 44100.0, 512);
        outras.back()->prepareToPlay (44100.0, 512);
        nomear (*outras.back(), nome);
    }
    outras[0]->carregarDaBiblioteca (4);
    int posicao = 0;
    tocar (processor, audio, posicao, 400);
    processor.carregarDaBiblioteca (0);  // voz guia
    processor.sendPrompt ("deixa a voz na cara, sem aspereza");
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* mm = dynamic_cast<MagicMixEditor*> (editor.get());
    editor->setSize (1240, 818);

    auto salvar = [&] (const juce::String& nome) {
        for (int i = 0; i < 30; ++i)  // ~1,5 s de música com a interface atualizando
        {
            tocar (processor, audio, posicao, 4);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
        const auto imagem = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream saida (pasta.getChildFile (nome + ".png"));
        saida.setPosition (0);
        saida.truncate();
        juce::PNGImageFormat().writeImageToStream (imagem, saida);
        std::printf ("retrato: %s\n", nome.toRawUTF8());
    };

    salvar ("1_fluxo");
    mm->mostrarAvancado (true);
    const auto& spec = processor.spec();
    for (size_t i = 0; i < spec.modules.size(); ++i)
    {
        mm->selecionarModulo ((int) i);
        salvar ("2_" + juce::String ((int) i) + "_" + tx (spec.modules[i].type));
    }
    // Bus Comp da bateria aprovada
    processor.carregarDaBiblioteca (5);
    for (size_t i = 0; i < processor.spec().modules.size(); ++i)
        if (processor.spec().modules[i].type == "bus_compressor")
        {
            mm->selecionarModulo ((int) i);
            salvar ("3_bus_compressor");
        }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--retratos")
        return retratos (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])),
                         argc > 3 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[3])) : juce::File());
    const juce::File repo = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1]))
                                     : juce::File (MM_SOURCE_DIR);
    if (! repo.getChildFile ("prototipo/examples").isDirectory())
    {
        std::printf ("não achei prototipo/examples em %s\n", repo.getFullPathName().toRawUTF8());
        return 2;
    }

    testJson (repo);
    testProcessor (repo);
    testCerebroEAjustes();

    std::printf ("%d verificações, %d falha(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
