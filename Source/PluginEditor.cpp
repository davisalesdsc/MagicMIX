#include "PluginEditor.h"

#include "Ai/Prompt.h"
#include "Texto.h"

namespace mm
{
// ------------------------------------------------------------------ aparência
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, cor::fundo);
    setColour (juce::TextEditor::backgroundColourId, cor::painel);
    setColour (juce::TextEditor::outlineColourId, cor::borda);
    setColour (juce::TextEditor::focusedOutlineColourId, cor::ciano);
    setColour (juce::TextEditor::textColourId, cor::texto);
    setColour (juce::TextEditor::highlightColourId, cor::cianoEscuro);
    setColour (juce::CaretComponent::caretColourId, cor::ciano);
    setColour (juce::Label::textColourId, cor::texto);
    setColour (juce::TextButton::buttonColourId, cor::painelClaro);
    setColour (juce::TextButton::buttonOnColourId, cor::cianoEscuro);
    setColour (juce::TextButton::textColourOffId, cor::texto);
    setColour (juce::TextButton::textColourOnId, cor::texto);
    setColour (juce::Slider::thumbColourId, cor::ciano);
    setColour (juce::Slider::trackColourId, cor::cianoEscuro);
    setColour (juce::Slider::backgroundColourId, cor::painelClaro);
    setColour (juce::Slider::textBoxTextColourId, cor::textoSuave);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, cor::painel);
    setColour (juce::ComboBox::outlineColourId, cor::borda);
    setColour (juce::ComboBox::textColourId, cor::texto);
    setColour (juce::ComboBox::arrowColourId, cor::ciano);
    setColour (juce::PopupMenu::backgroundColourId, cor::painel);
    setColour (juce::PopupMenu::textColourId, cor::texto);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cor::cianoEscuro);
    setColour (juce::PopupMenu::highlightedTextColourId, cor::texto);
    setColour (juce::ScrollBar::thumbColourId, cor::borda);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const float thickness = juce::jmax (3.0f, radius * 0.16f);
    const bool enabled = slider.isEnabled();

    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, endAngle, true);
    g.setColour (cor::painelClaro);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    value.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, angle, true);
    g.setColour (enabled ? cor::ciano : cor::borda);
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knob = radius - thickness * 2.2f;
    g.setColour (cor::painel);
    g.fillEllipse (juce::Rectangle<float> (knob * 2.0f, knob * 2.0f).withCentre (centre));
    g.setColour (cor::borda);
    g.drawEllipse (juce::Rectangle<float> (knob * 2.0f, knob * 2.0f).withCentre (centre), 1.0f);

    const auto tip = centre.getPointOnCircumference (knob * 0.82f, angle), base = centre.getPointOnCircumference (knob * 0.35f, angle);
    g.setColour (enabled ? cor::texto : cor::textoSuave);
    g.drawLine ({ base, tip }, 2.0f);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = colour;
    if (down) fill = fill.darker (0.2f);
    else if (highlighted) fill = fill.brighter (0.12f);
    if (! button.isEnabled()) fill = fill.withMultipliedAlpha (0.45f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.5f)));
}

// ------------------------------------------------------------------ configurações
class SettingsPanel : public juce::Component
{
public:
    SettingsPanel (MagicMixProcessor& p, std::function<void()> onClose) : processor (p), close (std::move (onClose))
    {
        auto addRow = [this] (juce::Label& label, const char* text, juce::Component& field) {
            label.setText (tx (text), juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, cor::textoSuave);
            addAndMakeVisible (label);
            addAndMakeVisible (field);
        };
        addRow (keyLabel, "Chave da API", keyBox);
        addRow (endpointLabel, "Endereço do serviço", endpointBox);
        addRow (modelLabel, "Modelo", modelBox);
        addRow (effortLabel, "Esforço de raciocínio", effortBox);
        addRow (styleLabel, "Estilo das respostas", styleBox);

        keyBox.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        effortBox.addItemList ({ "low", "medium", "high" }, 1);
        styleBox.addItemList (prompt::styleNames(), 1);

        note.setText (tx ("A chave fica guardada só neste computador, fora do projeto da DAW. "
                          "O áudio não sai do computador: só o pedido e as medidas da pista são enviados."),
                      juce::dontSendNotification);
        note.setColour (juce::Label::textColourId, cor::textoSuave);
        note.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (note);

        save.setButtonText ("Salvar");
        save.setColour (juce::TextButton::buttonColourId, cor::cianoEscuro);
        cancel.setButtonText ("Cancelar");
        addAndMakeVisible (save);
        addAndMakeVisible (cancel);
        cancel.onClick = [this] { close(); };
        save.onClick = [this] {
            Settings s;
            s.apiKey = keyBox.getText().trim();
            s.endpoint = endpointBox.getText().trim();
            s.model = modelBox.getText().trim();
            s.effort = effortBox.getText();
            s.style = styleBox.getText();
            processor.setSettings (s);
            close();
        };
    }

