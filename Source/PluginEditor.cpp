#include "PluginEditor.h"

#include "Nomes.h"
#include "Texto.h"

namespace mm
{
namespace
{
const juce::Colour corColuna { 0xff222427 }, corPistas { 0xff26282b }, corRodape { 0xff24262a }, corAtiva { 0xff28393e };

void caixaArredondada (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fundo, juce::Colour borda, float raio)
{
    g.setColour (fundo);
    g.fillRoundedRectangle (r, raio);
    g.setColour (borda);
    g.drawRoundedRectangle (r.reduced (0.5f), raio, 1.0f);
}

juce::String porcento (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String decibeis (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1).replaceCharacter ('.', ',') + " dB"; }
}

// ------------------------------------------------------------------ pistas
ListaDePistas::ListaDePistas (Sessao& s) : sessao (s)
{
    sessao.addChangeListener (this);
    startTimerHz (12);
}

ListaDePistas::~ListaDePistas() { sessao.removeChangeListener (this); }

void ListaDePistas::montar()
{
    linhas.clear();
    const auto todas = sessao.instancias();
    float y = 14.0f;
    for (int grupo = 0; grupo < 6; ++grupo)
    {
        std::vector<MagicMixProcessor*> doGrupo;
        for (auto* p : todas)
            if (grupoDaPista (p->nomeDaPista()) == grupo)
                doGrupo.push_back (p);
        if (doGrupo.empty()) continue;
        linhas.push_back ({ tx (kGrupos[grupo]), nullptr, { 10.0f, y, (float) getWidth() - 20.0f, 22.0f } });
        y += 26.0f;
        for (auto* p : doGrupo)
        {
            linhas.push_back ({ {}, p, { 10.0f, y, (float) getWidth() - 20.0f, 32.0f } });
            y += 36.0f;
        }
        y += 10.0f;
    }
    repaint();
}

void ListaDePistas::paint (juce::Graphics& g)
{
    g.fillAll (corPistas);
    for (auto& l : linhas)
    {
        if (l.pista == nullptr)
        {
            g.setColour (cor::textoFraco);
            g.setFont (fonteTexto (11.0f, true).withExtraKerningFactor (0.12f));
            g.drawText (maiusculas (l.grupo), l.caixa.withTrimmedLeft (8.0f), juce::Justification::bottomLeft);
            continue;
        }
        const bool estaAtiva = l.pista == ativa;
        if (estaAtiva)
        {
            g.setColour (corAtiva);
            g.fillRoundedRectangle (l.caixa, 7.0f);
            g.setColour (cor::ciano);
            g.fillRoundedRectangle (l.caixa.withWidth (2.5f), 1.0f);
        }
        else if (l.pista == sobre)
        {
            g.setColour (cor::painel);
            g.fillRoundedRectangle (l.caixa, 7.0f);
        }
        auto r = l.caixa.reduced (8.0f, 0.0f);
        auto direita = r.removeFromRight (22.0f);
        if (! l.pista->spec().modules.empty())
        {
            g.setColour (cor::textoFraco);
            g.setFont (fonteNumero (11.0f));
            g.drawText ("1", direita, juce::Justification::centredRight);
        }
        else
        {
            // três barrinhas com o nível da pista
            const float nivel = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (l.pista->nivelDaEntrada(), -60.0f) + 60.0f) / 60.0f);
            for (int b = 0; b < 3; ++b)
            {
                const float base[] = { 6.0f, 10.0f, 8.0f };
                const float h = juce::jmax (base[b] * 0.6f, base[b] + 4.0f * (nivel - 0.5f));
                g.setColour (estaAtiva ? cor::ciano : cor::textoSuave);
                g.fillRect (juce::Rectangle<float> (direita.getRight() - 14.0f + (float) b * 5.0f, direita.getCentreY() + 6.0f - h, 3.0f, h));
            }
        }
        g.setColour (estaAtiva || l.pista == sobre ? cor::texto : cor::textoSuave);
        g.setFont (fonteTexto (14.0f));
        g.drawText (l.pista->nomeDaPista(), r, juce::Justification::centredLeft, true);
    }
}

void ListaDePistas::mouseMove (const juce::MouseEvent& e)
{
    MagicMixProcessor* agora = nullptr;
    for (auto& l : linhas)
        if (l.pista != nullptr && l.caixa.contains (e.position)) agora = l.pista;
    if (agora != sobre) { sobre = agora; repaint(); }
}

void ListaDePistas::mouseUp (const juce::MouseEvent& e)
{
    for (auto& l : linhas)
        if (l.pista != nullptr && l.caixa.contains (e.position) && aoEscolher)
        {
            aoEscolher (l.pista);
            return;
        }
}

