// Cérebro local do MagicMIX. Roda no computador do músico, sem chave de API e sem internet.
//
// Hoje ele traz a biblioteca de cadeias aprovadas nas escutas e entende o vocabulário dos botões
// simples (brilho, presença, corpo, punch, compressão, aspereza, ambiente, intensidade), com
// "mais/menos/tira/sem" e "muito/um pouco". O que ele ainda não entende, ele diz, com exemplos.
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
    juce::String texto, detalhes;
    int carregarBiblioteca = -1;                   // começar por esta cadeia da biblioteca antes dos ajustes
    std::vector<std::pair<int, float>> macros;     // índice do macro e posição nova (0 a 1)
    juce::StringArray botoes;                      // nomes dos botões que o pedido mexeu (destacados no Fluxo)
};

class Cerebro
{
public:
    Cerebro();

    const std::vector<EntradaBiblioteca>& biblioteca() const { return entradas; }
    // Cadeia da biblioteca que combina com um grupo de pista (0 bateria, 1 vozes, 2 baixo, 3 teclas, 4 harmonia, 5 outros).
    int sugestaoPara (int grupo) const;

    // Responde a um pedido. `atual` traz os macros na posição em que estão; `grupo` é o da pista.
    Resposta responder (const juce::String& pedido, const ChainSpec& atual, int grupo) const;

    // Cadeias da biblioteca foram decididas para outra gravação: o volume de trabalho (módulo "ganho") é
    // refeito para a pista atual, e a saída compensa a diferença para o volume final continuar o mesmo.
    static ChainSpec ajustarVolumeDeTrabalho (ChainSpec cadeia, const Features& medidas);

    // Texto sem acentos, em minúsculas e com espaços nas pontas (para procurar palavras).
    static juce::String normalizar (const juce::String& texto);

private:
    std::vector<EntradaBiblioteca> entradas;
};
}
