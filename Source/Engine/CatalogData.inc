// GERADO por prototipo/scripts/exportar_para_plugin.py. Não edite à mão.

#define MM_MAX_MODULES 12
#define MM_MAX_MACROS 10
#define MM_MAX_SUGGESTIONS 3
#define MM_BRIGHT_SOURCE_MAX_BOOST_DB 6.0f
#ifdef MM_CATALOG_ENTRIES
{ "highpass", "Remove graves abaixo do corte (limpeza de rumble).", {
    { "freq_hz", 20.0f, 1000.0f, 80.0f, "Hz", "Frequência de corte/centro", true },
} },
{ "lowpass", "Remove agudos acima do corte (escurece).", {
    { "freq_hz", 500.0f, 20000.0f, 12000.0f, "Hz", "Frequência de corte/centro", true },
} },
{ "low_shelf", "Sobe ou desce todos os graves abaixo da frequência.", {
    { "freq_hz", 40.0f, 800.0f, 120.0f, "Hz", "Frequência de corte/centro", true },
    { "gain_db", -12.0f, 12.0f, 0.0f, "dB", "Ganho da banda (negativo corta)", false },
    { "q", 0.3f, 8.0f, 0.707f, "", "Largura da banda (maior = mais estreito)", true },
} },
{ "high_shelf", "Sobe ou desce todos os agudos acima da frequência (brilho/ar).", {
    { "freq_hz", 1500.0f, 16000.0f, 8000.0f, "Hz", "Frequência de corte/centro", true },
    { "gain_db", -12.0f, 12.0f, 0.0f, "dB", "Ganho da banda (negativo corta)", false },
    { "q", 0.3f, 8.0f, 0.707f, "", "Largura da banda (maior = mais estreito)", true },
} },
{ "peak", "Banda paramétrica: realça ou atenua em torno de uma frequência.", {
    { "freq_hz", 20.0f, 20000.0f, 1000.0f, "Hz", "Frequência de corte/centro", true },
    { "gain_db", -12.0f, 12.0f, 0.0f, "dB", "Ganho da banda (negativo corta)", false },
    { "q", 0.3f, 8.0f, 0.707f, "", "Largura da banda (maior = mais estreito)", true },
} },
{ "compressor", "Compressor de dinâmica com ganho de compensação e mistura paralela.", {
    { "threshold_db", -50.0f, 0.0f, -18.0f, "dBFS", "Nível a partir do qual comprime", false },
    { "ratio", 1.0f, 10.0f, 3.0f, ":1", "Taxa de compressão", true },
    { "attack_ms", 0.5f, 100.0f, 10.0f, "ms", "Tempo de ataque", true },
    { "release_ms", 20.0f, 1000.0f, 120.0f, "ms", "Tempo de liberação", true },
    { "makeup_db", 0.0f, 12.0f, 0.0f, "dB", "Ganho de compensação pós-compressão", false },
    { "mix", 0.0f, 1.0f, 1.0f, "0-1", "Proporção do sinal comprimido (menos de 1 = compressão paralela)", false },
} },
{ "saturation", "Saturação harmônica suave (calor de fita/válvula), com volume compensado.", {
    { "drive_db", 0.0f, 24.0f, 6.0f, "dB", "Quanto o sinal é empurrado contra o saturador", false },
    { "mix", 0.0f, 1.0f, 0.25f, "0-1", "Proporção do sinal processado", false },
} },
{ "delay", "Eco com realimentação.", {
    { "time_ms", 20.0f, 1500.0f, 350.0f, "ms", "Tempo entre repetições", true },
    { "feedback", 0.0f, 0.85f, 0.3f, "0-1", "Quanto de cada repetição volta para a entrada", false },
    { "highpass_hz", 20.0f, 2000.0f, 20.0f, "Hz", "Corta os graves só das repetições", true },
    { "lowpass_hz", 1000.0f, 20000.0f, 20000.0f, "Hz", "Corta os agudos só das repetições (eco escuro)", true },
    { "ping_pong", 0.0f, 1.0f, 0.0f, "0 ou 1", "1 = as repetições alternam entre esquerda e direita", false },
    { "mix", 0.0f, 1.0f, 0.25f, "0-1", "Proporção do sinal processado", false },
} },
{ "reverb", "Reverberação algorítmica (espaço/ambiente).", {
    { "size", 0.0f, 1.0f, 0.5f, "0-1", "Tamanho da sala", false },
    { "damping", 0.0f, 1.0f, 0.5f, "0-1", "Absorção de agudos na cauda (maior = mais escuro)", false },
    { "width", 0.0f, 1.0f, 1.0f, "0-1", "Abertura estéreo da cauda", false },
    { "pre_delay_ms", 0.0f, 200.0f, 0.0f, "ms", "Atraso antes da reverberação começar (separa a fonte da cauda)", false },
    { "mix", 0.0f, 1.0f, 0.25f, "0-1", "Proporção do sinal processado", false },
} },
{ "chorus", "Modulação que engrossa e alarga o som.", {
    { "rate_hz", 0.05f, 8.0f, 1.0f, "Hz", "Velocidade da modulação", true },
    { "depth", 0.0f, 1.0f, 0.25f, "0-1", "Profundidade da modulação", false },
    { "mix", 0.0f, 1.0f, 0.25f, "0-1", "Proporção do sinal processado", false },
} },
{ "de_esser", "Atenua sibilância (sons de S e T) só quando ela passa do limite.", {
    { "freq_hz", 3000.0f, 10000.0f, 6500.0f, "Hz", "A partir de onde procura sibilância", true },
    { "threshold_db", -60.0f, 0.0f, -30.0f, "dBFS", "Nível da sibilância a partir do qual atenua", false },
    { "max_reduction_db", 0.0f, 12.0f, 6.0f, "dB", "Atenuação máxima", false },
} },
{ "transient_shaper", "Realça ou suaviza o ataque e a sustentação das notas, sem depender do volume.", {
    { "attack_db", -12.0f, 12.0f, 0.0f, "dB", "Ganho aplicado no ataque (positivo = mais estalo)", false },
    { "sustain_db", -12.0f, 12.0f, 0.0f, "dB", "Ganho aplicado na sustentação (negativo = mais seco)", false },
} },
{ "tape", "Caráter de gravador de fita: saturação, perda de agudos e leve oscilação de afinação.", {
    { "drive_db", 0.0f, 18.0f, 6.0f, "dB", "Quanto o sinal satura a fita", false },
    { "tone_hz", 3000.0f, 20000.0f, 14000.0f, "Hz", "Até onde os agudos chegam (fita lenta = menor)", true },
    { "wow", 0.0f, 1.0f, 0.1f, "0-1", "Oscilação de afinação (0 = fita perfeita)", false },
} },
{ "band_compressor", "Comprime só uma faixa de frequências (compressão multibanda de uma banda, ou EQ dinâmico).", {
    { "low_hz", 20.0f, 20000.0f, 100.0f, "Hz", "Início da faixa", true },
    { "high_hz", 20.0f, 20000.0f, 400.0f, "Hz", "Fim da faixa", true },
    { "threshold_db", -60.0f, 0.0f, -24.0f, "dBFS", "Nível da faixa a partir do qual comprime", false },
    { "ratio", 1.0f, 10.0f, 3.0f, ":1", "Taxa de compressão", true },
    { "attack_ms", 0.5f, 100.0f, 10.0f, "ms", "Tempo de ataque", true },
    { "release_ms", 20.0f, 1000.0f, 120.0f, "ms", "Tempo de liberação", true },
} },
{ "exciter", "Cria harmônicos novos acima de uma frequência (brilho que o EQ não consegue, porque não estava lá).", {
    { "freq_hz", 200.0f, 10000.0f, 4000.0f, "Hz", "A partir de onde gera harmônicos (baixo = médios novos num sub)", true },
    { "drive_db", 0.0f, 24.0f, 9.0f, "dB", "Intensidade da geração de harmônicos", false },
    { "amount", 0.0f, 1.0f, 0.2f, "0-1", "Quanto dos harmônicos é somado ao som", false },
} },
{ "clipper", "Corta os picos de forma suave: mais volume percebido sem subir o medidor, e ataque menos pontudo.", {
    { "ceiling_db", -24.0f, 0.0f, -6.0f, "dBFS", "Nível máximo dos picos", false },
    { "softness", 0.0f, 1.0f, 0.5f, "0-1", "0 = corte seco, 1 = bem arredondado", false },
} },
{ "stereo_width", "Abre ou fecha a imagem estéreo. Só age em material que já é estéreo.", {
    { "width", 0.0f, 2.0f, 1.3f, "x", "0 = mono, 1 = original, 2 = lados dobrados", false },
} },
{ "gain", "Ajuste de volume.", {
    { "gain_db", -24.0f, 12.0f, 0.0f, "dB", "Ganho", false },
} },
#endif
