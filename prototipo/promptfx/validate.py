"""Validador determinístico: nada do LLM chega ao áudio sem passar por aqui.

O LLM propõe, este código decide. Cada correção fica registrada num relatório,
que é o material de estudo para ajustar o prompt e os limites.
"""

from __future__ import annotations

import math

from .modules import MAX_MACROS, MAX_MODULES, MODULES, Param
from .spec import SPEC_VERSION, ChainSpec, MacroSpec, MacroTarget, ModuleSpec

MAX_SUGGESTIONS = 3
# Realce máximo de agudos numa fonte que já é brilhante. Folgado de propósito:
# o músico precisa de curso no botão; o objetivo é só evitar o absurdo.
BRIGHT_SOURCE_MAX_BOOST_DB = 6.0


def _clamp(value: float, param: Param) -> float:
    if not math.isfinite(value):
        return param.default
    return min(max(value, param.min), param.max)


def _fmt(v: float) -> str:
    return f"{v:g}"


def validate(spec: ChainSpec, features: dict | None = None) -> tuple[ChainSpec, list[str]]:
    """Devolve uma cópia segura do spec e a lista do que foi corrigido."""
    report: list[str] = []
    modules: list[ModuleSpec] = []
    seen_ids: set[str] = set()

    for raw in spec.modules:
        definition = MODULES.get(raw.type)
        if definition is None:
            report.append(f"módulo '{raw.id}': tipo desconhecido '{raw.type}', removido")
            continue
        if len(modules) >= MAX_MODULES:
            report.append(f"módulo '{raw.id}': excede o limite de {MAX_MODULES} módulos, removido")
            continue
        if not raw.id or raw.id in seen_ids:
            report.append(f"módulo '{raw.id}': id vazio ou repetido, removido")
            continue
        seen_ids.add(raw.id)

        params: dict[str, float] = {}
        for name in raw.params:
            if definition.param(name) is None:
                report.append(f"{raw.id}.{name}: parâmetro inexistente em '{raw.type}', ignorado")
        for p in definition.params:
            if p.name not in raw.params:
                params[p.name] = p.default
                continue
            value = raw.params[p.name]
            safe = _clamp(value, p)
            if safe != value:
                report.append(f"{raw.id}.{p.name}: {_fmt(value)} -> {_fmt(safe)} (faixa {_fmt(p.min)} a {_fmt(p.max)})")
            params[p.name] = safe
        modules.append(ModuleSpec(id=raw.id, type=raw.type, params=params))

    by_id = {m.id: m for m in modules}
    macros: list[MacroSpec] = []
    taken: set[tuple[str, str]] = set()  # cada parâmetro obedece a no máximo um macro

    for raw_macro in spec.macros:
        if len(macros) >= MAX_MACROS:
            report.append(f"macro '{raw_macro.name}': excede o limite de {MAX_MACROS} macros, removido")
            continue
        targets: list[MacroTarget] = []
        for t in raw_macro.targets:
            module = by_id.get(t.module_id)
            param = MODULES[module.type].param(t.param) if module else None
            where = f"macro '{raw_macro.name}' -> {t.module_id}.{t.param}"
            if param is None:
                report.append(f"{where}: alvo inexistente, ignorado")
                continue
            if (t.module_id, t.param) in taken:
                report.append(f"{where}: parâmetro já controlado por outro macro, ignorado")
                continue
            lo, hi = _clamp(t.value_at_0, param), _clamp(t.value_at_1, param)
            if (lo, hi) != (t.value_at_0, t.value_at_1):
                report.append(f"{where}: faixa {_fmt(t.value_at_0)}..{_fmt(t.value_at_1)} -> {_fmt(lo)}..{_fmt(hi)}")
            taken.add((t.module_id, t.param))
            targets.append(MacroTarget(module_id=t.module_id, param=t.param, value_at_0=lo, value_at_1=hi))
        if not targets:
            report.append(f"macro '{raw_macro.name}': sem alvos válidos, removido")
            continue
        value = raw_macro.value if math.isfinite(raw_macro.value) else 0.5
        macros.append(
            MacroSpec(
                name=raw_macro.name,
                description=raw_macro.description,
                value=min(max(value, 0.0), 1.0),
                targets=targets,
            )
        )

    clean = ChainSpec(
        version=SPEC_VERSION,
        title=spec.title,
        prompts=spec.prompts,
        summary=spec.summary,
        details=spec.details,
        suggestions=spec.suggestions[:MAX_SUGGESTIONS],
        modules=modules,
        macros=macros,
    )
    if features:
        for rule in SIGNAL_RULES:
            rule(clean, features, report)
    return clean, report


