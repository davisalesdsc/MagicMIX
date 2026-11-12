#include "Visual.h"

#include <cmath>

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "Texto.h"

namespace mm
{
// Fontes do protótipo, embutidas no plugin (licença OFL, ver Source/Fontes).
static juce::Typeface::Ptr fonteEmbutida (const char* recurso)
{
    int tamanho = 0;
    if (const char* dados = BinaryData::getNamedResource (recurso, tamanho))
        return juce::Typeface::createSystemTypefaceFor (dados, (size_t) tamanho);
    return nullptr;
}

static juce::Font comFonte (juce::Typeface::Ptr tipo, float tamanhoPx, bool negritoSeFaltar)
{
    // Tamanhos em px, como no CSS do protótipo.
    if (tipo != nullptr)
        return juce::Font (juce::FontOptions (tipo).withPointHeight (tamanhoPx));
    return juce::Font (juce::FontOptions (tamanhoPx * 1.3f, negritoSeFaltar ? juce::Font::bold : juce::Font::plain));
}

juce::Font fonteTexto (float altura, bool negrito)
{
    static auto medio = fonteEmbutida ("ManropeMedium_ttf"), forte = fonteEmbutida ("ManropeBold_ttf");
    return comFonte (negrito ? forte : medio, altura, negrito);
}

juce::Font fonteMarca (float altura)
{
    static auto tipo = fonteEmbutida ("UnboundedBold_ttf");
    return comFonte (tipo, altura, true);
}

int larguraDoTexto (const juce::Font& fonte, const juce::String& texto)
{
    return juce::GlyphArrangement::getStringWidthInt (fonte, texto);
}

juce::String maiusculas (const juce::String& texto)
{
    juce::String saida;
    for (auto c : texto)
    {
        // Latim-1: as minúsculas acentuadas (à a þ, menos ÷) ficam 32 posições depois das maiúsculas.
        if (c >= 0xe0 && c <= 0xfe && c != 0xf7)
            c -= 32;
        else
            c = juce::CharacterFunctions::toUpperCase (c);
        saida += juce::String::charToString (c);
    }
    return saida;
}

juce::Font fonteNumero (float altura)
{
    static auto tipo = fonteEmbutida ("IBMPlexMonoRegular_ttf");
    if (tipo != nullptr)
        return comFonte (tipo, altura, false);
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), altura * 1.3f, juce::Font::plain));
}

// ------------------------------------------------------------------ aparência
Visual::Visual()
{
    setColour (juce::ResizableWindow::backgroundColourId, cor::fundo);
    setColour (juce::TextButton::buttonColourId, cor::painelClaro);
    setColour (juce::TextButton::buttonOnColourId, cor::cianoForte);
    setColour (juce::TextButton::textColourOffId, cor::textoSuave);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff062a33));
    setColour (juce::TextEditor::backgroundColourId, cor::app);
    setColour (juce::TextEditor::textColourId, cor::texto);
    setColour (juce::TextEditor::outlineColourId, cor::linha);
    setColour (juce::TextEditor::focusedOutlineColourId, cor::ciano);
    setColour (juce::TextEditor::highlightColourId, cor::cianoEscuro);
    setColour (juce::CaretComponent::caretColourId, cor::ciano);
    setColour (juce::Label::textColourId, cor::texto);
    setColour (juce::Slider::rotarySliderFillColourId, cor::ciano);
    setColour (juce::Slider::rotarySliderOutlineColourId, cor::painelClaro);
    setColour (juce::PopupMenu::backgroundColourId, cor::painel);
    setColour (juce::PopupMenu::textColourId, cor::texto);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cor::cianoEscuro);
    setColour (juce::PopupMenu::highlightedTextColourId, cor::texto);
    setColour (juce::ScrollBar::thumbColourId, cor::linha);
}

