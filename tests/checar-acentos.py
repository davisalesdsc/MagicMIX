"""Confere que todo texto com acento no C++ da interface é convertido de UTF-8.

juce::String ("ação") gera texto corrompido: o construtor espera ASCII. O certo
é mm::tx ("ação") ou juce::String::fromUTF8 ("ação"). Este script acusa os
literais com acento que não estão dentro de uma dessas duas chamadas.

Uso: python tests/checar-acentos.py Source
"""

import re
import sys
from pathlib import Path

LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
SAFE_CALL = re.compile(r"(?:\btx|fromUTF8)\s*\(\s*$")
# Parâmetros destes ajudantes do editor passam por tx() dentro da função.
SAFE_HELPERS = re.compile(r"(?:setupButton|setupLinear|addRow)\s*\([^()]*,\s*$")


def strip_comment(line: str) -> str:
    in_string = False
    for i, ch in enumerate(line):
        if ch == '"' and (i == 0 or line[i - 1] != "\\"):
            in_string = not in_string
        if not in_string and line.startswith("//", i):
            return line[:i]
    return line


def problems(path: Path) -> list[str]:
    text = "\n".join(strip_comment(line) for line in path.read_text(encoding="utf-8").splitlines())
    found = []
    for match in LITERAL.finditer(text):
        if match.group(0).isascii():
            continue
        # Volta por cima de literais vizinhos ("a" "b" é um texto só) até achar a chamada.
        before = text[: match.start()]
        while True:
            stripped = before.rstrip()
            previous = None
            for candidate in LITERAL.finditer(stripped):
                previous = candidate
            if previous is not None and previous.end() == len(stripped):
                before = stripped[: previous.start()]
            else:
                break
        tail = before[-200:]
        if SAFE_CALL.search(tail) or SAFE_HELPERS.search(tail):
            continue
        line = text.count("\n", 0, match.start()) + 1
        found.append(f"{path}:{line}: {match.group(0)[:60]}")
    return found


def main() -> None:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "Source")
    files = [p for p in sorted(root.rglob("*")) if p.suffix in (".cpp", ".h") and "Engine" not in p.parts]
    found = [problem for path in files for problem in problems(path)]
    for problem in found:
        print(problem)
    if found:
        sys.exit(f"{len(found)} texto(s) com acento fora de tx() ou fromUTF8()")
    print(f"acentos conferidos em {len(files)} arquivos")


if __name__ == "__main__":
    main()
