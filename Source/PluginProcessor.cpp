#include "PluginProcessor.h"

#include "Ai/SpecJson.h"
#include "Engine/Analyzer.h"
#include "Engine/Validate.h"
#include "PluginEditor.h"
#include "Texto.h"

namespace mm
{
// ------------------------------------------------------------------ parâmetro de macro
MacroParameter::MacroParameter (int index)
    : juce::AudioParameterFloat (juce::ParameterID { "macro" + juce::String (index + 1), 1 },
                                 "Macro " + juce::String (index + 1), 0.0f, 1.0f, 0.5f),
      fallback ("Macro " + juce::String (index + 1))
{
}

juce::String MacroParameter::getName (int maximumStringLength) const
{
    const juce::ScopedLock sl (lock);
    return (label.isEmpty() ? fallback : label).substring (0, maximumStringLength);
}

void MacroParameter::setLabel (const juce::String& newLabel)
{
    const juce::ScopedLock sl (lock);
    label = newLabel;
}

// ------------------------------------------------------------------ construção
static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "MagicMIX";
    o.filenameSuffix = ".settings";
    o.folderName = "MagicMIX";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return o;
}

MagicMixProcessor::MagicMixProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < kMaxMacros; ++i)
    {
        auto* param = new MacroParameter (i);
        macroParams.push_back (param);
        addParameter (param);
    }
    addParameter (mix = new juce::AudioParameterFloat (juce::ParameterID { "mix", 1 }, "Mix", 0.0f, 1.0f, 1.0f));
    addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "outputGain", 1 }, "Output Gain", -24.0f, 12.0f, 0.0f));

    active = new Chain (currentSpec);
    loadSettings();
    startTimerHz (10);

    // Para testes e demonstrações: começa com a cadeia do arquivo indicado.
    const auto initial = juce::SystemStats::getEnvironmentVariable ("MAGICMIX_CADEIA_INICIAL", {});
    if (initial.isNotEmpty())
    {
        ChainSpec spec;
        juce::String error;
        if (json::fromString (juce::File (initial).loadFileAsString(), spec, error))
            applySpec (spec, false);
    }
}

MagicMixProcessor::~MagicMixProcessor()
{
    *alive = false;
    stopTimer();
    delete pending.exchange (nullptr);
    delete active;
    timerCallback();
}

void MagicMixProcessor::timerCallback()
{
    // Apaga, fora da thread de áudio, as cadeias que ela já largou.
    int start1, size1, start2, size2;
    retiredFifo.prepareToRead (retiredFifo.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) delete retired[start1 + i];
    for (int i = 0; i < size2; ++i) delete retired[start2 + i];
    retiredFifo.finishedRead (size1 + size2);
}

// ------------------------------------------------------------------ áudio
bool MagicMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && out.size() >= in.size();  // mono, estéreo ou mono para estéreo
}

void MagicMixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate;
    preparedBlock = juce::jmax (samplesPerBlock, 64);
    preparedChannels = juce::jlimit (1, 2, getTotalNumOutputChannels());

    capture.setSize (2, (int) (sampleRate * 8.0));
    capture.clear();
    captureWrite = 0;
    captureFilled = 0;

    // O áudio está parado aqui: dá para trocar a cadeia direto.
    delete pending.exchange (nullptr);
    delete active;
    active = new Chain (currentSpec);
    active->prepare (preparedRate, preparedBlock, preparedChannels);
    fade = Fade::none;
}

void MagicMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels(), n = buffer.getNumSamples();
    if (n == 0 || numOut == 0 || active == nullptr || preparedRate <= 0.0)
        return;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm, std::memory_order_relaxed);

    // Guarda a entrada para a análise.
    const int captureCh = juce::jlimit (1, 2, numIn), captureSize = capture.getNumSamples();
    if (numIn > 0 && captureSize > 0)
    {
        int write = captureWrite.load (std::memory_order_relaxed);
        for (int done = 0; done < n;)
        {
            const int chunk = juce::jmin (n - done, captureSize - write);
            for (int c = 0; c < captureCh; ++c)
                capture.copyFrom (c, write, buffer, c, done, chunk);
            write = (write + chunk) % captureSize;
            done += chunk;
        }
        captureWrite.store (write, std::memory_order_relaxed);
        captureChannels.store (captureCh, std::memory_order_relaxed);
        captureFilled.store (juce::jmin (captureSize, captureFilled.load (std::memory_order_relaxed) + n), std::memory_order_relaxed);
    }

    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);  // mono para estéreo
    for (int c = juce::jmax (numIn, 2); c < numOut; ++c)
        buffer.clear (c, 0, n);

    // Há cadeia nova esperando: some com a atual neste bloco e entra com a nova no próximo.
    if (fade == Fade::none && pending.load (std::memory_order_acquire) != nullptr && retiredFifo.getFreeSpace() > 0)
        fade = Fade::out;

    float macros[kMaxMacros];
    for (int i = 0; i < kMaxMacros; ++i)
        macros[i] = macroParams[(size_t) i]->get();

    const int channels = juce::jmin (numOut, 2);
    float* ch[2] = { buffer.getWritePointer (0), channels == 2 ? buffer.getWritePointer (1) : nullptr };
    active->process (ch, channels, n, macros, mix->get(), gain->get());

    if (fade == Fade::out)
    {
        for (int c = 0; c < channels; ++c)
            buffer.applyGainRamp (c, 0, n, 1.0f, 0.0f);
        if (auto* incoming = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            int start1, size1, start2, size2;
            retiredFifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 > 0) retired[start1] = active; else retired[start2] = active;
            retiredFifo.finishedWrite (1);
            active = incoming;
        }
        fade = Fade::in;
    }
    else if (fade == Fade::in)
    {
        for (int c = 0; c < channels; ++c)
            buffer.applyGainRamp (c, 0, n, 0.0f, 1.0f);
        fade = Fade::none;
    }
}

