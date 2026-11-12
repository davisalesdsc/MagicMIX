// Testes da camada do plugin: JSON do ChainSpec, pedido ao serviço de IA e o
// processador rodando áudio. Roda sem DAW e sem rede.
//
//   PluginTests [pasta_do_repositorio]

#include <juce_audio_utils/juce_audio_utils.h>

#include "Ai/Prompt.h"
#include "Ai/SpecJson.h"
#include "Engine/Catalog.h"
#include "Engine/Validate.h"
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

static void testPrompt (const juce::File& repo)
{
    gTest = "prompt de sistema";
    const auto system = prompt::systemPrompt();
    CHECK (system.contains ("- highpass: ") && system.contains ("- stereo_width: "));
    CHECK (! system.contains ("@@"));
    CHECK (system.contains (tx ("Critérios de mixagem")));
    CHECK (prompt::styleNames().contains ("didatico"));

    gTest = "mensagem do usuario";
    ChainSpec current;
    juce::String error;
    json::fromString (readExample (repo, "voz_calorosa.json"), current, error);
    Features features;
    features.valid = true;
    features.peakDbfs = -9.0f;
    const auto message = prompt::userMessage ("  menos eco  ", json::featuresToVar (features, 96.0, "VOX lead"), true, &current, "tecnico");
    CHECK (message.contains ("<cadeia_atual>") && message.contains ("\"id\": \"eco\""));
    CHECK (message.contains ("- Quero uma voz calorosa"));
    CHECK (message.contains ("\"bpm\": 96") && message.contains ("\"track_name\": \"VOX lead\""));
    CHECK (message.contains ("tecnico:") && message.trimEnd().endsWith ("<pedido>\nmenos eco\n</pedido>"));
    CHECK (prompt::userMessage ("x", {}, false, nullptr, "artistico").contains (tx ("indisponível")));

    gTest = "corpo do pedido";
    const auto body = juce::JSON::parse (prompt::requestBody ("claude-opus-5-5", "medium", system, message));
    CHECK (body["model"].toString() == "claude-opus-5-5");
    CHECK (body["output_config"]["effort"].toString() == "medium");
    CHECK (body["output_config"]["format"]["type"].toString() == "json_schema");
    const auto types = body["output_config"]["format"]["schema"]["properties"]["modules"]["items"]["properties"]["type"]["enum"];
    CHECK (types.isArray() && types.size() == (int) catalog().size());
    CHECK (body["system"][0]["text"].toString() == system);
    CHECK (body["messages"][0]["content"].toString() == message);

    gTest = "leitura da resposta";
    auto block = new juce::DynamicObject();
    block->setProperty ("type", "text");
    block->setProperty ("text", readExample (repo, "gerados/bumbo_na_cara.json"));
    juce::DynamicObject::Ptr response = new juce::DynamicObject();  // Ptr: o objeto precisa sobreviver às duas leituras
    response->setProperty ("stop_reason", "end_turn");
    response->setProperty ("content", juce::Array<juce::var> { juce::var (block) });
    ChainSpec parsed;
    CHECK (prompt::parseResponse (juce::JSON::toString (juce::var (response.get())), parsed, error));
    CHECK (parsed.modules.size() == 4 && parsed.macros.size() == 5);

    response->setProperty ("stop_reason", "refusal");
    CHECK (! prompt::parseResponse (juce::JSON::toString (juce::var (response.get())), parsed, error));
    CHECK (! prompt::parseResponse (R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})", parsed, error));
    CHECK (error.contains ("invalid x-api-key"));
    CHECK (! prompt::parseResponse ("<html>", parsed, error));
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
    CHECK (processor.chainLines().size() == 9);
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
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (mono.checkBusesLayoutSupported (layout));
    layout.inputBuses.clear();
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.clear();
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (! mono.checkBusesLayoutSupported (layout));

    gTest = "processador: sem chave da API";
    auto settings = processor.settings();
    const auto savedKey = settings.apiKey;
    settings.apiKey = {};
    settings.endpoint = "https://api.anthropic.com/v1/messages";
    // não grava no disco do usuário: só confere a mensagem
    if (savedKey.isEmpty())
    {
        processor.sendPrompt ("voz mais brilhante");
        CHECK (processor.statusIsError() && processor.status().contains ("chave"));
        CHECK (! processor.busy());
    }
}

// Salva uma imagem da interface com uma cadeia carregada (para conferir o visual).
static int captureInterface (const juce::File& repo, const juce::File& output)
{
    MagicMixProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    juce::String error;
    if (! processor.loadSpecJson (readExample (repo, "gerados/conversa/voz_pop_turno2.json"), error))
        return 1;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (960, 600);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    output.deleteFile();
    juce::FileOutputStream fresh (output);
    return fresh.openedOk() && juce::PNGImageFormat().writeImageToStream (image, fresh) ? 0 : 1;
}

// Faz um pedido de verdade pelo cliente HTTP do plugin, contra o endereço dado
// (use com tests/servico_de_mentira.py). Confere os dois caminhos: sucesso e chave recusada.
static int requestThroughHttp (const juce::String& endpoint)
{
    auto call = [&endpoint] (const juce::String& apiKey) {
        IntentRequest request;
        request.prompt = "bumbo mais na cara";
        request.style = "tecnico";
        request.trackName = "KICK";
        request.apiKey = apiKey;
        request.endpoint = endpoint;
        request.model = "claude-opus-5-5";
        request.effort = "medium";

        IntentResult result;
        bool done = false;
        IntentClient client;
        client.start (std::move (request), [&] (IntentResult r) { result = std::move (r); done = true; });
        for (int i = 0; i < 300 && ! done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        if (! done)
            result.error = "sem resposta em 30 s";
        return result;
    };

    const auto good = call ("chave-de-teste");
    std::printf ("pedido: %s (%d módulos, %d macros) %s\n", good.ok ? "ok" : "ERRO", (int) good.spec.modules.size(),
                 (int) good.spec.macros.size(), good.error.toRawUTF8());
    const auto bad = call ("chave-errada");
    std::printf ("chave errada: %s\n", bad.ok ? "aceita (ERRO)" : bad.error.toRawUTF8());

    const bool passed = good.ok && good.spec.modules.size() == 4 && good.spec.prompts.size() == 1
                        && ! bad.ok && bad.error.contains ("x-api-key");
    return passed ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--pedido")
        return requestThroughHttp (juce::String::fromUTF8 (argv[2]));
    if (argc > 2 && juce::String (argv[1]) == "--captura")
        return captureInterface (juce::File (MM_SOURCE_DIR), juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])));
    const juce::File repo = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1]))
                                     : juce::File (MM_SOURCE_DIR);
    if (! repo.getChildFile ("prototipo/examples").isDirectory())
    {
        std::printf ("não achei prototipo/examples em %s\n", repo.getFullPathName().toRawUTF8());
        return 2;
    }

    testJson (repo);
    testPrompt (repo);
    testProcessor (repo);

    std::printf ("%d verificações, %d falha(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