void Visual::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float raio = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto centro = area.getCentre();
    const float espessura = juce::jmax (3.0f, raio * 0.13f);
    const float angulo = startAngle + position * (endAngle - startAngle);
    const bool ativo = slider.isEnabled();

    juce::Path trilho;
    trilho.addCentredArc (centro.x, centro.y, raio - espessura * 0.5f, raio - espessura * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (cor::painelClaro);
    g.strokePath (trilho, juce::PathStrokeType (espessura, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Parâmetros com ganho (dB de -x a +x) acendem a partir do centro.
    const bool doCentro = slider.getProperties().getWithDefault ("bipolar", false);
    const float inicio = doCentro ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path valor;
    valor.addCentredArc (centro.x, centro.y, raio - espessura * 0.5f, raio - espessura * 0.5f, 0.0f,
                         juce::jmin (inicio, angulo), juce::jmax (inicio, angulo), true);
    g.setColour (ativo ? cor::ciano : cor::textoFraco);
    g.strokePath (valor, juce::PathStrokeType (espessura, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float miolo = raio - espessura * 2.2f;
    g.setColour (cor::app);
    g.fillEllipse (juce::Rectangle<float> (miolo * 2.0f, miolo * 2.0f).withCentre (centro));
    g.setColour (cor::linha);
    g.drawEllipse (juce::Rectangle<float> (miolo * 2.0f, miolo * 2.0f).withCentre (centro), 1.0f);
    const auto ponta = centro.getPointOnCircumference (miolo * 0.82f, angulo), base = centro.getPointOnCircumference (miolo * 0.3f, angulo);
    g.setColour (cor::texto);
    g.drawLine ({ base, ponta }, juce::jmax (2.0f, raio * 0.06f));
}

void Visual::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& fundoBotao, bool highlighted, bool down)
{
    auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getProperties().getWithDefault ("tracejado", false))  // "+ Adicionar...": borda tracejada, sem fundo
    {
        juce::Path r, t;
        r.addRoundedRectangle (area, 10.0f);
        const float tracos[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (t, r, tracos, 2);
        g.setColour (highlighted ? cor::ciano.withAlpha (0.6f) : cor::linha);
        g.fillPath (t);
        return;
    }
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : fundoBotao;
    if (! b.isEnabled()) c = c.withAlpha (0.4f);
    else if (down) c = c.brighter (0.15f);
    else if (highlighted) c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (area, 7.0f);
    if (! b.getToggleState() && ! (bool) b.getProperties().getWithDefault ("semBorda", false))
    {
        g.setColour (cor::linha);
        g.drawRoundedRectangle (area, 7.0f, 1.0f);
    }
}

juce::Font Visual::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonteTexto (juce::jmin (13.0f, (float) buttonHeight * 0.45f));
}

void Visual::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                            int thumbStart, int thumbSize, bool over, bool down)
{
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x + width / 2 - 2, thumbStart, 4, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y + height / 2 - 2, thumbSize, 4);
    g.setColour (cor::linha.brighter (over || down ? 0.3f : 0.0f));
    g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
}

// ------------------------------------------------------------------ valores
static juce::String numero (float v, int casas)
{
    return juce::String (v, casas).replaceCharacter ('.', ',');
}

juce::String textoDoValor (const ParamDef& p, float v)
{
    const auto rotulo = p.labelFor (v);
    if (! rotulo.empty())
        return tx (rotulo);
    const juce::String unidade = tx (p.unit);
    if (unidade == "Hz")
        return v >= 1000.0f ? numero (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : numero (v, 0) + " Hz";
    if (unidade == "0-1")
        return numero (v * 100.0f, 0) + " %";
    if (unidade == "ms")
        return v >= 1000.0f ? numero (v / 1000.0f, 2) + " s" : numero (v, v < 10.0f ? 1 : 0) + " ms";
    if (unidade == ":1")
        return numero (v, v < 10.0f ? 1 : 0) + ":1";
    if (unidade == "dB" || unidade == "dBFS")
        return (v > 0.05f ? "+" : "") + numero (v, 1) + " dB";
    if (unidade == "x")
        return numero (v, 2) + "x";
    return numero (v, std::abs (v) < 10.0f ? 1 : 0) + (unidade.isNotEmpty() ? " " + unidade : juce::String());
}

static void configurarFaixa (juce::Slider& s, const ParamDef& p)
{
    juce::NormalisableRange<double> faixa (p.min, p.max);
    if (p.log && p.min > 0.0f)
    {
        const double minimo = p.min, maximo = p.max;
        faixa = juce::NormalisableRange<double> (minimo, maximo,
            [] (double lo, double hi, double t) { return lo * std::pow (hi / lo, t); },
            [] (double lo, double hi, double v) { return std::log (v / lo) / std::log (hi / lo); });
    }
    s.setNormalisableRange (faixa);
    s.getProperties().set ("bipolar", p.min < 0.0f && p.max > 0.0f && std::abs (p.min + p.max) < 0.01f);
}

// ------------------------------------------------------------------ knob de parâmetro
KnobParametro::KnobParametro (MagicMixProcessor& proc, int mod, const ParamDef& p, const juce::String& r)
    : processor (proc), modulo (mod), param (p), rotulo (r)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    configurarFaixa (knob, p);
    knob.setDoubleClickReturnValue (true, p.def);
    knob.setTooltip (tx (p.description));
    knob.onDragStart = [this] { arrastando = true; processor.comecarAjuste(); };
    knob.onDragEnd = [this] { arrastando = false; };
    knob.onValueChange = [this] {
        if (! relendo)  // mudou pela mão do músico (arrasto, roda do mouse, duplo clique ou teclado)
        {
            if (! arrastando)
                processor.comecarAjuste();
            processor.ajustarParametro (modulo, param.name, (float) knob.getValue());
        }
        repaint();
    };
    addAndMakeVisible (knob);
    atualizar();
}