// ------------------------------------------------------------------ conversa
void Conversa::atualizar (int largura, bool forcar)
{
    const auto& falas = processor.conversa();
    if (! forcar && falas.size() == contadas && largura == larguraFeita)
        return;
    contadas = falas.size();
    larguraFeita = largura;
    bolhas.clear();

    float y = 16.0f;
    const float maxBolha = (float) largura * 0.86f;
    for (size_t i = 0; i < falas.size(); ++i)
    {
        const auto& f = falas[i];
        juce::AttributedString s;
        s.append (f.texto, fonteTexto (14.0f), cor::texto);
        s.setLineSpacing (5.0f);
        Bolha b;
        b.indice = i;
        b.doMusico = f.doMusico;
        b.layout.createLayout (s, f.doMusico ? maxBolha - 24.0f : (float) largura - 36.0f);
        if (f.doMusico)
        {
            const float w = juce::jmin (maxBolha, b.layout.getWidth() + 24.0f), h = b.layout.getHeight() + 20.0f;
            b.caixa = { (float) largura - 16.0f - w, y, w, h };
            y += h + 18.0f;
        }
        else
        {
            b.caixa = { 16.0f, y, (float) largura - 36.0f, b.layout.getHeight() };
            y += b.caixa.getHeight() + 8.0f;
            b.temDetalhes = f.detalhes.isNotEmpty();
            if (b.temDetalhes)
            {
                b.botaoDetalhes = { 16.0f, y, 90.0f, 18.0f };
                y += 22.0f;
                b.aberto = abertos.count (i) != 0;
                if (b.aberto)
                {
                    juce::AttributedString d;
                    d.append (f.detalhes, fonteTexto (13.0f), cor::textoSuave);
                    d.setLineSpacing (4.0f);
                    b.detalhes.createLayout (d, (float) largura - 50.0f);
                    b.caixaDetalhes = { 30.0f, y, (float) largura - 50.0f, b.detalhes.getHeight() };
                    y += b.detalhes.getHeight() + 6.0f;
                }
            }
            y += 14.0f;
        }
        bolhas.push_back (std::move (b));
    }
    setSize (largura, juce::jmax ((int) y + 6, 120));
    repaint();
}

void Conversa::paint (juce::Graphics& g)
{
    if (bolhas.empty())
    {
        juce::AttributedString s;
        s.append (tx ("Peça do seu jeito o que quer para esta pista: \"deixa a voz na cara\", \"menos embolado\", "
                      "\"mais punch na bateria\".\n\n"), fonteTexto (14.0f), cor::textoFraco);
        s.append (tx ("Eu entendo brilho, presença, corpo, punch, compressão, aspereza, ambiente e intensidade, com "
                      "\"mais\", \"menos\" ou \"tira\". Os botões que o pedido mexer aparecem marcados no Fluxo; "
                      "se ainda não chegar lá, o modo Avançado tem todos os efeitos."),
                  fonteTexto (14.0f), cor::textoFraco);
        s.setLineSpacing (5.0f);
        juce::TextLayout l;
        l.createLayout (s, (float) getWidth() - 36.0f);
        l.draw (g, getLocalBounds().toFloat().reduced (18.0f, 18.0f));
        return;
    }
    for (auto& b : bolhas)
    {
        if (b.doMusico)
        {
            g.setColour (cor::painelClaro);
            g.fillRoundedRectangle (b.caixa, 12.0f);
            b.layout.draw (g, b.caixa.reduced (12.0f, 10.0f));
            continue;
        }
        b.layout.draw (g, b.caixa);
        if (b.temDetalhes)
        {
            g.setColour (cor::textoFraco);
            g.setFont (fonteTexto (12.5f));
            g.drawText (juce::String::fromUTF8 (b.aberto ? "\xe2\x96\xbc  Detalhes" : "\xe2\x96\xb6  Detalhes"), b.botaoDetalhes, juce::Justification::centredLeft);
            if (b.aberto)
                b.detalhes.draw (g, b.caixaDetalhes);
        }
    }
}

void Conversa::mouseUp (const juce::MouseEvent& e)
{
    for (auto& b : bolhas)
        if (b.temDetalhes && b.botaoDetalhes.contains (e.position))
        {
            if (abertos.count (b.indice) != 0) abertos.erase (b.indice); else abertos.insert (b.indice);
            atualizar (larguraFeita, true);
            return;
        }
}

