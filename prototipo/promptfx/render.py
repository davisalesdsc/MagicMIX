"""Render offline: transforma um ChainSpec validado em áudio processado.

Aqui o `pedalboard` faz o papel que a biblioteca de módulos em C++ fará no
plugin. Só este arquivo conhece o `pedalboard`.
"""

from __future__ import annotations

from typing import Callable

import numpy as np
import pedalboard as pb
from pedalboard.io import AudioFile

from .macros import resolve
from .spec import ChainSpec

SAFETY_CEILING_DB = -1.0
TAIL_SECONDS = 2.0  # silêncio extra no fim para a cauda de reverb/delay


def _db_to_lin(db: float) -> float:
    return 10.0 ** (db / 20.0)


def _dry_wet(mix: float, wet: list[pb.Plugin]) -> pb.Plugin:
    """Soma o sinal seco com o processado, em paralelo."""
    mix = min(max(mix, 0.0), 1.0)
    floor = -120.0
    dry_db = 20.0 * np.log10(1.0 - mix) if mix < 1.0 else floor
    wet_db = 20.0 * np.log10(mix) if mix > 0.0 else floor
    return pb.Mix([
        pb.Chain([pb.Gain(max(dry_db, floor))]),
        pb.Chain([*wet, pb.Gain(max(wet_db, floor))]),
    ])


Processor = Callable[[np.ndarray, float], np.ndarray]


def _board(*plugins: pb.Plugin) -> Processor:
    board = pb.Pedalboard(list(plugins))
    return lambda audio, sample_rate: board(audio, sample_rate)


def _smooth(x: np.ndarray, sample_rate: float, ms: float) -> np.ndarray:
    """Média móvel ao longo do tempo (último eixo)."""
    n = max(1, int(sample_rate * ms / 1000.0))
    kernel = np.ones(n) / n
    return np.apply_along_axis(lambda v: np.convolve(v, kernel, mode="same"), -1, x)


def _level(audio: np.ndarray, sample_rate: float, ms: float) -> np.ndarray:
    """Envoltória de nível, uma só para todos os canais (formato 1 x amostras)."""
    power = np.max(np.square(audio, dtype=np.float64), axis=0, keepdims=True)
    return np.sqrt(_smooth(power, sample_rate, ms))


def _high_band(audio: np.ndarray, sample_rate: float, freq_hz: float) -> np.ndarray:
    """Parte do sinal acima de `freq_hz`, sem deslocar a fase (corte suave no espectro).

    Sem deslocamento de fase, `audio - agudos` dá exatamente os graves. No plugin
    em tempo real este papel é de um crossover de fase linear ou Linkwitz-Riley.
    """
    spectrum = np.fft.rfft(audio, axis=-1)
    freqs = np.fft.rfftfreq(audio.shape[-1], 1.0 / sample_rate)
    mask = 1.0 / (1.0 + (freq_hz / np.maximum(freqs, 1e-6)) ** 8)
    return np.fft.irfft(spectrum * mask, n=audio.shape[-1], axis=-1)


def _de_esser(p: dict[str, float]) -> Processor:
    def run(audio: np.ndarray, sample_rate: float) -> np.ndarray:
        # Separa os agudos, mede o nível deles e abaixa só essa faixa quando passa do limite.
        high = _high_band(audio, sample_rate, p["freq_hz"])
        low = audio - high
        over_db = 20.0 * np.log10(np.maximum(_level(high, sample_rate, 5.0), 1e-9)) - p["threshold_db"]
        reduction_db = np.minimum(np.clip(over_db, 0.0, None) * 0.75, p["max_reduction_db"])  # taxa 4:1
        gain = 10.0 ** (-_smooth(reduction_db, sample_rate, 10.0) / 20.0)
        return (low + high * gain).astype(np.float32)

    return run


def _transient_shaper(p: dict[str, float]) -> Processor:
    def run(audio: np.ndarray, sample_rate: float) -> np.ndarray:
        # Envoltória rápida acima da lenta = ataque; abaixo = sustentação.
        fast = _level(audio, sample_rate, 1.0) + 1e-6
        slow = _level(audio, sample_rate, 60.0) + 1e-6
        diff_db = 20.0 * np.log10(fast / slow)
        gain_db = p["attack_db"] * np.clip(diff_db / 6.0, 0.0, 1.0) + p["sustain_db"] * np.clip(-diff_db / 6.0, 0.0, 1.0)
        return (audio * 10.0 ** (_smooth(gain_db, sample_rate, 2.0) / 20.0)).astype(np.float32)

    return run


