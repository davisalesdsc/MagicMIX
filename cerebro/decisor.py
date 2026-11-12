"""Decisor: mede cada pista e decide a cadeia com atitude, em dois níveis (firme e ousado).

É a parte do cérebro que olha o áudio: mede o timbre médio (1/6 de oitava), a variação de
volume, os picos e a sibilância, compara com o perfil de referência do instrumento e decide
quanto mexer. A compressão é ajustada em laço fechado: renderiza, mede a variação que sobrou
e corrige o limiar até chegar no alvo. Cada decisão vira exemplo de treino da rede neural.

Uso: MM_PISTAS=pasta_das_pistas MM_PISTAS_2=pasta_da_segunda_remessa python3 decisor.py
(grava render2/, cadeias2/ e decisoes.json; o áudio fica fora do repositório)
"""
import json
import os
import subprocess
from pathlib import Path

import numpy as np

from audio_util import (CENTROS, SR, ativos_db, crista, db, espectro, gravar, ler, suavizar, variacao)

AQUI = Path(__file__).parent
TRAB, CEREBRO = Path(os.environ.get("MM_PISTAS", AQUI / "trab")), Path(os.environ.get("MM_PISTAS_2", AQUI / "raw"))
RENDER, CADEIAS = AQUI / "render2", AQUI / "cadeias2"
RENDER.mkdir(exist_ok=True)
CADEIAS.mkdir(exist_ok=True)
RENDERIZAR = os.environ.get("MM_RENDERIZAR", str(Path(__file__).resolve().parent.parent / "build/Renderizar_artefacts/Release/Renderizar"))
DUR, PRE = 20.0, 4.0

NIVEIS = {"firme": {"eq": 0.8, "eq_max": 8.0, "forca": 1.0}, "ousado": {"eq": 1.0, "eq_max": 11.0, "forca": 1.6}}

# Perfis de referência: timbre médio de um instrumento bem resolvido numa mix (dB por 1/6 de
# oitava, relativos a 1 kHz). São o ponto de partida; as correções do músico ajustam esses perfis.
PERFIS = {
    "voz": {80: -24, 120: -10, 200: -1, 300: 1, 500: 1, 800: 0.5, 1000: 0, 1600: -0.5, 2500: -0.5, 3500: -0.5, 5000: -3, 8000: -6, 12000: -8, 16000: -12},
    "caixa": {100: -12, 160: 0, 250: 2, 400: 0, 630: -2, 1000: 0, 1600: 0, 2500: 1, 4000: 2, 6300: 0, 10000: -5, 16000: -14},
    "bumbo": {40: 8, 63: 14, 100: 14, 160: 10, 250: 3, 400: -2, 630: -2, 1000: 0, 1600: 1, 2500: 4, 4000: 4, 6300: -2, 10000: -10, 16000: -20},
    "bateria": {40: 4, 63: 8, 100: 7, 160: 5, 250: 2, 400: 0, 630: -1, 1000: 0, 1600: 0, 2500: 0, 4000: 1, 6300: 1, 10000: 0, 16000: -3},
    "baixo": {40: 16, 63: 18, 100: 18, 160: 15, 250: 10, 400: 4, 630: 2, 1000: 0, 1600: -1, 2500: -3, 4000: -6, 6300: -10, 10000: -18, 16000: -28},
    "sanfona": {100: -20, 160: -10, 250: -3, 400: 0, 630: 1, 1000: 0, 1600: -1, 2500: -3, 4000: -5, 6300: -8, 10000: -13, 16000: -22},
    "violao": {50: -20, 80: -10, 120: -2, 200: 0, 400: 0, 1000: 0, 2500: -0.5, 5000: 0, 10000: 0, 16000: -2},
    "base": {40: 4, 63: 6, 100: 6, 160: 5, 250: 3, 400: 1.5, 630: 0.5, 1000: 0, 1600: -0.5, 2500: -1.5, 4000: -3, 6300: -4.5, 10000: -7, 16000: -11},
}
FAIXA = {"voz": (100, 14000), "caixa": (120, 14000), "bumbo": (40, 8000), "bateria": (40, 16000), "baixo": (40, 5000),
         "sanfona": (150, 12000), "violao": (100, 14000), "base": (60, 16000)}
