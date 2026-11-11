"""ChainSpec: o contrato entre a IA e o motor de áudio.

O mesmo JSON serve de saída do LLM, preset, item de histórico e estado salvo.
Há dois formatos:

- `LLMChain`: o que o LLM devolve. Esquema simples e fechado, compatível com
  saída estruturada (parâmetros como lista de pares nome/valor).
- `ChainSpec`: o formato canônico gravado em disco (parâmetros como dicionário).
"""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, model_validator

from .modules import MODULES

SPEC_VERSION = 2

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
    title: str = ""
    prompts: list[str] = Field(default_factory=list)  # a conversa: um pedido por revisão
    summary: str = ""  # resposta curta ao músico sobre o último pedido
    details: str = ""  # explicação técnica do que cada módulo faz e por quê
    suggestions: list[str] = Field(default_factory=list)  # próximos pedidos sugeridos
    modules: list[ModuleSpec] = Field(default_factory=list)
    macros: list[MacroSpec] = Field(default_factory=list)
    # Controles do músico sobre a cadeia inteira; o LLM não mexe neles.
    mix: float = 1.0  # 0 = som original, 1 = só o processado
    output_gain_db: float = 0.0

    @model_validator(mode="before")
    @classmethod
    def _accept_version_1(cls, data):
        # A versão 1 guardava um único pedido no campo `prompt`.
        if isinstance(data, dict) and "prompt" in data:
            data = dict(data)
            prompt = data.pop("prompt")
            if prompt and not data.get("prompts"):
                data["prompts"] = [prompt]
        return data


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
    title: str
    summary: str
    details: str
    suggestions: list[str]
    modules: list[LLMModule]
    macros: list[LLMMacro]

    def to_spec(self, prompts: list[str] | None = None) -> ChainSpec:
        return ChainSpec(
            title=self.title,
            prompts=list(prompts or []),
            summary=self.summary,
            details=self.details,
            suggestions=self.suggestions,
            modules=[
                ModuleSpec(id=m.id, type=m.type, params={p.name: p.value for p in m.params})
                for m in self.modules
            ],
            macros=[MacroSpec(**m.model_dump()) for m in self.macros],
        )

    @classmethod
    def from_spec(cls, spec: ChainSpec) -> "LLMChain":
        """Cadeia existente no formato que o LLM lê e devolve (para revisões)."""
        return cls(
            title=spec.title,
            summary=spec.summary,
            details=spec.details,
            suggestions=spec.suggestions,
            modules=[
                LLMModule(id=m.id, type=m.type, params=[LLMParam(name=k, value=v) for k, v in m.params.items()])
                for m in spec.modules
            ],
            macros=[LLMMacro(**m.model_dump()) for m in spec.macros],
        )
