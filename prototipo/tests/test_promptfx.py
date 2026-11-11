import json
from pathlib import Path

import numpy as np
import pytest

from promptfx.analyze import analyze
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
    assert params["hs"]["gain_db"] == 3.0
    assert clean.macros[0].targets[0].value_at_1 == 3.0
    assert params["g"]["gain_db"] == pytest.approx(3.0)
    assert params["c"]["ratio"] == 3.0
    assert report

    dark_and_dynamic = {"spectral_centroid_hz": 900, "peak_dbfs": -20.0, "crest_factor_db": 15.0}
    clean, report = validate(spec, dark_and_dynamic)
    assert not report


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
    "summary": "Mais brilho.",
    "modules": [{"id": "ar", "type": "high_shelf", "params": [{"name": "gain_db", "value": 4}]}],
    "macros": [{"name": "Brilho", "description": "", "value": 0.5, "targets": [
        {"module_id": "ar", "param": "gain_db", "value_at_0": 0, "value_at_1": 8}]}],
}


def test_llm_answer_converts_to_canonical_spec():
    spec = LLMChain.model_validate(LLM_ANSWER).to_spec("mais brilho")
    assert spec.prompt == "mais brilho"
    assert spec.modules[0].params == {"gain_db": 4}
    clean, report = validate(spec)
    assert not report and clean.macros[0].name == "Brilho"


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
    assert spec.modules[0].type == "high_shelf"
    assert client.request["output_format"] is LLMChain
    assert "high_shelf" in client.request["system"][0]["text"]


@pytest.mark.parametrize("stop_reason,parsed", [("refusal", None), ("max_tokens", None), ("end_turn", None)])
def test_generate_raises_when_answer_is_unusable(stop_reason, parsed):
    from promptfx.intent import IntentError

    with pytest.raises(IntentError):
        generate("x", None, client=FakeClient(stop_reason, parsed))
