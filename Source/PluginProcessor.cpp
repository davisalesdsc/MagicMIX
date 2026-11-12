#include "PluginProcessor.h"

#include "Ai/SpecJson.h"
#include "Engine/Analyzer.h"
#include "Engine/Edicao.h"
#include "Engine/Validate.h"
#include "Nomes.h"
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
MagicMixProcessor::MagicMixProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false))
{
    for (int i = 0; i < kMaxMacros; ++i)
    {
        auto* param = new MacroParameter (i);
        macroParams.push_back (param);
        addParameter (param);
    }
    // Os ids continuam os da versão 1, para projetos antigos abrirem com as automações no lugar.
    addParameter (mix = new juce::AudioParameterFloat (juce::ParameterID { "mix", 1 }, "Wet", 0.0f, 1.0f, 1.0f));
    addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "outputGain", 1 }, "Output", -24.0f, 12.0f, 0.0f));

    active = new Chain (currentSpec);
    ultima = active;
    numeroDaInstancia = (int) sessaoCompartilhada->instancias().size() + 1;
    sessaoCompartilhada->entrar (this);
    saidaRta.assign ((size_t) kTamanhoRta, 0.0f);
    entradaRta.assign ((size_t) kTamanhoRta, 0.0f);
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
    sessaoCompartilhada->sair (this);
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

void MagicMixProcessor::aposentar (Chain* chain)
{
    if (chain == nullptr)
        return;
    int start1, size1, start2, size2;
    retiredFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) retired[start1] = chain;
    else if (size2 > 0) retired[start2] = chain;
    else return;  // fila cheia: nunca acontece com o timer rodando; vaza em vez de apagar na thread de áudio
    retiredFifo.finishedWrite (1);
}

// ------------------------------------------------------------------ áudio
bool MagicMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return inOk && outOk && out.size() >= in.size();  // mono, estéreo ou mono para estéreo
}

void MagicMixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate;
    preparedBlock = juce::jmax (samplesPerBlock, 64);
    preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());

    capture.setSize (2, (int) (sampleRate * 8.0));
    scCopia.setSize (2, juce::jmax (preparedBlock, 8192));
    capture.clear();
    captureWrite = 0;
    captureFilled = 0;

    // O áudio está parado aqui: dá para trocar a cadeia direto. As antigas são apagadas pelo timer,
    // porque a interface pode estar lendo os medidores delas.
    aposentar (pending.exchange (nullptr));
    aposentar (active);
    active = new Chain (currentSpec);
    active->prepare (preparedRate, preparedBlock, preparedChannels);
    ultima = active;
    fade = Fade::none;
    setLatencySamples (active->latencySamples());
}

void MagicMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int numIn = mainIn.getNumChannels(), numOut = getMainBusNumOutputChannels(), n = buffer.getNumSamples();
    if (n == 0 || numOut == 0 || active == nullptr || preparedRate <= 0.0)
        return;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm, std::memory_order_relaxed);

    // Guarda a entrada para a análise e para o analisador de espectro.
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
    {
        int w = rtaWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            entradaRta[(size_t) ((w + i) % kTamanhoRta)] = numIn > 1 ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i))
                                                                       : numIn == 1 ? buffer.getSample (0, i) : 0.0f;
    }

    // Sidechain: entrada auxiliar, se o host ligou uma. Copiada antes de tudo, porque no buffer do host os
    // canais dela podem ser os mesmos da saída.
    const float* sc[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && n <= scCopia.getNumSamples())
    {
        auto aux = getBusBuffer (buffer, true, 1);
        scCh = juce::jmin (2, aux.getNumChannels());
        for (int c = 0; c < scCh; ++c)
        {
            scCopia.copyFrom (c, 0, aux, c, 0, n);
            sc[c] = scCopia.getReadPointer (c);
        }
    }

    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);  // mono para estéreo
    for (int c = juce::jmax (numIn, 2); c < numOut; ++c)
        buffer.clear (c, 0, n);

    // Nível da pista (para a coluna de pistas) e energia de entrada (para comparar com volume igual).
    const int canaisMedidos = juce::jmin (numOut, 2);
    float pico = 0.0f;
    double energia = 0.0;
    for (int c = 0; c < canaisMedidos; ++c)
    {
        pico = juce::jmax (pico, buffer.getMagnitude (c, 0, n));
        const float rms = buffer.getRMSLevel (c, 0, n);
        energia += (double) rms * rms / canaisMedidos;
    }
    nivelEntrada.store (juce::jmax (pico, nivelEntrada.load (std::memory_order_relaxed) * std::pow (0.5f, (float) n / (float) (preparedRate * 0.15))),
                        std::memory_order_relaxed);
    const double lento = std::exp (-(double) n / (preparedRate * 3.0));  // média de uns 3 s
    const bool comparandoBloco = comparandoAgora.load (std::memory_order_relaxed);
    if (! comparandoBloco)
        energiaEntrada = lento * energiaEntrada + (1.0 - lento) * energia;

    // Há cadeia nova esperando: some com a atual neste bloco e entra com a nova no próximo.
    if (fade == Fade::none && pending.load (std::memory_order_acquire) != nullptr && retiredFifo.getFreeSpace() > 0)
        fade = Fade::out;

    float macros[kMaxMacros];
    for (int i = 0; i < kMaxMacros; ++i)
        macros[i] = macroParams[(size_t) i]->get();

    const int channels = juce::jmin (numOut, 2);
    float* ch[2] = { buffer.getWritePointer (0), channels == 2 ? buffer.getWritePointer (1) : nullptr };
    const bool ligadoBloco = tratamentoAtivo.load (std::memory_order_relaxed);
    if (comparandoBloco)
    {
        // Original alinhado (a cadeia atrasa o seco igual), no volume que o tratado vinha tendo.
        const float alvo = (float) (10.0 * std::log10 ((energiaSaida + 1.0e-12) / (energiaEntrada + 1.0e-12)));
        ganhoComparacaoDb = juce::jlimit (-24.0f, 12.0f, alvo);
        active->process (ch, channels, n, macros, 0.0f, ganhoComparacaoDb, scCh > 0 ? sc : nullptr, scCh);
    }
    else if (! ligadoBloco)
        active->process (ch, channels, n, macros, 0.0f, 0.0f, scCh > 0 ? sc : nullptr, scCh);
    else
    {
        active->process (ch, channels, n, macros, mix->get(), gain->get(), scCh > 0 ? sc : nullptr, scCh);
        double energiaOut = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const float rms = buffer.getRMSLevel (c, 0, n);
            energiaOut += (double) rms * rms / channels;
        }
        energiaSaida = lento * energiaSaida + (1.0 - lento) * energiaOut;
    }

    if (fade == Fade::out)
    {
        for (int c = 0; c < channels; ++c)
            buffer.applyGainRamp (c, 0, n, 1.0f, 0.0f);
        if (auto* incoming = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            aposentar (active);
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

    int w = rtaWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        saidaRta[(size_t) ((w + i) % kTamanhoRta)] = channels > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
    rtaWrite.store ((w + n) % kTamanhoRta, std::memory_order_release);
}

void MagicMixProcessor::ultimasAmostras (bool saida, float* destino, int n) const
{
    n = juce::jmin (n, kTamanhoRta);
    const auto& fonte = saida ? saidaRta : entradaRta;
    const int w = rtaWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        destino[i] = fonte[(size_t) ((w - n + i + kTamanhoRta) % kTamanhoRta)];
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
        // Se a anterior ainda não tinha entrado, sai da fila; quem apaga é o timer.
        if (auto* antiga = pending.exchange (chain))
            delete antiga;  // nunca tocou e a interface já vai ler a nova: pode apagar aqui
        ultima = chain;
        setLatencySamples (chain->latencySamples());
    }
    sendChangeMessage();
    sessaoCompartilhada->avisar();
}

void MagicMixProcessor::comecarAjuste()
{
    undoStack.push_back (specWithLiveValues());
    if (undoStack.size() > 30)
        undoStack.erase (undoStack.begin());
}

void MagicMixProcessor::ajustarParametro (int modulo, const std::string& param, float valor)
{
    float pos[kMaxMacros] = {};
    for (int i = 0; i < kMaxMacros; ++i)
        pos[i] = macroParams[(size_t) i]->get();
    bool ok = false;
    const auto edit = editarParametro (currentSpec, modulo, param, valor, pos, ok);
    if (! ok)
        return;
    if (ultima != nullptr)
        ultima->queueEdit (edit);
    sendChangeMessage();
}

void MagicMixProcessor::ligarModulo (int modulo, bool ligado)
{
    comecarAjuste();
    const auto edit = mm::ligarModulo (currentSpec, modulo, ligado);
    if (ultima != nullptr)
        ultima->queueEdit (edit);
    sendChangeMessage();
}

float MagicMixProcessor::valorDoParametro (int modulo, const std::string& param) const
{
    float pos[kMaxMacros] = {};
    for (int i = 0; i < kMaxMacros; ++i)
        pos[i] = macroParams[(size_t) i]->get();
    return valorAtual (currentSpec, modulo, param, pos);
}

float MagicMixProcessor::posicaoDoMacro (int indice) const
{
    return indice >= 0 && indice < kMaxMacros ? macroParams[(size_t) indice]->get() : 0.0f;
}

// ------------------------------------------------------------------ conversa e biblioteca
void MagicMixProcessor::sendPrompt (const juce::String& text)
{
    const auto pedido = text.trim();
    if (pedido.isEmpty())
        return;
    mensagens.push_back ({ true, pedido, {} });
    responder (pedido);
}

