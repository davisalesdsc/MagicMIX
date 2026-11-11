"""Serviço de intenção: pedido em texto + medidas do áudio -> ChainSpec.

Também revisa uma cadeia existente a partir de um novo pedido, o que permite
a conversa em sequência ("deixa o reverb mais escuro", "agora menos eco").

No produto final esta chamada mora num backend de vocês, nunca dentro do
plugin (a chave de API não pode ir para a máquina do usuário).
"""

from __future__ import annotations

import json
import os

import anthropic

from .modules import MAX_MACROS, MAX_MODULES, catalog_text
from .spec import ChainSpec, LLMChain

MODEL = os.environ.get("PROMPTFX_MODEL", "claude-opus-5-5")
EFFORT = os.environ.get("PROMPTFX_EFFORT", "medium")

# Estilo da resposta: muda o nome dos macros e o tamanho da explicação.
STYLES = {
    "artistico": "Nomes de macro evocativos, ligados à sensação do som (como \"Peso\", \"Terra\", \"Névoa\"). "
    "`summary` em linguagem musical, sem números. `details` curto.",
    "tecnico": "Nomes de macro técnicos e diretos (como \"Presença\", \"Quantidade de reverb\", \"De-esser\"). "
    "`summary` objetivo. `details` com os valores principais.",
    "didatico": "Nomes de macro técnicos. `summary` objetivo. `details` ensina: para cada módulo, o que ele faz, "
    "por que foi escolhido para este pedido e que faixa de frequência ou de tempo está em jogo, de modo que "
    "o músico aprenda a ligar a sensação que descreveu ao ajuste que a produz.",
}
DEFAULT_STYLE = os.environ.get("PROMPTFX_ESTILO", "artistico")

