// Aplica uma cadeia (ChainSpec em JSON) a um arquivo de áudio, com o mesmo motor do plugin.
//
//   Renderizar <cadeia.json> <entrada.wav> <saida.wav> [--sidechain arquivo.wav]
//
// A saída sai alinhada com a entrada: o atraso da cadeia é descontado e a cauda é incluída.
// Imprime as correções do validador, o atraso e os picos.

#include <juce_audio_formats/juce_audio_formats.h>

#include "Ai/SpecJson.h"
#include "Engine/Analyzer.h"
#include "Engine/Chain.h"
#include "Engine/Validate.h"

using namespace mm;

static std::unique_ptr<juce::AudioBuffer<float>> ler (juce::AudioFormatManager& formatos, const juce::File& arquivo, double& taxa)
{
    std::unique_ptr<juce::AudioFormatReader> leitor (formatos.createReaderFor (arquivo));
    if (leitor == nullptr)
        return nullptr;
    taxa = leitor->sampleRate;
    auto buffer = std::make_unique<juce::AudioBuffer<float>> ((int) juce::jmin (2u, leitor->numChannels), (int) leitor->lengthInSamples);
    leitor->read (buffer.get(), 0, buffer->getNumSamples(), 0, true, buffer->getNumChannels() > 1);
    return buffer;
}

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf ("uso: Renderizar <cadeia.json> <entrada.wav> <saida.wav> [--sidechain arquivo.wav]\n");
        return 2;
    }
    const auto aqui = juce::File::getCurrentWorkingDirectory();
    const auto arquivoCadeia = aqui.getChildFile (juce::String::fromUTF8 (argv[1]));
    const auto entrada = aqui.getChildFile (juce::String::fromUTF8 (argv[2]));
    const auto saida = aqui.getChildFile (juce::String::fromUTF8 (argv[3]));
    juce::File arquivoSc;
    for (int i = 4; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--sidechain")
            arquivoSc = aqui.getChildFile (juce::String::fromUTF8 (argv[i + 1]));

    ChainSpec spec;
    juce::String erro;
    if (! json::fromString (arquivoCadeia.loadFileAsString(), spec, erro))
    {
        std::printf ("cadeia inválida: %s\n", erro.toRawUTF8());
        return 1;
    }

    juce::AudioFormatManager formatos;
    formatos.registerBasicFormats();
    double taxa = 0.0, taxaSc = 0.0;
    auto audio = ler (formatos, entrada, taxa);
    if (audio == nullptr)
    {
        std::printf ("não consegui ler %s\n", entrada.getFullPathName().toRawUTF8());
        return 1;
    }
    std::unique_ptr<juce::AudioBuffer<float>> sc;
    if (arquivoSc != juce::File())
    {
        sc = ler (formatos, arquivoSc, taxaSc);
        if (sc == nullptr || taxaSc != taxa)
        {
            std::printf ("sidechain ilegível ou com outra taxa de amostragem\n");
            return 1;
        }
    }

    const int canais = audio->getNumChannels(), total = audio->getNumSamples();
    const auto medidas = analyze (audio->getArrayOfReadPointers(), canais, total, taxa);
    std::vector<std::string> correcoes;
    const auto limpa = validate (spec, &medidas, correcoes);
    for (auto& c : correcoes)
        std::printf ("validador: %s\n", c.c_str());

    Chain cadeia (limpa);
    const int bloco = 512;
    cadeia.prepare (taxa, bloco, canais);
    const int atraso = cadeia.latencySamples(), cauda = (int) (taxa * 3.0);
    float macros[kMaxMacros] = {};
    for (size_t i = 0; i < limpa.macros.size() && i < (size_t) kMaxMacros; ++i)
        macros[i] = limpa.macros[i].value;

    juce::AudioBuffer<float> trabalho (canais, total + atraso + cauda);
    trabalho.clear();
    for (int c = 0; c < canais; ++c)
        trabalho.copyFrom (c, 0, *audio, c, 0, total);
    juce::AudioBuffer<float> scTrabalho;
    if (sc != nullptr)
    {
        scTrabalho.setSize (sc->getNumChannels(), trabalho.getNumSamples());
        scTrabalho.clear();
        for (int c = 0; c < sc->getNumChannels(); ++c)
            scTrabalho.copyFrom (c, 0, *sc, c, 0, juce::jmin (sc->getNumSamples(), total));
    }

    for (int inicio = 0; inicio < trabalho.getNumSamples(); inicio += bloco)
    {
        const int n = juce::jmin (bloco, trabalho.getNumSamples() - inicio);
        float* ch[2] = { trabalho.getWritePointer (0, inicio), canais > 1 ? trabalho.getWritePointer (1, inicio) : nullptr };
        const float* scp[2] = { sc != nullptr ? scTrabalho.getReadPointer (0, inicio) : nullptr,
                                sc != nullptr && scTrabalho.getNumChannels() > 1 ? scTrabalho.getReadPointer (1, inicio) : nullptr };
        cadeia.process (ch, canais, n, macros, limpa.mix, limpa.outputGainDb, sc != nullptr ? scp : nullptr, sc != nullptr ? scTrabalho.getNumChannels() : 0);
    }

    // descarta o atraso; mantém a cauda que ainda tiver som
    int fim = trabalho.getNumSamples();
    while (fim > atraso + total && trabalho.getMagnitude (fim - 512, 512) < 1.0e-5f)
        fim -= 512;
    juce::AudioBuffer<float> resultado (canais, fim - atraso);
    for (int c = 0; c < canais; ++c)
        resultado.copyFrom (c, 0, trabalho, c, atraso, fim - atraso);

    saida.deleteFile();
    std::unique_ptr<juce::OutputStream> fluxo (saida.createOutputStream());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> escritor (wav.createWriterFor (fluxo.get(), taxa, (unsigned) canais, 24, {}, 0));
    if (escritor == nullptr)
    {
        std::printf ("não consegui gravar %s\n", saida.getFullPathName().toRawUTF8());
        return 1;
    }
    fluxo.release();
    escritor->writeFromAudioSampleBuffer (resultado, 0, resultado.getNumSamples());
    std::printf ("atraso da cadeia: %d amostras; pico entrada %.1f dBFS, saída %.1f dBFS\n", atraso,
                 juce::Decibels::gainToDecibels (audio->getMagnitude (0, total)), juce::Decibels::gainToDecibels (resultado.getMagnitude (0, resultado.getNumSamples())));
    return 0;
}
