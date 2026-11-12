#include "Nomes.h"

#include "Texto.h"

namespace mm
{
juce::String nomeDoModulo (const ModuleSpec& m)
{
    // tabela UTF-8: passa por tx() no uso
    static const std::pair<const char*, const char*> nomes[] = {
        { "highpass", "Passa-altas" }, { "lowpass", "Passa-baixas" }, { "low_shelf", "Prateleira grave" },
        { "high_shelf", "Prateleira aguda" }, { "peak", "Sino" }, { "eq", "EQ" }, { "compressor", "Compressor" },
        { "bus_compressor", "Bus Comp" }, { "saturation", "Saturação" }, { "delay", "Eco" }, { "reverb", "Reverb" },
        { "chorus", "Chorus" }, { "de_esser", "De-esser" }, { "resonance", "Ressonâncias" }, { "transient_shaper", "Transientes" },
        { "tape", "Fita" }, { "band_compressor", "Compressor de banda" }, { "exciter", "Exciter" }, { "clipper", "Clipper" },
        { "stereo_width", "Largura estéreo" }, { "gain", "Volume" } };
    juce::String nome = tx (m.type);
    for (auto& [tipo, texto] : nomes)
        if (m.type == tipo)
            nome = tx (texto);
    if (m.type == "compressor")
    {
        // tabela UTF-8: passa por tx() no uso
        static const char* const caracteres[] = { "Limpo", "Óptico", "FET", "VCA", "Vari-mu" };
        const auto it = m.params.find ("model");
        const int c = it != m.params.end() ? juce::jlimit (0, 4, (int) it->second) : 0;
        if (c > 0) nome << " " << tx (caracteres[c]);
    }
    if (m.type == "gain" && m.id == "ganho")
        nome = tx ("Volume de trabalho");
    return nome;
}

juce::String rotuloDoParametro (const std::string& n)
{
    // tabela UTF-8: passa por tx() no uso
    static const std::pair<const char*, const char*> rotulos[] = {
        { "threshold_db", "Limite" }, { "ratio", "Taxa" }, { "attack_ms", "Ataque" }, { "release_ms", "Liberação" },
        { "release_s", "Liberação" }, { "makeup_db", "Ganho" }, { "mix", "Mix" }, { "sc_hpf_hz", "Filtro do detector" },
        { "freq_hz", "Frequência" }, { "gain_db", "Ganho" }, { "q", "Largura" }, { "depth_db", "Profundidade" },
        { "sharpness", "Nitidez" }, { "selectivity", "Seletividade" }, { "max_reduction_db", "Redução máx." },
        { "drive_db", "Drive" }, { "time_ms", "Tempo" }, { "feedback", "Repetições" }, { "highpass_hz", "Corte grave" },
        { "lowpass_hz", "Corte agudo" }, { "ping_pong", "Pingue-pongue" }, { "size", "Tamanho" }, { "damping", "Abafar" },
        { "width", "Largura" }, { "pre_delay_ms", "Pré-atraso" }, { "rate_hz", "Velocidade" }, { "depth", "Profundidade" },
        { "attack_db", "Ataque" }, { "sustain_db", "Sustentação" }, { "tone_hz", "Tom" }, { "wow", "Oscilação" },
        { "amount", "Quantidade" }, { "ceiling_db", "Teto" }, { "softness", "Suavidade" }, { "low_hz", "De" },
        { "high_hz", "Até" }, { "model", "Caráter" }, { "sidechain", "Quem controla" }, { "mode", "Modo" },
        { "listen", "Ouvir" }, { "delta", "Ouvir o que sai" } };
    for (auto& [nome, texto] : rotulos)
        if (n == nome)
            return tx (texto);
    return tx (n);
}

juce::String descreverCadeia (const ChainSpec& spec)
{
    juce::StringArray nomes;
    for (auto& m : spec.modules)
        nomes.add (nomeDoModulo (m) + (m.enabled ? juce::String() : tx (" (desligado)")));
    return nomes.joinIntoString (juce::String::fromUTF8 (" \xe2\x86\x92 "));
}
}