void KnobParametro::atualizar()
{
    if (arrastando)
        return;
    const float v = processor.valorDoParametro (modulo, param.name);
    if (std::abs ((float) knob.getValue() - v) > 1.0e-5f)
    {
        const juce::ScopedValueSetter<bool> s (relendo, true);
        knob.setValue (v, juce::sendNotificationSync);
    }
}

void KnobParametro::resized()
{
    knob.setBounds (getLocalBounds().removeFromTop (getHeight() - 38).reduced (6, 2));
}

void KnobParametro::paint (juce::Graphics& g)
{
    auto baixo = getLocalBounds().removeFromBottom (38);
    g.setColour (cor::textoSuave);
    g.setFont (fonteNumero (12.5f));
    g.drawText (textoDoValor (param, (float) knob.getValue()), baixo.removeFromTop (18), juce::Justification::centred);
    g.setColour (cor::texto);
    g.setFont (fonteTexto (13.5f));
    g.drawFittedText (rotulo, baixo, juce::Justification::centredTop, 1);
}

// ------------------------------------------------------------------ knob do host
KnobHost::KnobHost (juce::RangedAudioParameter& p, const juce::String& r, std::function<juce::String (float)> f)
    : param (p), rotulo (r), formatar (std::move (f)), ligacao (p, knob)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    knob.onValueChange = [this] { repaint(); };
    addAndMakeVisible (knob);
}

void KnobHost::resized()
{
    knob.setBounds (getLocalBounds().removeFromTop (getHeight() - (reservar || destaque ? 54 : 38)).reduced (4, 2));
}

void KnobHost::paint (juce::Graphics& g)
{
    auto baixo = getLocalBounds().removeFromBottom (reservar || destaque ? 54 : 38);
    if (reservar || destaque)
    {
        auto tag = baixo.removeFromBottom (16);
        if (destaque)
        {
        g.setColour (cor::ciano);
        g.setFont (fonteTexto (10.5f));
        g.drawText ("do pedido", tag, juce::Justification::centredTop);
        }
    }
    g.setColour (cor::textoSuave);
    g.setFont (fonteNumero (12.5f));
    g.drawText (formatar ((float) knob.getValue()), baixo.removeFromTop (18), juce::Justification::centred);
    if (rotuloCiano)
    {
        g.setColour (cor::ciano);
        g.setFont (fonteTexto (11.0f, true).withExtraKerningFactor (0.14f));
    }
    else
    {
        g.setColour (cor::texto);
        g.setFont (fonteTexto (12.5f));
    }
    g.drawFittedText (rotulo, baixo, juce::Justification::centredTop, 1);
}

