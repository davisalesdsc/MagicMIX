#include "Faces.h"

#include <cmath>
#include <complex>

#include "Nomes.h"
#include "PluginProcessor.h"
#include "Texto.h"

namespace mm
{
// ------------------------------------------------------------------ nomes
// ------------------------------------------------------------------ base
Face::Face (MagicMixProcessor& p, int m) : processor (p), modulo (m)
{
    const auto& spec = processor.spec();
    if (modulo >= 0 && modulo < (int) spec.modules.size())
        modelo = findModule (spec.modules[(size_t) modulo].type);
}

void Face::atualizar()
{
    for (auto* k : knobs)
        k->atualizar();
    repaint();
}

juce::String Face::explicacao() const
{
    return modelo != nullptr ? tx (modelo->description) : juce::String();
}

const ParamDef* Face::def (const std::string& param) const
{
    if (modelo == nullptr) return nullptr;
    const int i = modelo->paramIndex (param);
    return i >= 0 ? &modelo->params[(size_t) i] : nullptr;
}

float Face::valor (const std::string& param) const { return processor.valorDoParametro (modulo, param); }

void Face::ajustar (const std::string& param, float v)
{
    processor.comecarAjuste();
    processor.ajustarParametro (modulo, param, v);
}

KnobParametro* Face::novoKnob (const std::string& param, const juce::String& rotulo)
{
    const auto* d = def (param);
    if (d == nullptr) return nullptr;
    auto* k = knobs.add (new KnobParametro (processor, modulo, *d, rotulo.isNotEmpty() ? rotulo : rotuloDoParametro (param)));
    addAndMakeVisible (k);
    return k;
}

int Face::alturaDosKnobs (int largura, int quantos) const
{
    const int porLinha = juce::jmax (1, largura / KnobParametro::kLargura);
    return ((quantos + porLinha - 1) / porLinha) * KnobParametro::kAltura;
}

void Face::disporKnobs (juce::Rectangle<int> area, const std::vector<KnobParametro*>& lista) const
{
    const int porLinha = juce::jmax (1, juce::jmin ((int) lista.size(), area.getWidth() / KnobParametro::kLargura));
    const int largura = area.getWidth() / porLinha;
    for (size_t i = 0; i < lista.size(); ++i)
    {
        if (lista[i] == nullptr) continue;
        const int linha = (int) i / porLinha, coluna = (int) i % porLinha;
        lista[i]->setBounds (area.getX() + coluna * largura + (largura - KnobParametro::kLargura) / 2,
                             area.getY() + linha * KnobParametro::kAltura, KnobParametro::kLargura, KnobParametro::kAltura);
    }
}

// ------------------------------------------------------------------ medidores
void desenharVu (juce::Graphics& g, juce::Rectangle<float> area, float reducaoDb, const juce::String& titulo)
{
    g.setColour (cor::app);
    g.fillRoundedRectangle (area, 8.0f);
    const auto c = juce::Point<float> (area.getCentreX(), area.getBottom() - area.getHeight() * 0.18f);
    const float r = juce::jmin (area.getWidth() * 0.42f, area.getHeight() * 0.66f);
    const float a0 = -juce::MathConstants<float>::pi * 0.38f, a1 = juce::MathConstants<float>::pi * 0.38f;
    auto anguloDe = [&] (float db) { return juce::jmap (std::sqrt (juce::jlimit (0.0f, 20.0f, db) / 20.0f), a1, a0); };

    juce::Path arco;
    arco.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
    g.setColour (cor::linha);
    g.strokePath (arco, juce::PathStrokeType (2.0f));
    juce::Path vermelho;
    vermelho.addCentredArc (c.x, c.y, r, r, 0.0f, a0, anguloDe (10.0f), true);
    g.setColour (cor::reducao.withAlpha (0.8f));
    g.strokePath (vermelho, juce::PathStrokeType (3.0f));

    g.setFont (fonteNumero (11.0f));
    for (float db : { 0.0f, 3.0f, 5.0f, 10.0f, 20.0f })
    {
        const float a = anguloDe (db);
        const auto p1 = c.getPointOnCircumference (r, a), p2 = c.getPointOnCircumference (r + 6.0f, a), pt = c.getPointOnCircumference (r + 16.0f, a);
        g.setColour (cor::textoFraco);
        g.drawLine ({ p1, p2 }, 1.2f);
        g.drawText (db < 0.5f ? "0" : "-" + juce::String ((int) db), juce::Rectangle<float> (26.0f, 12.0f).withCentre (pt), juce::Justification::centred);
    }
    const float a = anguloDe (reducaoDb);
    g.setColour (cor::ciano);
    g.drawLine ({ c, c.getPointOnCircumference (r - 4.0f, a) }, 2.5f);
    g.setColour (cor::textoFraco);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (c));

    g.setColour (cor::ciano);
    g.setFont (fonteTexto (11.5f, true));
    g.drawText (titulo, area.withHeight (20.0f).translated (10.0f, 6.0f), juce::Justification::centredLeft);
    g.setColour (cor::textoSuave);
    g.setFont (fonteTexto (11.0f));
    g.drawText (tx ("REDUÇÃO"), juce::Rectangle<float> (120.0f, 14.0f).withCentre ({ c.x, c.y - r * 0.42f }), juce::Justification::centred);
    g.setColour (cor::texto);
    g.setFont (fonteNumero (13.0f));
    g.drawText ((reducaoDb > 0.05f ? "-" : "") + juce::String (reducaoDb, 1).replaceCharacter ('.', ',') + " dB",
                juce::Rectangle<float> (120.0f, 16.0f).withCentre ({ c.x, c.y - r * 0.42f + 16.0f }), juce::Justification::centred);
}

void desenharBarra (juce::Graphics& g, juce::Rectangle<float> area, float pico, const juce::String& rotulo)
{
    auto barra = area.withTrimmedBottom (16.0f);
    g.setColour (cor::app.brighter (0.05f));
    g.fillRoundedRectangle (barra, 2.0f);
    const float db = juce::Decibels::gainToDecibels (pico, -60.0f);
    const float h = barra.getHeight() * juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
    g.setColour (db > -1.0f ? cor::reducao : cor::cianoForte);
    g.fillRoundedRectangle (barra.withTop (barra.getBottom() - h), 2.0f);
    g.setColour (cor::textoFraco);
    g.setFont (fonteTexto (10.5f));
    g.drawText (rotulo, area.withTop (area.getBottom() - 14.0f).expanded (8.0f, 0.0f), juce::Justification::centred);
}

