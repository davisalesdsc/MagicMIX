"""ChainSpec: o contrato entre a IA e o motor de áudio.

O mesmo JSON serve de saída do LLM, preset, item de histórico e estado salvo.
Há dois formatos:

- `LLMChain`: o que o LLM devolve. Esquema simples e fechado, compatível com
  saída estruturada (parâmetros como lista de pares nome/valor).
- `ChainSpec`: o formato canônico gravado em disco (parâmetros como dicionário).
"""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field

from .modules import MODULES

SPEC_VERSION = 1

ModuleType = Literal[tuple(MODULES)]  # type: ignore[valid-type]


# ---------- formato canônico ----------

class ModuleSpec(BaseModel):
    id: str
    type: str
    params: dict[str, float] = Field(default_factory=dict)


class MacroTarget(BaseModel):
    module_id: str
    param: str
    value_at_0: float  # valor do parâmetro com o macro no mínimo
    value_at_1: float  # valor do parâmetro com o macro no máximo


class MacroSpec(BaseModel):
    name: str
    description: str = ""
    value: float = 0.5  # posição atual do botão, de 0 a 1
    targets: list[MacroTarget] = Field(default_factory=list)


class ChainSpec(BaseModel):
    version: int = SPEC_VERSION
    prompt: str | None = None
    summary: str = ""
    modules: list[ModuleSpec] = Field(default_factory=list)
    macros: list[MacroSpec] = Field(default_factory=list)


# ---------- formato de saída do LLM ----------

class LLMParam(BaseModel):
    name: str
    value: float


class LLMModule(BaseModel):
    id: str
    type: ModuleType
    params: list[LLMParam]


class LLMMacro(BaseModel):
    name: str
    description: str
    value: float
    targets: list[MacroTarget]


class LLMChain(BaseModel):
    summary: str
    modules: list[LLMModule]
    macros: list[LLMMacro]

    def to_spec(self, prompt: str | None = None) -> ChainSpec:
        return ChainSpec(
            prompt=prompt,
            summary=self.summary,
            modules=[
                ModuleSpec(id=m.id, type=m.type, params={p.name: p.value for p in m.params})
                for m in self.modules
            ],
            macros=[MacroSpec(**m.model_dump()) for m in self.macros],
        )
