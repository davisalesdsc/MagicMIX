// MagicMIX: processador do plugin. Liga o host, o motor de áudio e o serviço de IA.
#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Ai/IntentClient.h"
#include "Engine/Chain.h"
#include "Engine/ChainSpec.h"

namespace mm
{
// Configurações do usuário, guardadas no computador (não no projeto da DAW).
struct Settings
{
    juce::String apiKey;
    juce::String endpoint = "https://api.anthropic.com/v1/messages";
    juce::String model = "claude-opus-5-5";
    juce::String effort = "medium";
    juce::String style = "artistico";
};

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
    juce::StringArray chainLines() const;          // um texto por módulo, com os valores atuais
    juce::String status() const { return statusText; }
    bool statusIsError() const { return statusError; }
    bool busy() const { return client.isBusy(); }

    void sendPrompt (const juce::String& text);    // pedido novo ou revisão, conforme houver conversa
    bool loadSpecJson (const juce::String& jsonText, juce::String& error);
    juce::String specJson() const;                 // a cadeia com a posição atual dos botões
    void newConversation();                        // limpa a cadeia e o histórico de pedidos
    bool canUndo() const { return ! undoStack.empty(); }
    void undo();

    Settings settings() const { return userSettings; }
    void setSettings (const Settings& newSettings);
    juce::String trackName() const;

    MacroParameter* macroParam (int index) const { return macroParams[(size_t) index]; }
    juce::AudioParameterFloat* mixParam() const { return mix; }
    juce::AudioParameterFloat* gainParam() const { return gain; }

private:
    void timerCallback() override;
    void applySpec (const ChainSpec& raw, bool pushUndo);
    ChainSpec specWithLiveValues() const;
    Features snapshotFeatures() const;
    void setStatus (const juce::String& text, bool isError);
    void loadSettings();

    // parâmetros do host
    std::vector<MacroParameter*> macroParams;
    juce::AudioParameterFloat* mix = nullptr;
    juce::AudioParameterFloat* gain = nullptr;

    // estado da thread de mensagens
    ChainSpec currentSpec;
    std::vector<std::string> lastReport;
    std::vector<ChainSpec> undoStack;
    Settings userSettings;
    juce::String statusText;
    bool statusError = false;
    IntentClient client;
    mutable juce::CriticalSection trackLock;
    juce::String hostTrackName;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // áudio
    double preparedRate = 0.0;
    int preparedBlock = 0, preparedChannels = 2;
    Chain* active = nullptr;                    // só a thread de áudio usa depois de prepareToPlay
    std::atomic<Chain*> pending { nullptr };    // cadeia nova, pronta, esperando a troca
    enum class Fade { none, out, in };
    Fade fade = Fade::none;
    juce::AbstractFifo retiredFifo { 16 };      // cadeias antigas, para apagar fora da thread de áudio
    Chain* retired[16] = {};

    // últimos segundos da entrada, para a análise
    juce::AudioBuffer<float> capture;
    std::atomic<int> captureWrite { 0 };
    std::atomic<int> captureFilled { 0 };
    std::atomic<int> captureChannels { 1 };
    std::atomic<double> hostBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicMixProcessor)
};
}
