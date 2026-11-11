"""Análise do áudio de entrada com DSP clássico (sem rede neural).

As medidas vão para o LLM (para ele adaptar a cadeia ao material) e para o
validador (para os limites dependerem do sinal).
"""

from __future__ import annotations

import numpy as np

# Bandas usadas para descrever o equilíbrio tonal.
BANDS: dict[str, tuple[float, float]] = {
    "sub": (20, 80),
    "low": (80, 250),
    "low_mid": (250, 800),
    "mid": (800, 2500),
    "presence": (2500, 6000),
    "air": (6000, 20000),
}

_EPS = 1e-12


def _db(x: float) -> float:
    return float(20.0 * np.log10(max(x, _EPS)))


def _resonances(freqs: np.ndarray, power: np.ndarray, limit: int = 3, min_db: float = 6.0) -> list[dict]:
    """Picos estreitos que sobram acima do contorno geral do espectro.

    São candidatas a "frequência problema". É uma heurística: em fonte tonal,
    as notas mais tocadas também aparecem como pico.
    """
    if power.sum() <= _EPS:
        return []
    per_octave = 48
    grid = 80.0 * 2.0 ** (np.arange(int(np.log2(12000.0 / 80.0) * per_octave)) / per_octave)
    level = 10.0 * np.log10(np.interp(grid, freqs, power) + _EPS)

    def smooth(x: np.ndarray, points: int) -> np.ndarray:
        padded = np.pad(x, points, mode="edge")
        return np.convolve(padded, np.ones(points) / points, mode="same")[points:-points]

    detail = smooth(level, per_octave // 12)   # 1/12 de oitava
    contour = smooth(level, per_octave)        # 1 oitava
    prominence = detail - contour
    audible = level > level.max() - 40.0       # ignora o que está 40 dB abaixo do pico
    peaks = [
        i for i in range(1, grid.size - 1)
        if audible[i] and prominence[i] >= min_db and prominence[i] >= prominence[i - 1] and prominence[i] > prominence[i + 1]
    ]
    peaks.sort(key=lambda i: -prominence[i])
    return [{"freq_hz": int(round(grid[i])), "prominence_db": round(float(prominence[i]), 1)} for i in sorted(peaks[:limit])]


def analyze(audio: np.ndarray, sample_rate: float) -> dict:
    """Mede o áudio. `audio` tem formato (canais, amostras), em float."""
    audio = np.atleast_2d(np.asarray(audio, dtype=np.float64))
    mono = audio.mean(axis=0)
    n = mono.size

    peak = float(np.abs(audio).max()) if n else 0.0
    rms = float(np.sqrt(np.mean(mono**2))) if n else 0.0

    # Espectro médio de potência em janelas de Hann com 50% de sobreposição.
    size = 4096
    if n < size:
        mono_padded = np.pad(mono, (0, size - n))
    else:
        mono_padded = mono
    hop = size // 2
    window = np.hanning(size)
    starts = range(0, mono_padded.size - size + 1, hop)
    power = np.zeros(size // 2 + 1)
    for s in starts:
        power += np.abs(np.fft.rfft(mono_padded[s : s + size] * window)) ** 2
    freqs = np.fft.rfftfreq(size, 1.0 / sample_rate)
    total = float(power.sum())

    centroid = float((freqs * power).sum() / total) if total > _EPS else 0.0
    bands = {}
    for name, (lo, hi) in BANDS.items():
        share = float(power[(freqs >= lo) & (freqs < hi)].sum()) / total if total > _EPS else 0.0
        bands[name] = round(10.0 * np.log10(max(share, _EPS)), 1)

    features = {
        "duration_s": round(n / sample_rate, 2),
        "sample_rate": int(sample_rate),
        "channels": int(audio.shape[0]),
        "peak_dbfs": round(_db(peak), 1),
        "rms_dbfs": round(_db(rms), 1),
        "crest_factor_db": round(_db(peak) - _db(rms), 1),
        "spectral_centroid_hz": round(centroid),
        # Energia de cada banda em dB relativos à energia total (0 = toda a energia).
        "band_energy_db": bands,
    }
    resonances = _resonances(freqs, power)
    if resonances:
        features["resonances"] = resonances
    if audio.shape[0] == 2 and n:
        left, right = audio
        denom = float(np.sqrt((left**2).sum() * (right**2).sum()))
        features["stereo_correlation"] = round(float((left * right).sum()) / denom, 2) + 0.0 if denom > _EPS else 1.0
    return features
