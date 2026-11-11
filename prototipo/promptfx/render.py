"""Render offline: transforma um ChainSpec validado em áudio processado.

Aqui o `pedalboard` faz o papel que a biblioteca de módulos em C++ fará no
plugin. Só este arquivo conhece o `pedalboard`.
"""

from __future__ import annotations

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


def _build(module_type: str, p: dict[str, float]) -> list[pb.Plugin]:
    if module_type == "highpass":
        return [pb.HighpassFilter(p["freq_hz"])]
    if module_type == "lowpass":
        return [pb.LowpassFilter(p["freq_hz"])]
    if module_type == "low_shelf":
        return [pb.LowShelfFilter(p["freq_hz"], p["gain_db"], p["q"])]
    if module_type == "high_shelf":
        return [pb.HighShelfFilter(p["freq_hz"], p["gain_db"], p["q"])]
    if module_type == "peak":
        return [pb.PeakFilter(p["freq_hz"], p["gain_db"], p["q"])]
    if module_type == "compressor":
        return [
            pb.Compressor(p["threshold_db"], p["ratio"], p["attack_ms"], p["release_ms"]),
            pb.Gain(p["makeup_db"]),
        ]
    if module_type == "saturation":
        # O saturador (tanh) sobe o volume junto com o drive. Compensa a maior
        # parte para o botão mudar o timbre e não só a altura. A compensação
        # exata depende do nível de entrada; 0.75 é um meio-termo a calibrar.
        wet = [pb.Distortion(p["drive_db"]), pb.Gain(-0.75 * p["drive_db"])]
        return [_dry_wet(p["mix"], wet)]
    if module_type == "delay":
        return [pb.Delay(p["time_ms"] / 1000.0, p["feedback"], p["mix"])]
    if module_type == "reverb":
        return [pb.Reverb(p["size"], p["damping"], wet_level=p["mix"], dry_level=1.0 - p["mix"], width=p["width"])]
    if module_type == "chorus":
        return [pb.Chorus(rate_hz=p["rate_hz"], depth=p["depth"], mix=p["mix"])]
    if module_type == "gain":
        return [pb.Gain(p["gain_db"])]
    raise ValueError(f"módulo sem implementação de áudio: {module_type}")


def build_board(spec: ChainSpec, macro_overrides: dict[str, float] | None = None) -> pb.Pedalboard:
    """Monta a cadeia na ordem do spec."""
    params = resolve(spec, macro_overrides)
    plugins: list[pb.Plugin] = []
    for m in spec.modules:
        plugins.extend(_build(m.type, params[m.id]))
    return pb.Pedalboard(plugins)


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
    out = build_board(spec, macro_overrides)(np.concatenate([audio, tail], axis=1), sample_rate)

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
