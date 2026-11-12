// Interface do MagicMIX, no desenho do protótipo aprovado: pistas à esquerda, conversa no centro,
// tratamentos à direita (Fluxo ou Avançado), WET, OUTPUT e Comparar no rodapé. Ciano sobre cinza rato.
#pragma once

#include <set>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Ui/Faces.h"
#include "Ui/Visual.h"

namespace mm
{
// ------------------------------------------------------------------ pistas
// Todas as pistas da sessão com o MagicMIX, agrupadas pelo nome (bateria, vozes, baixo...).
class ListaDePistas : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit ListaDePistas (Sessao& s);
    ~ListaDePistas() override;
    std::function<void (MagicMixProcessor*)> aoEscolher;
    void setAtiva (MagicMixProcessor* p) { ativa = p; montar(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { sobre = nullptr; repaint(); }

private:
    struct Linha { juce::String grupo; MagicMixProcessor* pista = nullptr; juce::Rectangle<float> caixa; };
    void changeListenerCallback (juce::ChangeBroadcaster*) override { montar(); }
    void timerCallback() override { repaint(); }
    void montar();

    Sessao& sessao;
    MagicMixProcessor* ativa = nullptr;
    MagicMixProcessor* sobre = nullptr;
    std::vector<Linha> linhas;
};

// ------------------------------------------------------------------ conversa
class Conversa : public juce::Component
{
public:
    explicit Conversa (MagicMixProcessor& p) : processor (p) {}
    void atualizar (int largura, bool forcar = false);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    struct Bolha
    {
        juce::TextLayout layout, detalhes;
        juce::Rectangle<float> caixa, botaoDetalhes, caixaDetalhes;
        bool doMusico = true, temDetalhes = false, aberto = false;
        size_t indice = 0;
    };
    MagicMixProcessor& processor;
    std::vector<Bolha> bolhas;
    std::set<size_t> abertos;
    size_t contadas = (size_t) -1;
    int larguraFeita = 0;
};

// Caixa de texto do pedido. Dentro de uma DAW, clicar nela abre por cima a JanelaDePedido.
class CaixaDePedido : public juce::TextEditor
{
public:
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& e) override;
    std::function<void()> aoClicar;
};

// Janela própria do sistema, sem borda, desenhada exatamente em cima da caixa de pedido.
// Muitas DAWs pegam as teclas da janela do plugin antes dele (a barra de espaço vira play); numa janela
// separada o teclado vai inteiro para o texto. Fecha sozinha quando perde o foco.
class JanelaDePedido : public juce::Component
{
public:
    JanelaDePedido();
    ~JanelaDePedido() override;
    void abrir (juce::Rectangle<int> naTela, const juce::String& texto);
    void fechar();
    bool aberta() const { return isOnDesktop() && isVisible(); }
    void acompanhar (juce::Rectangle<int> naTela);
    juce::String texto() const { return caixa.getText(); }
    void limpar() { caixa.clear(); }
    std::function<void (const juce::String&)> aoEnviar, aoFechar;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextEditor caixa;
};

// Botão em forma de pílula (sugestões, selos).
class Pilula : public juce::Button
{
public:
    explicit Pilula (const juce::String& texto) : juce::Button (texto) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    int larguraIdeal() const;
};

// Faixa da cadeia: Entrada → módulos → Saída. Clicar num módulo abre a face dele no modo Avançado.
class FaixaDaCadeia : public juce::Component
{
public:
    explicit FaixaDaCadeia (MagicMixProcessor& p) : processor (p) {}
    std::function<void (int)> aoEscolher;
    void setSelecionado (int m) { selecionado = m; repaint(); }
    int alturaIdeal (int largura) const;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Peca { juce::Rectangle<float> caixa; juce::String texto; int modulo; bool ligado; };
    std::vector<Peca> pecas (int largura) const;
    MagicMixProcessor& processor;
    int selecionado = -1;
};

// Cartão do tratamento: título, chave, faixa da cadeia e, embaixo, os botões (Fluxo) ou a face do efeito (Avançado).
class CartaoTratamento : public juce::Component
{
public:
    explicit CartaoTratamento (MagicMixProcessor& p);
    void reconstruir();
    void setAvancado (bool sim);
    void abrirModulo (int m);
    void atualizar();  // timer: knobs e medidores
    int alturaIdeal (int largura);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    std::function<void()> aoMudarAltura, aoEntrarNoAvancado;

private:
    MagicMixProcessor& processor;
    FaixaDaCadeia faixa;
    Chave ligado, moduloLigado;
    juce::OwnedArray<KnobHost> macros;
    std::unique_ptr<Face> face;
    bool avancado = false;
    int modulo = -1;
    juce::Rectangle<int> areaFace;
};

// Tudo o que é de uma pista: conversa, tratamentos e rodapé.
class PainelDaPista : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PainelDaPista (MagicMixProcessor& p);
    ~PainelDaPista() override;
    MagicMixProcessor& pista() { return processor; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mostrarAvancado (bool sim);
    void selecionarModulo (int m);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void enviar();
    void montarSugestoes();
    void layoutTratamentos();

    MagicMixProcessor& processor;

    // conversa
    juce::Viewport rolagemConversa;
    Conversa conversa;
    juce::OwnedArray<Pilula> sugestoes;
    std::vector<std::function<void()>> acoesSugestoes;
    CaixaDePedido pedido;
    JanelaDePedido janelaDePedido;
    juce::TextButton enviarBotao { "Enviar" };

    // tratamentos
    juce::TextButton desfazer { "Desfazer" };
    Escolha modo;
    juce::Viewport rolagemTratamentos;
    juce::Component conteudoTratamentos;
    CartaoTratamento cartao;
    juce::TextButton adicionar;
    std::unique_ptr<KnobHost> wet, output;
    juce::TextButton comparar;

    juce::Rectangle<int> areaConversa, areaTratamentos, areaRodape, areaComposicao;
    juce::String assinatura;
};

class MagicMixEditor : public juce::AudioProcessorEditor, private Sessao::Ouvinte
{
public:
    explicit MagicMixEditor (MagicMixProcessor&);
    ~MagicMixEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Para os retratos da interface nos testes.
    void mostrarAvancado (bool sim) { if (painel) painel->mostrarAvancado (sim); }
    void selecionarModulo (int m) { if (painel) painel->selecionarModulo (m); }
    void abrirPista (MagicMixProcessor* p);

private:
    void instanciaSaindo (MagicMixProcessor*) override;
    void abrirBiblioteca();

    MagicMixProcessor& processor;
    Visual visual;
    juce::TooltipWindow dicas { this, 600 };
    juce::TextButton biblioteca { "Biblioteca" };
    ListaDePistas pistas;
    std::unique_ptr<PainelDaPista> painel;
    juce::String host;
    juce::Rectangle<int> areaTopo, areaPistas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicMixEditor)
};
}
