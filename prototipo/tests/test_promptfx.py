import json
from pathlib import Path

import numpy as np
import pytest

from promptfx.analyze import analyze
from promptfx.cli import _load_spec
from promptfx.intent import build_user_message, generate
from promptfx.macros import interpolate, resolve
from promptfx.modules import MAX_MODULES, MODULES
from promptfx.render import SAFETY_CEILING_DB, TAIL_SECONDS, process
from promptfx.spec import ChainSpec, LLMChain
from promptfx.validate import validate

SR = 44100
EXAMPLES = Path(__file__).parent.parent / "examples"


def sine(freq: float, peak_dbfs: float = -12.0, seconds: float = 1.0) -> np.ndarray:
    t = np.arange(int(seconds * SR)) / SR
    return (10 ** (peak_dbfs / 20) * np.sin(2 * np.pi * freq * t)).astype(np.float32)[None, :]


def spec_of(modules: list[dict], macros: list[dict] | None = None) -> ChainSpec:
    return ChainSpec.model_validate({"modules": modules, "macros": macros or []})


def load(name: str) -> ChainSpec:
    return ChainSpec.model_validate_json((EXAMPLES / name).read_text(encoding="utf-8"))


# ---------- análise ----------

def test_analyze_measures_level_and_centroid():
    f = analyze(sine(1000.0, peak_dbfs=-12.0), SR)
    assert f["peak_dbfs"] == pytest.approx(-12.0, abs=0.1)
    assert f["crest_factor_db"] == pytest.approx(3.0, abs=0.2)  # senoide: pico 3 dB acima da média
    assert f["spectral_centroid_hz"] == pytest.approx(1000, abs=30)
    assert max(f["band_energy_db"], key=f["band_energy_db"].get) == "mid"


def test_analyze_handles_silence_and_short_audio():
    f = analyze(np.zeros((2, 100), dtype=np.float32), SR)
    assert f["channels"] == 2 and np.isfinite(f["peak_dbfs"])


# ---------- validador ----------

def test_validate_clamps_fills_defaults_and_drops_unknowns():
    spec = spec_of([
        {"id": "c", "type": "compressor", "params": {"ratio": 20, "bogus": 1}},
        {"id": "x", "type": "exciter_neural", "params": {}},
    ])
    clean, report = validate(spec)
    assert [m.id for m in clean.modules] == ["c"]
    assert clean.modules[0].params["ratio"] == MODULES["compressor"].param("ratio").max
    assert set(clean.modules[0].params) == {p.name for p in MODULES["compressor"].params}
    assert len(report) == 3


def test_validate_limits_module_count_and_duplicate_ids():
    modules = [{"id": f"g{i}", "type": "gain", "params": {}} for i in range(MAX_MODULES + 2)]
    modules.insert(1, {"id": "g0", "type": "gain", "params": {}})
    clean, _ = validate(spec_of(modules))
    ids = [m.id for m in clean.modules]
    assert len(ids) == MAX_MODULES and len(set(ids)) == MAX_MODULES


def test_validate_handles_non_finite_values():
    clean, _ = validate(spec_of([{"id": "g", "type": "gain", "params": {"gain_db": float("nan")}}]))
    assert clean.modules[0].params["gain_db"] == 0.0


def test_validate_macros():
    spec = spec_of(
        [{"id": "hs", "type": "high_shelf", "params": {}}],
        [
            {"name": "A", "value": 3, "targets": [
                {"module_id": "hs", "param": "gain_db", "value_at_0": 0, "value_at_1": 40},
                {"module_id": "nope", "param": "gain_db", "value_at_0": 0, "value_at_1": 1},
            ]},
            {"name": "B", "targets": [{"module_id": "hs", "param": "gain_db", "value_at_0": 0, "value_at_1": 1}]},
        ],
    )
    clean, _ = validate(spec)
    assert [m.name for m in clean.macros] == ["A"]  # B só tinha um alvo já ocupado
    assert clean.macros[0].value == 1.0
    assert clean.macros[0].targets[0].value_at_1 == 12.0


