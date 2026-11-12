// MagicMIX: processador do plugin. Liga o host, o motor de áudio, o cérebro local e a interface.
#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Cerebro/Cerebro.h"
#include "Engine/Chain.h"
#include "Engine/ChainSpec.h"
#include "Sessao.h"

namespace mm
{
// Parâmetro de macro: fixo para o host, mas com o nome do macro atual da cadeia.
class MacroParameter : public juce::AudioParameterFloat
{
public:
    explicit MacroParameter (int index);
    juce::String getName (int maximumStringLength) const override;
    void setLabel (const juce::String& newLabel);

private:
    mutable juce::CriticalSection lock;
    juce::String label;
    const juce::String fallback;
};

// Uma fala da conversa com o MagicMIX.
struct Mensagem
{
    bool doMusico = true;
    juce::String texto;
    juce::String detalhes;  // o que mudou na cadeia, aberto em "Detalhes"
};

class MagicMixProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    MagicMixProcessor();
    ~MagicMixProcessor() override;

    // ---- juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MagicMIX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void updateTrackProperties (const TrackProperties& properties) override;

    // ---- usados pela interface, sempre na thread de mensagens
    const ChainSpec& spec() const { return currentSpec; }
    const std::vector<std::string>& report() const { return lastReport; }
    const std::vector<Mensagem>& conversa() const { return mensagens; }
    juce::String trackName() const;
    juce::String nomeDaPista() const;   // o nome da pista na DAW, ou um nome provisório
    double sampleRate() const { return preparedRate; }
    float nivelDaEntrada() const { return nivelEntrada.load (std::memory_order_relaxed); }  // pico recente, 0 a 1
    Sessao& sessao() { return *sessaoCompartilhada; }

    // Comparar com o original: toca o som sem tratamento, alinhado e com o mesmo volume percebido.
    void setComparando (bool sim) { comparandoAgora.store (sim); }
    bool comparando() const { return comparandoAgora.load(); }
    // Desliga o tratamento inteiro (o som passa alinhado, sem igualar volume).
    void ligarTratamento (bool sim);
    bool tratamentoLigado() const { return tratamentoAtivo.load(); }

    // Conversa: o pedido vai para o cérebro local, que responde e pode trocar a cadeia.
    void sendPrompt (const juce::String& text);
    void newConversation();
    bool canUndo() const { return ! undoStack.empty(); }
    void undo();

    // Biblioteca: pontos de partida aprovados.
    const std::vector<EntradaBiblioteca>& biblioteca() const { return cerebro.biblioteca(); }
    void carregarDaBiblioteca (int indice);

    // Ajuste manual (modo Avançado). Chame comecarAjuste() no começo de um gesto, para o Desfazer voltar o gesto inteiro.
    void comecarAjuste();
    void ajustarParametro (int modulo, const std::string& param, float valor);
    void ligarModulo (int modulo, bool ligado);
    float valorDoParametro (int modulo, const std::string& param) const;  // com os macros na posição atual
    float posicaoDoMacro (int indice) const;

    bool loadSpecJson (const juce::String& jsonText, juce::String& error);
    juce::String specJson() const;  // a cadeia com a posição atual dos botões

    // Medidores. A cadeia devolvida vale até a próxima troca de cadeia; use só na thread de mensagens.
    const Chain* cadeiaTocando() const { return ultima; }
    // Últimas `n` amostras (mono) da entrada ou da saída, para o analisador de espectro.
    void ultimasAmostras (bool saida, float* destino, int n) const;

    MacroParameter* macroParam (int index) const { return macroParams[(size_t) index]; }
    juce::AudioParameterFloat* mixParam() const { return mix; }
    juce::AudioParameterFloat* gainParam() const { return gain; }

    static constexpr int kTamanhoRta = 8192;

private:
    void timerCallback() override;
    void applySpec (const ChainSpec& raw, bool pushUndo);
    ChainSpec specWithLiveValues() const;
    Features snapshotFeatures() const;
    void aposentar (Chain* chain);
    void responder (const juce::String& texto);

    // parâmetros do host
    std::vector<MacroParameter*> macroParams;
    juce::AudioParameterFloat* mix = nullptr;
    juce::AudioParameterFloat* gain = nullptr;

    // estado da thread de mensagens
    ChainSpec currentSpec;
    std::vector<std::string> lastReport;
    std::vector<ChainSpec> undoStack;
    std::vector<Mensagem> mensagens;
    Cerebro cerebro;
    mutable juce::CriticalSection trackLock;
    juce::String hostTrackName;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    Chain* ultima = nullptr;  // a cadeia mais nova (tocando ou prestes a tocar): recebe os ajustes manuais

    // áudio
    double preparedRate = 0.0;
    int preparedBlock = 0, preparedChannels = 2;
    Chain* active = nullptr;                    // só a thread de áudio usa depois de prepareToPlay
    std::atomic<Chain*> pending { nullptr };    // cadeia nova, pronta, esperando a troca
    enum class Fade { none, out, in };
    Fade fade = Fade::none;
    // Cadeias antigas, para apagar fora da thread de áudio. Quem escreve é a thread de áudio ou o prepareToPlay,
    // que o host nunca roda ao mesmo tempo; quem apaga é o timer, na thread de mensagens.
    juce::AbstractFifo retiredFifo { 32 };
    Chain* retired[32] = {};

    // últimos segundos da entrada, para a análise; e o fim da saída, para o analisador de espectro
    juce::AudioBuffer<float> capture;
    juce::AudioBuffer<float> scCopia;          // cópia do sidechain do bloco
    std::atomic<int> captureWrite { 0 };
    std::atomic<int> captureFilled { 0 };
    std::atomic<int> captureChannels { 1 };
    std::vector<float> saidaRta, entradaRta;
    std::atomic<int> rtaWrite { 0 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<float> nivelEntrada { 0.0f };
    std::atomic<bool> comparandoAgora { false }, tratamentoAtivo { true };
    double energiaEntrada = 1.0e-9, energiaSaida = 1.0e-9;  // médias lentas, só na thread de áudio
    float ganhoComparacaoDb = 0.0f;
    int numeroDaInstancia = 0;
    juce::SharedResourcePointer<Sessao> sessaoCompartilhada;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicMixProcessor)
};
}
