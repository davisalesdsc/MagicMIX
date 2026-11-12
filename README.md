# MagicMIX: plugin VST3 / AU / app

Um assistente de mixagem que conversa com a sua pista. Cada pista com o MagicMIX ganha uma cadeia de
efeitos própria (EQ, compressores, Bus Comp, de-esser, supressor de ressonâncias, saturação...), com
um botão **Intensidade** que leva o som de discreto a expressivo, e tudo pode ser ajustado à mão no
modo **Avançado**.

Tudo roda no computador: sem conta, sem chave de API, e o áudio nunca sai da máquina.

![Interface do MagicMIX](docs/interface.png)

## Instalar

**Mac** (Apple Silicon ou Intel, macOS 11 ou mais novo): dê dois cliques em `MagicMIX-x.y-macOS.pkg`.
É um instalador só, que serve para os dois tipos de Mac.
**Windows** (10/11, 64 bits): dê dois cliques em `MagicMIX-x.y-Windows-Setup.exe`.

| Componente | macOS | Windows |
|---|---|---|
| Plugin VST3 (REAPER, Ableton Live, Cubase, Studio One...) | sim | sim |
| Plugin AU (Logic, MainStage, GarageBand) | sim | não existe AU no Windows |
| App sem DAW (usa a entrada da placa de som) | sim | sim |

Depois peça ao seu programa para **procurar plugins de novo**.

> **Enquanto o instalador não for assinado pela Apple/Microsoft:**
> - macOS: se aparecer "não pode ser aberto porque é de um desenvolvedor não identificado",
>   abra Ajustes do Sistema > Privacidade e Segurança e clique em **Abrir Mesmo Assim**.
> - Windows: se aparecer "O Windows protegeu o computador", clique em **Mais informações** >
>   **Executar assim mesmo**.

## Primeiro uso

1. Coloque o MagicMIX na pista e **dê play**, para o plugin ouvir o som.
2. Escolha um tratamento na **Biblioteca** (ou numa das sugestões da conversa). São cadeias aprovadas
   em escutas com gravações reais; o volume de trabalho é acertado para a sua pista.
3. Gire o botão **Intensidade** (40 % é o ponto aprovado) e use **Comparar com o original** para
   ouvir a diferença com o mesmo volume.

## Como usar

- **Pistas**: a coluna da esquerda mostra todas as pistas do projeto com o MagicMIX, agrupadas pelo
  nome (bateria, vozes, baixo...). Clique numa para abrir a pista na mesma janela.
- **Fluxo**: a cadeia e os botões dela. Os botões aparecem para a DAW como *Macro 1* a *Macro 10* e
  podem ser automatizados.
- **Avançado**: clique num efeito da cadeia para abrir o desenho dele: EQ de 8 bandas com analisador
  (arraste os pontos), compressor com 5 caracteres (Limpo, Óptico, FET, VCA, Vari-mu), Bus Comp,
  de-esser, supressor de ressonâncias (empurre a curva amarela onde ele deve prestar atenção) e
  knobs para os demais. Cada efeito tem a sua chave liga/desliga.
- **Intensidade e ajuste à mão**: se você ajustar um efeito, o botão Intensidade passa a crescer ou
  diminuir a partir do seu ajuste.
- **WET e OUTPUT**: misturam o tratamento com o som original e ajustam o volume final.
- **Sidechain**: o compressor e o Bus Comp podem ouvir outra pista pela entrada de sidechain.
- **Desfazer** (ou Ctrl/Cmd+Z): volta o último ajuste.

## Limitações desta versão

- Pedidos escritos ainda não são atendidos: o cérebro que entende texto é a próxima etapa.
- Uma cadeia por pista.

## Efeitos disponíveis

Filtros e equalização (passa-altas, passa-baixas, shelves e banda paramétrica), compressor com
mistura paralela, compressor de uma banda, de-esser, transient shaper, saturação, fita, exciter,
clipper suave, delay (com filtros e ping-pong), reverb (com pre-delay), chorus, largura estéreo e ganho.

---

## Para desenvolvedores

C++17 com [JUCE 8](https://juce.com), baixado automaticamente pelo CMake.

```
Source/
  Engine/                 motor de áudio em C++ puro (sem JUCE), em tempo real
    Dsp.h                 filtros, crossover, envoltória, linha de atraso, reverb
    Modules.*             um módulo por tipo de efeito
    Chain.*               cadeia compilada: macros, mix, ganho e limitador de segurança
    Validate.*            validador: nada da IA chega ao áudio sem passar por ele
    Analyzer.*            medidas da pista
    Catalog.*             catálogo de módulos (dados gerados, veja abaixo)
    Edicao.*              ajuste manual de parâmetros, re-ancorando os macros
  Ai/SpecJson.*           JSON do ChainSpec
  Cerebro/                cérebro local: biblioteca de cadeias aprovadas (e, em breve, os pedidos em texto)
  Ui/                     desenho dos knobs e faces dos efeitos (EQ, compressor, Bus Comp...)
  Fontes/                 Manrope, Unbounded e IBM Plex Mono (licença OFL)
  Sessao.*                as instâncias do projeto se enxergam (coluna de pistas)
  PluginProcessor.*       host, troca de cadeia sem clique, comparar, estado
  PluginEditor.*          interface
installer/
  macos/build-pkg.sh      monta o .pkg universal (VST3 + AU + app + desinstalador)
  windows/MagicMIX.iss    Setup.exe (Inno Setup)
tests/
  EngineTests.cpp         motor: cada módulo, validador, e a prova de que process() não aloca memória
  PluginTests.cpp         JSON, biblioteca, ajustes manuais e o processador rodando áudio
                          (PluginTests --retratos <pasta> [audio.wav] salva imagens da interface)
  HostTest.cpp            carrega o VST3/AU compilado como um DAW
cerebro/                  decisor (mede a pista e decide a cadeia), cadeias aprovadas e avaliações
prototipo/                protótipo em Python da primeira versão
```

Compilar e testar (macOS: Xcode; Windows: Visual Studio 2022 ou mais novo; CMake 3.22+):

```bash
cmake -S . -B build -DMM_BUILD_TESTS=ON
```

```bash
cmake --build build --config Release
```

O catálogo de módulos é mantido à mão em `Source/Engine/CatalogData.inc`.

A variável de ambiente `MAGICMIX_CADEIA_INICIAL` aponta para um arquivo JSON com uma cadeia que o
plugin carrega ao abrir. Serve para testes e demonstrações.

### Releases

Seguem o modelo do [BandChatLIVE](https://github.com/davisalesdsc/BandChatLIVE). O GitHub Actions
(`.github/workflows/build.yml`) faz tudo a cada push na `main`: compila e testa no Mac, no Windows e
no Linux; monta o `.pkg` universal e o `Setup.exe`; **instala de verdade** e confere (no Mac roda o
`auval` da Apple no AU instalado). Com tudo passando, publica os instaladores em **Releases**, na
versão escrita em `VERSAO`. Para lançar uma versão nova, basta mudar o `VERSAO`.

**Assinatura Apple (opcional):** com conta Apple Developer, cadastre em *Settings > Secrets and
variables > Actions*: `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID` e
`APPLE_APP_PASSWORD`. Com isso o `.pkg` sai assinado e notarizado, sem aviso ao abrir.