void MagicMixProcessor::responder (const juce::String& texto)
{
    const auto r = cerebro.responder (texto, specWithLiveValues(), grupoDaPista (nomeDaPista()));
    if (r.carregarBiblioteca >= 0)
        carregarDaBiblioteca (r.carregarBiblioteca, false);
    else if (! r.macros.empty())
        comecarAjuste();  // o Desfazer volta o que o pedido girou
    for (auto& [indice, valor] : r.macros)
        if (indice >= 0 && indice < kMaxMacros)
        {
            macroParams[(size_t) indice]->setValueNotifyingHost (valor);
            if (indice < (int) currentSpec.macros.size())
                currentSpec.macros[(size_t) indice].value = valor;
        }
    if (! r.botoes.isEmpty())
        botoesPedido = r.botoes;
    currentSpec.prompts.push_back (texto.toStdString());
    mensagens.push_back ({ false, r.texto, r.detalhes });
    sendChangeMessage();
}

int MagicMixProcessor::sugestaoDaBiblioteca() const { return cerebro.sugestaoPara (grupoDaPista (nomeDaPista())); }

void MagicMixProcessor::carregarDaBiblioteca (int indice, bool avisarNaConversa)
{
    const auto& lista = cerebro.biblioteca();
    if (indice < 0 || indice >= (int) lista.size())
        return;
    const auto features = snapshotFeatures();
    auto cadeia = Cerebro::ajustarVolumeDeTrabalho (lista[(size_t) indice].cadeia, features);
    cadeia.prompts = currentSpec.prompts;
    applySpec (cadeia, true);
    botoesPedido.clear();
    if (! avisarNaConversa)
        return;
    juce::String detalhes = tx ("Cadeia: ") + descreverCadeia (currentSpec) + "\n"
                            + tx ("Começa com o botão Intensidade em 40 %, o ponto aprovado nas escutas, e os outros botões no ponto neutro.");
    for (auto& linha : lastReport)
        detalhes << "\n" << tx ("Ajuste de segurança: ") << tx (linha);
    mensagens.push_back ({ false, tx ("Carreguei \"") + lista[(size_t) indice].titulo
                                      + (features.valid ? tx ("\" da Biblioteca e acertei o volume de trabalho para esta pista.")
                                                        : tx ("\" da Biblioteca. Dê play na pista para eu acertar o volume de trabalho.")),
                           detalhes });
    sendChangeMessage();
}

bool MagicMixProcessor::loadSpecJson (const juce::String& jsonText, juce::String& error)
{
    ChainSpec spec;
    if (! json::fromString (jsonText, spec, error))
        return false;
    applySpec (spec, true);
    return true;
}

juce::String MagicMixProcessor::specJson() const { return json::toString (specWithLiveValues()); }

void MagicMixProcessor::newConversation()
{
    mensagens.clear();
    applySpec (ChainSpec(), true);
}

void MagicMixProcessor::undo()
{
    if (undoStack.empty())
        return;
    const auto previous = undoStack.back();
    undoStack.pop_back();
    applySpec (previous, false);
}

// ------------------------------------------------------------------ estado
void MagicMixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto root = new juce::DynamicObject();
    root->setProperty ("magicmix", 2);
    root->setProperty ("spec", json::toVar (specWithLiveValues()));
    juce::Array<juce::var> falas;
    for (auto& m : mensagens)
    {
        auto fala = new juce::DynamicObject();
        fala->setProperty ("musico", m.doMusico);
        fala->setProperty ("texto", m.texto);
        if (m.detalhes.isNotEmpty())
            fala->setProperty ("detalhes", m.detalhes);
        falas.add (juce::var (fala));
    }
    root->setProperty ("conversa", falas);
    root->setProperty ("tratamento_ligado", tratamentoAtivo.load());
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
    std::vector<Mensagem> falas;
    if (auto* lista = root["conversa"].getArray())
        for (auto& f : *lista)
            falas.push_back ({ (bool) f.getProperty ("musico", true), f.getProperty ("texto", {}).toString(), f.getProperty ("detalhes", {}).toString() });
    tratamentoAtivo.store ((bool) root.getProperty ("tratamento_ligado", true));

    auto apply = [this, spec, falas] {
        undoStack.clear();
        mensagens = falas;
        applySpec (spec, false);
    };
    if (juce::MessageManager::existsAndIsCurrentThread())
        apply();
    else  // alguns hosts restauram o estado fora da thread de mensagens
        juce::MessageManager::callAsync ([flag = alive, apply] { if (*flag) apply(); });
}

void MagicMixProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::ScopedLock sl (trackLock);
        hostTrackName = properties.name.value_or (juce::String()).trim();
    }
    juce::MessageManager::callAsync ([flag = alive, this] { if (*flag) { sendChangeMessage(); sessaoCompartilhada->avisar(); } });
}

juce::String MagicMixProcessor::trackName() const
{
    const juce::ScopedLock sl (trackLock);
    return hostTrackName;
}

juce::String MagicMixProcessor::nomeDaPista() const
{
    const auto nome = trackName();
    return nome.isNotEmpty() ? nome : tx ("Pista ") + juce::String (numeroDaInstancia);
}

void MagicMixProcessor::ligarTratamento (bool sim)
{
    tratamentoAtivo.store (sim);
    sendChangeMessage();
    sessaoCompartilhada->avisar();
}

juce::AudioProcessorEditor* MagicMixProcessor::createEditor() { return new MagicMixEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mm::MagicMixProcessor(); }
