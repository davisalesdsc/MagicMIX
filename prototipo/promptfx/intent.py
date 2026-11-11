"""Serviço de intenção: pedido em texto + medidas do áudio -> ChainSpec.

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

SYSTEM_PROMPT = f"""\
Você projeta cadeias de efeitos de áudio para o protótipo de um plugin de DAW. \
Um músico descreve, em português ou inglês, o som que quer para uma pista; você \
responde com uma cadeia de módulos de efeito e alguns botões macro.

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

Use a análise para adaptar a receita ao material. O mesmo pedido de "mais \
brilho" pede bem menos realce numa pista que já é brilhante do que numa \
abafada, e uma pista já comprimida pede compressão mais leve.

## Como montar a cadeia

- Use só os módulos que o pedido justifica, normalmente de 2 a 6 e nunca mais \
de {MAX_MODULES}. O sinal passa pelos módulos na ordem da lista.
- A ordem habitual é: filtros de limpeza, depois compressão e saturação, \
depois equalização de timbre, depois modulação, delay e reverb. Fuja dela \
quando o som pedido exigir.
- Dê a cada módulo um id curto e único em ASCII, como "comp" ou "eq_ar".
- Prefira ajustes musicais e moderados. O músico ouve o resultado na hora, na \
própria pista, e processamento exagerado soa como defeito. Vá ao extremo só \
quando o pedido for claramente extremo.
- Mantenha o volume percebido perto do original: compense no makeup_db o que \
o compressor reduzir e não some ganho só para soar mais alto.

## Macros

Crie de 2 a {MAX_MACROS} macros. Cada macro é um botão de 0 a 1 com o nome de \
uma qualidade perceptiva que importa para este pedido (por exemplo "Brilho", \
"Espaço", "Calor"), e pode mover vários parâmetros de módulos diferentes ao \
mesmo tempo.

- Cada alvo leva um parâmetro de value_at_0 (botão no mínimo) até value_at_1 \
(botão no máximo). A interpolação é linear, exceto nos parâmetros marcados \
"escala log" no catálogo.
- `value` é a posição inicial do botão. Um parâmetro controlado por macro \
ignora o valor fixo do módulo e vale o que a interpolação der nessa posição. \
Escolha `value` e a faixa de modo que a posição inicial seja exatamente o som \
que você pretende.
- No mínimo o botão deve deixar o efeito claramente menor, em geral neutro; \
no máximo, claramente maior mas ainda utilizável.
- Cada parâmetro pode pertencer a um único macro.

## Idioma

Escreva `summary` e os nomes e descrições dos macros no idioma do pedido. O \
`summary` tem uma ou duas frases dizendo ao músico o que a cadeia faz e por quê.
"""


class IntentError(RuntimeError):
    """O modelo não devolveu uma cadeia utilizável."""


def build_user_message(prompt: str, features: dict | None) -> str:
    analysis = json.dumps(features, ensure_ascii=False, indent=1) if features else "indisponível"
    return f"<pedido>\n{prompt.strip()}\n</pedido>\n\n<analise_da_pista>\n{analysis}\n</analise_da_pista>"


def generate(prompt: str, features: dict | None = None, client: anthropic.Anthropic | None = None) -> ChainSpec:
    """Pede ao modelo uma cadeia para o pedido. O resultado ainda NÃO está validado."""
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
        messages=[{"role": "user", "content": build_user_message(prompt, features)}],
        output_format=LLMChain,
    )
    if response.stop_reason == "refusal":
        raise IntentError("o modelo recusou o pedido")
    if response.stop_reason == "max_tokens":
        raise IntentError("a resposta foi cortada antes de terminar")
    chain = response.parsed_output
    if chain is None:
        raise IntentError("a resposta não veio no formato esperado")
    return chain.to_spec(prompt)
