"""Macros: um botão de 0 a 1 que move vários parâmetros ao mesmo tempo."""

from __future__ import annotations

import math

from .modules import MODULES
from .spec import ChainSpec


def interpolate(value_at_0: float, value_at_1: float, position: float, scale: str) -> float:
    """Valor do parâmetro para a posição do macro. Frequências e tempos andam em log."""
    position = min(max(position, 0.0), 1.0)
    if scale == "log" and value_at_0 > 0 and value_at_1 > 0:
        return math.exp(math.log(value_at_0) + position * (math.log(value_at_1) - math.log(value_at_0)))
    return value_at_0 + position * (value_at_1 - value_at_0)


def resolve(spec: ChainSpec, overrides: dict[str, float] | None = None) -> dict[str, dict[str, float]]:
    """Parâmetros finais de cada módulo, com os macros aplicados.

    `overrides` troca a posição de macros pelo nome (ex.: {"Brilho": 0.8}).
    Um parâmetro controlado por macro ignora o valor fixo gravado no módulo.
    """
    overrides = {k.lower(): v for k, v in (overrides or {}).items()}
    known = {m.name.lower() for m in spec.macros}
    unknown = set(overrides) - known
    if unknown:
        raise KeyError(f"macro inexistente: {', '.join(sorted(unknown))}")

    resolved = {m.id: dict(m.params) for m in spec.modules}
    types = {m.id: m.type for m in spec.modules}
    for macro in spec.macros:
        position = overrides.get(macro.name.lower(), macro.value)
        for t in macro.targets:
            param = MODULES[types[t.module_id]].param(t.param)
            resolved[t.module_id][t.param] = interpolate(t.value_at_0, t.value_at_1, position, param.scale)
    return resolved