    void load()
    {
        const auto s = processor.settings();
        keyBox.setText (s.apiKey, false);
        endpointBox.setText (s.endpoint, false);
        modelBox.setText (s.model, false);
        effortBox.setText (s.effort, juce::dontSendNotification);
        styleBox.setText (s.style, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (cor::fundo.withAlpha (0.88f));
        g.setColour (cor::painel);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (cor::ciano);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (tx ("Configurações"), card.reduced (24, 18).removeFromTop (28), juce::Justification::centredLeft);
    }

    void resized() override
    {
        card = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 40), 400);
        auto area = card.reduced (24, 18);
        area.removeFromTop (40);
        auto row = [&area] (juce::Label& label, juce::Component& field) {
            auto line = area.removeFromTop (34);
            label.setBounds (line.removeFromLeft (170));
            field.setBounds (line.reduced (0, 3));
            area.removeFromTop (6);
        };
        row (keyLabel, keyBox);
        row (endpointLabel, endpointBox);
        row (modelLabel, modelBox);
        row (effortLabel, effortBox);
        row (styleLabel, styleBox);
        auto buttons = area.removeFromBottom (34);
        save.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        cancel.setBounds (buttons.removeFromRight (110));
        note.setBounds (area.reduced (0, 6));
    }

private:
    MagicMixProcessor& processor;
    std::function<void()> close;
    juce::Rectangle<int> card;
    juce::Label keyLabel, endpointLabel, modelLabel, effortLabel, styleLabel, note;
    juce::TextEditor keyBox, endpointBox, modelBox;
    juce::ComboBox effortBox, styleBox;
    juce::TextButton save, cancel;
};

// ------------------------------------------------------------------ janela principal
MagicMixEditor::MagicMixEditor (MagicMixProcessor& p) : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);

    auto setupButton = [this] (juce::TextButton& button, const char* text) {
        button.setButtonText (tx (text));
        addAndMakeVisible (button);
    };
    setupButton (newButton, "Nova");
    setupButton (undoButton, "Desfazer");
    setupButton (jsonButton, "JSON");
    setupButton (settingsButton, "Config");
    setupButton (sendButton, "Enviar");
    sendButton.setColour (juce::TextButton::buttonColourId, cor::cianoEscuro);

    newButton.onClick = [this] { processor.newConversation(); };
    undoButton.onClick = [this] { processor.undo(); };
    jsonButton.onClick = [this] { showJsonMenu(); };
    settingsButton.onClick = [this] { showSettings (true); };
    sendButton.onClick = [this] { send (promptBox.getText()); };

    auto setupReadOnly = [this] (juce::TextEditor& editor, float fontSize) {
        editor.setMultiLine (true);
        editor.setReadOnly (true);
        editor.setScrollbarsShown (true);
        editor.setCaretVisible (false);
        editor.setFont (juce::FontOptions (fontSize));
        editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (editor);
    };
    setupReadOnly (conversation, 15.0f);
    setupReadOnly (chainView, 13.0f);

    promptBox.setMultiLine (true);
    promptBox.setReturnKeyStartsNewLine (false);
    promptBox.setFont (juce::FontOptions (15.0f));
    promptBox.setTextToShowWhenEmpty (tx ("Descreva o som que você quer para esta pista..."), cor::textoSuave);
    promptBox.onReturnKey = [this] { send (promptBox.getText()); };
    addAndMakeVisible (promptBox);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    chainTitle.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    chainTitle.setColour (juce::Label::textColourId, cor::ciano);
    addAndMakeVisible (chainTitle);

    for (auto& button : suggestionButtons)
    {
        button.setColour (juce::TextButton::buttonColourId, cor::painel);
        button.setColour (juce::TextButton::textColourOffId, cor::ciano);
        button.onClick = [this, &button] { send (button.getButtonText()); };
        addChildComponent (button);
    }

    for (int i = 0; i < kMaxMacros; ++i)
    {
        auto& slider = macroSliders[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled (true, true, this);
        addChildComponent (slider);
        macroAttachments[i] = std::make_unique<juce::SliderParameterAttachment> (*processor.macroParam (i), slider);

        macroLabels[i].setJustificationType (juce::Justification::centredTop);
        macroLabels[i].setFont (juce::FontOptions (12.5f));
        macroLabels[i].setMinimumHorizontalScale (0.7f);
        addChildComponent (macroLabels[i]);
    }

    auto setupLinear = [this] (juce::Slider& slider, juce::Label& label, const char* text) {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        addAndMakeVisible (slider);
        label.setText (tx (text), juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, cor::textoSuave);
        label.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (label);
    };
    setupLinear (mixSlider, mixLabel, "Mix");
    setupLinear (gainSlider, gainLabel, "Saída");
    mixAttachment = std::make_unique<juce::SliderParameterAttachment> (*processor.mixParam(), mixSlider);
    gainAttachment = std::make_unique<juce::SliderParameterAttachment> (*processor.gainParam(), gainSlider);
    mixSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    gainSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    mixSlider.updateText();
    gainSlider.updateText();

    settingsPanel = std::make_unique<SettingsPanel> (processor, [this] { showSettings (false); });
    addChildComponent (*settingsPanel);

    processor.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (780, 520, 1500, 1000);
    setSize (960, 600);
    refresh();
    startTimerHz (6);
}

