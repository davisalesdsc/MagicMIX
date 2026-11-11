# MagicMIX

Plugin de áudio em que o músico descreve o som em texto ("voz calorosa, com eco de fita e
compressão sutil") e recebe uma cadeia de efeitos editável, com botões macro, dentro da DAW.

**Estado atual: fase 1, protótipo em Python.** Ainda não existe plugin para instalar.

## Como funciona

```
pedido em texto ─┐
                 ├─> LLM ─> ChainSpec (JSON) ─> validador ─> cadeia de efeitos ─> áudio
áudio da pista ──┴─> análise ────────────────────┘ (limites dependem do sinal)
```

- A IA só interpreta o pedido e propõe a cadeia. Ela não processa nem gera áudio.
- Um validador determinístico confere e limita tudo o que a IA propõe antes de chegar ao áudio.
- O áudio é processado por módulos DSP próprios (EQ, compressor, saturação, delay, reverb...).

## O que tem aqui

```
prototipo/               fase 1: texto -> cadeia -> áudio, offline, em Python (veja o README de lá)
VERSAO                   versão do projeto (usada nos instaladores e nas Releases)
.github/workflows/       CI: a cada push na main, roda os testes
```

## Fases

1. **Protótipo em Python** (aqui): prova se a ideia soa bem, sem escrever C++.
2. **Plugin em C++ com JUCE**: VST3, AU e app, carregando um ChainSpec, com macros.
3. **Integração**: backend próprio chamando o LLM, ligado ao plugin.
4. **Adaptação ao sinal**: análise do áudio e limites que dependem dele, dentro do plugin.
5. **Produto**: visão avançada, presets e histórico.

## Releases

Seguem o modelo do [BandChatLIVE](https://github.com/davisalesdsc/BandChatLIVE): tudo acontece na
branch `main`. A cada push, o GitHub Actions compila, testa e monta os instaladores; com tudo
passando, cria ou atualiza a Release da versão escrita em `VERSAO`. Para lançar uma versão nova,
basta mudar o `VERSAO`.

Enquanto o projeto está na fase 1 não há instaladores, então o CI só roda os testes e nenhuma
Release é publicada. Os jobs de instalador e de Release entram junto com o plugin, na fase 2.