// ------------------------------------------------------------------ cadeia
Features MagicMixProcessor::snapshotFeatures() const
{
    const int filled = captureFilled.load(), size = capture.getNumSamples();
    if (preparedRate <= 0.0 || size == 0 || filled < (int) (preparedRate * 0.5))
        return {};

    // Copia o trecho gravado em ordem. A thread de áudio continua escrevendo; para a análise isso não importa.
    const int channels = captureChannels.load(), write = captureWrite.load();
    juce::AudioBuffer<float> linear (channels, filled);
    const int start = (write - filled + size) % size;
    for (int c = 0; c < channels; ++c)
        for (int done = 0; done < filled;)
        {
            const int position = (start + done) % size, chunk = juce::jmin (filled - done, size - position);
            linear.copyFrom (c, done, capture, c, position, chunk);
            done += chunk;
        }

    auto features = analyze (linear.getArrayOfReadPointers(), channels, filled, preparedRate);
    if (features.peakDbfs < -80.0f)
        features.valid = false;  // só silêncio: não há o que medir
    return features;
}

ChainSpec MagicMixProcessor::specWithLiveValues() const
{
    auto spec = currentSpec;
    for (size_t i = 0; i < spec.macros.size() && i < macroParams.size(); ++i)
        spec.macros[i].value = macroParams[i]->get();
    spec.mix = mix->get();
    spec.outputGainDb = gain->get();
    return spec;
}

void MagicMixProcessor::applySpec (const ChainSpec& raw, bool pushUndo)
{
    const auto features = snapshotFeatures();
    std::vector<std::string> report;
    auto clean = validate (raw, features.valid ? &features : nullptr, report);

    if (pushUndo)
    {
        undoStack.push_back (specWithLiveValues());
        if (undoStack.size() > 30)
            undoStack.erase (undoStack.begin());
    }
    currentSpec = std::move (clean);
    lastReport = std::move (report);

    for (int i = 0; i < kMaxMacros; ++i)
    {
        const bool used = i < (int) currentSpec.macros.size();
        macroParams[(size_t) i]->setLabel (used ? tx (currentSpec.macros[(size_t) i].name) : juce::String());
        if (used)
            macroParams[(size_t) i]->setValueNotifyingHost (currentSpec.macros[(size_t) i].value);
    }
    mix->setValueNotifyingHost (mix->convertTo0to1 (currentSpec.mix));
    gain->setValueNotifyingHost (gain->convertTo0to1 (currentSpec.outputGainDb));
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));

    if (preparedRate > 0.0)
    {
        auto* chain = new Chain (currentSpec);
        chain->prepare (preparedRate, preparedBlock, preparedChannels);
        delete pending.exchange (chain);  // se a anterior ainda não tinha entrado, é descartada
    }
    sendChangeMessage();
}

juce::StringArray MagicMixProcessor::chainLines() const
{
    juce::StringArray lines;
    const auto spec = specWithLiveValues();
    for (auto& m : spec.modules)
    {
        const auto* def = findModule (m.type);
        if (def == nullptr)
            continue;
        juce::StringArray values;
        for (auto& p : def->params)
        {
            float value = m.params.count (p.name) != 0 ? m.params.at (p.name) : p.def;
            for (auto& macro : spec.macros)  // parâmetro controlado por macro vale o que o botão der
                for (auto& t : macro.targets)
                    if (t.moduleId == m.id && t.param == p.name)
                        value = interpolate (t.valueAt0, t.valueAt1, macro.value, p.log);
            values.add (juce::String (p.name) + " " + juce::String (value, std::abs (value) < 10.0f ? 2 : 0)
                        + (p.unit[0] != 0 && juce::String (p.unit) != "0-1" ? " " + tx (p.unit) : juce::String()));
        }
        lines.add (tx (m.id) + "  [" + tx (m.type) + "]\n" + values.joinIntoString ("   "));
    }
    return lines;
}

