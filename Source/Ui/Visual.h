// Peças visuais da interface: cores, knobs ligados aos parâmetros da cadeia, botões de escolha,
// chave liga/desliga e o analisador de espectro.
#pragma once

#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Engine/Catalog.h"
#include "Engine/Fft.h"

namespace mm
{
class MagicMixProcessor;

namespace cor
{
    const juce::Colour fundo { 0xff2c2e31 };       // cinza rato
    const juce::Colour painel { 0xff34373b };
    const juce::Colour painelClaro { 0xff3f4246 };
    const juce::Colour app { 0xff222427 };
    const juce::Colour linha { 0xff4f5358 };
    const juce::Colour texto { 0xffeef0f1 };
    const juce::Colour textoSuave { 0xffb0b4b8 };
    const juce::Colour textoFraco { 0xff868b90 };
    const juce::Colour ciano { 0xff3fd6ef };
    const juce::Colour cianoForte { 0xff13b4d4 };
    const juce::Colour cianoEscuro { 0xff1d5f6b };
    const juce::Colour aviso { 0xfff0bb6b };       // curva de sensibilidade
    const juce::Colour reducao { 0xffe0806e };     // o que está sendo tirado
}

juce::Font fonteTexto (float altura, bool negrito = false);
juce::Font fonteNumero (float altura);
juce::Font fonteMarca (float altura);  // Unbounded, para a marca e os títulos das faces
int larguraDoTexto (const juce::Font& fonte, const juce::String& texto);
juce::String maiusculas (const juce::String& texto);  // com acentos (o toUpperCase do JUCE não converte "â")

class Visual : public juce::LookAndFeel_V4
{
public:
    Visual();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    juce::Font getPopupMenuFont() override { return fonteTexto (15.0f); }
};

// Texto de um valor com a unidade do catálogo, no jeito brasileiro (vírgula decimal).
juce::String textoDoValor (const ParamDef& p, float valor);

// Knob de um parâmetro de um módulo da cadeia. Lê o valor atual do processador (que muda quando o
// macro Intensidade gira) e escreve com ajuste manual.
class KnobParametro : public juce::Component
{
public:
    KnobParametro (MagicMixProcessor&, int modulo, const ParamDef&, const juce::String& rotulo);
    void atualizar();  // relê o valor, a menos que o músico esteja arrastando
    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kLargura = 78, kAltura = 96;

private:
    MagicMixProcessor& processor;
    const int modulo;
    const ParamDef& param;
    juce::String rotulo;
    juce::Slider knob;
    bool arrastando = false, relendo = false;
};

// Knob grande com nome e valor, ligado a um parâmetro do host (macros, Wet, Output).
class KnobHost : public juce::Component, public juce::SettableTooltipClient
{
public:
    KnobHost (juce::RangedAudioParameter&, const juce::String& rotulo, std::function<juce::String (float)> formatar);
    void setRotulo (const juce::String& r) { rotulo = r; repaint(); }
    void setRotuloCiano (bool sim) { rotuloCiano = sim; repaint(); }  // WET e OUTPUT, como no protótipo
    void setDestaque (bool sim) { if (destaque != sim) { destaque = sim; repaint(); } }  // "do pedido"
    void setReservarDestaque (bool sim) { reservar = sim; resized(); }  // deixa o espaço da etiqueta sempre
    const juce::String& getRotulo() const { return rotulo; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::RangedAudioParameter& param;
    juce::String rotulo;
    std::function<juce::String (float)> formatar;
    juce::Slider knob;
    juce::SliderParameterAttachment ligacao;
    bool rotuloCiano = false, destaque = false, reservar = false;
};

// Fileira de botões em que só um fica ligado (caráter do compressor, modo, valores em passos).
class Escolha : public juce::Component
{
public:
    Escolha (juce::StringArray opcoes, std::function<void (int)> aoEscolher);
    void setEscolhido (int indice);
    int escolhido() const { return atual; }
    void resized() override;
    void paint (juce::Graphics&) override;
    int larguraIdeal() const;
    void setAgrupado (bool sim);  // uma moldura só em volta dos botões (Fluxo | Avançado)

private:
    bool agrupado = false;
    juce::OwnedArray<juce::TextButton> botoes;
    std::function<void (int)> aoEscolher;
    int atual = -1;
};

// Chave liga/desliga redonda, como a do protótipo.
class Chave : public juce::Button
{
public:
    Chave() : juce::Button ("chave") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// Analisador de espectro: FFT das últimas amostras do processador, em 1/6 de oitava, com queda suave.
class Espectro
{
public:
    static constexpr int kOrdem = 13, kTamanho = 1 << kOrdem, kPontos = 120;
    Espectro();
    void atualizar (MagicMixProcessor&, bool saida);
    float nivelDb (int ponto) const { return niveis[(size_t) ponto]; }   // dB relativo ao fundo de escala
    static float frequencia (int ponto);                                  // 20 Hz a 20 kHz, escala log

private:
    dsp::Fft fft;
    std::vector<float> janela, re, im, niveis;
};

// Mapas de frequência e dB para os gráficos.
float xDaFrequencia (float f, juce::Rectangle<float> area);
float frequenciaDoX (float x, juce::Rectangle<float> area);
void grade (juce::Graphics&, juce::Rectangle<float> area, float dbMin, float dbMax, float passoDb);
}