def test_signal_rules_tighten_limits_for_the_source():
    spec = spec_of(
        [
            {"id": "hs", "type": "high_shelf", "params": {"gain_db": 9}},
            {"id": "g", "type": "gain", "params": {"gain_db": 10}},
            {"id": "c", "type": "compressor", "params": {"ratio": 8}},
        ],
        [{"name": "Brilho", "targets": [{"module_id": "hs", "param": "gain_db", "value_at_0": 0, "value_at_1": 9}]}],
    )
    bright_and_dense = {"spectral_centroid_hz": 5000, "peak_dbfs": -4.0, "crest_factor_db": 5.0}
    clean, report = validate(spec, bright_and_dense)
    params = {m.id: m.params for m in clean.modules}
    assert params["hs"]["gain_db"] == 6.0
    assert clean.macros[0].targets[0].value_at_1 == 6.0
    assert params["g"]["gain_db"] == pytest.approx(3.0)
    assert params["c"]["ratio"] == 3.0
    assert report

    dark_and_dynamic = {"spectral_centroid_hz": 900, "peak_dbfs": -20.0, "crest_factor_db": 15.0}
    clean, report = validate(spec, dark_and_dynamic)
    assert not report


def test_capping_a_macro_range_keeps_the_starting_sound():
    spec = spec_of(
        [{"id": "vol", "type": "gain", "params": {}}],
        [{"name": "Volume", "value": 0.5, "targets": [{"module_id": "vol", "param": "gain_db", "value_at_0": -12, "value_at_1": 12}]}],
    )
    clean, report = validate(spec, {"peak_dbfs": -9.0})  # 8 dB de folga
    target = clean.macros[0].targets[0]
    assert report and target.value_at_1 == pytest.approx(8.0)
    assert resolve(clean)["vol"]["gain_db"] == pytest.approx(0.0)  # continua começando em 0 dB
    assert resolve(clean, {"Volume": 1.0})["vol"]["gain_db"] == pytest.approx(8.0)


# ---------- macros ----------

def test_interpolate_linear_and_log():
    assert interpolate(0, 10, 0.5, "lin") == 5
    assert interpolate(100, 10000, 0.5, "log") == pytest.approx(1000)
    assert interpolate(0, 10, 7, "lin") == 10  # posição fora de 0..1 é limitada


def test_resolve_macro_overrides_static_value():
    spec, _ = validate(load("voz_calorosa.json"))
    assert resolve(spec)["fita"]["drive_db"] == pytest.approx(8.0)
    assert resolve(spec, {"calor": 1.0})["fita"]["drive_db"] == pytest.approx(16.0)
    assert resolve(spec, {"Calor": 0.0})["eq_topo"]["gain_db"] == pytest.approx(0.0)
    with pytest.raises(KeyError):
        resolve(spec, {"Inexistente": 0.5})


# ---------- render ----------

@pytest.mark.parametrize("module_type", sorted(MODULES))
def test_every_module_renders(module_type):
    spec, _ = validate(spec_of([{"id": "m", "type": module_type, "params": {}}]))
    out = process(sine(440.0), SR, spec)
    assert out.shape == (1, SR + int(TAIL_SECONDS * SR))
    assert np.isfinite(out).all() and np.abs(out).max() > 0


def test_output_never_exceeds_safety_ceiling():
    spec, _ = validate(load("exagerado.json"))
    out = process(sine(3000.0, peak_dbfs=-0.5), SR, spec)
    assert 20 * np.log10(np.abs(out).max()) <= SAFETY_CEILING_DB + 0.01


def test_empty_chain_is_transparent():
    x = sine(440.0)
    out = process(x, SR, ChainSpec())
    assert np.allclose(out[:, : x.shape[1]], x, atol=1e-3)


def test_filters_actually_change_the_spectrum():
    spec, _ = validate(spec_of([{"id": "lp", "type": "lowpass", "params": {"freq_hz": 500}}]))
    x = sine(200.0) + sine(8000.0)
    before, after = analyze(x, SR), analyze(process(x, SR, spec), SR)
    assert after["spectral_centroid_hz"] < before["spectral_centroid_hz"] / 2


def test_match_loudness_keeps_average_level():
    spec, _ = validate(spec_of([{"id": "g", "type": "gain", "params": {"gain_db": -12}}]))
    x = sine(440.0)
    out = process(x, SR, spec, match_loudness=True)
    assert analyze(out[:, : x.shape[1]], SR)["rms_dbfs"] == pytest.approx(analyze(x, SR)["rms_dbfs"], abs=0.2)


def test_stereo_is_preserved():
    spec, _ = validate(load("voz_calorosa.json"))
    x = np.concatenate([sine(300.0), sine(500.0)])
    assert process(x, SR, spec).shape[0] == 2


# ---------- intenção (sem rede) ----------