// ------------------------------------------------------------------ EQ
// Resposta de um biquad no estilo do motor (RBJ), em dB, numa frequência.
static float respostaBanda (int tipo, float f0, float ganho, float q, float f, double taxa)
{
    if (tipo <= 0) return 0.0f;
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / taxa, cs = std::cos (w0), sn = std::sin (w0);
    const double alfa = sn / (2.0 * std::max (0.05f, q)), A = std::pow (10.0, ganho / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (tipo)
    {
        case 1: b0 = 1 + alfa * A; b1 = -2 * cs; b2 = 1 - alfa * A; a0 = 1 + alfa / A; a1 = -2 * cs; a2 = 1 - alfa / A; break;
        case 2: { const double s = 2 * std::sqrt (A) * alfa;
                  b0 = A * ((A + 1) - (A - 1) * cs + s); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - s);
                  a0 = (A + 1) + (A - 1) * cs + s; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - s; break; }
        case 3: { const double s = 2 * std::sqrt (A) * alfa;
                  b0 = A * ((A + 1) + (A - 1) * cs + s); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - s);
                  a0 = (A + 1) - (A - 1) * cs + s; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - s; break; }
        case 4: b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; a0 = 1 + alfa; a1 = -2 * cs; a2 = 1 - alfa; break;
        case 5: b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; a0 = 1 + alfa; a1 = -2 * cs; a2 = 1 - alfa; break;
        default: return 0.0f;
    }
    const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / taxa);
    const auto h = (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z);
    return (float) (20.0 * std::log10 (std::max (std::abs (h), 1.0e-6)));
}

class EqFace : public Face, private juce::Timer
{
public:
    EqFace (MagicMixProcessor& p, int m) : Face (p, m),
        tipos ({ "Sino", "Grave", "Agudo", "Corte", "Corte agudo" }, [this] (int t) {
            ajustar (nomeBanda ("type"), (float) (t + 1));
            remontarKnobs();
        })
    {
        addAndMakeVisible (tipos);
        remover.setButtonText (tx ("Remover banda"));
        remover.onClick = [this] {
            if (selecionada <= 0) return;
            ajustar (nomeBanda ("type"), 0.0f);
            selecionada = 0;
            for (int b = 1; b <= 8; ++b)
                if (tipoDa (b) > 0) { selecionada = b; break; }
            remontarKnobs();
        };
        addAndMakeVisible (remover);
        for (int b = 1; b <= 8; ++b)
            if (tipoDa (b) > 0) { selecionada = b; break; }
        remontarKnobs();
        startTimerHz (25);
    }

    juce::String explicacao() const override
    {
        return tx ("Arraste os pontos para mudar frequência e ganho. Roda do mouse sobre um ponto muda a largura; "
                   "duplo clique num espaço vazio cria uma banda.");
    }

    int alturaIdeal (int largura) const override { return 240 + 8 + alturaPilulas (largura) + 8 + KnobParametro::kAltura; }

