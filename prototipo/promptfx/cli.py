"""Linha de comando do protótipo. Uso: python -m promptfx <comando> ..."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .analyze import analyze
from .modules import catalog_json, catalog_text
from .spec import ChainSpec, LLMChain
from .validate import validate


def _load_spec(path: str) -> ChainSpec:
    """Lê um ChainSpec canônico ou uma resposta crua do LLM (parâmetros em lista)."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if any(isinstance(m.get("params"), list) for m in data.get("modules", [])):
        prompts = data.pop("prompts", None) or ([data.pop("prompt")] if data.get("prompt") else [])
        for field, default in (("title", ""), ("details", ""), ("suggestions", [])):
            data.setdefault(field, default)
        return LLMChain.model_validate(data).to_spec(prompts)
    return ChainSpec.model_validate(data)


def _save_spec(spec: ChainSpec, path: str) -> None:
    Path(path).write_text(spec.model_dump_json(indent=2) + "\n", encoding="utf-8")


def _features(audio_path: str, bpm: float | None) -> dict:
    from .render import read_audio

    features = analyze(*read_audio(audio_path))
    if bpm:
        features["bpm"] = bpm
    return features


def _parse_macros(items: list[str]) -> dict[str, float]:
    macros = {}
    for item in items:
        name, _, value = item.partition("=")
        if not value:
            sys.exit(f"macro inválido '{item}': use Nome=0.7")
        macros[name] = float(value)
    return macros


def _print_report(report: list[str]) -> None:
    if not report:
        print("validador: nada a corrigir")
        return
    print(f"validador: {len(report)} correção(ões)")
    for line in report:
        print(f"  - {line}")


def _describe(spec: ChainSpec) -> None:
    from .macros import resolve

    if spec.title:
        print(f"título: {spec.title}")
    if spec.summary:
        print(f"resumo: {spec.summary}")
    if spec.details:
        print(f"detalhes: {spec.details}")
    params = resolve(spec)
    print("cadeia:")
    for m in spec.modules:
        values = ", ".join(f"{k}={v:.4g}" for k, v in params[m.id].items())
        print(f"  {m.id} [{m.type}] {values}")
    if spec.macros:
        print("macros:")
        for macro in spec.macros:
            targets = ", ".join(f"{t.module_id}.{t.param} {t.value_at_0:g}..{t.value_at_1:g}" for t in macro.targets)
            print(f"  {macro.name} = {macro.value:g}  ({targets})")
    if spec.suggestions:
        print("sugestões de próximo pedido:")
        for s in spec.suggestions:
            print(f"  > {s}")


def _render(spec: ChainSpec, args: argparse.Namespace) -> None:
    from .render import process, read_audio, write_audio

    audio, sample_rate = read_audio(args.audio)
    features = analyze(audio, sample_rate)
    spec, report = validate(spec, features)
    _print_report(report)
    _describe(spec)
    out = process(audio, sample_rate, spec, _parse_macros(args.macro), args.match_loudness)
    write_audio(args.out, out, sample_rate)
    # Mede só o trecho original, sem a cauda extra, para a comparação ser justa.
    after = analyze(out[:, : audio.shape[1]], sample_rate)
    print(f"entrada: pico {features['peak_dbfs']} dBFS, média {features['rms_dbfs']} dBFS, centroide {features['spectral_centroid_hz']} Hz")
    print(f"saída:   pico {after['peak_dbfs']} dBFS, média {after['rms_dbfs']} dBFS, centroide {after['spectral_centroid_hz']} Hz")
    print(f"gravado: {args.out}")


def main(argv: list[str] | None = None) -> None:
    from .intent import DEFAULT_STYLE, STYLES

    parser = argparse.ArgumentParser(prog="promptfx", description="Protótipo: texto -> cadeia de efeitos -> áudio.")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_llm_args(p: argparse.ArgumentParser) -> None:
        p.add_argument("--estilo", choices=sorted(STYLES), default=DEFAULT_STYLE, help="tom dos nomes de macro e dos textos")
        p.add_argument("--bpm", type=float, help="andamento da música, para delays no tempo")

    def add_render_args(p: argparse.ArgumentParser) -> None:
        p.add_argument("audio", help="arquivo de entrada (wav, flac, mp3...)")
        p.add_argument("-o", "--out", required=True, help="arquivo de saída")
        p.add_argument("-m", "--macro", action="append", default=[], metavar="NOME=VALOR", help="posição de um macro, de 0 a 1")
        p.add_argument("--match-loudness", action="store_true", help="iguala o volume médio da saída ao da entrada")

    p = sub.add_parser("catalog", help="mostra o catálogo de módulos")
    p.add_argument("--json", action="store_true")

    p = sub.add_parser("analyze", help="mede um arquivo de áudio")
    p.add_argument("audio")

    p = sub.add_parser("prompt", help="mostra o texto a colar em qualquer chat de IA, para testar sem chave de API")
    p.add_argument("prompt")
    p.add_argument("audio")
    p.add_argument("--spec", help="cadeia atual, para pedir uma revisão")
    add_llm_args(p)

    p = sub.add_parser("generate", help="pede ao LLM uma cadeia e grava o ChainSpec")
    p.add_argument("prompt")
    p.add_argument("audio")
    p.add_argument("-o", "--out", required=True, help="arquivo .json do ChainSpec")
    add_llm_args(p)

    p = sub.add_parser("render", help="aplica um ChainSpec (ou a resposta crua do LLM) a um áudio")
    p.add_argument("spec")
    add_render_args(p)

    p = sub.add_parser("run", help="generate + render de uma vez")
    p.add_argument("prompt")
    add_render_args(p)
    add_llm_args(p)

    p = sub.add_parser("revise", help="novo pedido sobre uma cadeia existente: o LLM revisa e o áudio é refeito")
    p.add_argument("spec")
    p.add_argument("prompt")
    add_render_args(p)
    add_llm_args(p)

    args = parser.parse_args(argv)

    if args.command == "catalog":
        print(json.dumps(catalog_json(), ensure_ascii=False, indent=2) if args.json else catalog_text())
    elif args.command == "analyze":
        print(json.dumps(_features(args.audio, None), ensure_ascii=False, indent=2))
    elif args.command == "render":
        _render(_load_spec(args.spec), args)
    elif args.command == "prompt":
        from .intent import SYSTEM_PROMPT, build_user_message

        current = _load_spec(args.spec) if args.spec else None
        print(SYSTEM_PROMPT)
        print("## Formato da resposta\n\nResponda apenas com um objeto JSON, sem texto em volta, neste esquema:\n")
        print(json.dumps(LLMChain.model_json_schema(), ensure_ascii=False) + "\n")
        print(build_user_message(args.prompt, _features(args.audio, args.bpm), current, args.estilo))
    else:  # generate, run, revise
        from .intent import generate

        current = _load_spec(args.spec) if args.command == "revise" else None
        spec = generate(args.prompt, _features(args.audio, args.bpm), current, args.estilo)
        if args.command == "generate":
            _save_spec(spec, args.out)
            _describe(spec)
            print(f"gravado: {args.out} (ainda sem validação; o render valida)")
        else:
            spec_path = str(Path(args.out).with_suffix(".json"))
            _save_spec(spec, spec_path)
            print(f"spec do LLM gravado em: {spec_path}")
            _render(spec, args)


if __name__ == "__main__":
    main()