void MagicMixProcessor::setStatus (const juce::String& text, bool isError)
{
    statusText = text;
    statusError = isError;
    sendChangeMessage();
}

void MagicMixProcessor::sendPrompt (const juce::String& text)
{
    if (text.trim().isEmpty() || client.isBusy())
        return;
    if (userSettings.apiKey.isEmpty() && userSettings.endpoint.contains ("api.anthropic.com"))
    {
        setStatus (tx ("Falta a chave da API. Abra as configurações (engrenagem) e informe a sua."), true);
        return;
    }

    IntentRequest request;
    request.prompt = text;
    request.style = userSettings.style;
    request.trackName = trackName();
    request.bpm = hostBpm.load();
    request.features = snapshotFeatures();
    if (! currentSpec.modules.empty() || ! currentSpec.prompts.empty())
        request.current = specWithLiveValues();
    request.apiKey = userSettings.apiKey;
    request.endpoint = userSettings.endpoint;
    request.model = userSettings.model;
    request.effort = userSettings.effort;

    setStatus (request.features.valid ? tx ("Montando a cadeia...")
                                      : tx ("Montando a cadeia sem análise (dê play na pista para o plugin ouvir)..."), false);
    client.start (std::move (request), [this] (IntentResult result) {
        if (result.ok)
        {
            applySpec (result.spec, true);
            setStatus ({}, false);
        }
        else
            setStatus (result.error, true);
    });
}

bool MagicMixProcessor::loadSpecJson (const juce::String& jsonText, juce::String& error)
{
    ChainSpec spec;
    if (! json::fromString (jsonText, spec, error))
        return false;
    applySpec (spec, true);
    setStatus ({}, false);
    return true;
}

juce::String MagicMixProcessor::specJson() const { return json::toString (specWithLiveValues()); }

void MagicMixProcessor::newConversation()
{
    applySpec (ChainSpec(), true);
    setStatus ({}, false);
}

void MagicMixProcessor::undo()
{
    if (undoStack.empty())
        return;
    const auto previous = undoStack.back();
    undoStack.pop_back();
    applySpec (previous, false);
    setStatus ({}, false);
}

// ------------------------------------------------------------------ estado e configurações
void MagicMixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto root = new juce::DynamicObject();
    root->setProperty ("magicmix", 1);
    root->setProperty ("spec", json::toVar (specWithLiveValues()));
    const auto text = juce::JSON::toString (juce::var (root), true);
    destData.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void MagicMixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::var root;
    if (juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, sizeInBytes), root).failed())
        return;
    ChainSpec spec;
    juce::String error;
    if (! json::fromVar (root["spec"], spec, error))
        return;

    auto apply = [this, spec] {
        undoStack.clear();
        applySpec (spec, false);
    };
    if (juce::MessageManager::existsAndIsCurrentThread())
        apply();
    else  // alguns hosts restauram o estado fora da thread de mensagens
        juce::MessageManager::callAsync ([flag = alive, apply] { if (*flag) apply(); });
}

void MagicMixProcessor::loadSettings()
{
    juce::PropertiesFile file (settingsOptions());
    Settings defaults;
    userSettings.apiKey = file.getValue ("apiKey");
    userSettings.endpoint = file.getValue ("endpoint", defaults.endpoint);
    userSettings.model = file.getValue ("model", defaults.model);
    userSettings.effort = file.getValue ("effort", defaults.effort);
    userSettings.style = file.getValue ("style", defaults.style);
}

void MagicMixProcessor::setSettings (const Settings& newSettings)
{
    userSettings = newSettings;
    Settings defaults;
    if (userSettings.endpoint.trim().isEmpty()) userSettings.endpoint = defaults.endpoint;
    if (userSettings.model.trim().isEmpty()) userSettings.model = defaults.model;

    juce::PropertiesFile file (settingsOptions());
    file.setValue ("apiKey", userSettings.apiKey);
    file.setValue ("endpoint", userSettings.endpoint);
    file.setValue ("model", userSettings.model);
    file.setValue ("effort", userSettings.effort);
    file.setValue ("style", userSettings.style);
    file.saveIfNeeded();
    sendChangeMessage();
}

void MagicMixProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::ScopedLock sl (trackLock);
        hostTrackName = properties.name.value_or (juce::String()).trim();
    }
    juce::MessageManager::callAsync ([flag = alive, this] { if (*flag) sendChangeMessage(); });
}

juce::String MagicMixProcessor::trackName() const
{
    const juce::ScopedLock sl (trackLock);
    return hostTrackName;
}

juce::AudioProcessorEditor* MagicMixProcessor::createEditor() { return new MagicMixEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mm::MagicMixProcessor(); }