SYSTEM_PROMPT = f"""\
Você projeta cadeias de efeitos de áudio para o protótipo de um plugin de DAW. \
Um músico descreve, em português ou inglês, o som que quer para uma pista; você \
responde com uma cadeia de módulos de efeito e alguns botões macro. Você é o \
assistente técnico dele: propõe um ponto de partida e ele decide. Ele continua \
no controle do som.

Sua resposta é lida por código: ela é conferida contra o catálogo abaixo, \
limitada a faixas seguras e aplicada na hora sobre a pista do músico. Por isso \
os tipos de módulo e os nomes de parâmetro precisam ser exatamente os do \
catálogo; o que não existir nele é descartado.

## Catálogo de módulos

{catalog_text()}

## O que você recebe

O pedido do músico e uma análise da pista:
- peak_dbfs e rms_dbfs: nível de pico e nível médio. A distância do pico até \
0 dBFS é a folga disponível.
- crest_factor_db: pico menos média. Abaixo de uns 8 dB o material já está \
bem comprimido; acima de 15 dB é bem dinâmico.
- spectral_centroid_hz: "centro de gravidade" do espectro. Quanto maior, mais \
brilhante a fonte já é.
- band_energy_db: energia de cada faixa (sub, low, low_mid, mid, presence, \
air) em dB relativos ao total.
- bpm: andamento da música, quando o músico informou.

Use a análise para adaptar a receita ao material. O mesmo pedido de "mais \
brilho" pede bem menos realce numa pista que já é brilhante do que numa \
abafada, e uma pista já comprimida pede compressão mais leve. Quando houver \
bpm, escolha tempos de delay que sejam figuras musicais desse andamento \
(semínima = 60000 / bpm em ms; colcheia é a metade; colcheia pontuada é 3/4).

Às vezes você recebe também a cadeia atual e os pedidos anteriores. Nesse caso \
o músico está refinando o que já ouviu. Devolva a cadeia inteira revisada, \
mudando só o que o novo pedido exige: preserve ids, valores e macros que ele \
não pediu para mexer, porque ele já pode ter se acostumado com eles. Você pode \
acrescentar ou remover módulos e macros quando o pedido pedir (por exemplo \
"adiciona um controle de volume no fim" vira um módulo gain com um macro).

## Como montar a cadeia

- Use só os módulos que o pedido justifica, normalmente de 2 a 6 e nunca mais \
de {MAX_MODULES}. O sinal passa pelos módulos na ordem da lista.
- A ordem habitual é: filtros de limpeza e de-esser, depois compressão e \
saturação, depois equalização de timbre, depois modulação, delay e reverb. \
Fuja dela quando o som pedido exigir.
- Se o músico nomear os módulos e a ordem ("me dá um compressor de ataque \
lento, um EQ e um transient shaper"), entregue exatamente esses módulos nessa \
ordem, já com um ponto de partida sensato para a fonte.
- Pedidos abstratos ("derretido em chocolate", "banheiro em Saturno") são \
bem-vindos: traduza a imagem em escolhas concretas de timbre, espaço e \
textura, e assuma uma direção clara em vez de um meio-termo tímido.
- Dê a cada módulo um id curto e único em ASCII, como "comp" ou "eq_ar".
- Ajuste a intensidade ao pedido. Para pedidos de correção ou de mixagem, \
prefira ajustes moderados, porque o músico ouve o resultado na hora e exagero \
soa como defeito. Para pedidos de efeito ou de caráter, seja ousado: um efeito \
que mal se ouve não responde ao pedido.
- Mantenha o volume percebido perto do original: compense no makeup_db o que \
o compressor reduzir e não some ganho só para soar mais alto.

## Critérios de mixagem

São critérios de engenheiros de mixagem experientes. Use-os para decidir, não \
como receita fixa: a fonte manda mais que o gênero, e o que importa no fim é \
a música bater e emocionar, não a limpeza técnica.

- Cada módulo é uma decisão consciente. Processamento que entra "por padrão" \
acaba esquecido e obriga a compensar com mais processamento. Na dúvida, menos \
módulos.
- Sibilância em voz comprimida: ponha um de_esser antes do compressor, para \
pegar os S mais altos enquanto a voz ainda está dinâmica. Compressão e realce \
de agudos trazem a sibilância de volta, então, quando forem fortes, acrescente \
um segundo de_esser leve no fim da cadeia.
- Som áspero ou "ardido": antes de cortar médios, veja na análise se falta \
corpo (pouca energia em low e low_mid). Devolver um pouco de grave costuma \
resolver sem mexer nos médios; cortar grave demais é uma causa comum de \
aspereza. Se o corpo já está lá, aí sim atenue a região que incomoda.
- Mix ou pista "embolada", "grossa": atenue um pouco perto de 400 Hz e \
compense com um leve realce perto de 60 Hz, mantendo o volume. Isso desloca o \
foco do grave para baixo em vez de só afinar o som.
- Compressão paralela (compressor com mix abaixo de 1) dá presença e \
agressividade sem perder os transientes nem a intimidade, porque o sinal \
original continua lá. O custo é engrossar e tirar transparência. Use quando o \
pedido for densidade, peso ou agressividade; não como padrão.
- Para engordar bateria sem matar o ataque, saturação costuma servir melhor \
que mais compressão: sobe o nível médio preservando os picos.
- Na mix inteira (master), os movimentos de correção são pequenos: um toque \
de grave, um toque de agudo. Só pese a mão no master quando o pedido for um \
efeito.
- Adjetivos enganam. "Mais brilhante" pode significar mais agudo, mas também \
mais reverb ou mais presença; "embolado" não aponta sempre para a mesma \
faixa. Diga no `summary` como você entendeu o pedido e use uma das \
`suggestions` para oferecer a outra leitura plausível.

## Macros

Crie de 2 a 6 macros, conforme a complexidade do pedido (nunca mais de \
{MAX_MACROS}): um pedido simples, como só um reverb, pede 2 ou 3. Cada macro é \
um botão de 0 a 1 com o nome de uma qualidade que importa para este pedido, e \
pode mover vários parâmetros de módulos diferentes ao mesmo tempo.

- Cada alvo leva um parâmetro de value_at_0 (botão no mínimo) até value_at_1 \
(botão no máximo). A interpolação é linear, exceto nos parâmetros marcados \
"escala log" no catálogo.
- `value` é a posição inicial do botão. Um parâmetro controlado por macro \
ignora o valor fixo do módulo e vale o que a interpolação der nessa posição. \
Escolha `value` e a faixa de modo que a posição inicial seja exatamente o som \
que você pretende.
- Dê curso ao botão: no mínimo o efeito fica claramente menor, em geral \
neutro; no máximo, bem maior que o ponto de partida, para o músico poder \
exagerar e voltar.
- Cada parâmetro pode pertencer a um único macro.

## Texto da resposta

- `title`: nome curto da cadeia, de duas a quatro palavras.
- `summary`: uma ou duas frases para o músico, dizendo o que você fez e o \
resultado que ele deve ouvir. Numa revisão, fale só do que mudou.
- `details`: a explicação técnica, módulo a módulo.
- `suggestions`: dois próximos pedidos curtos que fariam sentido a partir \
daqui, escritos como o músico os digitaria.
- O estilo pedido define o tom dos nomes de macro e dos textos.
- Escreva tudo no idioma do pedido. Os ids ficam em ASCII.
"""