PASSA_ALTAS = {"voz": 90, "caixa": 70, "bumbo": 30, "bateria": 30, "baixo": 35, "sanfona": 120, "violao": 90, "base": 25}

SINO, GRAVE, AGUDO, HPF = 1, 2, 3, 4
LIMPO, OPTICO, FET, VCA, VARIMU = 0, 1, 2, 3, 4

EXEMPLOS = [
    {"id": "voz", "fonte": TRAB / "voz.wav", "inst": "voz", "titulo": "Voz guia", "pista": "voz guia.wav",
     "pedido": "deixa a voz na cara, sem aspereza e sem os S estourando", "estilo": "seco"},
    {"id": "rael", "fonte": CEREBRO / "RAEL BLOCO SEU ASTRAL.mp3", "inst": "voz", "titulo": "Voz do Rael", "pista": "RAEL BLOCO SEU ASTRAL.mp3",
     "pedido": "voz com cara de disco: na frente, brilhante e com um ambiente bonito", "estilo": "disco"},
    {"id": "zevitor", "fonte": CEREBRO / "ZE VITOR AUTORAL FOLEGO.mp3", "inst": "voz", "titulo": "Voz do Zé Vitor", "pista": "ZE VITOR AUTORAL FOLEGO.mp3",
     "pedido": "a voz está escura e some na base, abre ela e põe na frente", "estilo": "disco"},
    {"id": "caixa", "fonte": TRAB / "caixa.wav", "inst": "caixa", "titulo": "Caixa", "pista": "CX (1).wav",
     "pedido": "caixa mais estalada e com mais corpo, sem ficar caixotada"},
    {"id": "bumbo", "fonte": TRAB / "bumbo.wav", "inst": "bumbo", "titulo": "Bumbo", "pista": "BB [A].wav",
     "pedido": "bumbo com mais peso e um clique que apareça na mix"},
    {"id": "bateria", "fonte": TRAB / "bateria.wav", "inst": "bateria", "titulo": "Bateria inteira", "pista": "bumbo, caixa, sd, chimbal, overs e tom somados",
     "pedido": "cola a bateria e dá mais punch, com um pouco de compressão paralela"},
    {"id": "sidechain", "fonte": TRAB / "baixo_c.wav", "inst": "baixo", "sidechain": TRAB / "bumbo_c.wav", "titulo": "Baixo abrindo espaço para o bumbo",
     "pista": "BAIXO.mp3 com sidechain do BUMBO.mp3", "pedido": "o baixo e o bumbo estão brigando; faz o baixo abaixar quando o bumbo bate"},
    {"id": "sanfona", "fonte": TRAB / "sanfona.wav", "inst": "sanfona", "titulo": "Sanfona", "pista": "sanf guia.wav",
     "pedido": "a sanfona está ardida, suaviza sem tirar o brilho"},
    {"id": "sanfsolo", "fonte": CEREBRO / "SANFONA SOLO BLOCO SEU ASTRAL.mp3", "inst": "sanfona", "titulo": "Sanfona solo", "pista": "SANFONA SOLO BLOCO SEU ASTRAL.mp3",
     "pedido": "sanfona do solo cantando na frente, com presença e um pouco de ambiente", "ambiente": True},
    {"id": "violao", "fonte": TRAB / "violao.wav", "inst": "violao", "titulo": "Violão", "pista": "vl guia [A].wav",
     "pedido": "violão mais aberto e menos embolado"},
    {"id": "base", "fonte": CEREBRO / "BASE BLOCO SEU ASTRAL.mp3", "inst": "base", "titulo": "Base inteira", "pista": "BASE BLOCO SEU ASTRAL.mp3",
     "pedido": "dá uma cara de disco pra base: mais cheia, colada e aberta"},
]


# ------------------------------------------------------------------ medidas e EQ