    void resized() override
    {
        auto area = getLocalBounds();
        grafico = area.removeFromTop (240).toFloat();
        area.removeFromTop (8);
        montarPilulas (area.getWidth());
        area.removeFromTop (alturaPilulas (area.getWidth()) + 8);
        std::vector<KnobParametro*> lista (knobs.begin(), knobs.end());
        auto esquerda = area.removeFromLeft (juce::jmin (area.getWidth() / 2, 3 * KnobParametro::kLargura));
        disporKnobs (esquerda, lista);
        area.removeFromLeft (16);
        rotuloBanda = area.removeFromTop (22);
        tipos.setBounds (area.removeFromTop (30).removeFromLeft (juce::jmin (area.getWidth(), tipos.larguraIdeal())));
        area.removeFromTop (8);
        remover.setBounds (area.removeFromTop (28).removeFromLeft (larguraDoTexto (fonteTexto (13.0f), remover.getButtonText()) + 24));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cor::app);
        g.fillRoundedRectangle (grafico, 8.0f);
        const auto area = areaCurva();
        grade (g, area.withTrimmedBottom (-14.0f), -18.0f, 18.0f, 6.0f);
        g.setFont (fonteNumero (10.5f));
        g.setColour (cor::textoFraco);
        for (int db : { 12, 6, 0, -6, -12 })
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (grafico.getX() + 4.0f, juce::jmap ((float) db, 18.0f, -18.0f, area.getY(), area.getBottom()) - 7.0f, 26.0f, 14.0f),
                        juce::Justification::centredRight);

        // botão do analisador, no canto
        g.setColour (analisador ? cor::ciano : cor::textoFraco);
        g.setFont (fonteNumero (10.5f));
        g.drawText (analisador ? "ANALISADOR" : "ANALISADOR DESLIGADO", botaoAnalisador(), juce::Justification::centredRight);

        // analisador (o som que sai), atrás de tudo
        if (analisador)
        {
            juce::Path rta;
            rta.startNewSubPath (area.getX(), area.getBottom());
            for (int i = 0; i < Espectro::kPontos; ++i)
            {
                const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, espectro.nivelDb (i)), -90.0f, 0.0f, area.getBottom(), area.getY());
                rta.lineTo (xDaFrequencia (Espectro::frequencia (i), area), y);
            }
            rta.lineTo (area.getRight(), area.getBottom());
            rta.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0.0f, area.getY(),
                                                     juce::Colours::white.withAlpha (0.04f), 0.0f, area.getBottom(), false));
            g.fillPath (rta);
        }

        // curva do EQ, com o preenchimento até a linha do zero
        const double taxa = processor.sampleRate() > 0.0 ? processor.sampleRate() : 48000.0;
        juce::Path curva;
        for (int px = 0; px <= (int) area.getWidth(); px += 2)
        {
            const float f = frequenciaDoX (area.getX() + (float) px, area);
            float db = 0.0f;
            for (int b = 1; b <= 8; ++b)
                db += respostaBanda (tipoDa (b), valor (banda (b, "freq_hz")), valor (banda (b, "gain_db")), valor (banda (b, "q")), f, taxa);
            const float y = juce::jmap (juce::jlimit (-19.0f, 19.0f, db), 18.0f, -18.0f, area.getY(), area.getBottom());
            if (px == 0) curva.startNewSubPath (area.getX(), y); else curva.lineTo (area.getX() + (float) px, y);
        }
        const float zero = juce::jmap (0.0f, 18.0f, -18.0f, area.getY(), area.getBottom());
        juce::Path preenchida (curva);
        preenchida.lineTo (area.getRight(), zero);
        preenchida.lineTo (area.getX(), zero);
        preenchida.closeSubPath();
        g.setColour (cor::ciano.withAlpha (0.10f));
        g.fillPath (preenchida);
        g.setColour (cor::ciano);
        g.strokePath (curva, juce::PathStrokeType (2.2f));

        for (int b = 1; b <= 8; ++b)
        {
            if (tipoDa (b) <= 0) continue;
            const auto p = pontoDa (b, area);
            const float raio = b == selecionada ? 9.0f : 7.5f;
            g.setColour (corDa (b));
            g.fillEllipse (juce::Rectangle<float> (raio * 2.0f, raio * 2.0f).withCentre (p));
            if (b == selecionada)
            {
                g.setColour (cor::texto);
                g.drawEllipse (juce::Rectangle<float> (raio * 2.0f, raio * 2.0f).withCentre (p), 1.5f);
            }
            g.setColour (cor::app);
            g.setFont (fonteTexto (10.0f, true));
            g.drawText (juce::String (b), juce::Rectangle<float> (14.0f, 14.0f).withCentre (p), juce::Justification::centred);
        }

        // pílulas das bandas
        for (auto& pl : pilulas)
        {
            const bool sel = pl.banda == selecionada, mais = pl.banda == 0;
            if (mais)
            {
                juce::Path r, t;
                r.addRoundedRectangle (pl.caixa.reduced (0.5f), pl.caixa.getHeight() * 0.5f);
                const float tracos[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (t, r, tracos, 2);
                g.setColour (cor::ciano.withAlpha (0.5f));
                g.fillPath (t);
                g.setColour (cor::ciano);
                g.setFont (fonteTexto (12.0f));
                g.drawText (pl.texto, pl.caixa, juce::Justification::centred);
                continue;
            }
            g.setColour (sel ? cor::ciano.withAlpha (0.10f) : cor::painelClaro);
            g.fillRoundedRectangle (pl.caixa, pl.caixa.getHeight() * 0.5f);
            g.setColour (sel ? cor::ciano : cor::linha);
            g.drawRoundedRectangle (pl.caixa.reduced (0.5f), pl.caixa.getHeight() * 0.5f, 1.0f);
            g.setColour (corDa (pl.banda));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ pl.caixa.getX() + 13.0f, pl.caixa.getCentreY() }));
            g.setColour (sel ? cor::texto : cor::textoSuave);
            g.setFont (fonteTexto (12.0f));
            g.drawText (pl.texto, pl.caixa.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
        }

        g.setColour (cor::textoFraco);
        g.setFont (fonteTexto (11.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (selecionada > 0 ? "BANDA " + juce::String (selecionada) : tx ("NENHUMA BANDA"), rotuloBanda, juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        arrastada = 0;
        if (botaoAnalisador().contains (e.position))
        {
            analisador = ! analisador;
            repaint();
            return;
        }
        for (auto& pl : pilulas)
            if (pl.caixa.contains (e.position))
            {
                if (pl.banda == 0) criarBanda (1000.0f, 0.0f);
                else selecionar (pl.banda);
                return;
            }
        const auto area = areaCurva();
        arrastada = maisPerto (e.position, area);
        if (arrastada > 0)
        {
            selecionar (arrastada);
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                const char* const nomes[] = { "Sino", "Prateleira grave", "Prateleira aguda", "Corte (passa-altas)", "Corte agudo (passa-baixas)" };
                for (int t = 1; t <= 5; ++t)
                    menu.addItem (t, tx (nomes[t - 1]), true, tipoDa (arrastada) == t);
                menu.addSeparator();
                menu.addItem (100, tx ("Remover banda"));
                const int b = arrastada;
                menu.showMenuAsync (juce::PopupMenu::Options(), [this, b] (int r) {
                    if (r <= 0) return;
                    ajustar (banda (b, "type"), r == 100 ? 0.0f : (float) r);
                    remontarKnobs();
                });
                arrastada = 0;
                return;
            }
            processor.comecarAjuste();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (arrastada <= 0) return;
        const auto area = areaCurva();
        processor.ajustarParametro (modulo, banda (arrastada, "freq_hz"), frequenciaDoX (e.position.x, area));
        const int t = tipoDa (arrastada);
        if (t >= 1 && t <= 3)
            processor.ajustarParametro (modulo, banda (arrastada, "gain_db"), juce::jmap (e.position.y, area.getY(), area.getBottom(), 18.0f, -18.0f));
        montarPilulas (getWidth());
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { arrastada = 0; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto area = areaCurva();
        if (! area.contains (e.position) || maisPerto (e.position, area) > 0) return;
        criarBanda (frequenciaDoX (e.position.x, area), juce::jmap (e.position.y, area.getY(), area.getBottom(), 18.0f, -18.0f));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = maisPerto (e.position, areaCurva());
        if (b <= 0) return;
        processor.comecarAjuste();
        processor.ajustarParametro (modulo, banda (b, "q"), valor (banda (b, "q")) * std::pow (1.25f, w.deltaY * 4.0f));
        selecionar (b);
    }

    void atualizar() override
    {
        montarPilulas (getWidth());
        Face::atualizar();
    }

private:
    struct PilulaBanda { int banda = 0; juce::String texto; juce::Rectangle<float> caixa; };

    void timerCallback() override
    {
        if (analisador)
        {
            espectro.atualizar (processor, true);
            repaint (grafico.toNearestInt());
        }
    }

    static std::string banda (int b, const char* p) { return "b" + std::to_string (b) + "_" + p; }
    std::string nomeBanda (const char* p) const { return banda (juce::jmax (1, selecionada), p); }
    int tipoDa (int b) const { return (int) std::lround (valor (banda (b, "type"))); }
    juce::Rectangle<float> areaCurva() const { return grafico.reduced (10.0f, 8.0f).withTrimmedLeft (24.0f).withTrimmedBottom (14.0f); }
    juce::Rectangle<float> botaoAnalisador() const { return grafico.reduced (12.0f, 6.0f).removeFromTop (14.0f).removeFromRight (150.0f); }

    static juce::Colour corDa (int b)
    {
        static const juce::Colour cores[] = { cor::ciano, cor::aviso, juce::Colour (0xff72d8a6), juce::Colour (0xffff8a7c),
                                              juce::Colour (0xffb39dfa), juce::Colour (0xfff48fb1), juce::Colour (0xffe6d36b), juce::Colour (0xff7aa7ff) };
        return cores[(size_t) juce::jlimit (0, 7, b - 1)];
    }

    juce::String textoDaBanda (int b) const
    {
        const int t = tipoDa (b);
        const float f = valor (banda (b, "freq_hz")), ganho = valor (banda (b, "gain_db"));
        juce::String regiao;
        if (t == 4) regiao = "Corte";
        else if (t == 5) regiao = "Corte agudo";
        else regiao = f < 120.0f ? "Peso" : f < 500.0f ? "Grave" : f < 2000.0f ? tx ("Médio") : f < 6000.0f ? tx ("Presença") : "Ar";
        const auto* d = def (banda (b, "freq_hz"));
        juce::String s = juce::String (b) + tx (" · ") + regiao + tx (" · ") + (d != nullptr ? textoDoValor (*d, f) : juce::String());
        if (t >= 1 && t <= 3)
            s << tx (" · ") << (ganho > 0.05f ? "+" : ganho < -0.05f ? juce::String::fromUTF8 ("\xe2\x88\x92") : "")
              << juce::String (std::abs (ganho), 1).replaceCharacter ('.', ',') << " dB";
        return s;
    }

    void montarPilulas (int largura)
    {
        pilulas.clear();
        float x = 0.0f, y = grafico.getBottom() + 8.0f;
        auto por = [&] (int b, const juce::String& texto) {
            const float w = (float) larguraDoTexto (fonteTexto (12.0f), texto) + (b > 0 ? 34.0f : 22.0f);
            if (x + w > (float) largura && x > 0.0f) { x = 0.0f; y += 34.0f; }
            pilulas.push_back ({ b, texto, { x, y, w, 28.0f } });
            x += w + 6.0f;
        };
        for (int b = 1; b <= 8; ++b)
            if (tipoDa (b) > 0) por (b, textoDaBanda (b));
        por (0, "+ banda");
    }

    int alturaPilulas (int largura) const
    {
        float x = 0.0f;
        int linhas = 1;
        auto medir = [&] (const juce::String& texto, bool comPonto) {
            const float w = (float) larguraDoTexto (fonteTexto (12.0f), texto) + (comPonto ? 34.0f : 22.0f);
            if (x + w > (float) largura && x > 0.0f) { x = 0.0f; ++linhas; }
            x += w + 6.0f;
        };
        for (int b = 1; b <= 8; ++b)
            if (tipoDa (b) > 0) medir (textoDaBanda (b), true);
        medir ("+ banda", false);
        return linhas * 34 - 6;
    }

    void criarBanda (float f, float ganho)
    {
        for (int b = 1; b <= 8; ++b)
            if (tipoDa (b) <= 0)
            {
                processor.comecarAjuste();
                processor.ajustarParametro (modulo, banda (b, "type"), 1.0f);
                processor.ajustarParametro (modulo, banda (b, "freq_hz"), f);
                processor.ajustarParametro (modulo, banda (b, "gain_db"), ganho);
                processor.ajustarParametro (modulo, banda (b, "q"), 1.0f);
                selecionar (b);
                if (auto* pai = getParentComponent()) pai->resized();
                return;
            }
    }

    juce::Point<float> pontoDa (int b, juce::Rectangle<float> area) const
    {
        const int t = tipoDa (b);
        const float g = t >= 1 && t <= 3 ? valor (banda (b, "gain_db")) : 0.0f;
        return { xDaFrequencia (valor (banda (b, "freq_hz")), area), juce::jmap (g, 18.0f, -18.0f, area.getY(), area.getBottom()) };
    }

    int maisPerto (juce::Point<float> p, juce::Rectangle<float> area) const
    {
        int melhor = 0;
        float distancia = 14.0f;
        for (int b = 1; b <= 8; ++b)
            if (tipoDa (b) > 0 && pontoDa (b, area).getDistanceFrom (p) < distancia)
            {
                distancia = pontoDa (b, area).getDistanceFrom (p);
                melhor = b;
            }
        return melhor;
    }

    void selecionar (int b)
    {
        if (b != selecionada) { selecionada = b; remontarKnobs(); }
        repaint();
    }

    void remontarKnobs()
    {
        knobs.clear();
        if (selecionada > 0)
        {
            const int t = tipoDa (selecionada);
            tipos.setEscolhido (t - 1);
            novoKnob (nomeBanda ("freq_hz"), tx ("Frequência"));
            if (t >= 1 && t <= 3) novoKnob (nomeBanda ("gain_db"), tx ("Ganho"));
            novoKnob (nomeBanda ("q"), "Q");
        }
        tipos.setVisible (selecionada > 0);
        remover.setVisible (selecionada > 0);
        resized();
        repaint();
    }

    Escolha tipos;
    juce::TextButton remover;
    Espectro espectro;
    std::vector<PilulaBanda> pilulas;
    juce::Rectangle<float> grafico;
    juce::Rectangle<int> rotuloBanda;
    int selecionada = 0, arrastada = 0;
    bool analisador = true;
};

// ------------------------------------------------------------------ compressor
static float curvaComJoelho (float x, float limite, float taxa, float joelho)
{
    const float acima = x - limite;
    if (2.0f * acima < -joelho) return x;
    if (2.0f * std::abs (acima) <= joelho)
    {
        const float t = acima + joelho * 0.5f;
        return x + (1.0f / taxa - 1.0f) * t * t / (2.0f * joelho);
    }
    return limite + acima / taxa;
}

static void desenharTransferencia (juce::Graphics& g, juce::Rectangle<float> area, float limite, float taxa, float joelho, float entradaDb)
{
    g.setColour (cor::app);
    g.fillRoundedRectangle (area, 8.0f);
    const auto a = area.reduced (14.0f, 12.0f);
    auto X = [&] (float db) { return juce::jmap (db, -60.0f, 0.0f, a.getX(), a.getRight()); };
    auto Y = [&] (float db) { return juce::jmap (db, -60.0f, 0.0f, a.getBottom(), a.getY()); };
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (float d = -50.0f; d < 0.0f; d += 10.0f)
    {
        g.drawVerticalLine ((int) X (d), a.getY(), a.getBottom());
        g.drawHorizontalLine ((int) Y (d), a.getX(), a.getRight());
    }
    g.setColour (cor::textoFraco);
    const float tracos[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ X (-60.0f), Y (-60.0f), X (0.0f), Y (0.0f) }, tracos, 2, 1.0f);
    g.setColour (cor::aviso.withAlpha (0.8f));
    g.drawDashedLine ({ X (limite), a.getY(), X (limite), a.getBottom() }, tracos, 2, 1.0f);
    g.setFont (fonteNumero (11.0f));
    g.drawText ("limite", juce::Rectangle<float> (X (limite) + 3.0f, a.getY(), 60.0f, 14.0f), juce::Justification::centredLeft);

    juce::Path p;
    for (int i = 0; i <= 120; ++i)
    {
        const float d = -60.0f + 0.5f * (float) i;
        const float y = Y (juce::jmax (-60.0f, curvaComJoelho (d, limite, taxa, joelho)));
        if (i == 0) p.startNewSubPath (X (d), y); else p.lineTo (X (d), y);
    }
    g.setColour (cor::ciano);
    g.strokePath (p, juce::PathStrokeType (2.2f));
    if (entradaDb > -60.0f)
    {
        g.setColour (cor::texto);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ X (entradaDb), Y (curvaComJoelho (entradaDb, limite, taxa, joelho)) }));
    }
    g.setColour (cor::textoFraco);
    g.drawText ("entrada", a.withTop (a.getBottom() - 14.0f), juce::Justification::centredRight);
}

