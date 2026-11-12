"""Leitura, gravação e medidas de áudio compartilhadas pelos scripts de escuta."""
import subprocess

import numpy as np

SR = 44100


def ler(caminho, mono=False):
    info = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=channels", "-of", "csv=p=0", str(caminho)],
                          capture_output=True, text=True).stdout.strip()
    nc = int(info)
    dados = subprocess.run(["ffmpeg", "-v", "error", "-i", str(caminho), "-f", "f32le", "-ar", str(SR), "-"], capture_output=True, check=True).stdout
    x = np.frombuffer(dados, dtype=np.float32).reshape(-1, nc).T.copy()
    if mono:
        return x.mean(axis=0, keepdims=True)
    return x


def gravar(caminho, x, bits=None):
    x = np.atleast_2d(x).astype(np.float32)
    codec = ["-c:a", "pcm_f32le"] if bits is None else ["-c:a", "pcm_s24le"]
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ar", str(SR), "-ac", str(x.shape[0]), "-i", "-"] + codec + [str(caminho)],
                   input=x.T.tobytes(), check=True)


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-12))


def quadros_rms(x, ms=100):
    mono = np.atleast_2d(x).mean(axis=0)
    j = int(SR * ms / 1000)
    n = mono.size // j
    return np.sqrt((mono[: n * j].reshape(n, j) ** 2).mean(axis=1))


def ativos_db(x, abaixo=30, ms=100):
    r = db(quadros_rms(x, ms))
    return r[r > r.max() - abaixo]


def variacao(x):
    a = ativos_db(x)
    return float(np.percentile(a, 95) - np.percentile(a, 10))


def crista(x):
    a = ativos_db(x)
    media = db(np.sqrt(np.mean((10 ** (a / 20)) ** 2)))
    return float(db(np.abs(x).max()) - media)


CENTROS = 30 * 2 ** (np.arange(0, np.log2(18000 / 30) * 6 + 1) / 6)


def espectro(x, so_ativo=True):
    """Espectro médio em 1/6 de oitava (potência por banda, em dB), só dos trechos com som."""
    mono = np.atleast_2d(x).mean(axis=0)
    tam = 8192
    quadros = [mono[i:i + tam] for i in range(0, mono.size - tam, tam // 2)]
    if so_ativo:
        e = np.array([np.mean(q ** 2) for q in quadros])
        quadros = [q for q, ee in zip(quadros, e) if ee > e.max() * 10 ** (-3.0)]
    p = np.mean([np.abs(np.fft.rfft(q * np.hanning(tam))) ** 2 for q in quadros], axis=0)
    f = np.fft.rfftfreq(tam, 1 / SR)
    out = []
    for c in CENTROS:
        sel = (f >= c / 2 ** (1 / 12)) & (f < c * 2 ** (1 / 12))
        out.append(float(10 * np.log10(p[sel].sum() + 1e-20)) if sel.any() else -120.0)
    return np.array(out)


def suavizar(v, largura_bandas=3):
    """Média móvel em bandas de 1/6 de oitava (3 = meia oitava)."""
    k = np.ones(largura_bandas) / largura_bandas
    pad = np.pad(v, largura_bandas // 2, mode="edge")
    return np.convolve(pad, k, mode="valid")[: v.size]
