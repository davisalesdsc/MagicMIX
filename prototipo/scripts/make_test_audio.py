"""Gera áudios sintéticos para testar o protótipo sem depender de gravações.

Uso: python scripts/make_test_audio.py [pasta_de_saida]
São sinais de laboratório; para julgar o som, use pistas reais.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from pedalboard.io import AudioFile

SR = 44100


def _normalize(x: np.ndarray, peak_dbfs: float) -> np.ndarray:
    return (x / np.abs(x).max() * 10 ** (peak_dbfs / 20)).astype(np.float32)


def voice() -> np.ndarray:
    """Tom tipo voz: harmônicos moldados por formantes, com vibrato. Dinâmico e meio abafado."""
    notes = [196.0, 220.0, 261.6, 246.9, 220.0, 196.0]
    note_len = int(0.7 * SR)
    formants = [(700, 130), (1200, 150), (2600, 250)]
    out = []
    for f0 in notes:
        t = np.arange(note_len) / SR
        phase = 2 * np.pi * f0 * t + 0.6 * np.sin(2 * np.pi * 5.5 * t)
        note = np.zeros(note_len)
        for k in range(1, int(8000 / f0)):
            freq = k * f0
            weight = sum(np.exp(-0.5 * ((freq - fc) / bw) ** 2) for fc, bw in formants) + 0.02
            note += weight / k * np.sin(k * phase)
        envelope = np.minimum(t / 0.05, 1.0) * np.exp(-1.5 * t)
        out.append(note * envelope)
    return _normalize(np.concatenate(out), -9.0)[None, :]


def drums() -> np.ndarray:
    """Batida simples: bumbo, caixa e chimbal. Muito transiente."""
    rng = np.random.default_rng(1)
    beat = int(0.5 * SR)
    x = np.zeros(8 * beat)

    def add(start: int, sound: np.ndarray) -> None:
        x[start : start + sound.size] += sound[: x.size - start]

    t = np.arange(int(0.3 * SR)) / SR
    kick = np.sin(2 * np.pi * (50 * t + 70 / 30 * (1 - np.exp(-30 * t)))) * np.exp(-12 * t)
    snare = (0.6 * rng.standard_normal(t.size) + 0.4 * np.sin(2 * np.pi * 190 * t)) * np.exp(-22 * t)
    th = np.arange(int(0.06 * SR)) / SR
    hat = np.diff(rng.standard_normal(th.size + 1)) * np.exp(-70 * th) * 0.25
    for b in range(8):
        add(b * beat, kick if b % 2 == 0 else snare)
        add(b * beat, hat)
        add(b * beat + beat // 2, hat)
    return _normalize(x, -6.0)[None, :]


def bright_pad() -> np.ndarray:
    """Acorde de dente de serra em estéreo: fonte que já é brilhante."""
    rng = np.random.default_rng(2)
    t = np.arange(4 * SR) / SR
    channels = []
    for detune in (0.997, 1.003):
        ch = np.zeros(t.size)
        for f0 in (220.0, 277.2, 329.6, 440.0):
            f = f0 * detune
            for k in range(1, int(16000 / f)):
                # Fases aleatórias evitam um pico gigante no início.
                ch += np.sin(2 * np.pi * k * f * t + rng.uniform(0, 2 * np.pi)) / k**0.3
        channels.append(ch * np.minimum(t / 0.3, 1.0))
    return _normalize(np.stack(channels), -3.0)


def main() -> None:
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "audio")
    folder.mkdir(parents=True, exist_ok=True)
    for name, audio in (("voz_sintetica", voice()), ("bateria_sintetica", drums()), ("pad_brilhante", bright_pad())):
        path = folder / f"{name}.wav"
        with AudioFile(str(path), "w", SR, audio.shape[0]) as f:
            f.write(audio)
        print(f"gravado: {path}")


if __name__ == "__main__":
    main()
