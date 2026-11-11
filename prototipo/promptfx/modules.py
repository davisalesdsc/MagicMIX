"""Catálogo de módulos DSP: a fonte única de verdade do protótipo.

Tudo o que o LLM pode pedir e tudo o que o validador aceita sai daqui.
Este arquivo é só dados (sem dependência de áudio) de propósito: no plugin
em C++ o mesmo catálogo vira a lista de módulos e as faixas de parâmetro.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Param:
    name: str
    min: float
    max: float
    default: float
    unit: str
    description: str
    scale: str = "lin"  # "lin" ou "log" (frequências e tempos interpolam em log)


@dataclass(frozen=True)
class Module:
    type: str
    description: str
    params: tuple[Param, ...]

    def param(self, name: str) -> Param | None:
        return next((p for p in self.params if p.name == name), None)


def _freq(default: float, lo: float = 20.0, hi: float = 20000.0) -> Param:
    return Param("freq_hz", lo, hi, default, "Hz", "Frequência de corte/centro", "log")


_GAIN_EQ = Param("gain_db", -12.0, 12.0, 0.0, "dB", "Ganho da banda (negativo corta)")
_Q = Param("q", 0.3, 8.0, 0.707, "", "Largura da banda (maior = mais estreito)", "log")
_MIX = Param("mix", 0.0, 1.0, 0.25, "0-1", "Proporção do sinal processado")

MODULES: dict[str, Module] = {
    m.type: m
    for m in (
        Module("highpass", "Remove graves abaixo do corte (limpeza de rumble).", (_freq(80.0, 20.0, 1000.0),)),
        Module("lowpass", "Remove agudos acima do corte (escurece).", (_freq(12000.0, 500.0, 20000.0),)),
        Module("low_shelf", "Sobe ou desce todos os graves abaixo da frequência.", (_freq(120.0, 40.0, 800.0), _GAIN_EQ, _Q)),
        Module("high_shelf", "Sobe ou desce todos os agudos acima da frequência (brilho/ar).", (_freq(8000.0, 1500.0, 16000.0), _GAIN_EQ, _Q)),
        Module("peak", "Banda paramétrica: realça ou atenua em torno de uma frequência.", (_freq(1000.0), _GAIN_EQ, _Q)),
        Module(
            "compressor",
            "Compressor de dinâmica com ganho de compensação e mistura paralela.",
            (
                Param("threshold_db", -50.0, 0.0, -18.0, "dBFS", "Nível a partir do qual comprime"),
                Param("ratio", 1.0, 10.0, 3.0, ":1", "Taxa de compressão", "log"),
                Param("attack_ms", 0.5, 100.0, 10.0, "ms", "Tempo de ataque", "log"),
                Param("release_ms", 20.0, 1000.0, 120.0, "ms", "Tempo de liberação", "log"),
                Param("makeup_db", 0.0, 12.0, 0.0, "dB", "Ganho de compensação pós-compressão"),
                Param("mix", 0.0, 1.0, 1.0, "0-1", "Proporção do sinal comprimido (menos de 1 = compressão paralela)"),
            ),
        ),
        Module(
            "saturation",
            "Saturação harmônica suave (calor de fita/válvula), com volume compensado.",
            (
                Param("drive_db", 0.0, 24.0, 6.0, "dB", "Quanto o sinal é empurrado contra o saturador"),
                _MIX,
            ),
        ),
        Module(
            "delay",
            "Eco com realimentação.",
            (
                Param("time_ms", 20.0, 1500.0, 350.0, "ms", "Tempo entre repetições", "log"),
                Param("feedback", 0.0, 0.85, 0.3, "0-1", "Quanto de cada repetição volta para a entrada"),
                Param("highpass_hz", 20.0, 2000.0, 20.0, "Hz", "Corta os graves só das repetições", "log"),
                Param("lowpass_hz", 1000.0, 20000.0, 20000.0, "Hz", "Corta os agudos só das repetições (eco escuro)", "log"),
                Param("ping_pong", 0.0, 1.0, 0.0, "0 ou 1", "1 = as repetições alternam entre esquerda e direita"),
                _MIX,
            ),
        ),
        Module(
            "reverb",
            "Reverberação algorítmica (espaço/ambiente).",
            (
                Param("size", 0.0, 1.0, 0.5, "0-1", "Tamanho da sala"),
                Param("damping", 0.0, 1.0, 0.5, "0-1", "Absorção de agudos na cauda (maior = mais escuro)"),
                Param("width", 0.0, 1.0, 1.0, "0-1", "Abertura estéreo da cauda"),
                Param("pre_delay_ms", 0.0, 200.0, 0.0, "ms", "Atraso antes da reverberação começar (separa a fonte da cauda)"),
                _MIX,
            ),
        ),
        Module(
            "chorus",
            "Modulação que engrossa e alarga o som.",
            (
                Param("rate_hz", 0.05, 8.0, 1.0, "Hz", "Velocidade da modulação", "log"),
                Param("depth", 0.0, 1.0, 0.25, "0-1", "Profundidade da modulação"),
                _MIX,
            ),
        ),
        Module(
            "de_esser",
            "Atenua sibilância (sons de S e T) só quando ela passa do limite.",
            (
                Param("freq_hz", 3000.0, 10000.0, 6500.0, "Hz", "A partir de onde procura sibilância", "log"),
                Param("threshold_db", -60.0, 0.0, -30.0, "dBFS", "Nível da sibilância a partir do qual atenua"),
                Param("max_reduction_db", 0.0, 12.0, 6.0, "dB", "Atenuação máxima"),
            ),
        ),
        Module(
            "transient_shaper",
            "Realça ou suaviza o ataque e a sustentação das notas, sem depender do volume.",
            (
                Param("attack_db", -12.0, 12.0, 0.0, "dB", "Ganho aplicado no ataque (positivo = mais estalo)"),
                Param("sustain_db", -12.0, 12.0, 0.0, "dB", "Ganho aplicado na sustentação (negativo = mais seco)"),
            ),
        ),
        Module(
            "tape",
            "Caráter de gravador de fita: saturação, perda de agudos e leve oscilação de afinação.",
            (
                Param("drive_db", 0.0, 18.0, 6.0, "dB", "Quanto o sinal satura a fita"),
                Param("tone_hz", 3000.0, 20000.0, 14000.0, "Hz", "Até onde os agudos chegam (fita lenta = menor)", "log"),
                Param("wow", 0.0, 1.0, 0.1, "0-1", "Oscilação de afinação (0 = fita perfeita)"),
            ),
        ),
        Module(
            "band_compressor",
            "Comprime só uma faixa de frequências (compressão multibanda de uma banda, ou EQ dinâmico).",
            (
                Param("low_hz", 20.0, 20000.0, 100.0, "Hz", "Início da faixa", "log"),
                Param("high_hz", 20.0, 20000.0, 400.0, "Hz", "Fim da faixa", "log"),
                Param("threshold_db", -60.0, 0.0, -24.0, "dBFS", "Nível da faixa a partir do qual comprime"),
                Param("ratio", 1.0, 10.0, 3.0, ":1", "Taxa de compressão", "log"),
                Param("attack_ms", 0.5, 100.0, 10.0, "ms", "Tempo de ataque", "log"),
                Param("release_ms", 20.0, 1000.0, 120.0, "ms", "Tempo de liberação", "log"),
            ),
        ),
        Module(
            "exciter",
            "Cria harmônicos novos acima de uma frequência (brilho que o EQ não consegue, porque não estava lá).",
            (
                Param("freq_hz", 200.0, 10000.0, 4000.0, "Hz", "A partir de onde gera harmônicos (baixo = médios novos num sub)", "log"),
                Param("drive_db", 0.0, 24.0, 9.0, "dB", "Intensidade da geração de harmônicos"),
                Param("amount", 0.0, 1.0, 0.2, "0-1", "Quanto dos harmônicos é somado ao som"),
            ),
        ),
        Module(
            "clipper",
            "Corta os picos de forma suave: mais volume percebido sem subir o medidor, e ataque menos pontudo.",
            (
                Param("ceiling_db", -24.0, 0.0, -6.0, "dBFS", "Nível máximo dos picos"),
                Param("softness", 0.0, 1.0, 0.5, "0-1", "0 = corte seco, 1 = bem arredondado"),
            ),
        ),
        Module(
            "stereo_width",
            "Abre ou fecha a imagem estéreo. Só age em material que já é estéreo.",
            (Param("width", 0.0, 2.0, 1.3, "x", "0 = mono, 1 = original, 2 = lados dobrados"),),
        ),
        Module("gain", "Ajuste de volume.", (Param("gain_db", -24.0, 12.0, 0.0, "dB", "Ganho"),)),
    )
}

MAX_MODULES = 12  # no plugin: número fixo de slots pré-alocados (cada banda de EQ ocupa um)
MAX_MACROS = 10  # no plugin: parâmetros fixos expostos à DAW


def catalog_text() -> str:
    """Catálogo em texto, do jeito que é mostrado ao LLM."""
    lines: list[str] = []
    for m in MODULES.values():
        lines.append(f"- {m.type}: {m.description}")
        for p in m.params:
            unit = f" {p.unit}" if p.unit else ""
            log = ", escala log" if p.scale == "log" else ""
            lines.append(f"    {p.name}: {p.min:g} a {p.max:g}{unit} (padrão {p.default:g}{log}) - {p.description}")
    return "\n".join(lines)


def catalog_json() -> list[dict]:
    """Catálogo em JSON, para alimentar no futuro o lado C++."""
    return [
        {
            "type": m.type,
            "description": m.description,
            "params": [p.__dict__ for p in m.params],
        }
        for m in MODULES.values()
    ]
