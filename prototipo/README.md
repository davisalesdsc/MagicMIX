# promptfx

Protótipo da ideia central do plugin: o músico descreve o som em texto, um LLM
propõe uma cadeia de efeitos, um validador determinístico a torna segura e o
áudio é processado offline. Serve para responder, sem escrever C++, à pergunta
"isso soa bem o bastante para virar produto?".

## Fluxo

```
pedido em texto ─┐
                 ├─> intent.py (LLM) ─> ChainSpec ─> validate.py ─> render.py ─> áudio
áudio de entrada ─> analyze.py ──────────────────────────┘ (limites dependem do sinal)
```

| Arquivo | Papel | Equivalente no plugin final |
|---|---|---|
| `promptfx/modules.py` | Catálogo de módulos, parâmetros e faixas | Biblioteca de módulos DSP em C++ |
| `promptfx/spec.py` | ChainSpec, o contrato em JSON | Preset, histórico e estado salvo na sessão |
| `promptfx/analyze.py` | Medidas do áudio com DSP clássico | Analisador na thread de trabalho |
| `promptfx/intent.py` | Prompt de sistema, critérios de mixagem e chamada ao LLM | Backend próprio |
| `promptfx/validate.py` | Faixas, limites e regras por sinal | Validador em C++ antes da thread de áudio |
| `promptfx/macros.py` | Um botão move vários parâmetros | Macros fixos expostos à DAW |
| `promptfx/render.py` | Processa o áudio com `pedalboard` | Motor de cadeia em tempo real |

## Instalação

Todos os comandos abaixo rodam de dentro da pasta `prototipo/`.

```bash
pip install -r requirements.txt
```

## Uso sem LLM

Gera os áudios sintéticos de teste (voz, bateria, baixo, pad e mix) na pasta `audio/`:

```bash
python scripts/make_test_audio.py
```

Aplica uma cadeia escrita à mão:

```bash
python -m promptfx render examples/voz_calorosa.json audio/voz_sintetica.wav -o audio/saida.wav
```

Gira os macros e iguala o volume para comparar com o original:

```bash
python -m promptfx render examples/voz_calorosa.json audio/voz_sintetica.wav -o audio/saida.wav -m Calor=1 -m Eco=0 --match-loudness
```

Mostra o validador corrigindo uma resposta absurda:

```bash
python -m promptfx render examples/exagerado.json audio/pad_brilhante.wav -o audio/saida.wav
```

Outros comandos: `python -m promptfx analyze <audio>` e `python -m promptfx catalog`.

## Uso com LLM

Precisa de uma chave da API da Anthropic na variável `ANTHROPIC_API_KEY`.

```bash
python -m promptfx run "voz calorosa, com eco de fita dos anos 70 e compressão sutil" minha_voz.wav -o saida.wav
```

O comando grava `saida.json` (o ChainSpec como o LLM devolveu) e `saida.wav`.
Depois dá para girar macros sem chamar o LLM de novo, com `render saida.json`.

O modelo padrão é `claude-opus-5-5`; troque com `PROMPTFX_MODEL` e ajuste o
esforço de raciocínio com `PROMPTFX_EFFORT` (`low`, `medium`, `high`).

### Conversa em sequência

Um novo pedido revisa a cadeia existente em vez de recriá-la. O LLM recebe a
cadeia atual e os pedidos anteriores, e muda só o que o novo pedido exige:

```bash
python -m promptfx revise saida.json "o eco está demais, deixa mais discreto e escuro" minha_voz.wav -o saida2.wav
```

A pasta `examples/gerados/conversa/` tem uma conversa de dois turnos.

### Estilo e andamento

- `--estilo artistico|tecnico|didatico` muda o nome dos macros e os textos. O
  `didatico` explica, módulo a módulo, o que foi feito e por quê.
- `--bpm 96` informa o andamento, e os delays saem em figuras musicais.

Além da cadeia e dos macros, a resposta traz um título, um resumo de como o
pedido foi entendido, os detalhes técnicos e duas sugestões de próximo pedido.

### Avaliar os casos de referência

`examples/referencia/casos_observados.json` tem dez casos pedido -> cadeia. O
script abaixo roda cada um pelo LLM e confere se a cadeia usa os módulos
esperados:

```bash
python scripts/avaliar_casos.py
```

## Uso com qualquer chat de IA, sem chave

O comando `prompt` imprime exatamente o texto que o LLM receberia:

```bash
python -m promptfx prompt "deixa esse pad mais brilhante e sonhador" audio/pad_brilhante.wav
```

Cole a saída num chat de IA, salve o JSON da resposta num arquivo e aplique:

```bash
python -m promptfx render resposta.json audio/pad_brilhante.wav -o audio/saida.wav --match-loudness
```

A pasta `examples/gerados/` tem três respostas obtidas assim, uma para cada
áudio de teste.

## Testes

```bash
python -m pytest -q
```

## O que este protótipo não é

- Não roda em tempo real nem dentro de uma DAW.
- Os módulos são simples de propósito: os do `pedalboard`, mais de-esser e
  transient shaper escritos em NumPy, que só funcionam offline. Faltam modelos
  de compressor, tipos de reverb, multibanda e largura estéreo, entre outros.
- O teto de segurança abaixa o arquivo inteiro quando um pico passa de
  -1 dBFS. No plugin esse papel é de um limitador com lookahead.
- Os limites em `validate.py` são pontos de partida, a calibrar ouvindo.