MagicMixEditor::~MagicMixEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void MagicMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (cor::fundo);

    // faixa do topo
    auto header = getLocalBounds().removeFromTop (48);
    g.setColour (cor::painel);
    g.fillRect (header);
    g.setColour (cor::ciano);
    g.fillRect (header.removeFromBottom (2));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("MagicMIX", header.reduced (16, 0).removeFromLeft (130), juce::Justification::centredLeft);

    const auto track = processor.trackName();
    if (track.isNotEmpty())
    {
        g.setColour (cor::textoSuave);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (track, header.reduced (16, 0).withTrimmedLeft (140).removeFromLeft (220), juce::Justification::centredLeft);
    }

    g.setColour (cor::painel);
    g.fillRoundedRectangle (leftArea.toFloat(), 8.0f);
    g.fillRoundedRectangle (rightArea.toFloat(), 8.0f);
}

void MagicMixEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (48).reduced (12, 9);
    settingsButton.setBounds (header.removeFromRight (70));
    header.removeFromRight (6);
    jsonButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (6);
    undoButton.setBounds (header.removeFromRight (86));
    header.removeFromRight (6);
    newButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (12);
    statusLabel.setBounds (header.withTrimmedLeft (370));

    area.reduce (12, 12);
    leftArea = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.44f));
    area.removeFromLeft (12);
    rightArea = area;

    // esquerda: conversa, sugestões e caixa do pedido
    auto left = leftArea.reduced (12);
    auto promptRow = left.removeFromBottom (64);
    sendButton.setBounds (promptRow.removeFromRight (84).reduced (0, 12));
    promptRow.removeFromRight (8);
    promptBox.setBounds (promptRow);
    left.removeFromBottom (8);
    for (int i = kMaxSuggestions - 1; i >= 0; --i)
        if (suggestionButtons[i].isVisible())
        {
            suggestionButtons[i].setBounds (left.removeFromBottom (28));
            left.removeFromBottom (5);
        }
    conversation.setBounds (left);

    // direita: título, macros, cadeia, mix e saída
    auto right = rightArea.reduced (12);
    chainTitle.setBounds (right.removeFromTop (28));
    auto bottom = right.removeFromBottom (58);
    auto mixRow = bottom.removeFromTop (27);
    mixLabel.setBounds (mixRow.removeFromLeft (48));
    mixSlider.setBounds (mixRow);
    bottom.removeFromTop (4);
    gainLabel.setBounds (bottom.removeFromLeft (48));
    gainSlider.setBounds (bottom);

    int visible = 0;
    for (auto& slider : macroSliders)
        visible += slider.isVisible() ? 1 : 0;
    if (visible > 0)
    {
        const int columns = juce::jmin (5, visible), rows = (visible + columns - 1) / columns;
        auto grid = right.removeFromTop (rows * 108);
        const int cellWidth = grid.getWidth() / columns;
        for (int i = 0; i < visible; ++i)
        {
            auto cell = juce::Rectangle<int> (grid.getX() + (i % columns) * cellWidth, grid.getY() + (i / columns) * 108, cellWidth, 108);
            macroLabels[i].setBounds (cell.removeFromBottom (30));
            macroSliders[i].setBounds (cell);
        }
        right.removeFromTop (8);
    }
    chainView.setBounds (right.withTrimmedBottom (8));

    settingsPanel->setBounds (getLocalBounds());
}