class CompressorFace : public Face
{
public:
    CompressorFace (MagicMixProcessor& p, int m) : Face (p, m),
        caracteres ({ "Limpo", tx ("Óptico"), "FET", "VCA", "Vari-mu" }, [this] (int c) { ajustar ("model", (float) c); repaint(); }),
        quem ({ tx ("Própria pista"), "Sidechain" }, [this] (int c) { ajustar ("sidechain", (float) c); })
    {
        addAndMakeVisible (caracteres);
        addAndMakeVisible (quem);
        for (auto* n : { "threshold_db", "ratio", "attack_ms", "release_ms", "makeup_db", "mix" })
            principais.push_back (novoKnob (n));
        filtro = novoKnob ("sc_hpf_hz");
        atualizar();
    }

    juce::String explicacao() const override
    {
        // tabela UTF-8: passa por tx() no uso
        static const char* const textos[] = {
            "Limpo: controla a dinâmica sem colorir.",
            "Óptico: suave e lento; quanto mais comprime, mais devagar solta. Assenta sem aparecer.",
            "FET: rápido e agressivo, com harmônicos ímpares. Dá pegada e deixa na cara.",
            "VCA: preciso, segura os picos com punch.",
            "Vari-mu: macio e encorpado; a taxa cresce com o volume e aquece com harmônicos pares." };
        return tx (textos[juce::jlimit (0, 4, (int) valor ("model"))]);
    }

