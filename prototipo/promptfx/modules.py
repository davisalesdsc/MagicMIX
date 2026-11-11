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
            "Compressor de dinâmica com ganho de compensação.",
            (
                Param("threshold_db", -50.0, 0.0, -18.0, "dBFS", "Nível a partir do qual comprime"),
                Param("ratio", 1.0, 10.0, 3.0, ":1", "Taxa de compressão", "log"),
                Param("attack_ms", 0.5, 100.0, 10.0, "ms", "Tempo de ataque", "log"),
                Param("release_ms", 20.0, 1000.0, 120.0, "ms", "Tempo de liberação", "log"),
                Param("makeup_db", 0.0, 12.0, 0.0, "dB", "Ganho de compensação pós-compressão"),
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
        Module("gain", "Ajuste de volume.", (Param("gain_db", -24.0, 12.0, 0.0, "dB", "Ganho"),)),
    )
}

MAX_MODULES = 8  # no plugin: número fixo de slots pré-alocados
MAX_MACROS = 4   # no plugin: parâmetros fixos expostos à DAW


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