def perfil(inst):
    pts = sorted(PERFIS[inst].items())
    fs, gs = np.log([p[0] for p in pts]), [p[1] for p in pts]
    return np.interp(np.log(CENTROS), fs, gs)


def resposta_sino(f0, ganho, q, freqs):
    """Módulo da resposta de um sino RBJ, em dB, nas frequências dadas."""
    a = 10 ** (ganho / 40)
    w0 = 2 * np.pi * f0 / SR
    alfa = np.sin(w0) / (2 * q)
    b = np.array([1 + alfa * a, -2 * np.cos(w0), 1 - alfa * a])
    an = np.array([1 + alfa / a, -2 * np.cos(w0), 1 - alfa / a])
    z = np.exp(-1j * 2 * np.pi * freqs / SR)
    h = (b[0] + b[1] * z + b[2] * z ** 2) / (an[0] + an[1] * z + an[2] * z ** 2)
    return 20 * np.log10(np.abs(h))


def resposta_prateleira(f0, ganho, freqs, agudo):
    """Aproximação suave de uma prateleira de Q 0,71 (só para o ajuste guloso)."""
    t = 1 / (1 + (f0 / freqs) ** 2) if agudo else 1 / (1 + (freqs / f0) ** 2)
    return ganho * t


def notas(x):
    """Faixa das notas (Hz, percentis 10 e 90) por autocorrelação em quadros de 40 ms."""
    mono = np.atleast_2d(x).mean(axis=0)
    n, f = int(0.04 * SR), []
    lo, hi = int(SR / 1000), int(SR / 70)
    topo = np.abs(mono).max()
    for i in range(0, mono.size - n, n):
        q = mono[i:i + n] - mono[i:i + n].mean()
        if np.sqrt(np.mean(q ** 2)) < topo * 0.01:
            continue
        ac = np.correlate(q, q, "full")[n - 1:]
        j = lo + int(np.argmax(ac[lo:hi]))
        if ac[j] > 0.5 * ac[0]:
            f.append(SR / j)
    return (float(np.percentile(f, 10)), float(np.percentile(f, 90))) if len(f) > 20 else None


def decidir_eq(inst, esp, nivel, zona_notas=None):
    """Ajuste guloso: põe sinos (ou prateleiras nas pontas) onde o timbre mais se afasta do perfil."""
    lo, hi = FAIXA[inst]
    dentro = (CENTROS >= lo) & (CENTROS <= hi) & (esp > esp.max() - 40)
    fonte = suavizar(esp, 3)
    resid = perfil(inst) - (fonte - fonte[np.argmin(abs(CENTROS - 1000))])
    resid -= np.percentile(resid[dentro], 70)  # prefere cortar a reforçar
    resid = np.where(dentro, resid, 0.0)
    bandas = []
    for _ in range(14):
        if len(bandas) == 6:
            break
        i = int(np.argmax(np.abs(resid)))
        if abs(resid[i]) < 1.5:
            break
        if any(abs(np.log2(CENTROS[i] / b[1])) < 0.6 for b in bandas):
            resid[max(0, i - 2):i + 3] = 0.0  # perto demais de uma banda que já existe
            continue
        teto_reforco = nivel["eq_max"] * (0.45 if CENTROS[i] < 100 or CENTROS[i] > 10000 else 0.65)
        teto_corte = nivel["eq_max"]
        if zona_notas and zona_notas[0] <= CENTROS[i] <= 2 * zona_notas[1]:
            teto_corte = nivel["eq_max"] * 0.5  # é onde moram as notas e o 1º harmônico: corte contido
        g = float(np.clip(resid[i] * nivel["eq"], -teto_corte, teto_reforco))
        # largura: bandas vizinhas com o mesmo sinal e pelo menos metade do desvio
        j0 = j1 = i
        while j0 > 0 and np.sign(resid[j0 - 1]) == np.sign(resid[i]) and abs(resid[j0 - 1]) > abs(resid[i]) / 2:
            j0 -= 1
        while j1 < resid.size - 1 and np.sign(resid[j1 + 1]) == np.sign(resid[i]) and abs(resid[j1 + 1]) > abs(resid[i]) / 2:
            j1 += 1
        oitavas = max((j1 - j0 + 1) / 6, 1 / 3)
        f = float(CENTROS[i])
        if CENTROS[i] >= 7000 and CENTROS[j1] >= hi * 0.7 and g > 0:
            tipo, f, q = AGUDO, float(CENTROS[j0]) * 1.2, 0.71
            resp = resposta_prateleira(f, g, CENTROS, True)
        else:
            tipo = SINO
            q = float(np.clip(1 / (2 * np.sinh(np.log(2) / 2 * oitavas)), 0.6, 4.0))
            resp = resposta_sino(f, g, q, CENTROS)
        bandas.append((tipo, round(f), round(g, 1), round(q, 2)))
        resid = np.where(dentro, resid - resp / nivel["eq"], 0.0)
        resid[max(0, min(j0, i - 3)):max(j1, i + 3) + 1] = 0.0  # esta região (pelo menos meia oitava para cada lado) já tem a sua banda
    return bandas