    int alturaIdeal (int largura) const override { return 34 + 170 + 8 + alturaDosKnobs (largura, 7); }

    void atualizar() override
    {
        caracteres.setEscolhido ((int) valor ("model"));
        quem.setEscolhido ((int) valor ("sidechain"));
        Face::atualizar();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto linha = area.removeFromTop (30);
        caracteres.setBounds (linha.removeFromLeft (caracteres.larguraIdeal()));
        quem.setBounds (linha.removeFromRight (quem.larguraIdeal()));
        area.removeFromTop (4);
        medidores = area.removeFromTop (170).toFloat();
        area.removeFromTop (8);
        auto todos = principais;
        todos.push_back (filtro);
        disporKnobs (area, todos);
    }

    void paint (juce::Graphics& g) override
    {
        const auto* cadeia = processor.cadeiaTocando();
        const auto* mod = cadeia != nullptr ? cadeia->moduleAt (modulo) : nullptr;
        const float gr = mod != nullptr ? mod->gainReductionDb() : 0.0f;
        const float pin = cadeia != nullptr ? cadeia->slotInputPeak (modulo) : 0.0f;
        const float pout = cadeia != nullptr ? cadeia->slotOutputPeak (modulo) : 0.0f;
        static const float joelhos[] = { 6.0f, 12.0f, 2.0f, 4.0f, 14.0f };
        // tabela UTF-8: passa por tx() no uso
        static const char* const nomes[] = { "LIMPO", "ÓPTICO", "FET", "VCA", "VARI-MU" };
        const int c = juce::jlimit (0, 4, (int) valor ("model"));

        auto esquerda = medidores.withWidth (medidores.getWidth() * 0.5f - 6.0f);
        const auto direita = medidores.withLeft (esquerda.getRight() + 12.0f);
        g.setColour (cor::app);
        g.fillRoundedRectangle (esquerda, 8.0f);
        desenharBarra (g, esquerda.removeFromLeft (30.0f).reduced (8.0f, 10.0f), pin, "ENT");
        desenharBarra (g, esquerda.removeFromRight (30.0f).reduced (8.0f, 10.0f), pout, tx ("SAÍ"));
        desenharVu (g, esquerda, gr, tx (nomes[c]));
        desenharTransferencia (g, direita, valor ("threshold_db"), valor ("ratio"), joelhos[c], juce::Decibels::gainToDecibels (pin, -100.0f));
    }

private:
    Escolha caracteres, quem;
    std::vector<KnobParametro*> principais;
    KnobParametro* filtro = nullptr;
    juce::Rectangle<float> medidores;
};

