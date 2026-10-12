"""Feature-based modulation classifier: train, evaluate, export, C inference check.

    python -m knc_rf_lab.classify train      --data data/dev --out artifacts/models
    python -m knc_rf_lab.classify evaluate   --data data/dev --models artifacts/models --out results/eval \
                                             [--shifted data/shifted/test.kncds]
    python -m knc_rf_lab.classify infer_check --data data/dev --models artifacts/models --out results/eval

Models are scikit-learn LogisticRegression and MLPClassifier on standardized
features (docs/features.md). Both export to plain arrays ("KNCMODEL 1" text
for C, JSON for humans).
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

from .dsformat import Dataset, sha256_file
from .features import FEATURE_NAMES, features_batch

SNR_BINS = [(-10, -5), (-5, 0), (0, 5), (5, 10), (10, 15), (15, 20.0001)]
MODELS = ("logreg", "mlp")


def class_names(split_path: Path) -> list[str]:
    side = json.loads(split_path.with_suffix(".json").read_text())
    cmap = side["class_map"]
    return [c for c, _ in sorted(cmap.items(), key=lambda kv: kv[1])]


def load_features(path: Path, cache_dir: Path | None = None):
    """Features + labels for a split, cached by dataset SHA-256."""
    ds = Dataset(path)
    sha = sha256_file(path)
    cache = (cache_dir / f"features-{sha[:16]}.npy") if cache_dir else None
    if cache and cache.exists():
        X = np.load(cache)
    else:
        X = features_batch(np.asarray(ds.iq))
        if cache:
            cache.parent.mkdir(parents=True, exist_ok=True)
            np.save(cache, X)
    return X, ds.class_id.astype(np.int64), ds.snr_db.astype(np.float64), sha


def build_models(seed: int = 0):
    from sklearn.linear_model import LogisticRegression
    from sklearn.neural_network import MLPClassifier
    from sklearn.pipeline import make_pipeline
    from sklearn.preprocessing import StandardScaler

    return {
        "logreg": make_pipeline(StandardScaler(), LogisticRegression(max_iter=5000, C=1.0)),
        "mlp": make_pipeline(StandardScaler(), MLPClassifier(hidden_layer_sizes=(64, 32), activation="relu",
                                                             max_iter=2000, random_state=seed,
                                                             alpha=1e-3)),
    }


def export_model(pipe, names: list[str], out_base: Path) -> dict:
    """Write <out_base>.txt (KNCMODEL 1) and <out_base>.json. Returns the dict."""
    scaler, clf = pipe[0], pipe[-1]
    mean = scaler.mean_.astype(np.float64)
    scale = scaler.scale_.astype(np.float64)
    if hasattr(clf, "coefs_"):
        assert clf.activation == "relu"
        layers = [{"W": W.T, "b": b, "act": 1} for W, b in zip(clf.coefs_, clf.intercepts_)]
        layers[-1]["act"] = 0  # output: identity logits (softmax is monotone)
        kind = "mlp"
    else:
        layers = [{"W": clf.coef_, "b": clf.intercept_, "act": 0}]
        kind = "logreg"
    if list(clf.classes_) != list(range(len(names))):
        raise ValueError("model must be trained on all classes 0..C-1")
    lines = ["KNCMODEL 1", f"{len(mean)} {len(names)} {len(layers)}",
             " ".join(f"{v:.17g}" for v in mean), " ".join(f"{v:.17g}" for v in scale)]
    for L in layers:
        W = np.asarray(L["W"], np.float64)
        lines.append(f"{W.shape[1]} {W.shape[0]} {L['act']}")
        lines += [" ".join(f"{v:.17g}" for v in row) for row in W]
        lines.append(" ".join(f"{v:.17g}" for v in np.asarray(L["b"], np.float64)))
    out_base.parent.mkdir(parents=True, exist_ok=True)
    out_base.with_suffix(".txt").write_text("\n".join(lines) + "\n")
    doc = {"format": "KNCMODEL", "version": 1, "type": kind, "classes": names,
           "features": list(FEATURE_NAMES), "mean": mean.tolist(), "scale": scale.tolist(),
           "layers": [{"in": int(np.shape(L["W"])[1]), "out": int(np.shape(L["W"])[0]),
                       "activation": "relu" if L["act"] else "identity",
                       "W": np.asarray(L["W"]).tolist(), "b": np.asarray(L["b"]).tolist()} for L in layers]}
    out_base.with_suffix(".json").write_text(json.dumps(doc) + "\n")
    return doc


def logits_from_export(doc: dict, X: np.ndarray) -> np.ndarray:
    """Forward pass of an exported model in NumPy (same math as csrc/src/model.c)."""
    a = (X - np.array(doc["mean"])) / np.array(doc["scale"])
    for L in doc["layers"]:
        a = a @ np.array(L["W"]).T + np.array(L["b"])
        if L["activation"] == "relu":
            a = np.maximum(a, 0)
    return a


def train(data: Path, out: Path, seed: int = 0) -> dict:
    import pickle

    names = class_names(data / "train.kncds")
    Xtr, ytr, _, sha_tr = load_features(data / "train.kncds", out / "cache")
    Xva, yva, _, sha_va = load_features(data / "val.kncds", out / "cache")
    summary = {"train_sha256": sha_tr, "val_sha256": sha_va, "classes": names, "models": {}}
    for name, pipe in build_models(seed).items():
        t0 = time.perf_counter()
        pipe.fit(Xtr, ytr)
        dt = time.perf_counter() - t0
        doc = export_model(pipe, names, out / name)
        # exported arrays must reproduce sklearn exactly (up to float formatting)
        lg = logits_from_export(doc, Xva)
        if not np.array_equal(np.argmax(lg, axis=1), pipe.predict(Xva)):
            raise RuntimeError(f"{name}: exported model disagrees with sklearn on val")
        with open(out / f"{name}.pkl", "wb") as f:
            pickle.dump(pipe, f)
        acc = float(np.mean(pipe.predict(Xva) == yva))
        summary["models"][name] = {"val_accuracy": acc, "fit_seconds": dt,
                                   "train_accuracy": float(np.mean(pipe.predict(Xtr) == ytr))}
        print(f"{name:7s} train={summary['models'][name]['train_accuracy']:.3f} val={acc:.3f} ({dt:.1f}s)")
    (out / "train_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary


def metrics(y, pred, snr, names) -> dict:
    out = {"n": int(len(y)), "accuracy": float(np.mean(pred == y)), "per_class": {}, "per_snr": []}
    for i, c in enumerate(names):
        m = y == i
        out["per_class"][c] = {"n": int(m.sum()), "accuracy": float(np.mean(pred[m] == i)) if m.any() else None}
    for lo, hi in SNR_BINS:
        m = (snr >= lo) & (snr < hi)
        out["per_snr"].append({"snr_lo": lo, "snr_hi": round(hi), "n": int(m.sum()),
                               "accuracy": float(np.mean(pred[m] == y[m])) if m.any() else None})
    return out


def confusion(y, pred, k) -> np.ndarray:
    cm = np.zeros((k, k), dtype=np.int64)
    np.add.at(cm, (y, pred), 1)
    return cm


def evaluate(data: Path, models: Path, out: Path, shifted: Path | None = None) -> dict:
    import pickle

    names = class_names(data / "test.kncds")
    sets = {"test": data / "test.kncds"}
    if shifted:
        sets["shifted_test"] = shifted
    out.mkdir(parents=True, exist_ok=True)
    result = {"classes": names, "features": list(FEATURE_NAMES), "results": {}}
    for mname in MODELS:
        with open(models / f"{mname}.pkl", "rb") as f:
            pipe = pickle.load(f)
        result["results"][mname] = {}
        for sname, path in sets.items():
            if class_names(path) != names:
                raise ValueError(f"{path}: class map differs from training")
            X, y, snr, sha = load_features(path, models / "cache")
            pred = pipe.predict(X)
            m = metrics(y, pred, snr, names)
            m["dataset_sha256"] = sha
            result["results"][mname][sname] = m
            cm = confusion(y, pred, len(names))
            with open(out / f"confusion_{mname}_{sname}.csv", "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["true\\pred"] + names)
                for i, c in enumerate(names):
                    w.writerow([c] + cm[i].tolist())
            print(f"{mname:7s} {sname:13s} acc={m['accuracy']:.3f}  per-SNR: "
                  + " ".join(f"{b['snr_lo']:+d}:{b['accuracy']:.2f}" for b in m["per_snr"] if b["accuracy"] is not None))
    (out / "metrics.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def infer_check(data: Path, models: Path, out: Path, tie_margin: float = 1e-6) -> dict:
    """Run C inference (C features + C forward pass) and compare with sklearn on Python features."""
    import pickle

    from .cbin import find_binary

    exe = find_binary("knc_infer")
    X, y, _, _ = load_features(data / "test.kncds", models / "cache")
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for mname in MODELS:
        with open(models / f"{mname}.pkl", "rb") as f:
            pipe = pickle.load(f)
        doc = json.loads((models / f"{mname}.json").read_text())
        csv_path = out / f"c_predictions_{mname}.csv"
        subprocess.run([str(exe), "--model", str(models / f"{mname}.txt"), "--dataset",
                        str(data / "test.kncds"), "--out", str(csv_path)], check=True)
        rows = np.loadtxt(csv_path, delimiter=",", skiprows=1)
        c_pred = rows[:, 2].astype(np.int64)
        c_logits = rows[:, 4:]
        sk_pred = pipe.predict(X)
        py_logits = logits_from_export(doc, X)
        mism = np.nonzero(c_pred != sk_pred)[0]
        srt = np.sort(py_logits, axis=1)
        margins = srt[:, -1] - srt[:, -2]
        unexplained = [int(i) for i in mism if margins[i] >= tie_margin]
        report[mname] = {
            "n": int(len(y)), "mismatches": int(len(mism)),
            "mismatch_indices": mism.tolist(), "unexplained_mismatches": unexplained,
            "tie_margin": tie_margin,
            "max_abs_logit_diff": float(np.max(np.abs(c_logits - py_logits))),
            "c_accuracy": float(np.mean(c_pred == y)), "sklearn_accuracy": float(np.mean(sk_pred == y)),
        }
        print(f"{mname:7s} C vs sklearn: {len(mism)} mismatches of {len(y)} "
              f"(unexplained: {len(unexplained)}), max |logit diff| = {report[mname]['max_abs_logit_diff']:.2e}")
    (out / "infer_check.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m knc_rf_lab.classify")
    ap.add_argument("command", choices=["train", "evaluate", "infer_check"])
    ap.add_argument("--data", type=Path, required=True, help="directory with train/val/test.kncds")
    ap.add_argument("--models", type=Path, default=Path("artifacts/models"))
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--shifted", type=Path, default=None, help="shifted-channel test .kncds")
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args(argv)
    if a.command == "train":
        train(a.data, a.out or a.models, a.seed)
    elif a.command == "evaluate":
        evaluate(a.data, a.models, a.out or Path("results/eval"), a.shifted)
    else:
        rep = infer_check(a.data, a.models, a.out or Path("results/eval"))
        if any(r["unexplained_mismatches"] for r in rep.values()):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