void Conversa::mouseMove (const juce::MouseEvent& e)
{
    bool emCima = false;
    for (auto& b : bolhas)
        emCima = emCima || (b.temDetalhes && b.botaoDetalhes.contains (e.position));
    setMouseCursor (emCima ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

bool CaixaDePedido::keyPressed (const juce::KeyPress& key)
{
    juce::TextEditor::keyPressed (key);
    return true;  // com o foco aqui, nenhuma tecla vai para a DAW (barra de espaço inclusive)
}

void CaixaDePedido::mouseDown (const juce::MouseEvent& e)
{
    if (aoClicar)
    {
        aoClicar();
        return;
    }
    grabKeyboardFocus();
    juce::TextEditor::mouseDown (e);
}

JanelaDePedido::JanelaDePedido()
{
    setOpaque (true);
    caixa.setMultiLine (true, true);
    caixa.setReturnKeyStartsNewLine (false);
    caixa.setFont (fonteTexto (14.0f));
    caixa.setTextToShowWhenEmpty (tx ("Ex.: deixa a voz mais na cara e com um eco discreto"), cor::textoFraco);
    caixa.setIndents (0, 4);
    caixa.setColour (juce::TextEditor::backgroundColourId, cor::painel);
    caixa.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    caixa.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    caixa.setColour (juce::TextEditor::textColourId, cor::texto);
    caixa.setColour (juce::TextEditor::highlightColourId, cor::cianoEscuro);
    caixa.setColour (juce::CaretComponent::caretColourId, cor::ciano);
    caixa.onReturnKey = [this] {
        const auto t = caixa.getText().trim();
        if (t.isNotEmpty() && aoEnviar) { caixa.clear(); aoEnviar (t); }
    };
    caixa.onEscapeKey = [this] { fechar(); };
    // Clicou fora (no plugin ou na DAW): devolve o texto para a caixa normal e some.
    juce::Component::SafePointer<JanelaDePedido> seguro (this);
    caixa.onFocusLost = [seguro] {
        juce::MessageManager::callAsync ([seguro] {
            if (seguro != nullptr && ! seguro->caixa.hasKeyboardFocus (true)) seguro->fechar();
        });
    };
    addAndMakeVisible (caixa);
}

JanelaDePedido::~JanelaDePedido() { if (isOnDesktop()) removeFromDesktop(); }

void JanelaDePedido::abrir (juce::Rectangle<int> naTela, const juce::String& texto)
{
    caixa.setText (texto, false);
    setBounds (naTela);
    if (! isOnDesktop())
        addToDesktop (0);  // sem barra de título: parece a própria caixa
    setAlwaysOnTop (true);
    setVisible (true);
    toFront (true);
    caixa.grabKeyboardFocus();
    caixa.moveCaretToEnd();
}

void JanelaDePedido::fechar()
{
    if (! isOnDesktop()) return;
    const auto t = caixa.getText();
    setVisible (false);
    removeFromDesktop();
    if (aoFechar) aoFechar (t);
}

void JanelaDePedido::acompanhar (juce::Rectangle<int> naTela)
{
    if (aberta() && getBounds() != naTela)
        setBounds (naTela);
}

void JanelaDePedido::paint (juce::Graphics& g) { g.fillAll (cor::painel); }
void JanelaDePedido::resized() { caixa.setBounds (getLocalBounds()); }

void Pilula::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (highlighted ? cor::ciano.withAlpha (0.10f) : juce::Colours::transparentBlack);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (cor::ciano.withAlpha (0.35f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (cor::ciano);
    g.setFont (fonteTexto (12.5f));
    g.drawText (getButtonText(), getLocalBounds().reduced (11, 0), juce::Justification::centredLeft, true);
}

int Pilula::larguraIdeal() const { return larguraDoTexto (fonteTexto (12.5f), getButtonText()) + 24; }

// ------------------------------------------------------------------ faixa da cadeia
std::vector<FaixaDaCadeia::Peca> FaixaDaCadeia::pecas (int largura) const
{
    std::vector<Peca> lista;
    const auto& spec = processor.spec();
    const auto fonte = fonteTexto (12.0f);
    float x = 0.0f, y = 0.0f;
    auto por = [&] (const juce::String& texto, int modulo, bool ligado) {
        const float w = (float) larguraDoTexto (fonte, texto) + 18.0f;
        if (x + w > (float) largura && x > 0.0f) { x = 0.0f; y += 32.0f; }
        lista.push_back ({ { x, y, w, 26.0f }, texto, modulo, ligado });
        x += w + 20.0f;  // espaço para a seta
    };
    por ("Entrada", -2, true);
    for (size_t i = 0; i < spec.modules.size(); ++i)
        por (nomeDoModulo (spec.modules[i]), (int) i, spec.modules[i].enabled);
    por (tx ("Saída"), -3, true);
    return lista;
}

int FaixaDaCadeia::alturaIdeal (int largura) const
{
    const auto lista = pecas (largura);
    return lista.empty() ? 26 : (int) lista.back().caixa.getBottom() + 1;
}

void FaixaDaCadeia::paint (juce::Graphics& g)
{
    const auto lista = pecas (getWidth());
    for (size_t i = 0; i < lista.size(); ++i)
    {
        const auto& p = lista[i];
        const bool ponta = p.modulo < 0, sel = p.modulo == selecionado;
        if (ponta)
        {
            juce::Path r, t;
            r.addRoundedRectangle (p.caixa.reduced (0.5f), 6.0f);
            const float tracos[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (t, r, tracos, 2);
            g.setColour (cor::linha);
            g.fillPath (t);
            g.setColour (cor::textoFraco);
        }
        else
        {
            caixaArredondada (g, p.caixa, sel ? cor::ciano.withAlpha (0.10f) : cor::painelClaro, sel ? cor::ciano : cor::linha, 6.0f);
            g.setColour (! p.ligado ? cor::textoFraco : sel ? cor::texto : cor::textoSuave);
        }
        g.setFont (fonteTexto (12.0f));
        g.drawText (p.texto, p.caixa, juce::Justification::centred);
        if (! ponta && ! p.ligado)  // desligado: riscado
            g.drawHorizontalLine ((int) p.caixa.getCentreY(), p.caixa.getX() + 7.0f, p.caixa.getRight() - 7.0f);
        if (i + 1 < lista.size() && std::abs (lista[i + 1].caixa.getY() - p.caixa.getY()) < 1.0f)
        {
            g.setColour (cor::textoFraco);
            g.setFont (fonteTexto (11.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), juce::Rectangle<float> (p.caixa.getRight(), p.caixa.getY(), 20.0f, 26.0f), juce::Justification::centred);
        }
    }
}

void FaixaDaCadeia::mouseUp (const juce::MouseEvent& e)
{
    for (auto& p : pecas (getWidth()))
        if (p.modulo >= 0 && p.caixa.contains (e.position))
        {
            selecionado = p.modulo;
            if (aoEscolher) aoEscolher (p.modulo);
            repaint();
        }
}

// ------------------------------------------------------------------ cartão do tratamento
static constexpr int kTopoCartao = 46, kTopoFace = 88;

CartaoTratamento::CartaoTratamento (MagicMixProcessor& p) : processor (p), faixa (p)
{
    addAndMakeVisible (faixa);
    addAndMakeVisible (ligado);
    addChildComponent (moduloLigado);
    ligado.setTooltip (tx ("Liga e desliga o tratamento inteiro"));
    ligado.onClick = [this] { processor.ligarTratamento (ligado.getToggleState()); repaint(); };
    moduloLigado.setTooltip (tx ("Liga e desliga só este efeito"));
    moduloLigado.onClick = [this] { if (modulo >= 0) processor.ligarModulo (modulo, moduloLigado.getToggleState()); };
    faixa.aoEscolher = [this] (int m) {
        avancado = true;
        abrirModulo (m);
        if (aoEntrarNoAvancado) aoEntrarNoAvancado();
    };
}

void CartaoTratamento::reconstruir()
{
    const auto& spec = processor.spec();
    macros.clear();
    for (size_t i = 0; i < spec.macros.size() && i < (size_t) kMaxMacros; ++i)
    {
        auto* k = macros.add (new KnobHost (*processor.macroParam ((int) i), tx (spec.macros[i].name), porcento));
        k->setTooltip (tx (spec.macros[i].description));
        k->setReservarDestaque (true);
        addChildComponent (k);
    }
    if (modulo >= (int) spec.modules.size() || (avancado && modulo < 0))
        modulo = spec.modules.empty() ? -1 : 0;
    abrirModulo (modulo);
}

void CartaoTratamento::setAvancado (bool sim)
{
    avancado = sim;
    if (avancado && modulo < 0 && ! processor.spec().modules.empty())
        modulo = 0;
    abrirModulo (modulo);
}

void CartaoTratamento::abrirModulo (int m)
{
    modulo = m;
    face.reset();
    const auto& spec = processor.spec();
    faixa.setSelecionado (avancado ? modulo : -1);
    if (avancado && modulo >= 0 && modulo < (int) spec.modules.size())
    {
        face = criarFace (processor, modulo);
        addAndMakeVisible (*face);
        moduloLigado.setToggleState (spec.modules[(size_t) modulo].enabled, juce::dontSendNotification);
    }
    moduloLigado.setVisible (face != nullptr);
    for (auto* k : macros) k->setVisible (! avancado);
    ligado.setToggleState (processor.tratamentoLigado(), juce::dontSendNotification);
    if (aoMudarAltura) aoMudarAltura();
    resized();
    repaint();
}

void CartaoTratamento::atualizar()
{
    if (face != nullptr) face->atualizar();
    const auto& pedidos = processor.botoesDoPedido();
    for (auto* k : macros)
        k->setDestaque (pedidos.contains (k->getRotulo()));
    const auto& spec = processor.spec();
    if (modulo >= 0 && modulo < (int) spec.modules.size())
        moduloLigado.setToggleState (spec.modules[(size_t) modulo].enabled, juce::dontSendNotification);
    ligado.setToggleState (processor.tratamentoLigado(), juce::dontSendNotification);
}

int CartaoTratamento::alturaIdeal (int largura)
{
    const int interno = largura - 28;
    int h = 14 + kTopoCartao - 14 + 14 + faixa.alturaIdeal (interno) + 16;
    if (avancado && face != nullptr)
        h += 12 + kTopoFace - 12 + face->alturaIdeal (interno - 28) + 12;
    else
    {
        const int porLinha = juce::jmax (1, interno / 112);
        h += juce::jmax (1, (macros.size() + porLinha - 1) / porLinha) * 144;
    }
    return h + 14;
}

void CartaoTratamento::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto topo = area.removeFromTop (kTopoCartao - 14);
    ligado.setBounds (topo.removeFromRight (44).withSizeKeepingCentre (44, 24));
    area.removeFromTop (14);
    faixa.setBounds (area.removeFromTop (faixa.alturaIdeal (area.getWidth())));
    area.removeFromTop (16);
    if (avancado && face != nullptr)
    {
        areaFace = area;
        auto interno = area.reduced (14, 12);
        auto cab = interno.removeFromTop (kTopoFace - 12);
        moduloLigado.setBounds (cab.removeFromTop (24).removeFromRight (44));
        face->setBounds (interno.withHeight (face->alturaIdeal (interno.getWidth())));
    }
    else
    {
        const int porLinha = juce::jmax (1, area.getWidth() / 112);
        for (int i = 0; i < macros.size(); ++i)
            macros[i]->setBounds (area.getX() + (i % porLinha) * 112, area.getY() + (i / porLinha) * 144, 104, 136);
    }
}

void CartaoTratamento::paint (juce::Graphics& g)
{
    caixaArredondada (g, getLocalBounds().toFloat(), cor::painel, cor::linha, 10.0f);
    const auto& spec = processor.spec();
    auto topo = getLocalBounds().reduced (14).removeFromTop (kTopoCartao - 14);
    g.setColour (cor::texto);
    g.setFont (fonteTexto (15.0f, true));
    g.drawText (spec.title.empty() ? tx ("Tratamento") : tx (spec.title), topo, juce::Justification::centredLeft);

    if (avancado && face != nullptr && modulo >= 0 && modulo < (int) spec.modules.size())
    {
        caixaArredondada (g, areaFace.toFloat(), juce::Colour (0xff2f3236), cor::linha, 9.0f);
        auto cab = areaFace.reduced (14, 12).removeFromTop (kTopoFace - 12);
        auto linha = cab.removeFromTop (24);
        g.setColour (cor::ciano);
        g.setFont (fonteMarca (12.0f).withExtraKerningFactor (0.1f));
        g.drawText (maiusculas (nomeDoModulo (spec.modules[(size_t) modulo])), linha, juce::Justification::centredLeft);
        juce::AttributedString s;
        s.append (face->explicacao(), fonteTexto (12.5f), cor::textoFraco);
        s.setLineSpacing (3.0f);
        juce::TextLayout l;
        l.createLayout (s, (float) cab.getWidth());
        l.draw (g, cab.toFloat().withTrimmedTop (4.0f));
    }
    else if (! avancado && macros.isEmpty())
    {
        g.setColour (cor::textoFraco);
        g.setFont (fonteTexto (13.0f));
        g.drawText (tx ("Esta cadeia não tem botões. Ajuste os efeitos no modo Avançado."),
                    getLocalBounds().reduced (14).withTop (faixa.getBottom() + 16).withHeight (24), juce::Justification::centredLeft);
    }
}

void CartaoTratamento::paintOverChildren (juce::Graphics& g)
{
    if (! processor.tratamentoLigado())  // desligado: tudo apagado, menos a chave
    {
        g.setColour (cor::painel.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop ((float) kTopoCartao), 10.0f);
    }
}

// ------------------------------------------------------------------ painel da pista
PainelDaPista::PainelDaPista (MagicMixProcessor& p)
    : processor (p), conversa (p), modo ({ "Fluxo", tx ("Avançado") }, [this] (int i) { mostrarAvancado (i == 1); }), cartao (p)
{
    rolagemConversa.setViewedComponent (&conversa, false);
    rolagemConversa.setScrollBarsShown (true, false);
    addAndMakeVisible (rolagemConversa);

    pedido.setMultiLine (true, true);
    pedido.setReturnKeyStartsNewLine (false);
    pedido.setFont (fonteTexto (14.0f));
    pedido.setTextToShowWhenEmpty (tx ("Ex.: deixa a voz mais na cara e com um eco discreto"), cor::textoFraco);
    pedido.setIndents (0, 4);
    pedido.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    pedido.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    pedido.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    pedido.onReturnKey = [this] { enviar(); };
    addAndMakeVisible (pedido);
    if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        // Numa DAW, o texto é digitado numa janela própria por cima da caixa (a DAW não rouba o espaço).
        pedido.aoClicar = [this] { janelaDePedido.abrir (pedido.getScreenBounds(), pedido.getText()); };
        janelaDePedido.aoEnviar = [this] (const juce::String& t) { pedido.clear(); processor.sendPrompt (t); };
        janelaDePedido.aoFechar = [this] (const juce::String& t) { pedido.setText (t, false); };
    }
    enviarBotao.setColour (juce::TextButton::buttonColourId, cor::cianoForte);
    enviarBotao.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff062a33));
    enviarBotao.onClick = [this] { enviar(); };
    addAndMakeVisible (enviarBotao);

    desfazer.onClick = [this] { processor.undo(); };
    addAndMakeVisible (desfazer);
    modo.setAgrupado (true);
    addAndMakeVisible (modo);
    modo.setEscolhido (0);

    conteudoTratamentos.addAndMakeVisible (cartao);
    adicionar.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    adicionar.getProperties().set ("tracejado", true);
    adicionar.setColour (juce::TextButton::textColourOffId, cor::textoFraco);
    adicionar.onClick = [this] {
        juce::PopupMenu menu;
        menu.addSectionHeader (tx ("Cadeias aprovadas nas escutas"));
        const auto& lista = processor.biblioteca();
        for (size_t i = 0; i < lista.size(); ++i)
            menu.addItem ((int) i + 1, lista[i].titulo + (lista[i].descricao.isNotEmpty() ? "  -  " + lista[i].descricao : juce::String()));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&adicionar),
                            [this] (int r) { if (r > 0) processor.carregarDaBiblioteca (r - 1); });
    };
    conteudoTratamentos.addAndMakeVisible (adicionar);
    rolagemTratamentos.setViewedComponent (&conteudoTratamentos, false);
    rolagemTratamentos.setScrollBarsShown (true, false);
    addAndMakeVisible (rolagemTratamentos);
    cartao.aoMudarAltura = [this] { layoutTratamentos(); };
    cartao.aoEntrarNoAvancado = [this] { modo.setEscolhido (1); layoutTratamentos(); };

    wet = std::make_unique<KnobHost> (*processor.mixParam(), "WET", porcento);
    output = std::make_unique<KnobHost> (*processor.gainParam(), "OUTPUT", decibeis);
    wet->setRotuloCiano (true);
    output->setRotuloCiano (true);
    addAndMakeVisible (*wet);
    addAndMakeVisible (*output);
    comparar.setButtonText (tx ("Comparar com o original"));
    comparar.setClickingTogglesState (true);
    comparar.onClick = [this] { processor.setComparando (comparar.getToggleState()); repaint (areaRodape); };
    addAndMakeVisible (comparar);

    processor.addChangeListener (this);
    changeListenerCallback (nullptr);
    startTimerHz (15);
}

