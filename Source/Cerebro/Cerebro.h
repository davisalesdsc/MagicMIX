// Cérebro local do MagicMIX. Roda no computador do músico, sem chave de API e sem internet.
//
// Hoje ele traz a biblioteca de cadeias aprovadas nas escutas. A parte que entende o pedido em
// texto (a rede neural) e a que ouve a pista e decide (o decisor) entram aqui quando estiverem
// treinadas; até lá ele diz isso com clareza, em vez de inventar uma resposta.
#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "Engine/ChainSpec.h"

namespace mm
{
struct EntradaBiblioteca
{
    juce::String titulo, descricao;
    ChainSpec cadeia;
};

struct Resposta
{
    juce::String texto;
    bool trocaCadeia = false;
    ChainSpec cadeia;
};

class Cerebro
{
public:
    Cerebro();

    const std::vector<EntradaBiblioteca>& biblioteca() const { return entradas; }

    // Responde a um pedido do músico, conhecendo a cadeia atual e as medidas da pista.
    Resposta responder (const juce::String& pedido, const ChainSpec& atual, const Features& medidas) const;

    // Cadeias da biblioteca foram decididas para outra gravação: o volume de trabalho (módulo "ganho") é
    // refeito para a pista atual, e a saída compensa a diferença para o volume final continuar o mesmo.
    static ChainSpec ajustarVolumeDeTrabalho (ChainSpec cadeia, const Features& medidas);

private:
    std::vector<EntradaBiblioteca> entradas;
};
}
