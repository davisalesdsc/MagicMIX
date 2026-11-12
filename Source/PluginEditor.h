// MagicMIX: interface. Conversa à esquerda, controles da cadeia à direita.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace mm
{
namespace cor
{
// Azul ciano sobre cinza rato.
const juce::Colour fundo { 0xff2b2e33 }, painel { 0xff383c42 }, painelClaro { 0xff474c54 }, borda { 0xff5b616a },
    texto { 0xffe9ebed }, textoSuave { 0xffa4a9b0 }, ciano { 0xff22c7e8 }, cianoEscuro { 0xff0e8fae }, erro { 0xffff8a7a };
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

class SettingsPanel;

class MagicMixEditor : public juce::AudioProcessorEditor,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MagicMixEditor (MagicMixProcessor&);
    ~MagicMixEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void send (const juce::String& text);
    void showJsonMenu();
    void showSettings (bool visible);

    MagicMixProcessor& processor;
    Look look;

    juce::TextButton newButton, undoButton, jsonButton, settingsButton, sendButton;
    juce::TextEditor conversation, promptBox, chainView;
    juce::Label statusLabel, chainTitle, mixLabel, gainLabel;
    juce::TextButton suggestionButtons[kMaxSuggestions];

    juce::Slider macroSliders[kMaxMacros], mixSlider, gainSlider;
    juce::Label macroLabels[kMaxMacros];
    std::unique_ptr<juce::SliderParameterAttachment> macroAttachments[kMaxMacros], mixAttachment, gainAttachment;

    std::unique_ptr<SettingsPanel> settingsPanel;
    juce::Rectangle<int> leftArea, rightArea;
    juce::String lastChainText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicMixEditor)
};
}