PainelDaPista::~PainelDaPista()
{
    janelaDePedido.aoFechar = nullptr;
    janelaDePedido.fechar();
    processor.setComparando (false);
    processor.removeChangeListener (this);
}

void PainelDaPista::enviar()
{
    auto texto = pedido.getText().trim();
    if (janelaDePedido.aberta())
    {
        texto = janelaDePedido.texto().trim();
        janelaDePedido.limpar();
    }
    if (texto.isEmpty()) return;
    pedido.clear();
    processor.sendPrompt (texto);
}

void PainelDaPista::montarSugestoes()
{
    sugestoes.clear();
    acoesSugestoes.clear();
    const auto& spec = processor.spec();
    if (! spec.suggestions.empty())
    {
        for (auto& s : spec.suggestions)
        {
            const auto texto = tx (s);
            sugestoes.add (new Pilula (texto));
            acoesSugestoes.push_back ([this, texto] { processor.sendPrompt (texto); });
        }
    }
    else if (! spec.modules.empty())
    {
        // Com cadeia: pedidos prontos que o MagicMIX entende, com os botões que esta cadeia tem.
        // tabela UTF-8: passa por tx() no uso
        const std::pair<const char*, const char*> exemplos[] = { { "Suavizar", "Tira a aspereza" }, { "Brilho", "Mais brilho" },
                                                                 { "Punch", "Mais punch" }, { "Presença", "Mais na cara" }, { "Corpo", "Menos embolado" } };
        for (auto& [botao, pedidoPronto] : exemplos)
        {
            bool tem = false;
            for (auto& m : spec.macros) tem = tem || tx (m.name) == tx (botao);
            if (! tem || sugestoes.size() >= 3) continue;
            const auto texto = tx (pedidoPronto);
            sugestoes.add (new Pilula (texto));
            acoesSugestoes.push_back ([this, texto] { processor.sendPrompt (texto); });
        }
    }
    else
    {
        // Sem cadeia ainda: começar por uma cadeia aprovada que combina com o nome da pista.
        static const int porGrupo[6][3] = { { 5, 3, 4 }, { 0, 1, 2 }, { 6, 10, 7 }, { 10, 7, 8 }, { 7, 8, 9 }, { 10, 0, 5 } };
        const int grupo = grupoDaPista (processor.nomeDaPista());
        const auto& lista = processor.biblioteca();
        for (int k = 0; k < 3; ++k)
        {
            const int i = porGrupo[grupo][k];
            if (i >= (int) lista.size()) continue;
            sugestoes.add (new Pilula (tx ("Começar pela cadeia \"") + lista[(size_t) i].titulo + "\""));
            acoesSugestoes.push_back ([this, i] { processor.carregarDaBiblioteca (i); });
        }
    }
    for (int i = 0; i < sugestoes.size(); ++i)
    {
        sugestoes[i]->onClick = [this, i] { auto acao = acoesSugestoes[(size_t) i]; acao(); };
        addAndMakeVisible (sugestoes[i]);
    }
}