def params_eq(inst, bandas):
    p = {"b1_type": HPF, "b1_freq_hz": PASSA_ALTAS[inst], "b1_gain_db": 0, "b1_q": 0.71}
    for k, (tipo, f, g, q) in enumerate(bandas, start=2):
        p.update({f"b{k}_type": tipo, f"b{k}_freq_hz": f, f"b{k}_gain_db": g, f"b{k}_q": q})
    return p


def picos_golpes(x, ms=10):
    """Nível de pico (dBFS) dos golpes: quadros curtos perto do topo."""
    m = np.abs(np.atleast_2d(x)).max(axis=0)
    j = int(SR * ms / 1000)
    n = m.size // j
    p = db(m[: n * j].reshape(n, j).max(axis=1))
    return p[p > p.max() - 20]


def banda_alta_db(x, freq):
    mono = np.atleast_2d(x).mean(axis=0)
    e = np.fft.rfft(mono)
    e[np.fft.rfftfreq(mono.size, 1 / SR) < freq] = 0
    alto = np.abs(np.fft.irfft(e, n=mono.size))
    j = int(SR * 0.005)
    n = alto.size // j
    p = db(alto[: n * j].reshape(n, j).max(axis=1))
    return p[p > p.max() - 40]


# ------------------------------------------------------------------ render

def janela_mais_cheia(x):
    mono = np.atleast_2d(x).mean(axis=0)
    n = int(DUR * SR)
    melhor, inicio = -1.0, 0
    for i in range(int(PRE * SR), mono.size - n, SR):
        e = float(np.mean(mono[i:i + n] ** 2))
        if e > melhor:
            melhor, inicio = e, i
    return inicio


def renderizar(cadeia, entrada, saida, sc=None):
    arq = RENDER / "_cadeia.json"
    arq.write_text(json.dumps(cadeia, ensure_ascii=False), encoding="utf-8")
    args = [RENDERIZAR, str(arq), str(entrada), str(saida)] + (["--sidechain", str(sc)] if sc else [])
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    avisos = [l for l in r.stdout.splitlines() if l.startswith("validador")]
    return ler(saida), avisos


def modulo(mid, tipo, **params):
    return {"id": mid, "type": tipo, "params": params}


# ------------------------------------------------------------------ cadeias por instrumento

def cadeia_base(ex, mods, resumo):
    return {"version": 2, "title": ex["titulo"], "prompts": [ex["pedido"]], "summary": resumo, "modules": mods, "macros": [], "mix": 1.0, "output_gain_db": 0.0}


def ajustar_limiar(ex, construir, alvo_var, lo, hi, entrada, sc=None):
    """Laço fechado: procura o limiar que deixa a variação de volume no alvo."""
    melhor = None
    for _ in range(5):
        t = (lo + hi) / 2
        y, _ = renderizar(construir(t), entrada, RENDER / "_teste.wav", sc)
        v = variacao(y[:, int(PRE * SR):])
        if melhor is None or abs(v - alvo_var) < abs(melhor[1] - alvo_var):
            melhor = (t, v)
        if v > alvo_var:
            hi = t
        else:
            lo = t
    return melhor


