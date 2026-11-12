#include "Cerebro.h"

#include <algorithm>

#include "Ai/SpecJson.h"
#include "BinaryData.h"
#include "Texto.h"

namespace mm
{
Cerebro::Cerebro()
{
    // Ordem de apresentação: vozes, bateria, baixo, harmonia e a mix.
    static const char* const ordem[] = { "voz_json", "rael_json", "zevitor_json", "caixa_json", "bumbo_json", "bateria_json",
                                         "sidechain_json", "violao_json", "sanfona_json", "sanfsolo_json", "base_json" };
    for (const char* nome : ordem)
    {
        int tamanho = 0;
        const char* dados = BinaryData::getNamedResource (nome, tamanho);
        if (dados == nullptr)
            continue;
        ChainSpec spec;
        juce::String erro;
        if (! json::fromString (juce::String::fromUTF8 (dados, tamanho), spec, erro))
            continue;
        EntradaBiblioteca e;
        e.titulo = tx (spec.title);
        e.descricao = spec.prompts.empty() ? juce::String() : tx (spec.prompts.front());
        spec.prompts.clear();  // a conversa é do músico, não da biblioteca
        e.cadeia = std::move (spec);
        entradas.push_back (std::move (e));
    }
}

Resposta Cerebro::responder (const juce::String& pedido, const ChainSpec& atual, const Features& medidas) const
{
    juce::ignoreUnused (pedido, atual, medidas);
    Resposta r;
    r.texto = tx ("Nesta versão ainda não atendo pedidos escritos. Escolha um ponto de partida na Biblioteca "
                  "e ajuste o que quiser no modo Avançado; o botão Intensidade leva a cadeia de discreta a expressiva.");
    return r;
}

ChainSpec Cerebro::ajustarVolumeDeTrabalho (ChainSpec cadeia, const Features& medidas)
{
    if (! medidas.valid)
        return cadeia;
    for (auto& m : cadeia.modules)
        if (m.type == "gain" && m.id == "ganho")
        {
            const float antes = m.params.count ("gain_db") ? m.params["gain_db"] : 0.0f;
            const float novo = std::min ({ -20.0f - medidas.rmsDbfs, -1.5f - medidas.peakDbfs, 12.0f });
            m.params["gain_db"] = std::max (novo, -24.0f);
            cadeia.outputGainDb = std::min (std::max (cadeia.outputGainDb + antes - m.params["gain_db"], -24.0f), 12.0f);
            break;
        }
    return cadeia;
}
}