void PainelDaPista::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto& spec = processor.spec();
    juce::String a;
    for (auto& m : spec.modules) a << tx (m.id) << ":" << tx (m.type) << ";";
    for (auto& m : spec.macros) a << "|" << tx (m.name);
    a << "#" << (int) spec.suggestions.size() << "#" << (int) spec.modules.empty();
    if (a != assinatura)
    {
        assinatura = a;
        cartao.reconstruir();
        montarSugestoes();
        resized();
    }
    else
    {
        cartao.atualizar();
        cartao.repaint();
    }
    desfazer.setEnabled (processor.canUndo());
    conversa.atualizar (juce::jmax (200, rolagemConversa.getMaximumVisibleWidth()));
    rolagemConversa.setViewPosition (0, juce::jmax (0, conversa.getHeight() - rolagemConversa.getHeight()));
    layoutTratamentos();
    repaint();
}

void PainelDaPista::timerCallback()
{
    if (janelaDePedido.aberta())
    {
        if (pedido.isShowing()) janelaDePedido.acompanhar (pedido.getScreenBounds());
        else janelaDePedido.fechar();
    }
    cartao.atualizar();
    repaint (areaConversa.withHeight (52));  // selo "Ouvindo"
}

void PainelDaPista::mostrarAvancado (bool sim)
{
    modo.setEscolhido (sim ? 1 : 0);
    cartao.setAvancado (sim);
    layoutTratamentos();
}

