// Faces do modo Avançado: o desenho de cada efeito, como um plugin dedicado.
// EQ com analisador e pontos arrastáveis, compressor com os 5 caracteres, Bus Comp com valores em passos,
// de-esser, supressor de ressonâncias com a curva de sensibilidade, e uma face com knobs para os demais.
#pragma once

#include <memory>

#include "Engine/ChainSpec.h"
#include "Nomes.h"
#include "Visual.h"

namespace mm
{
class MagicMixProcessor;


class Face : public juce::Component
{
public:
    Face (MagicMixProcessor&, int modulo);
    // Chamado pelo timer da interface: relê os valores (o macro pode ter mexido) e redesenha os medidores.
    virtual void atualizar();
    virtual int alturaIdeal (int largura) const = 0;
    // Uma linha que explica o efeito, mostrada no topo da face.
    virtual juce::String explicacao() const;

protected:
    float valor (const std::string& param) const;
    void ajustar (const std::string& param, float v);     // um clique: já conta como um passo do Desfazer
    const ParamDef* def (const std::string& param) const;
    KnobParametro* novoKnob (const std::string& param, const juce::String& rotulo = {});
    void disporKnobs (juce::Rectangle<int> area, const std::vector<KnobParametro*>& lista) const;
    int alturaDosKnobs (int largura, int quantos) const;

    MagicMixProcessor& processor;
    const int modulo;
    const ModuleDef* modelo = nullptr;
    juce::OwnedArray<KnobParametro> knobs;
};

std::unique_ptr<Face> criarFace (MagicMixProcessor&, int modulo);

// Medidor de redução de ganho em arco, com ponteiro (0 a -20 dB).
void desenharVu (juce::Graphics&, juce::Rectangle<float> area, float reducaoDb, const juce::String& titulo);
// Barra vertical de nível (pico em escala de 0 a 1).
void desenharBarra (juce::Graphics&, juce::Rectangle<float> area, float pico, const juce::String& rotulo);
}