LLM_ANSWER = {
    "title": "Brilho Suave",
    "summary": "Mais brilho.",
    "details": "Um high shelf abre o ar.",
    "suggestions": ["menos brilho", "adiciona reverb"],
    "modules": [{"id": "ar", "type": "high_shelf", "params": [{"name": "gain_db", "value": 4}]}],
    "macros": [{"name": "Brilho", "description": "", "value": 0.5, "targets": [
        {"module_id": "ar", "param": "gain_db", "value_at_0": 0, "value_at_1": 8}]}],
}


def test_llm_answer_converts_to_canonical_spec():
    spec = LLMChain.model_validate(LLM_ANSWER).to_spec(["mais brilho"])
    assert spec.prompts == ["mais brilho"] and spec.title == "Brilho Suave"
    assert spec.modules[0].params == {"gain_db": 4}
    clean, report = validate(spec)
    assert not report and clean.macros[0].name == "Brilho"
    assert clean.suggestions == ["menos brilho", "adiciona reverb"]


def test_llm_schema_rejects_unknown_module_type():
    bad = json.loads(json.dumps(LLM_ANSWER))
    bad["modules"][0]["type"] = "exciter_neural"
    with pytest.raises(ValueError):
        LLMChain.model_validate(bad)


def test_user_message_carries_prompt_and_analysis():
    message = build_user_message("  mais brilho  ", {"peak_dbfs": -9.0})
    assert "<pedido>\nmais brilho\n</pedido>" in message and '"peak_dbfs": -9.0' in message
    assert "indisponível" in build_user_message("x", None)


class FakeClient:
    """Imita só o caminho client.beta.messages.parse usado por generate()."""

    def __init__(self, stop_reason="end_turn", parsed=LLM_ANSWER):
        self.beta = self.messages = self
        self._stop, self._parsed = stop_reason, parsed
        self.request = None

    def parse(self, **kwargs):
        self.request = kwargs
        parsed = LLMChain.model_validate(self._parsed) if self._parsed else None
        return type("Response", (), {"stop_reason": self._stop, "parsed_output": parsed})()


def test_generate_builds_request_and_returns_spec():
    client = FakeClient()
    spec = generate("mais brilho", {"peak_dbfs": -9.0}, client=client)
    assert spec.modules[0].type == "high_shelf" and spec.prompts == ["mais brilho"]
    assert client.request["output_format"] is LLMChain
    assert "high_shelf" in client.request["system"][0]["text"]


@pytest.mark.parametrize("stop_reason,parsed", [("refusal", None), ("max_tokens", None), ("end_turn", None)])
def test_generate_raises_when_answer_is_unusable(stop_reason, parsed):
    from promptfx.intent import IntentError

    with pytest.raises(IntentError):
        generate("x", None, client=FakeClient(stop_reason, parsed))


# ---------- conversa em sequência ----------

def test_revision_message_carries_current_chain_and_history():
    current, _ = validate(load("voz_calorosa.json"))
    message = build_user_message("menos eco", {"peak_dbfs": -9.0}, current, "tecnico")
    assert "<cadeia_atual>" in message and '"id": "eco"' in message
    assert "- Quero uma voz calorosa" in message
    assert "tecnico:" in message and message.rstrip().endswith("</pedido>")


def test_generate_revision_appends_to_history():
    current, _ = validate(load("voz_calorosa.json"))
    spec = generate("menos eco", None, current=current, client=FakeClient())
    assert spec.prompts == [current.prompts[0], "menos eco"]


def test_unknown_style_is_rejected():
    with pytest.raises(ValueError):
        build_user_message("x", None, style="poetico")


def test_version_1_spec_still_loads():
    spec = ChainSpec.model_validate({"version": 1, "prompt": "antigo", "modules": []})
    assert spec.prompts == ["antigo"]


def test_raw_llm_answers_load_even_without_the_newer_fields():
    for path in sorted((EXAMPLES / "gerados").glob("*.json")):
        spec, report = validate(_load_spec(str(path)))
        assert spec.modules and spec.macros, path.name


# ---------- módulos novos ----------

def noise(seconds: float = 1.0, peak_dbfs: float = -12.0, seed: int = 0) -> np.ndarray:
    x = np.random.default_rng(seed).standard_normal(int(seconds * SR))
    return (x / np.abs(x).max() * 10 ** (peak_dbfs / 20)).astype(np.float32)[None, :]


def band_db(audio: np.ndarray, band: str) -> float:
    f = analyze(audio, SR)
    return f["rms_dbfs"] + f["band_energy_db"][band]


def render_one(module_type: str, params: dict, x: np.ndarray) -> np.ndarray:
    spec, report = validate(spec_of([{"id": "m", "type": module_type, "params": params}]))
    assert not report
    return process(x, SR, spec)[:, : x.shape[1]]


