#include "Cerebro.h"

#include <algorithm>
#include <cmath>

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

int Cerebro::sugestaoPara (int grupo) const
{
    static const int porGrupo[6] = { 5, 0, 6, 10, 7, 10 };
    const int i = porGrupo[juce::jlimit (0, 5, grupo)];
    return i < (int) entradas.size() ? i : (entradas.empty() ? -1 : 0);
}

juce::String Cerebro::normalizar (const juce::String& texto)
{
    juce::String saida;
    for (auto c : texto.toLowerCase())
    {
        if (c >= 0xc0 && c <= 0xde && c != 0xd7)
            c += 32;  // maiúsculas acentuadas (o toLowerCase do JUCE não converte)
        switch (c)
        {
            case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: c = 'a'; break;
            case 0xe8: case 0xe9: case 0xea: case 0xeb: c = 'e'; break;
            case 0xec: case 0xed: case 0xee: case 0xef: c = 'i'; break;
            case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: c = 'o'; break;
            case 0xf9: case 0xfa: case 0xfb: case 0xfc: c = 'u'; break;
            case 0xe7: c = 'c'; break;
            default: break;
        }
        saida += juce::String::charToString (c);
    }
    return " " + saida + " ";
}

namespace
{
// Um assunto que o músico pode pedir. `sentido` diz para que lado o botão gira quando ele pede "mais"
// do assunto. Queixas ("embolado", "aspereza", "escuro") sem "mais" ou "menos" são pedidos para tirar.
struct Assunto
{
    std::initializer_list<const char*> chaves;
    const char* botao;
    int sentido;
    bool queixa;
};

const Assunto kAssuntos[] = {
    { { "asper", "ardid", "estrid", "agressiv", "chiad", "sibil", "estour" }, "Suavizar", -1, true },
    { { "escur", "abafad", "fechad", "sem brilho", "apagad" }, "Brilho", -1, true },
    { { "brilh", "agud", "aberto", "aberta", "abrir", "abre ", "clareza", "claro", " ar " }, "Brilho", +1, false },
    { { "embolad", "embolo", "lamacent", "caixotad", "abafad" }, "Corpo", +1, true },
    { { "magr", "fino", "fina " }, "Corpo", -1, true },
    { { "corpo", "grave", "peso", "pesad", "encorpad", "gord", "cheio", "cheia" }, "Corpo", +1, false },
    { { "presenc", "na cara", "na frente", "pra frente", "para frente", "aparec", "destac", "some ", "sumind" }, "Presenca", +1, false },
    { { "punch", "pegada", "ataque", "estal", "clique", "soco", "impacto" }, "Punch", +1, false },
    { { "natural", "dinamic", "solt", "respir", "esmagad", "comprimido demais" }, "Compressao", -1, false },
    { { "compress", "comprim", "estavel", "segur", "nivelad", "uniform" }, "Compressao", +1, false },
    { { "seco", "seca ", "sem reverb", "sem eco" }, "Ambiente", -1, false },
    { { "ambient", "reverb", "sala", "espaco", " eco", "profund" }, "Ambiente", +1, false },
    { { "sutil", "discret", "menos processad" }, "Intensidade", -1, false },
    { { "intens", "atitude", "expressiv", "ousad", "mais forte", "mais processad" }, "Intensidade", +1, false },
};

const char* const kNegacoes[] = { "menos", "tira", "tirar", "sem ", "reduz", "diminu", "abaix", "corta", "cortar", "nao ", "evita", "limpa" };
const char* const kAfirmacoes[] = { "mais", "aument", "sobe", "subir", "coloca", "poe ", "bota", "quero", "deixa", "da um", "realca", "acrescent" };

// Nome do botão como aparece na interface.
juce::String nomeDoBotao (const char* botao)
{
    const juce::String b (botao);
    if (b == "Presenca") return tx ("Presença");
    if (b == "Compressao") return tx ("Compressão");
    return b;
}

int indiceDoMacro (const ChainSpec& spec, const juce::String& nome)
{
    for (size_t i = 0; i < spec.macros.size(); ++i)
        if (tx (spec.macros[i].name) == nome)
            return (int) i;
    return -1;
}

juce::String porcento (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
}

Resposta Cerebro::responder (const juce::String& pedido, const ChainSpec& atual, int grupo) const
{
    Resposta r;
    const auto texto = normalizar (pedido);

    // 1) o que foi pedido: assunto, para que lado e quanto
    struct Pedido { const Assunto* assunto; int lado; float passo; };
    std::vector<Pedido> pedidos;
    for (auto& a : kAssuntos)
    {
        int onde = -1;
        for (auto* chave : a.chaves)
        {
            const int i = texto.indexOf (chave);
            if (i >= 0 && (onde < 0 || i < onde)) onde = i;
        }
        if (onde < 0) continue;
        bool repetido = false;
        for (auto& p : pedidos)
            repetido = repetido || juce::String (p.assunto->botao) == a.botao;
        if (repetido) continue;

        // olha as palavras logo antes do assunto (texto grudado também funciona)
        const auto antes = texto.substring (juce::jmax (0, onde - 16), onde);
        int lado = a.queixa ? -1 : +1;
        int achado = -1;
        for (auto* n : kNegacoes)
            achado = juce::jmax (achado, antes.lastIndexOf (n));
        int afirmado = -1;
        for (auto* m : kAfirmacoes)
            afirmado = juce::jmax (afirmado, antes.lastIndexOf (m));
        if (achado >= 0 || afirmado >= 0)
            lado = achado > afirmado ? -1 : +1;
        float passo = 0.15f;
        if (texto.contains ("muito") || texto.contains ("bem mais") || texto.contains ("bastante") || texto.contains ("demais"))
            passo = 0.25f;
        if (texto.contains ("um pouco") || texto.contains ("pouquinho") || texto.contains ("levemente") || texto.contains ("sutilmente"))
            passo = 0.08f;
        pedidos.push_back ({ &a, lado, passo });
    }

    if (pedidos.empty())
    {
        r.texto = tx ("Ainda não entendi esse pedido. Por enquanto eu entendo brilho, presença, corpo, punch, compressão, "
                      "aspereza, ambiente e intensidade, com \"mais\", \"menos\" ou \"tira\". "
                      "Por exemplo: \"mais brilho e menos embolado\", \"tira a aspereza\", \"mais punch\".");
        return r;
    }

    // 2) sem cadeia ainda: começa pela cadeia aprovada que combina com a pista
    const ChainSpec* base = &atual;
    juce::String inicio;
    if (atual.modules.empty())
    {
        r.carregarBiblioteca = sugestaoPara (grupo);
        if (r.carregarBiblioteca < 0)
        {
            r.texto = tx ("Não encontrei a biblioteca de cadeias. Reinstale o MagicMIX.");
            return r;
        }
        base = &entradas[(size_t) r.carregarBiblioteca].cadeia;
        inicio = tx ("Comecei pela cadeia aprovada \"") + entradas[(size_t) r.carregarBiblioteca].titulo + "\". ";
    }

    // 3) gira os botões
    juce::StringArray feitos, faltando, detalhes;
    for (auto& p : pedidos)
    {
        const auto nome = nomeDoBotao (p.assunto->botao);
        const int m = indiceDoMacro (*base, nome);
        if (m < 0)
        {
            faltando.add (nome);
            continue;
        }
        const float antes = base->macros[(size_t) m].value;
        const float depois = juce::jlimit (0.0f, 1.0f, antes + (float) (p.lado * p.assunto->sentido) * p.passo);
        r.macros.push_back ({ m, depois });
        r.botoes.add (nome);
        if (std::abs (depois - antes) < 0.005f)
            feitos.add (nome + tx (" já estava no limite (") + porcento (depois) + ")");
        else
            feitos.add (nome + (depois > antes ? tx (" subiu de ") : tx (" desceu de ")) + porcento (antes) + tx (" para ") + porcento (depois));
        detalhes.add (nome + ": " + tx (base->macros[(size_t) m].description));
    }

    if (feitos.isEmpty())
    {
        r.texto = inicio + tx ("Esta cadeia não tem o botão ") + faltando.joinIntoString (", ")
                  + tx (". Abra o modo Avançado para ajustar direto nos efeitos.");
        return r;
    }
    r.texto = inicio + tx ("Feito: ") + feitos.joinIntoString ("; ") + ". "
              + tx ("Os botões do pedido estão marcados no Fluxo: se ainda não chegou lá, gire mais um pouco ou abra o Avançado.");
    if (! faltando.isEmpty())
        r.texto << tx (" Esta cadeia não tem botão de ") << faltando.joinIntoString (", ") << ".";
    r.detalhes = detalhes.joinIntoString ("\n");
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