// ------------------------------------------------------------------ Bus Comp
class BusCompFace : public Face
{
public:
    BusCompFace (MagicMixProcessor& p, int m) : Face (p, m),
        taxa (rotulosDe ("ratio"), [this] (int i) { escolherPasso ("ratio", i); }),
        ataque (rotulosDe ("attack_ms"), [this] (int i) { escolherPasso ("attack_ms", i); }),
        liberacao (rotulosDe ("release_s"), [this] (int i) { escolherPasso ("release_s", i); }),
        quem ({ tx ("Própria pista"), "Sidechain" }, [this] (int c) { ajustar ("sidechain", (float) c); })
    {
        for (auto* e : { &taxa, &ataque, &liberacao, &quem })
            addAndMakeVisible (e);
        for (auto* n : { "threshold_db", "makeup_db", "mix", "sc_hpf_hz" })
            lista.push_back (novoKnob (n));
        atualizar();
    }

    juce::String explicacao() const override
    {
        return tx ("Cola o grupo e dá punch. Taxa, ataque e liberação andam em passos fixos, como nos compressores de barramento "
                   "clássicos; com o Mix abaixo de 100 % vira compressão paralela.");
    }

    int alturaIdeal (int largura) const override { return 150 + 8 + alturaDosKnobs (largura, 4) + 36; }

    void atualizar() override
    {
        taxa.setEscolhido (indiceDoPasso ("ratio"));
        ataque.setEscolhido (indiceDoPasso ("attack_ms"));
        liberacao.setEscolhido (indiceDoPasso ("release_s"));
        quem.setEscolhido ((int) valor ("sidechain"));
        Face::atualizar();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topo = area.removeFromTop (150);
        vu = topo.removeFromLeft (juce::jmin (300, topo.getWidth() / 2)).toFloat();
        topo.removeFromLeft (16);
        topo.removeFromTop (12);
        area.removeFromTop (8);
        rotulos.clear();
        for (auto* e : { &taxa, &ataque, &liberacao })
        {
            auto linha = topo.removeFromTop (42);
            rotulos.push_back (linha.removeFromLeft (80));
            e->setBounds (linha.removeFromLeft (juce::jmin (linha.getWidth(), e->larguraIdeal())).reduced (0, 5));
        }
        disporKnobs (area.removeFromTop (alturaDosKnobs (area.getWidth(), 4)), lista);
        quem.setBounds (area.removeFromTop (36).removeFromLeft (quem.larguraIdeal()).reduced (0, 4));
    }

    void paint (juce::Graphics& g) override
    {
        const auto* cadeia = processor.cadeiaTocando();
        const auto* mod = cadeia != nullptr ? cadeia->moduleAt (modulo) : nullptr;
        desenharVu (g, vu, mod != nullptr ? mod->gainReductionDb() : 0.0f, "BUS COMP");
        g.setColour (cor::textoSuave);
        g.setFont (fonteTexto (13.5f));
        // tabela UTF-8: passa por tx() no uso
        const char* const nomes[] = { "Taxa", "Ataque (ms)", "Liberação" };
        for (size_t i = 0; i < rotulos.size(); ++i)
            g.drawText (tx (nomes[i]), rotulos[i], juce::Justification::centredLeft);
    }

private:
    juce::StringArray rotulosDe (const char* param) const
    {
        juce::StringArray r;
        if (const auto* d = def (param))
            for (float s : d->steps)
                r.add (std::string (param) == "attack_ms" ? juce::String (s, s < 1.0f ? 1 : 0).replaceCharacter ('.', ',') : textoDoValor (*d, s));
        return r;
    }
    int indiceDoPasso (const char* param) const
    {
        const auto* d = def (param);
        if (d == nullptr || d->steps.empty()) return -1;
        const float v = valor (param);
        int melhor = 0;
        for (size_t i = 0; i < d->steps.size(); ++i)
            if (std::abs (d->steps[i] - v) < std::abs (d->steps[(size_t) melhor] - v)) melhor = (int) i;
        return melhor;
    }
    void escolherPasso (const char* param, int i)
    {
        if (const auto* d = def (param))
            if (i >= 0 && i < (int) d->steps.size())
                ajustar (param, d->steps[(size_t) i]);
    }

    Escolha taxa, ataque, liberacao, quem;
    std::vector<KnobParametro*> lista;
    std::vector<juce::Rectangle<int>> rotulos;
    juce::Rectangle<float> vu;
};

// ------------------------------------------------------------------ de-esser
class DeEsserFace : public Face
{
public:
    DeEsserFace (MagicMixProcessor& p, int m) : Face (p, m),
        modo ({ "Faixa", "Larga" }, [this] (int i) { ajustar ("mode", (float) i); }),
        ouvir ({ tx ("Som inteiro"), tx ("Ouvir só os S") }, [this] (int i) { ajustar ("listen", (float) i); })
    {
        addAndMakeVisible (modo);
        addAndMakeVisible (ouvir);
        for (auto* n : { "freq_hz", "threshold_db", "max_reduction_db" })
            lista.push_back (novoKnob (n));
        atualizar();
    }

    juce::String explicacao() const override
    {
        return tx ("Abaixa os S e T só quando passam do limite. Faixa tira só os agudos; Larga abaixa o som inteiro no momento do S.");
    }

    int alturaIdeal (int largura) const override { return 150 + 44 + alturaDosKnobs (largura, 3); }