def decidir(ex, nivel_nome, trecho, trecho_sc):
    nivel = NIVEIS[nivel_nome]
    forca = nivel["forca"]
    inst = ex["inst"]
    x = trecho
    # 1) volume de trabalho: média dos trechos com som em -20 dBFS, sem passar de -1 dBFS de pico
    ganho = float(min(-20 - np.mean(ativos_db(x)), -1.5 - db(np.abs(x).max()), 12.0))
    xs = x * 10 ** (ganho / 20)
    esp = espectro(xs)
    zona = notas(xs) if inst in ("voz", "sanfona", "violao") else None
    bandas = decidir_eq(inst, esp, nivel, zona)
    niveis_q = ativos_db(xs)
    var0 = variacao(xs)
    golpes = picos_golpes(xs)
    mods = [modulo("ganho", "gain", gain_db=round(ganho, 1)), modulo("eq", "eq", **params_eq(inst, bandas))]
    leitura = {"ganho": ganho, "bandas": bandas, "variacao": var0, "crista": crista(xs), "notas": zona}
    entrada = RENDER / f"_entrada_{ex['id']}.wav"
    gravar(entrada, x)
    sc = None
    if trecho_sc is not None:
        sc = RENDER / f"_sc_{ex['id']}.wav"
        gravar(sc, trecho_sc)

    if inst == "voz":
        alvo = max(4.0, 7.5 - 1.5 * forca)
        disco = ex.get("estilo") == "disco"

        def construir(t, deess=None, verb=True):
            m = mods + [
                modulo("fet", "compressor", model=FET, threshold_db=round(t + 6, 1), ratio=round(4 + 2 * forca), attack_ms=2, release_ms=60, makeup_db=0),
                modulo("opto", "compressor", model=OPTICO, threshold_db=round(t, 1), ratio=3, attack_ms=15, release_ms=150, makeup_db=0),
                modulo("res", "resonance", depth_db=round(6 + 4 * forca), sharpness=6, selectivity=4, attack_ms=4, release_ms=80,
                       s4_freq_hz=3000, s4_gain_db=6, s5_freq_hz=5000, s5_gain_db=3),
                modulo("sat", "saturation", drive_db=round(6 + 4 * forca), mix=round(0.25 + 0.15 * forca, 2)),
                modulo("exc", "exciter", freq_hz=2500 if ex["id"] == "zevitor" else 3500, drive_db=9, amount=round(0.1 + 0.08 * forca, 2)),
            ]
            if deess is not None:
                m.insert(4, modulo("deess", "de_esser", freq_hz=6000, threshold_db=round(deess, 1), max_reduction_db=round(6 + 3 * forca)))
            if disco and verb:
                m.append(modulo("verb", "reverb", size=0.45, damping=0.55, width=1.0, pre_delay_ms=45, mix=round(0.06 + 0.05 * forca, 2)))
            return cadeia_base(ex, m, "")

        # o laço mede sem o reverb: a cauda preenche as pausas e engana a medida de variação
        t, v = ajustar_limiar(ex, lambda t: construir(t, verb=False), alvo, float(np.percentile(niveis_q, 5)) - 6, float(np.percentile(niveis_q, 95)), entrada)
        y, _ = renderizar(construir(t, verb=False), entrada, RENDER / "_teste.wav")
        sib = banda_alta_db(y[:, int(PRE * SR):], 6000)
        deess = float(np.percentile(sib, 90)) - 2 * forca
        cadeia = construir(t, deess)
        leitura.update(limiar=t, alvo=alvo, deess=deess)

    elif inst in ("caixa", "bumbo"):
        topo = float(np.percentile(golpes, 75))
        gr = 4 + 3 * forca
        r = 4 + 2 * forca
        limiar = topo - gr * r / (r - 1)
        m = mods + [
            modulo("trans", "transient_shaper", attack_db=round(3 + 3 * forca, 1), sustain_db=round((1.5 if inst == "caixa" else -1.5) * forca, 1)),
            modulo("comp", "compressor", model=FET if inst == "caixa" else VCA, threshold_db=round(limiar, 1), ratio=r,
                   attack_ms=10 if inst == "caixa" else 15, release_ms=80, makeup_db=0),
            modulo("sat", "saturation", drive_db=round(6 + 5 * forca), mix=round(0.3 + 0.15 * forca, 2)),
            modulo("clip", "clipper", ceiling_db=round(topo - 1.5 * forca, 1), softness=0.6),
        ]
        cadeia = cadeia_base(ex, m, "")
        leitura.update(limiar=limiar, gr=gr)

    elif inst == "bateria":
        gr = 3 * (forca - 1) / 0.6  # 0 no firme, 3 dB a mais no ousado
        limiar = -22 + ganho - gr
        topo = float(np.percentile(golpes, 75))
        m = mods + [
            modulo("trans", "transient_shaper", attack_db=round(4 * (forca - 1) / 0.6 + 1, 1), sustain_db=0),
            modulo("bus", "bus_compressor", threshold_db=round(limiar, 1), ratio=4, attack_ms=10 if forca < 1.2 else 3, release_s=0,
                   makeup_db=0, mix=0.6 if forca < 1.2 else 0.75, sc_hpf_hz=120),
            modulo("res", "resonance", depth_db=4, sharpness=4, selectivity=5),
            modulo("fita", "tape", drive_db=round(3 + 3 * forca), tone_hz=16000, wow=0),
            modulo("clip", "clipper", ceiling_db=round(topo - 1.5 * forca, 1), softness=0.6),
            modulo("larg", "stereo_width", width=round(1 + 0.1 * forca, 2)),
        ]
        cadeia = cadeia_base(ex, m, "")
        leitura.update(limiar=limiar, gr=gr, aprovado=True)

    elif inst == "baixo":
        sc_golpes = picos_golpes(trecho_sc)
        duck = 5 + 4 * forca
        r = 8
        lim_duck = float(np.percentile(sc_golpes, 50)) - duck * r / (r - 1)
        alvo = 6.0

        def construir(t):
            m = mods + [
                modulo("opto", "compressor", model=OPTICO, threshold_db=round(t, 1), ratio=4, attack_ms=20, release_ms=150, makeup_db=0),
                modulo("sat", "saturation", drive_db=round(6 + 5 * forca), mix=round(0.3 + 0.15 * forca, 2)),
                modulo("exc", "exciter", freq_hz=700, drive_db=9, amount=round(0.12 + 0.1 * forca, 2)),
                modulo("duck", "compressor", model=VCA, threshold_db=round(lim_duck, 1), ratio=r, attack_ms=1, release_ms=round(100 + 30 * forca), sidechain=1),
            ]
            return cadeia_base(ex, m, "")

        t = float(np.percentile(niveis_q, 50))
        cadeia = construir(t)
        leitura.update(limiar=t, duck=duck)

    elif inst in ("sanfona", "violao"):
        alvo = max(4.5, 8 - 1.5 * forca)
        amb = ex.get("ambiente")

        def construir(t):
            m = list(mods)
            if inst == "sanfona":
                m.append(modulo("res", "resonance", depth_db=round(8 + 4 * forca), sharpness=6, selectivity=4, attack_ms=5, release_ms=100,
                                s4_freq_hz=2500, s4_gain_db=6, s5_freq_hz=4000, s5_gain_db=3))
            m.append(modulo("opto", "compressor", model=OPTICO, threshold_db=round(t, 1), ratio=3, attack_ms=15, release_ms=150, makeup_db=0))
            if inst == "sanfona":
                m.append(modulo("fita", "tape", drive_db=round(4 + 3 * forca), tone_hz=15000, wow=0))
            else:
                m.append(modulo("trans", "transient_shaper", attack_db=round(2 + 2 * forca, 1), sustain_db=0))
                m.append(modulo("res", "resonance", depth_db=round(4 + 2 * forca), sharpness=5, selectivity=5))
            m.append(modulo("exc", "exciter", freq_hz=4500, drive_db=9, amount=round(0.12 + 0.1 * forca, 2)))
            if amb:
                m.append(modulo("verb", "reverb", size=0.5, damping=0.5, width=1.0, pre_delay_ms=30, mix=round(0.07 + 0.05 * forca, 2)))
            return cadeia_base(ex, m, "")

        if inst == "violao":
            t = -20 + ganho - 4 * (forca - 1) / 0.6
            leitura.update(aprovado=True)
        else:
            t, v = ajustar_limiar(ex, construir, alvo, float(np.percentile(niveis_q, 5)) - 6, float(np.percentile(niveis_q, 95)), entrada)
        cadeia = construir(t)
        leitura.update(limiar=t, alvo=alvo)

    elif inst == "base":
        topo = float(np.percentile(golpes, 75))
        gr = 2 + 2 * forca
        limiar = topo - gr * 2
        m = mods + [
            modulo("bus", "bus_compressor", threshold_db=round(limiar, 1), ratio=2, attack_ms=30 if forca < 1.2 else 10, release_s=0, makeup_db=0, mix=1.0, sc_hpf_hz=90),
            modulo("fita", "tape", drive_db=round(4 + 3 * forca), tone_hz=17000, wow=0),
            modulo("exc", "exciter", freq_hz=6000, drive_db=9, amount=round(0.1 + 0.08 * forca, 2)),
            modulo("larg", "stereo_width", width=round(1 + 0.15 * forca, 2)),
            modulo("clip", "clipper", ceiling_db=round(topo - 1.5 * forca, 1), softness=0.7),
        ]
        cadeia = cadeia_base(ex, m, "")
        leitura.update(limiar=limiar, gr=gr)

    y, avisos = renderizar(cadeia, entrada, RENDER / f"{ex['id']}_{nivel_nome}.wav", sc)
    return cadeia, y[:, : x.shape[1]], leitura, avisos


