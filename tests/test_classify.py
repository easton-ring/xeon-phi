import json
import warnings

import numpy as np
import pytest

pytest.importorskip("sklearn", reason="scikit-learn is in the [ml]/[dev] extras")

from knc_rf_lab import classify  # noqa: E402
from knc_rf_lab.features import NUM_FEATURES  # noqa: E402


@pytest.fixture(scope="module")
def trained(tiny_dataset, tmp_path_factory):
    out = tmp_path_factory.mktemp("models")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")  # MLP ConvergenceWarning on 48 examples
        classify.train(tiny_dataset, out)
    return out


def test_export_format(trained):
    txt = (trained / "logreg.txt").read_text().split("\n")
    assert txt[0] == "KNCMODEL 1"
    assert txt[1].split() == [str(NUM_FEATURES), "8", "1"]
    doc = json.loads((trained / "mlp.json").read_text())
    assert [L["activation"] for L in doc["layers"]] == ["relu", "relu", "identity"]
    assert doc["layers"][0]["in"] == NUM_FEATURES and doc["layers"][-1]["out"] == 8


def test_evaluate_outputs(tiny_dataset, trained, tmp_path):
    res = classify.evaluate(tiny_dataset, trained, tmp_path, shifted=tiny_dataset / "val.kncds")
    m = res["results"]["logreg"]["test"]
    assert m["n"] == 24 and 0.0 <= m["accuracy"] <= 1.0
    assert set(m["per_class"]) == set(res["classes"])
    assert sum(b["n"] for b in m["per_snr"]) == 24
    cm = np.loadtxt(tmp_path / "confusion_mlp_test.csv", delimiter=",", skiprows=1,
                    usecols=range(1, 9))
    assert cm.sum() == 24
    assert json.loads((tmp_path / "metrics.json").read_text())["results"]["mlp"]["shifted_test"]["n"] == 24


def test_c_inference_matches_sklearn(tiny_dataset, trained, tmp_path):
    rep = classify.infer_check(tiny_dataset, trained, tmp_path)
    for name, r in rep.items():
        assert r["unexplained_mismatches"] == [], name
        assert r["max_abs_logit_diff"] < 1e-6, name


def test_metrics_helpers():
    y = np.array([0, 0, 1, 1])
    p = np.array([0, 1, 1, 1])
    m = classify.metrics(y, p, np.array([-8.0, 3.0, 12.0, 19.0]), ["a", "b"])
    assert m["accuracy"] == 0.75 and m["per_class"]["a"]["accuracy"] == 0.5
    assert classify.confusion(y, p, 2).tolist() == [[1, 1], [0, 2]]