    void atualizar() override
    {
        modo.setEscolhido ((int) valor ("mode"));
        ouvir.setEscolhido ((int) valor ("listen"));
        Face::atualizar();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        vu = area.removeFromTop (150).toFloat().withSizeKeepingCentre (juce::jmin (320.0f, (float) area.getWidth()), 150.0f);
        auto linha = area.removeFromTop (44).reduced (0, 7);
        modo.setBounds (linha.removeFromLeft (modo.larguraIdeal()));
        linha.removeFromLeft (16);
        ouvir.setBounds (linha.removeFromLeft (ouvir.larguraIdeal()));
        disporKnobs (area, lista);
    }

    void paint (juce::Graphics& g) override
    {
        const auto* cadeia = processor.cadeiaTocando();
        const auto* mod = cadeia != nullptr ? cadeia->moduleAt (modulo) : nullptr;
        desenharVu (g, vu, mod != nullptr ? mod->gainReductionDb() : 0.0f, "DE-ESSER");
    }

private:
    Escolha modo, ouvir;
    std::vector<KnobParametro*> lista;
    juce::Rectangle<float> vu;
};

// ------------------------------------------------------------------ supressor de ressonâncias
class RessonanciaFace : public Face, private juce::Timer
{
public:
    RessonanciaFace (MagicMixProcessor& p, int m) : Face (p, m),
        modo ({ "Suave", "Duro" }, [this] (int i) { ajustar ("mode", (float) i); }),
        delta ({ tx ("Som tratado"), tx ("Ouvir o que sai") }, [this] (int i) { ajustar ("delta", (float) i); })
    {
        addAndMakeVisible (modo);
        addAndMakeVisible (delta);
        for (auto* n : { "depth_db", "sharpness", "selectivity", "attack_ms", "release_ms", "mix" })
            lista.push_back (novoKnob (n));
        curva.assign (1100, 0.0f);
        atualizar();
        startTimerHz (25);
    }

    juce::String explicacao() const override
    {
        return tx ("Só tira o excesso quando a ressonância aparece. Empurre a curva amarela para cima onde ele deve "
                   "prestar mais atenção, e para baixo onde deve deixar passar.");
    }

    int alturaIdeal (int largura) const override { return 40 + 230 + 6 + alturaDosKnobs (largura, 6); }

    void atualizar() override
    {
        modo.setEscolhido ((int) valor ("mode"));
        delta.setEscolhido ((int) valor ("delta"));
        Face::atualizar();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto linha = area.removeFromTop (40).reduced (0, 5);
        modo.setBounds (linha.removeFromLeft (modo.larguraIdeal()));
        linha.removeFromLeft (16);
        delta.setBounds (linha.removeFromLeft (delta.larguraIdeal()));
        grafico = area.removeFromTop (230).toFloat();
        area.removeFromTop (6);
        disporKnobs (area, lista);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cor::app);
        g.fillRoundedRectangle (grafico, 8.0f);
        const auto a = area();
        grade (g, a.withTrimmedBottom (-14.0f), -12.0f, 12.0f, 6.0f);

        // som que entra
        juce::Path som;
        som.startNewSubPath (a.getX(), a.getBottom());
        for (int i = 0; i < Espectro::kPontos; ++i)
            som.lineTo (xDaFrequencia (Espectro::frequencia (i), a),
                        juce::jmap (juce::jlimit (-90.0f, 0.0f, espectro.nivelDb (i)), -90.0f, 0.0f, a.getBottom(), a.getY() + a.getHeight() * 0.15f));
        som.lineTo (a.getRight(), a.getBottom());
        som.closeSubPath();
        g.setGradientFill (juce::ColourGradient (cor::cianoEscuro.withAlpha (0.85f), 0.0f, a.getY(), cor::cianoEscuro.withAlpha (0.2f), 0.0f, a.getBottom(), false));
        g.fillPath (som);

        // o que está sendo tirado, descendo do topo
        float binHz = 0.0f;
        const auto* cadeia = processor.cadeiaTocando();
        const auto* mod = cadeia != nullptr ? cadeia->moduleAt (modulo) : nullptr;
        const int pontos = mod != nullptr ? mod->reductionCurve (curva.data(), (int) curva.size(), binHz) : 0;
        if (pontos > 0 && binHz > 0.0f)
        {
            juce::Path r;
            r.startNewSubPath (a.getX(), a.getY());
            for (int b = 1; b < pontos; ++b)
            {
                const float f = (float) b * binHz;
                if (f < 20.0f || f > 20000.0f) continue;
                r.lineTo (xDaFrequencia (f, a), a.getY() + juce::jmin (12.0f, curva[(size_t) b]) / 12.0f * a.getHeight() * 0.45f);
            }
            r.lineTo (a.getRight(), a.getY());
            r.closeSubPath();
            g.setColour (cor::reducao.withAlpha (0.55f));
            g.fillPath (r);
        }
        g.setFont (fonteNumero (11.0f));
        g.setColour (cor::reducao);
        g.drawText (tx ("redução"), a.withHeight (14.0f).translated (4.0f, 2.0f), juce::Justification::centredLeft);
        g.setColour (cor::aviso);
        g.drawText ("SENSIBILIDADE", a.withHeight (14.0f).translated (-4.0f, 2.0f), juce::Justification::centredRight);

        // curva de sensibilidade
        juce::Path s;
        const float tracos[] = { 5.0f, 4.0f };
        for (int px = 0; px <= (int) a.getWidth(); px += 3)
        {
            const float y = yDoGanho (sensibilidadeEm (frequenciaDoX (a.getX() + (float) px, a)), a);
            if (px == 0) s.startNewSubPath (a.getX(), y); else s.lineTo (a.getX() + (float) px, y);
        }
        juce::Path tracejado;
        juce::PathStrokeType (2.0f).createDashedStroke (tracejado, s, tracos, 2);
        g.setColour (cor::aviso);
        g.fillPath (tracejado);
        for (int k = 1; k <= 6; ++k)
        {
            const auto p = pontoDo (k, a);
            g.setColour (cor::aviso);
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p));
            g.setColour (cor::app);
            g.drawEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p), 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        arrastado = 0;
        float d = 14.0f;
        for (int k = 1; k <= 6; ++k)
            if (pontoDo (k, area()).getDistanceFrom (e.position) < d)
            {
                d = pontoDo (k, area()).getDistanceFrom (e.position);
                arrastado = k;
            }
        if (arrastado > 0)
            processor.comecarAjuste();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (arrastado <= 0) return;
        const auto a = area();
        processor.ajustarParametro (modulo, "s" + std::to_string (arrastado) + "_freq_hz", frequenciaDoX (e.position.x, a));
        processor.ajustarParametro (modulo, "s" + std::to_string (arrastado) + "_gain_db", juce::jmap (e.position.y, a.getY(), a.getBottom(), 12.0f, -12.0f));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { arrastado = 0; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Duplo clique num ponto devolve ele ao neutro.
        for (int k = 1; k <= 6; ++k)
            if (pontoDo (k, area()).getDistanceFrom (e.position) < 12.0f)
                ajustar ("s" + std::to_string (k) + "_gain_db", 0.0f);
    }

