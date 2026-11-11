"""Roda os casos de referência pelo LLM e confere se a cadeia usa os módulos esperados.

Uso (de dentro de prototipo/, com ANTHROPIC_API_KEY definida):
    python scripts/make_test_audio.py
    python scripts/avaliar_casos.py [pasta_dos_audios]

Cada caso é uma conversa: os pedidos são enviados em sequência, cada um
revisando a cadeia anterior. O teste é fraco de propósito (só confere os
tipos de módulo); o julgamento do som continua sendo de ouvido.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

CASES = ROOT / "examples" / "referencia" / "casos_observados.json"
AUDIO = {"voz": "voz_sintetica.wav", "bateria": "bateria_sintetica.wav", "baixo": "baixo_sintetico.wav", "mix": "mix_sintetica.wav"}

# Cada grupo esperado é satisfeito por qualquer um destes tipos do catálogo.
GROUPS = {
    "eq": {"peak", "low_shelf", "high_shelf", "highpass", "lowpass"},
    "saturacao": {"saturation", "tape"},
}


def missing_groups(expected: list[str], module_types: set[str]) -> list[str]:
    return [g for g in expected if not (GROUPS.get(g, {g}) & module_types)]


def evaluate(cases: list[dict], features_for, generate, validate) -> list[dict]:
    """Roda cada caso e devolve uma linha de resultado por caso."""
    results = []
    for case in cases:
        features = features_for(case["audio_teste"])
        spec = None
        for prompt in case["pedidos"]:
            spec = generate(prompt, features, current=spec)
        clean, report = validate(spec, features)
        types = {m.type for m in clean.modules}
        results.append({
            "id": case["id"],
            "modulos": [m.type for m in clean.modules],
            "faltando": missing_groups(case["modulos_esperados"], types),
            "macros": [m.name for m in clean.macros],
            "correcoes": len(report),
        })
    return results


def main() -> None:
    from promptfx.analyze import analyze
    from promptfx.intent import generate
    from promptfx.render import read_audio
    from promptfx.validate import validate

    folder = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "audio")
    cases = json.loads(CASES.read_text(encoding="utf-8"))["casos"]
    results = evaluate(cases, lambda kind: analyze(*read_audio(str(folder / AUDIO[kind]))), generate, validate)
    ok = 0
    for r in results:
        status = "ok " if not r["faltando"] else "FALTA " + ",".join(r["faltando"])
        ok += not r["faltando"]
        print(f"{r['id']:28} {status:22} {' -> '.join(r['modulos'])}  | macros: {', '.join(r['macros'])} | correções: {r['correcoes']}")
    print(f"\n{ok} de {len(results)} casos com todos os módulos esperados")


if __name__ == "__main__":
    main()