def main():
    decisoes = {}
    for ex in EXEMPLOS:
        x = ler(ex["fonte"])
        if x.shape[0] == 2 and np.allclose(x[0], x[1], atol=1e-4):
            x = x[:1]
        sc_full = ler(ex["sidechain"], mono=True) if ex.get("sidechain") else None
        base_janela = x if sc_full is None else x[:, : sc_full.shape[1]] + sc_full[:, : x.shape[1]]
        i = janela_mais_cheia(base_janela)
        a, b = i - int(PRE * SR), i + int(DUR * SR)
        trecho = x[:, a:b]
        trecho_sc = sc_full[:, a:b] if sc_full is not None else None
        decisoes[ex["id"]] = {"inicio_s": round(i / SR, 1), "niveis": {}}
        np.save(RENDER / f"{ex['id']}_original.npy", trecho[:, int(PRE * SR):])
        if trecho_sc is not None:
            np.save(RENDER / f"{ex['id']}_sc.npy", trecho_sc[:, int(PRE * SR):])
        for nome in NIVEIS:
            cadeia, y, leitura, avisos = decidir(ex, nome, trecho, trecho_sc)
            np.save(RENDER / f"{ex['id']}_{nome}.npy", y[:, int(PRE * SR):])
            (CADEIAS / f"{ex['id']}_{nome}.json").write_text(json.dumps(cadeia, ensure_ascii=False, indent=1), encoding="utf-8")
            decisoes[ex["id"]]["niveis"][nome] = {"leitura": leitura, "modulos": cadeia["modules"], "avisos": avisos}
            print(f"{ex['id']:9} {nome:6} ganho {leitura['ganho']:+5.1f}  var {leitura['variacao']:4.1f} -> {variacao(y[:, int(PRE * SR):]):4.1f}  "
                  f"EQ {[(b[1], b[2]) for b in leitura['bandas']]}  {avisos if avisos else ''}", flush=True)
    (AQUI / "decisoes.json").write_text(json.dumps({"exemplos": [{k: (str(v) if isinstance(v, Path) else v) for k, v in ex.items()} for ex in EXEMPLOS],
                                                     "decisoes": decisoes}, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