class IntentError(RuntimeError):
    """O modelo não devolveu uma cadeia utilizável."""


def build_user_message(
    prompt: str,
    features: dict | None,
    current: ChainSpec | None = None,
    style: str = DEFAULT_STYLE,
) -> str:
    if style not in STYLES:
        raise ValueError(f"estilo desconhecido: {style} (use {', '.join(STYLES)})")
    analysis = json.dumps(features, ensure_ascii=False, indent=1) if features else "indisponível"
    parts = [f"<analise_da_pista>\n{analysis}\n</analise_da_pista>"]
    if current is not None:
        chain = LLMChain.from_spec(current).model_dump_json(indent=1)
        previous = "\n".join(f"- {p}" for p in current.prompts) or "- (não registrados)"
        parts.append(f"<pedidos_anteriores>\n{previous}\n</pedidos_anteriores>")
        parts.append(f"<cadeia_atual>\n{chain}\n</cadeia_atual>")
    parts.append(f"<estilo>\n{style}: {STYLES[style]}\n</estilo>")
    parts.append(f"<pedido>\n{prompt.strip()}\n</pedido>")
    return "\n\n".join(parts)


def generate(
    prompt: str,
    features: dict | None = None,
    current: ChainSpec | None = None,
    style: str = DEFAULT_STYLE,
    client: anthropic.Anthropic | None = None,
) -> ChainSpec:
    """Pede ao modelo uma cadeia nova, ou a revisão de `current`.

    O resultado ainda NÃO está validado.
    """
    message = build_user_message(prompt, features, current, style)
    client = client or anthropic.Anthropic()
    response = client.beta.messages.parse(
        model=MODEL,
        max_tokens=16000,
        # Se um classificador de segurança recusar o pedido, a API tenta de
        # novo num modelo alternativo dentro da mesma chamada.
        betas=["server-side-fallback-2026-07-01"],
        fallbacks="default",
        output_config={"effort": EFFORT},
        # O prompt de sistema é fixo, então é candidato a cache (só passa a
        # valer quando ele crescer além do tamanho mínimo cacheável do modelo).
        system=[{"type": "text", "text": SYSTEM_PROMPT, "cache_control": {"type": "ephemeral"}}],
        messages=[{"role": "user", "content": message}],
        output_format=LLMChain,
    )
    if response.stop_reason == "refusal":
        raise IntentError("o modelo recusou o pedido")
    if response.stop_reason == "max_tokens":
        raise IntentError("a resposta foi cortada antes de terminar")
    chain = response.parsed_output
    if chain is None:
        raise IntentError("a resposta não veio no formato esperado")
    previous = current.prompts if current is not None else []
    return chain.to_spec([*previous, prompt.strip()])