void PainelDaPista::selecionarModulo (int m)
{
    if (modo.escolhido() != 1) mostrarAvancado (true);
    cartao.abrirModulo (m);
    layoutTratamentos();
}

void PainelDaPista::paint (juce::Graphics& g)
{
    g.fillAll (corColuna);
    g.setColour (cor::linha);
    g.drawVerticalLine (areaConversa.getRight(), 0.0f, (float) getHeight());

    // topo da conversa: nome da pista e se ela está tocando
    auto topo = areaConversa.withHeight (52);
    g.drawHorizontalLine (topo.getBottom(), (float) topo.getX(), (float) topo.getRight());
    auto t = topo.reduced (16, 0);
    g.setColour (cor::texto);
    g.setFont (fonteTexto (15.0f, true));
    const auto nome = processor.nomeDaPista();
    const int wNome = juce::jmin (t.getWidth() - 130, larguraDoTexto (fonteTexto (15.0f, true), nome) + 4);
    g.drawText (nome, t.removeFromLeft (wNome), juce::Justification::centredLeft, true);
    const bool ouvindo = processor.nivelDaEntrada() > 0.001f;
    const auto selo = ouvindo ? tx ("Ouvindo a pista") : tx ("Em silêncio");
    const int wSelo = larguraDoTexto (fonteTexto (11.5f), selo) + 18;
    auto r = juce::Rectangle<float> ((float) t.getX() + 10.0f, (float) t.getCentreY() - 11.0f, (float) wSelo, 22.0f);
    caixaArredondada (g, r, ouvindo ? cor::ciano.withAlpha (0.10f) : cor::painelClaro, ouvindo ? cor::ciano.withAlpha (0.25f) : cor::linha, 11.0f);
    g.setColour (ouvindo ? cor::ciano : cor::textoFraco);
    g.setFont (fonteTexto (11.5f));
    g.drawText (selo, r, juce::Justification::centred);

    // caixa de composição
    g.setColour (cor::linha);
    g.drawHorizontalLine (areaComposicao.getY(), (float) areaConversa.getX(), (float) areaConversa.getRight());
    auto caixa = areaComposicao.reduced (16, 12).removeFromTop (88).toFloat();
    caixaArredondada (g, caixa, cor::painel, cor::linha, 10.0f);
    auto rotulo = caixa.reduced (12.0f, 8.0f).removeFromTop (22.0f);
    g.setColour (cor::textoFraco);
    g.setFont (fonteTexto (12.5f));
    const auto antes = tx ("O que fazemos com");
    const int wAntes = larguraDoTexto (fonteTexto (12.5f), antes);
    g.drawText (antes, rotulo.removeFromLeft ((float) wAntes + 6.0f), juce::Justification::centredLeft);
    const auto chip = nome.toLowerCase();
    const int wChip = juce::jmin ((int) rotulo.getWidth() - 20, larguraDoTexto (fonteTexto (11.0f), chip) + 16);
    auto c = rotulo.removeFromLeft ((float) wChip).withSizeKeepingCentre ((float) wChip, 20.0f);
    caixaArredondada (g, c, cor::ciano.withAlpha (0.10f), cor::ciano.withAlpha (0.25f), 10.0f);
    g.setColour (cor::ciano);
    g.setFont (fonteTexto (11.0f));
    g.drawText (chip, c, juce::Justification::centred, true);
    g.setColour (cor::textoFraco);
    g.setFont (fonteTexto (12.5f));
    g.drawText (" ?", rotulo, juce::Justification::centredLeft);
    juce::AttributedString dica;
    dica.append (tx ("Escreva do seu jeito: pedido técnico ganha números, pedido musical ganha palavras."), fonteTexto (12.0f), cor::textoFraco);
    dica.setLineSpacing (3.0f);
    juce::TextLayout l;
    auto areaDica = areaComposicao.reduced (16, 12).withTrimmedTop (98).withTrimmedRight (100);
    l.createLayout (dica, (float) areaDica.getWidth());
    l.draw (g, areaDica.toFloat());

    // topo dos tratamentos
    auto topoT = areaTratamentos.withHeight (52);
    g.setColour (cor::linha);
    g.drawHorizontalLine (topoT.getBottom(), (float) topoT.getX(), (float) topoT.getRight());
    g.setColour (cor::texto);
    g.setFont (fonteTexto (15.0f, true));
    g.drawText (tx ("Tratamentos · ") + nome, topoT.reduced (16, 0).withRight (desfazer.getX() - 10), juce::Justification::centredLeft, true);

    // rodapé
    g.setColour (corRodape);
    g.fillRect (areaRodape);
    g.setColour (cor::linha);
    g.drawHorizontalLine (areaRodape.getY(), (float) areaRodape.getX(), (float) areaRodape.getRight());
    g.setColour (processor.comparando() ? cor::ciano : cor::textoFraco);
    g.setFont (fonteTexto (12.0f));
    g.drawText (processor.comparando() ? tx ("Tocando o original, no mesmo volume do tratado. Clique de novo para voltar.")
                                       : tx ("O original toca com o mesmo volume, para a comparação ser justa."),
                juce::Rectangle<int> (comparar.getX(), comparar.getBottom() + 8, areaRodape.getRight() - comparar.getX() - 12, 18),
                juce::Justification::centredLeft, true);
}