# ---------- limites que dependem do sinal de entrada ----------
# Cada regra recebe o spec já limpo e pode apertar valores e faixas de macro.
# Os números abaixo são pontos de partida para calibrar ouvindo.

def _reanchor(target: MacroTarget, position: float, start: float, param: Param, capped_end: int) -> None:
    """Move a ponta que não foi limitada para o macro continuar começando em `start`."""
    log = param.scale == "log" and start > 0 and target.value_at_0 > 0 and target.value_at_1 > 0
    f = math.log if log else (lambda v: v)
    inverse = math.exp if log else (lambda v: v)
    if capped_end == 1 and position < 1.0:
        free = (f(start) - position * f(target.value_at_1)) / (1.0 - position)
        target.value_at_0 = _clamp(inverse(free), param)
    elif capped_end == 0 and position > 0.0:
        free = (f(start) - (1.0 - position) * f(target.value_at_0)) / position
        target.value_at_1 = _clamp(inverse(free), param)


def _cap(spec: ChainSpec, module: ModuleSpec, param: str, ceiling: float, why: str, report: list[str]) -> None:
    """Limita por cima o valor do parâmetro e a faixa de qualquer macro que o controle.

    Ao limitar a faixa de um macro, preserva o som de partida sempre que ele já
    estava abaixo do teto: só o curso do botão encolhe.
    """
    from .macros import interpolate

    definition = MODULES[module.type].param(param)
    if module.params[param] > ceiling:
        report.append(f"{module.id}.{param}: {_fmt(module.params[param])} -> {_fmt(ceiling)} ({why})")
        module.params[param] = ceiling
    for macro in spec.macros:
        for t in macro.targets:
            if (t.module_id, t.param) != (module.id, param) or max(t.value_at_0, t.value_at_1) <= ceiling:
                continue
            report.append(f"macro '{macro.name}' -> {module.id}.{param}: teto {_fmt(ceiling)} ({why})")
            start = min(interpolate(t.value_at_0, t.value_at_1, macro.value, definition.scale), ceiling)
            both = t.value_at_0 > ceiling and t.value_at_1 > ceiling
            capped_end = 1 if t.value_at_1 > ceiling else 0
            t.value_at_0 = min(t.value_at_0, ceiling)
            t.value_at_1 = min(t.value_at_1, ceiling)
            if not both:
                _reanchor(t, macro.value, start, definition, capped_end)


def rule_bright_source(spec: ChainSpec, features: dict, report: list[str]) -> None:
    """Fonte que já é brilhante recebe menos realce de agudos, para não ficar estridente."""
    if features.get("spectral_centroid_hz", 0) < 3500:
        return
    for m in spec.modules:
        boosts_highs = m.type == "high_shelf" or (m.type == "peak" and m.params["freq_hz"] >= 2500)
        if boosts_highs:
            _cap(spec, m, "gain_db", BRIGHT_SOURCE_MAX_BOOST_DB, "fonte já brilhante", report)


def rule_headroom(spec: ChainSpec, features: dict, report: list[str]) -> None:
    """O ganho estático somado não pode empurrar o pico de entrada acima de -1 dBFS."""
    available = max(0.0, -1.0 - features.get("peak_dbfs", -12.0))
    for m in spec.modules:
        if m.type == "gain":
            _cap(spec, m, "gain_db", available, f"headroom de {available:.1f} dB", report)


def rule_dense_source(spec: ChainSpec, features: dict, report: list[str]) -> None:
    """Material já muito comprimido (fator de crista baixo) não aguenta taxa alta."""
    if features.get("crest_factor_db", 20.0) >= 8.0:
        return
    for m in spec.modules:
        if m.type == "compressor":
            _cap(spec, m, "ratio", 3.0, "fonte já comprimida", report)


SIGNAL_RULES = [rule_bright_source, rule_headroom, rule_dense_source]