private:
    void timerCallback() override { espectro.atualizar (processor, false); repaint (grafico.toNearestInt()); }
    juce::Rectangle<float> area() const { return grafico.reduced (10.0f, 8.0f).withTrimmedBottom (14.0f); }
    float yDoGanho (float g, juce::Rectangle<float> a) const { return juce::jmap (g, 12.0f, -12.0f, a.getY(), a.getBottom()); }
    juce::Point<float> pontoDo (int k, juce::Rectangle<float> a) const
    {
        return { xDaFrequencia (valor ("s" + std::to_string (k) + "_freq_hz"), a), yDoGanho (valor ("s" + std::to_string (k) + "_gain_db"), a) };
    }
    float sensibilidadeEm (float f) const
    {
        // Linear em oitavas entre os pontos, plano depois das pontas (como o motor faz).
        std::vector<std::pair<float, float>> pts;
        for (int k = 1; k <= 6; ++k)
            pts.push_back ({ valor ("s" + std::to_string (k) + "_freq_hz"), valor ("s" + std::to_string (k) + "_gain_db") });
        std::sort (pts.begin(), pts.end());
        if (f <= pts.front().first) return pts.front().second;
        if (f >= pts.back().first) return pts.back().second;
        for (size_t i = 1; i < pts.size(); ++i)
            if (f <= pts[i].first)
            {
                const float t = std::log (f / pts[i - 1].first) / std::log (pts[i].first / pts[i - 1].first);
                return pts[i - 1].second + t * (pts[i].second - pts[i - 1].second);
            }
        return 0.0f;
    }

    Escolha modo, delta;
    std::vector<KnobParametro*> lista;
    Espectro espectro;
    std::vector<float> curva;
    juce::Rectangle<float> grafico;
    int arrastado = 0;
};

// ------------------------------------------------------------------ demais efeitos
class FaceGeral : public Face
{
public:
    FaceGeral (MagicMixProcessor& p, int m) : Face (p, m)
    {
        if (modelo == nullptr) return;
        for (auto& d : modelo->params)
        {
            if (d.isChoice() || ! d.steps.empty())
            {
                const std::string nome = d.name;
                juce::StringArray opcoes;
                if (d.isChoice())
                    for (auto& l : d.labelList) opcoes.add (tx (l));
                else
                    for (float s : d.steps) opcoes.add (textoDoValor (d, s));
                auto* e = escolhas.add (new Escolha (opcoes, [this, nome, &d] (int i) {
                    ajustar (nome, d.isChoice() ? (float) i : d.steps[(size_t) i]);
                }));
                nomesEscolha.push_back (nome);
                addAndMakeVisible (e);
            }
            else if (std::string (d.name) == "ping_pong")
            {
                auto* e = escolhas.add (new Escolha ({ tx ("Não"), "Sim" }, [this] (int i) { ajustar ("ping_pong", (float) i); }));
                nomesEscolha.push_back ("ping_pong");
                addAndMakeVisible (e);
            }
            else
                lista.push_back (novoKnob (d.name));
        }
        atualizar();
    }

    int alturaIdeal (int largura) const override { return (int) escolhas.size() * 40 + alturaDosKnobs (largura, (int) lista.size()); }

    void atualizar() override
    {
        for (int i = 0; i < escolhas.size(); ++i)
        {
            const auto* d = def (nomesEscolha[(size_t) i]);
            const float v = valor (nomesEscolha[(size_t) i]);
            int indice = (int) std::lround (v);
            if (d != nullptr && ! d->steps.empty())
                for (size_t s = 0; s < d->steps.size(); ++s)
                    if (std::abs (d->steps[s] - v) < 1.0e-3f) indice = (int) s;
            escolhas[i]->setEscolhido (indice);
        }
        Face::atualizar();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        rotulos.clear();
        for (auto* e : escolhas)
        {
            auto linha = area.removeFromTop (40);
            rotulos.push_back (linha.removeFromLeft (130));
            e->setBounds (linha.removeFromLeft (e->larguraIdeal()).reduced (0, 5));
        }
        disporKnobs (area, lista);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cor::textoSuave);
        g.setFont (fonteTexto (13.5f));
        for (size_t i = 0; i < rotulos.size(); ++i)
            g.drawText (rotuloDoParametro (nomesEscolha[i]), rotulos[i], juce::Justification::centredLeft);
    }

private:
    juce::OwnedArray<Escolha> escolhas;
    std::vector<std::string> nomesEscolha;
    std::vector<KnobParametro*> lista;
    std::vector<juce::Rectangle<int>> rotulos;
};

std::unique_ptr<Face> criarFace (MagicMixProcessor& p, int modulo)
{
    const auto& spec = p.spec();
    if (modulo < 0 || modulo >= (int) spec.modules.size())
        return nullptr;
    const auto& tipo = spec.modules[(size_t) modulo].type;
    if (tipo == "eq") return std::make_unique<EqFace> (p, modulo);
    if (tipo == "compressor") return std::make_unique<CompressorFace> (p, modulo);
    if (tipo == "bus_compressor") return std::make_unique<BusCompFace> (p, modulo);
    if (tipo == "de_esser") return std::make_unique<DeEsserFace> (p, modulo);
    if (tipo == "resonance") return std::make_unique<RessonanciaFace> (p, modulo);
    return std::make_unique<FaceGeral> (p, modulo);
}
}