void PainelDaPista::resized()
{
    auto area = getLocalBounds();
    areaConversa = area.removeFromLeft ((int) (area.getWidth() * 0.4f));
    areaTratamentos = area;

    // conversa
    auto c = areaConversa.withTrimmedTop (53);
    areaComposicao = c.removeFromBottom (156);
    int ySug = c.getBottom() - 8;
    for (int i = sugestoes.size(); --i >= 0;)
    {
        const int w = juce::jmin (c.getWidth() - 32, sugestoes[i]->larguraIdeal());
        ySug -= 34;
        sugestoes[i]->setBounds (c.getX() + 16, ySug, w, 28);
    }
    rolagemConversa.setBounds (c.withBottom (ySug - 6));
    auto caixa = areaComposicao.reduced (16, 12).removeFromTop (88).reduced (12, 8);
    caixa.removeFromTop (24);
    pedido.setBounds (caixa);
    enviarBotao.setBounds (areaComposicao.reduced (16, 12).withTrimmedTop (98).removeFromRight (80).withHeight (36));
    conversa.atualizar (juce::jmax (200, rolagemConversa.getMaximumVisibleWidth()));

    // tratamentos
    auto topo = areaTratamentos.withHeight (52).reduced (16, 10);
    modo.setBounds (topo.removeFromRight (modo.larguraIdeal()));
    topo.removeFromRight (8);
    desfazer.setBounds (topo.removeFromRight (larguraDoTexto (fonteTexto (13.0f, true), "Desfazer") + 28));
    areaRodape = areaTratamentos.withTop (areaTratamentos.getBottom() - 132);
    auto rod = areaRodape.reduced (16, 10);
    wet->setBounds (rod.removeFromLeft (92));
    rod.removeFromLeft (4);
    output->setBounds (rod.removeFromLeft (92));
    rod.removeFromLeft (22);
    comparar.setBounds (rod.getX(), rod.getCentreY() - 26, larguraDoTexto (fonteTexto (13.0f, true), comparar.getButtonText()) + 32, 34);
    rolagemTratamentos.setBounds (areaTratamentos.withTrimmedTop (53).withBottom (areaRodape.getY()));
    layoutTratamentos();
}

void PainelDaPista::layoutTratamentos()
{
    const int largura = rolagemTratamentos.getMaximumVisibleWidth();
    if (largura <= 0) return;
    const bool vazio = processor.spec().modules.empty();
    cartao.setVisible (! vazio);
    int y = 14;
    if (! vazio)
    {
        const int h = cartao.alturaIdeal (largura - 32);
        cartao.setBounds (16, y, largura - 32, h);
        y += h + 12;
    }
    adicionar.setButtonText (vazio ? tx ("+ Escolher um tratamento na Biblioteca") : tx ("+ Trocar por outro tratamento da Biblioteca"));
    adicionar.setBounds (16, y, largura - 32, 46);
    conteudoTratamentos.setSize (largura, y + 46 + 16);
}

