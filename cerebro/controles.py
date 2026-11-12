"""Botões simples do Fluxo: Brilho, Presença, Corpo, Punch, Compressão, Suavizar e Ambiente.

Para quem não quer abrir o Avançado. Cada botão é um macro que mexe nos efeitos certos por baixo e
nasce no ponto neutro (o som aprovado não muda). Quando um parâmetro já é do macro Intensidade, o
botão usa um estágio próprio (um EQ de tom, um realce de ataque, um reverb) para os dois não brigarem.

Uso: python3 controles.py exemplos/voz.json ...   (reescreve os arquivos)
"""
import json
import sys
from pathlib import Path

MAX_MODULOS = 12

# grupos de instrumento pelo id da cadeia (o decisor usa os mesmos)
GRUPO = {"voz": "voz", "rael": "voz", "zevitor": "voz", "caixa": "bateria", "bumbo": "bateria", "bateria": "bateria",
         "sidechain": "baixo", "violao": "harmonia", "sanfona": "harmonia", "sanfsolo": "harmonia", "base": "mix"}

# frequências do EQ de tom por grupo: (corpo, presença, brilho)
TOM = {"voz": (180, 3000, 9000), "bateria": (90, 4000, 9000), "baixo": (80, 1200, 4000),
       "harmonia": (200, 2500, 9000), "mix": (100, 3000, 10000)}


def alvos_do_intensidade(cadeia):
    return {(t["module_id"], t["param"]) for m in cadeia.get("macros", []) for t in m["targets"]}


def adicionar_controles(cadeia, id_cadeia):
    grupo = GRUPO.get(id_cadeia, "mix")
    ocupados = alvos_do_intensidade(cadeia)
    mods = cadeia["modules"]
    ids = {m["id"] for m in mods}
    macros = cadeia.setdefault("macros", [])
    if any(m["name"] == "Brilho" for m in macros):
        return cadeia  # já tem

    def antes_do_fim(mod):
        # entra antes de efeitos de espaço e de proteção (reverb, largura, clipper), se houver
        for i, m in enumerate(mods):
            if m["type"] in ("reverb", "stereo_width", "clipper"):
                mods.insert(i, mod)
                return
        mods.append(mod)

    def macro(nome, descricao, alvos, valor=0.5):
        macros.append({"name": nome, "description": descricao, "value": valor,
                       "targets": [{"module_id": a, "param": p, "value_at_0": v0, "value_at_1": v1} for a, p, v0, v1 in alvos]})

    # EQ de tom: três bandas largas, neutras no meio do botão
    corpo, presenca, brilho = TOM[grupo]
    if "tom" not in ids and len(mods) < MAX_MODULOS:
        antes_do_fim({"id": "tom", "type": "eq", "params": {
            "b1_type": 2, "b1_freq_hz": corpo, "b1_gain_db": 0, "b1_q": 0.71,
            "b2_type": 1, "b2_freq_hz": presenca, "b2_gain_db": 0, "b2_q": 0.8,
            "b3_type": 3, "b3_freq_hz": brilho, "b3_gain_db": 0, "b3_q": 0.71}})
        ids.add("tom")
        macro("Brilho", "Agudos e ar: mais aberto para a direita, mais escuro para a esquerda.", [("tom", "b3_gain_db", -6, 6)])
        macro("Presença", "O quanto o som vem para a frente da mix.", [("tom", "b2_gain_db", -5, 5)])
        macro("Corpo", "Grave e peso: mais cheio para a direita, mais leve e limpo para a esquerda.", [("tom", "b1_gain_db", -6, 6)])

    # Punch: realce de ataque próprio (instrumentos de golpe e a mix)
    if grupo in ("bateria", "baixo", "harmonia", "mix") and "punch" not in ids and len(mods) < MAX_MODULOS:
        antes_do_fim({"id": "punch", "type": "transient_shaper", "params": {"attack_db": 0, "sustain_db": 0}})
        ids.add("punch")
        macro("Punch", "Ataque das notas e golpes: mais estalo e pegada para a direita.", [("punch", "attack_db", -6, 6)])

    # Compressão: a taxa de um compressor que o Intensidade não usa
    for m in mods:
        if m["type"] == "compressor" and (m["id"], "ratio") not in ocupados and m["params"].get("sidechain", 0) < 0.5:
            r = float(m["params"].get("ratio", 3))
            macro("Compressão", "Quanto o volume é segurado: mais estável para a direita, mais solto e natural para a esquerda.",
                  [(m["id"], "ratio", round(max(1.0, r / 2.5), 2), round(min(10.0, r * 2.5), 2))])
            break

    # Suavizar: o supressor de ressonâncias age mais (seletividade menor) para a direita
    for m in mods:
        if m["type"] == "resonance" and (m["id"], "selectivity") not in ocupados:
            s = float(m["params"].get("selectivity", 5))
            macro("Suavizar", "Tira aspereza e ardido: para a direita o supressor de ressonâncias age mais.",
                  [(m["id"], "selectivity", round(min(10.0, s + 3), 2), round(max(1.0, s - 3), 2))])
            break

    # Ambiente: o reverb da cadeia, se estiver livre; senão um reverb próprio que nasce desligado
    livre = [m for m in mods if m["type"] == "reverb" and (m["id"], "mix") not in ocupados]
    if livre:
        mix = float(livre[0]["params"].get("mix", 0.1))
        macro("Ambiente", "Quanto espaço (reverb) em volta do som.", [(livre[0]["id"], "mix", 0.0, min(0.6, mix * 2))])
    elif "ambiente" not in ids and len(mods) < MAX_MODULOS:
        mods.append({"id": "ambiente", "type": "reverb", "params": {"size": 0.4, "damping": 0.55, "width": 1.0, "pre_delay_ms": 30, "mix": 0.0}})
        macro("Ambiente", "Quanto espaço (reverb) em volta do som. Começa seco.", [("ambiente", "mix", 0.0, 0.35)], valor=0.0)
    return cadeia


if __name__ == "__main__":
    for caminho in sys.argv[1:]:
        p = Path(caminho)
        cadeia = json.loads(p.read_text(encoding="utf-8"))
        adicionar_controles(cadeia, p.stem)
        p.write_text(json.dumps(cadeia, ensure_ascii=False, indent=1), encoding="utf-8")
        print(p.stem, len(cadeia["modules"]), "módulos:", ", ".join(m["name"] for m in cadeia["macros"]))
