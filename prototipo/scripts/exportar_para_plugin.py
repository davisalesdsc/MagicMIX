"""Gera para o plugin em C++ o catálogo de módulos e o prompt de sistema.

O protótipo em Python é a fonte da verdade. Este script escreve:
    Source/Engine/CatalogData.inc   catálogo de módulos e limites
    Source/Ai/PromptSistema.txt     prompt de sistema, com @@CATALOGO@@ no lugar do catálogo
    Source/Ai/Estilos.inc           estilos de resposta

Uso (de dentro de prototipo/):  python scripts/exportar_para_plugin.py [--check]
Com --check não escreve nada e falha se algum arquivo estiver desatualizado (usado no CI).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from promptfx.intent import STYLES, system_prompt  # noqa: E402
from promptfx.modules import MAX_MACROS, MAX_MODULES, MODULES  # noqa: E402
from promptfx.validate import BRIGHT_SOURCE_MAX_BOOST_DB, MAX_SUGGESTIONS  # noqa: E402

SOURCE = ROOT.parent / "Source"
HEADER = "// GERADO por prototipo/scripts/exportar_para_plugin.py. Não edite à mão.\n"


def cstr(text: str) -> str:
    return json.dumps(text, ensure_ascii=False)


def catalog_inc() -> str:
    lines = [HEADER]
    lines.append(f"#define MM_MAX_MODULES {MAX_MODULES}")
    lines.append(f"#define MM_MAX_MACROS {MAX_MACROS}")
    lines.append(f"#define MM_MAX_SUGGESTIONS {MAX_SUGGESTIONS}")
    lines.append(f"#define MM_BRIGHT_SOURCE_MAX_BOOST_DB {BRIGHT_SOURCE_MAX_BOOST_DB}f")
    lines.append("#ifdef MM_CATALOG_ENTRIES")
    for m in MODULES.values():
        lines.append(f"{{ {cstr(m.type)}, {cstr(m.description)}, {{")
        for p in m.params:
            log = "true" if p.scale == "log" else "false"
            lines.append(f"    {{ {cstr(p.name)}, {p.min!r}f, {p.max!r}f, {p.default!r}f, {cstr(p.unit)}, {cstr(p.description)}, {log} }},")
        lines.append("} },")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def styles_inc() -> str:
    lines = [HEADER]
    for name, text in STYLES.items():
        lines.append(f"{{ {cstr(name)}, {cstr(text)} }},")
    return "\n".join(lines) + "\n"


def main() -> None:
    files = {
        SOURCE / "Engine" / "CatalogData.inc": catalog_inc(),
        SOURCE / "Ai" / "PromptSistema.txt": system_prompt("@@CATALOGO@@"),
        SOURCE / "Ai" / "Estilos.inc": styles_inc(),
    }
    check = "--check" in sys.argv
    stale = []
    for path, content in files.items():
        if check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                stale.append(path.name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
            print(f"gravado: {path.relative_to(ROOT.parent)}")
    if stale:
        sys.exit(f"desatualizado: {', '.join(stale)}. Rode: python scripts/exportar_para_plugin.py")
    if check:
        print("arquivos do plugin em dia com o protótipo")


if __name__ == "__main__":
    main()