void MagicMixEditor::timerCallback()
{
    // Os valores da cadeia acompanham os botões.
    const auto text = processor.chainLines().joinIntoString ("\n\n");
    if (text != lastChainText)
    {
        lastChainText = text;
        chainView.setText (text.isEmpty() ? tx ("Nenhum efeito ainda. O som passa sem alteração.") : text, false);
    }
    const bool busy = processor.busy();
    sendButton.setEnabled (! busy);
    for (auto& button : suggestionButtons)
        button.setEnabled (! busy);
}

void MagicMixEditor::refresh()
{
    const auto& spec = processor.spec();

    // conversa
    conversation.clear();
    auto append = [this] (const juce::String& text, juce::Colour colour, float size, bool bold) {
        conversation.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        conversation.setColour (juce::TextEditor::textColourId, colour);
        conversation.moveCaretToEnd();
        conversation.insertTextAtCaret (text);
    };
    if (spec.prompts.empty() && spec.modules.empty())
        append (tx ("Dê play na pista para o plugin ouvir o som e descreva o que você quer.\n\n"
                    "Exemplos:\n  voz mais na cara e com brilho\n  bumbo com mais peso, sem estourar\n"
                    "  não sei o que fazer com esse teclado, me dá sugestões"),
                cor::textoSuave, 15.0f, false);
    for (auto& p : spec.prompts)
        append (tx ("Você  ") + tx (p) + "\n\n", cor::ciano, 15.0f, false);
    if (spec.summary.size() > 0)
        append (tx (spec.summary) + "\n\n", cor::texto, 15.0f, false);
    if (spec.details.size() > 0)
        append (tx ("Detalhes\n"), cor::textoSuave, 13.0f, true), append (tx (spec.details) + "\n\n", cor::textoSuave, 13.5f, false);
    if (! processor.report().empty())
    {
        append (tx ("Ajustes de segurança\n"), cor::textoSuave, 13.0f, true);
        for (auto& line : processor.report())
            append (tx ("  ") + tx (line) + "\n", cor::textoSuave, 13.0f, false);
    }
    conversation.setColour (juce::TextEditor::textColourId, cor::texto);

    // sugestões
    for (int i = 0; i < kMaxSuggestions; ++i)
    {
        const bool used = i < (int) spec.suggestions.size();
        suggestionButtons[i].setVisible (used);
        if (used)
            suggestionButtons[i].setButtonText (tx (spec.suggestions[(size_t) i]));
    }

    // macros
    chainTitle.setText (spec.title.empty() ? tx (spec.modules.empty() ? "Sem cadeia" : "Cadeia") : tx (spec.title), juce::dontSendNotification);
    for (int i = 0; i < kMaxMacros; ++i)
    {
        const bool used = i < (int) spec.macros.size();
        macroSliders[i].setVisible (used);
        macroLabels[i].setVisible (used);
        if (used)
        {
            macroLabels[i].setText (tx (spec.macros[(size_t) i].name), juce::dontSendNotification);
            macroSliders[i].setTooltip (tx (spec.macros[(size_t) i].description));
        }
    }

    statusLabel.setText (processor.status(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, processor.statusIsError() ? cor::erro : cor::textoSuave);
    undoButton.setEnabled (processor.canUndo());
    lastChainText = "\x01";  // força a atualização do texto da cadeia
    timerCallback();
    resized();
    repaint();
}

void MagicMixEditor::send (const juce::String& text)
{
    if (text.trim().isEmpty() || processor.busy())
        return;
    processor.sendPrompt (text);
    promptBox.clear();
}

void MagicMixEditor::showJsonMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&look);
    menu.addItem (1, tx ("Copiar a cadeia (JSON)"), ! processor.spec().modules.empty());
    menu.addItem (2, tx ("Colar uma cadeia (JSON)"));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (jsonButton),
                        [safe = juce::Component::SafePointer<MagicMixEditor> (this)] (int choice) {
                            if (safe == nullptr)
                                return;
                            if (choice == 1)
                                juce::SystemClipboard::copyTextToClipboard (safe->processor.specJson());
                            else if (choice == 2)
                            {
                                juce::String error;
                                if (! safe->processor.loadSpecJson (juce::SystemClipboard::getTextFromClipboard(), error))
                                {
                                    safe->statusLabel.setText (error, juce::dontSendNotification);
                                    safe->statusLabel.setColour (juce::Label::textColourId, cor::erro);
                                }
                            }
                        });
}

void MagicMixEditor::showSettings (bool visible)
{
    if (visible)
        settingsPanel->load();
    settingsPanel->setVisible (visible);
    settingsPanel->toFront (visible);
}
}