// ------------------------------------------------------------------ editor
MagicMixEditor::MagicMixEditor (MagicMixProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), pistas (p.sessao())
{
    setLookAndFeel (&visual);
    setWantsKeyboardFocus (true);
    juce::PluginHostType tipo;
    host = juce::String (tipo.getHostDescription());
    if (host == "Unknown" || juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone)
        host = {};

    biblioteca.onClick = [this] { abrirBiblioteca(); };
    addAndMakeVisible (biblioteca);
    pistas.aoEscolher = [this] (MagicMixProcessor* outra) { abrirPista (outra); };
    addAndMakeVisible (pistas);
    processor.sessao().adicionarOuvinte (this);

    setResizable (true, true);
    setResizeLimits (1100, 700, 1900, 1200);
    setSize (1240, 818);
    abrirPista (&processor);
}

MagicMixEditor::~MagicMixEditor()
{
    processor.sessao().removerOuvinte (this);
    painel.reset();
    setLookAndFeel (nullptr);
}

void MagicMixEditor::abrirPista (MagicMixProcessor* p)
{
    if (p == nullptr) p = &processor;
    if (painel != nullptr && &painel->pista() == p) return;
    painel = std::make_unique<PainelDaPista> (*p);
    addAndMakeVisible (*painel);
    pistas.setAtiva (p);
    resized();
    repaint();
}

void MagicMixEditor::instanciaSaindo (MagicMixProcessor* p)
{
    if (painel != nullptr && &painel->pista() == p)
    {
        painel.reset();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MagicMixEditor> (this)] {
            if (safe != nullptr) safe->abrirPista (nullptr);
        });
    }
}

void MagicMixEditor::abrirBiblioteca()
{
    if (painel == nullptr) return;
    auto& alvo = painel->pista();
    juce::PopupMenu menu;
    menu.addSectionHeader (tx ("Cadeias aprovadas nas escutas"));
    const auto& lista = alvo.biblioteca();
    for (size_t i = 0; i < lista.size(); ++i)
        menu.addItem ((int) i + 1, lista[i].titulo + (lista[i].descricao.isNotEmpty() ? "  -  " + lista[i].descricao : juce::String()));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&biblioteca),
                        [safe = juce::Component::SafePointer<MagicMixEditor> (this), alvoPtr = &alvo] (int r) {
                            if (safe != nullptr && r > 0 && safe->painel != nullptr && &safe->painel->pista() == alvoPtr)
                                alvoPtr->carregarDaBiblioteca (r - 1);
                        });
}

void MagicMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (corColuna);
    g.setColour (cor::painel);
    g.fillRect (areaTopo);
    g.setColour (cor::linha);
    g.drawHorizontalLine (areaTopo.getBottom() - 1, 0.0f, (float) getWidth());
    g.drawVerticalLine (areaPistas.getRight(), (float) areaTopo.getBottom(), (float) getHeight());

    auto t = areaTopo.reduced (18, 0);
    g.setFont (fonteMarca (15.0f));
    const int wMagic = larguraDoTexto (fonteMarca (15.0f), "Magic");
    g.setColour (cor::texto);
    g.drawText ("Magic", t.removeFromLeft (wMagic), juce::Justification::centredLeft);
    g.setColour (cor::ciano);
    g.drawText ("MIX", t.removeFromLeft (larguraDoTexto (fonteMarca (15.0f), "MIX") + 4), juce::Justification::centredLeft);
    const int n = (int) processor.sessao().instancias().size();
    g.setColour (cor::textoSuave);
    g.setFont (fonteTexto (13.0f));
    g.drawText (n == 1 ? tx ("1 pista nesta sessão") : juce::String (n) + tx (" pistas nesta sessão"), t.withTrimmedLeft (14), juce::Justification::centredLeft);

    // "Ligado ao Logic Pro": só quando o plugin está numa DAW conhecida
    if (host.isNotEmpty())
    {
        const auto ligado = tx ("Ligado ao ") + host;
        const int w = larguraDoTexto (fonteTexto (12.5f), ligado) + 34;
        auto chip = juce::Rectangle<float> ((float) biblioteca.getX() - 12.0f - (float) w, (float) areaTopo.getCentreY() - 13.0f, (float) w, 26.0f);
        g.setColour (cor::painelClaro);
        g.fillRoundedRectangle (chip, 13.0f);
        g.setColour (juce::Colour (0xff72d8a6));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ chip.getX() + 14.0f, chip.getCentreY() }));
        g.setColour (cor::textoSuave);
        g.setFont (fonteTexto (12.5f));
        g.drawText (ligado, chip.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
    }
}

void MagicMixEditor::resized()
{
    auto area = getLocalBounds();
    areaTopo = area.removeFromTop (56);
    biblioteca.setBounds (areaTopo.reduced (14, 11).removeFromRight (larguraDoTexto (fonteTexto (13.0f, true), "Biblioteca") + 30));
    areaPistas = area.removeFromLeft (200);
    pistas.setBounds (areaPistas);
    pistas.setAtiva (painel != nullptr ? &painel->pista() : &processor);
    if (painel != nullptr)
        painel->setBounds (area.withTrimmedLeft (1));
}

bool MagicMixEditor::keyPressed (const juce::KeyPress& key)
{
    // Ctrl/Cmd+Z desfaz; o resto segue para a DAW (play, gravar...) quando a caixa de texto não tem o foco.
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0) && painel != nullptr)
    {
        painel->pista().undo();
        return true;
    }
    return false;
}
}