def test_de_esser_tames_loud_highs_and_leaves_lows_alone():
    x = sine(200.0, peak_dbfs=-12.0) + noise(peak_dbfs=-6.0)
    out = render_one("de_esser", {"freq_hz": 6000, "threshold_db": -40, "max_reduction_db": 9}, x)
    assert band_db(out, "air") < band_db(x, "air") - 4
    assert band_db(out, "low") == pytest.approx(band_db(x, "low"), abs=0.5)


def test_de_esser_ignores_signal_below_threshold():
    x = sine(200.0) + noise(peak_dbfs=-50.0)
    out = render_one("de_esser", {"threshold_db": -10}, x)
    assert np.allclose(out, x, atol=1e-4)


def drum_hits() -> np.ndarray:
    t = np.arange(SR) / SR
    x = np.zeros(SR)
    for start in (0.1, 0.4, 0.7):
        n = int(start * SR)
        x[n:] += np.sin(2 * np.pi * 120 * t[: SR - n]) * np.exp(-8 * t[: SR - n])
    return (0.25 * x).astype(np.float32)[None, :]


def test_transient_shaper_changes_crest_factor_in_both_directions():
    x = drum_hits()
    base = analyze(x, SR)["crest_factor_db"]
    punchier = analyze(render_one("transient_shaper", {"attack_db": 9, "sustain_db": -6}, x), SR)["crest_factor_db"]
    softer = analyze(render_one("transient_shaper", {"attack_db": -9, "sustain_db": 6}, x), SR)["crest_factor_db"]
    assert punchier > base + 1 and softer < base - 1


def test_transient_shaper_at_zero_is_transparent():
    x = drum_hits()
    assert np.allclose(render_one("transient_shaper", {}, x), x, atol=1e-5)


def test_delay_filters_darken_only_the_repeats():
    x = noise(seconds=0.2)
    x = np.concatenate([x, np.zeros((1, SR), dtype=np.float32)], axis=1)
    base = {"time_ms": 300, "feedback": 0.0, "mix": 0.5}
    bright = render_one("delay", base, x)
    dark = render_one("delay", {**base, "lowpass_hz": 1500}, x)
    echo = slice(int(0.3 * SR), int(0.5 * SR))
    direct = slice(0, int(0.2 * SR))
    assert band_db(dark[:, echo], "air") < band_db(bright[:, echo], "air") - 10
    assert band_db(dark[:, direct], "air") == pytest.approx(band_db(bright[:, direct], "air"), abs=0.5)


def test_parallel_compression_keeps_more_of_the_peaks():
    x = drum_hits()
    settings = {"threshold_db": -40, "ratio": 8, "attack_ms": 0.5, "release_ms": 100}
    full = render_one("compressor", settings, x)
    parallel = render_one("compressor", {**settings, "mix": 0.5}, x)
    assert np.abs(full).max() < np.abs(parallel).max() < np.abs(x).max()


def test_tape_tone_rolls_off_highs():
    x = noise()
    open_tape = render_one("tape", {"drive_db": 0, "tone_hz": 20000, "wow": 0}, x)
    slow_tape = render_one("tape", {"drive_db": 0, "tone_hz": 4000, "wow": 0}, x)
    assert band_db(slow_tape, "air") < band_db(open_tape, "air") - 3


# ---------- casos de referência ----------

def test_reference_cases_are_consistent_with_the_catalog():
    import importlib.util

    path = Path(__file__).parent.parent / "scripts" / "avaliar_casos.py"
    module_spec = importlib.util.spec_from_file_location("avaliar_casos", path)
    avaliar = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(avaliar)

    cases = json.loads(avaliar.CASES.read_text(encoding="utf-8"))["casos"]
    assert len(cases) == 10
    for case in cases:
        assert case["audio_teste"] in avaliar.AUDIO and case["pedidos"]
        for group in case["modulos_esperados"]:
            assert avaliar.GROUPS.get(group, {group}) <= set(MODULES), group

    # Com um LLM falso que sempre devolve um high shelf, só passa quem espera apenas EQ.
    def fake_generate(prompt, features, current=None):
        return LLMChain.model_validate(LLM_ANSWER).to_spec([prompt])

    results = avaliar.evaluate(cases, lambda kind: None, fake_generate, validate)
    assert len(results) == 10 and all("eq" not in r["faltando"] for r in results)
    assert next(r for r in results if r["id"] == "caixa_reverb_reggae")["faltando"] == ["reverb"]