// ------------------------------------------------------------------ escolha
Escolha::Escolha (juce::StringArray opcoes, std::function<void (int)> f) : aoEscolher (std::move (f))
{
    for (int i = 0; i < opcoes.size(); ++i)
    {
        auto* b = botoes.add (new juce::TextButton (opcoes[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] {
            setEscolhido (i);
            if (aoEscolher) aoEscolher (i);
        };
        addAndMakeVisible (b);
    }
}

void Escolha::setEscolhido (int indice)
{
    atual = indice;
    for (int i = 0; i < botoes.size(); ++i)
        botoes[i]->setToggleState (i == indice, juce::dontSendNotification);
}

void Escolha::setAgrupado (bool sim)
{
    agrupado = sim;
    for (auto* b : botoes)
    {
        b->setColour (juce::TextButton::buttonColourId, sim ? juce::Colours::transparentBlack : cor::painelClaro);
        b->getProperties().set ("semBorda", sim);
    }
    resized();
    repaint();
}

void Escolha::paint (juce::Graphics& g)
{
    if (! agrupado) return;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff2a2c2f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (cor::linha);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

int Escolha::larguraIdeal() const
{
    int w = 0;
    for (auto* b : botoes)
        w += larguraDoTexto (fonteTexto (14.0f, true), b->getButtonText()) + 24 + 4;
    return w;
}

void Escolha::resized()
{
    auto area = getLocalBounds();
    if (agrupado) area = area.reduced (3);
    const float escala = (float) area.getWidth() / (float) juce::jmax (1, larguraIdeal());
    for (auto* b : botoes)
    {
        const int w = (int) ((larguraDoTexto (fonteTexto (14.0f, true), b->getButtonText()) + 24) * juce::jmin (escala, 1.0f));
        b->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (4);
    }
}

void Chave::paintButton (juce::Graphics& g, bool, bool)
{
    auto area = getLocalBounds().toFloat().withSizeKeepingCentre (38.0f, 20.0f);
    const bool on = getToggleState();
    g.setColour (on ? cor::cianoForte : cor::painelClaro);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (cor::linha);
    if (! on) g.drawRoundedRectangle (area, 10.0f, 1.0f);
    const auto bola = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ on ? area.getRight() - 10.0f : area.getX() + 10.0f, area.getCentreY() });
    g.setColour (on ? juce::Colours::white : cor::textoFraco);
    g.fillEllipse (bola);
}

// ------------------------------------------------------------------ espectro
Espectro::Espectro() : janela ((size_t) kTamanho), re ((size_t) kTamanho), im ((size_t) kTamanho), niveis ((size_t) kPontos, -90.0f)
{
    fft.prepare (kTamanho);
    for (int i = 0; i < kTamanho; ++i)
        janela[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kTamanho);
}

float Espectro::frequencia (int ponto)
{
    return 20.0f * std::pow (1000.0f, (float) ponto / (float) (kPontos - 1));
}

void Espectro::atualizar (MagicMixProcessor& processor, bool saida)
{
    const double taxa = processor.sampleRate() > 0.0 ? processor.sampleRate() : 44100.0;
    processor.ultimasAmostras (saida, re.data(), kTamanho);
    for (int i = 0; i < kTamanho; ++i)
    {
        re[(size_t) i] *= janela[(size_t) i];
        im[(size_t) i] = 0.0f;
    }
    fft.transform (re.data(), im.data(), false);
    const float binHz = (float) taxa / (float) kTamanho;
    const float normal = 2.0f / (float) kTamanho * 2.0f;  // a janela de Hann tira metade da amplitude
    for (int p = 0; p < kPontos; ++p)
    {
        const float f = frequencia (p);
        const int lo = juce::jlimit (1, kTamanho / 2 - 1, (int) (f / std::pow (2.0f, 1.0f / 12.0f) / binHz));
        const int hi = juce::jlimit (lo + 1, kTamanho / 2, (int) (f * std::pow (2.0f, 1.0f / 12.0f) / binHz) + 1);
        float soma = 0.0f;
        for (int b = lo; b < hi; ++b)
            soma += re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b];
        const float db = juce::Decibels::gainToDecibels (std::sqrt (soma) * normal, -100.0f);
        auto& n = niveis[(size_t) p];
        n = db > n ? db : n - 2.5f;  // sobe na hora, cai devagar
    }
}

float xDaFrequencia (float f, juce::Rectangle<float> area)
{
    return area.getX() + area.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float frequenciaDoX (float x, juce::Rectangle<float> area)
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()));
}

void grade (juce::Graphics& g, juce::Rectangle<float> area, float dbMin, float dbMax, float passoDb)
{
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawVerticalLine ((int) xDaFrequencia (f, area), area.getY(), area.getBottom());
    for (float db = dbMin; db <= dbMax + 0.01f; db += passoDb)
        g.drawHorizontalLine ((int) juce::jmap (db, dbMax, dbMin, area.getY(), area.getBottom()), area.getX(), area.getRight());
    g.setColour (cor::textoFraco);
    g.setFont (fonteNumero (11.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto t = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        g.drawText (t, juce::Rectangle<float> (xDaFrequencia (f, area) - 20.0f, area.getBottom() - 15.0f, 40.0f, 14.0f), juce::Justification::centred);
    }
}
}