def _build(module_type: str, p: dict[str, float]) -> Processor:
    if module_type == "highpass":
        return _board(pb.HighpassFilter(p["freq_hz"]))
    if module_type == "lowpass":
        return _board(pb.LowpassFilter(p["freq_hz"]))
    if module_type == "low_shelf":
        return _board(pb.LowShelfFilter(p["freq_hz"], p["gain_db"], p["q"]))
    if module_type == "high_shelf":
        return _board(pb.HighShelfFilter(p["freq_hz"], p["gain_db"], p["q"]))
    if module_type == "peak":
        return _board(pb.PeakFilter(p["freq_hz"], p["gain_db"], p["q"]))
    if module_type == "compressor":
        wet = [pb.Compressor(p["threshold_db"], p["ratio"], p["attack_ms"], p["release_ms"]), pb.Gain(p["makeup_db"])]
        return _board(*wet) if p["mix"] >= 1.0 else _board(_dry_wet(p["mix"], wet))
    if module_type == "saturation":
        # O saturador (tanh) sobe o volume junto com o drive. Compensa a maior
        # parte para o botão mudar o timbre e não só a altura. A compensação
        # exata depende do nível de entrada; 0.75 é um meio-termo a calibrar.
        wet = [pb.Distortion(p["drive_db"]), pb.Gain(-0.75 * p["drive_db"])]
        return _board(_dry_wet(p["mix"], wet))
    if module_type == "tape":
        plugins = [pb.Distortion(p["drive_db"]), pb.Gain(-0.75 * p["drive_db"]), pb.LowpassFilter(p["tone_hz"])]
        if p["wow"] > 0.0:
            # Chorus 100% processado vira vibrato lento: a oscilação de afinação da fita.
            plugins.append(pb.Chorus(rate_hz=0.7, depth=0.15 * p["wow"], centre_delay_ms=7.0, feedback=0.0, mix=1.0))
        return _board(*plugins)
    if module_type == "delay":
        wet = [
            pb.Delay(p["time_ms"] / 1000.0, p["feedback"], 1.0),
            pb.HighpassFilter(p["highpass_hz"]),
            pb.LowpassFilter(p["lowpass_hz"]),
        ]
        return _board(_dry_wet(p["mix"], wet))
    if module_type == "reverb":
        return _board(pb.Reverb(p["size"], p["damping"], wet_level=p["mix"], dry_level=1.0 - p["mix"], width=p["width"]))
    if module_type == "chorus":
        return _board(pb.Chorus(rate_hz=p["rate_hz"], depth=p["depth"], mix=p["mix"]))
    if module_type == "de_esser":
        return _de_esser(p)
    if module_type == "transient_shaper":
        return _transient_shaper(p)
    if module_type == "gain":
        return _board(pb.Gain(p["gain_db"]))
    raise ValueError(f"módulo sem implementação de áudio: {module_type}")


def build_chain(spec: ChainSpec, macro_overrides: dict[str, float] | None = None) -> list[Processor]:
    """Um processador por módulo, na ordem do spec, com os macros aplicados."""
    params = resolve(spec, macro_overrides)
    return [_build(m.type, params[m.id]) for m in spec.modules]


def _rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64)))) if x.size else 0.0


def process(
    audio: np.ndarray,
    sample_rate: float,
    spec: ChainSpec,
    macro_overrides: dict[str, float] | None = None,
    match_loudness: bool = False,
) -> np.ndarray:
    """Processa `audio` (canais, amostras). Espera um spec já validado.

    Com `match_loudness`, o resultado sai com o mesmo volume médio da entrada,
    para a comparação A/B não ser enganada por "mais alto soa melhor".
    """
    audio = np.atleast_2d(np.asarray(audio, dtype=np.float32))
    dry_frames = audio.shape[1]
    tail = np.zeros((audio.shape[0], int(TAIL_SECONDS * sample_rate)), dtype=np.float32)
    out = np.concatenate([audio, tail], axis=1)
    for processor in build_chain(spec, macro_overrides):
        out = processor(out, sample_rate)

    if match_loudness:
        rms_in, rms_out = _rms(audio), _rms(out[:, :dry_frames])
        if rms_in > 0 and rms_out > 0:
            out = out * (rms_in / rms_out)

    # Teto de segurança: se algum pico passar, abaixa o arquivo inteiro. É
    # transparente (não muda o timbre), o que serve para avaliar a cadeia. O
    # limitador do pedalboard não serve aqui: ele soma ganho por conta própria.
    # No plugin em tempo real este papel é de um limitador com lookahead.
    ceiling = _db_to_lin(SAFETY_CEILING_DB)
    peak = float(np.abs(out).max()) if out.size else 0.0
    if peak > ceiling:
        out = out * (ceiling / peak)
    return out.astype(np.float32)


def read_audio(path: str) -> tuple[np.ndarray, float]:
    with AudioFile(path) as f:
        return f.read(f.frames), f.samplerate


def write_audio(path: str, audio: np.ndarray, sample_rate: float) -> None:
    with AudioFile(path, "w", sample_rate, audio.shape[0]) as f:
        f.write(audio)
