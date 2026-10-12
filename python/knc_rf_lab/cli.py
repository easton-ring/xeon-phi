"""Command-line entry point: ``knc-rf-lab <command>`` or ``python -m knc_rf_lab``."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path


def cmd_generate(a) -> int:
    from .generator import generate

    t0 = time.perf_counter()
    sidecars = generate(a.config, a.out)
    for s in sidecars:
        print(f"{s['split']:>5}: {s['num_examples']} examples x {s['samples_per_example']} "
              f"samples  sha256={s['sha256'][:16]}...")
    print(f"generated in {time.perf_counter() - t0:.2f} s -> {a.out}")
    return 0


def cmd_validate(a) -> int:
    from .validate import validate_file

    rc = 0
    for f in a.files:
        r = validate_file(f)
        print(json.dumps(r))
        rc |= r["status"] != "ok"
    if a.c:
        from .cbin import find_binary
        import subprocess

        exe = find_binary("knc_validate")
        p = subprocess.run([str(exe), *map(str, a.files)])
        rc |= p.returncode != 0
    return rc


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="knc-rf-lab")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate", help="generate a dataset from a YAML config")
    g.add_argument("--config", type=Path, default=None)
    g.add_argument("--out", type=Path, required=True)
    g.set_defaults(func=cmd_generate)
    v = sub.add_parser("validate", help="validate .kncds files")
    v.add_argument("files", nargs="+", type=Path)
    v.add_argument("--c", action="store_true", help="also run the C validator")
    v.set_defaults(func=cmd_validate)
    for name in ("benchmark", "train", "evaluate", "infer-check"):
        p = sub.add_parser(name, help=f"see python -m knc_rf_lab.{name.replace('-', '_')} --help")
        p.add_argument("rest", nargs=argparse.REMAINDER)
        p.set_defaults(func=None, mod=name.replace("-", "_"))
    a = ap.parse_args(argv)
    if a.func is None:
        import importlib

        mod = "classify" if a.mod in ("train", "evaluate", "infer_check") else a.mod
        m = importlib.import_module(f"knc_rf_lab.{mod}")
        return m.main(([a.mod] if mod == "classify" else []) + a.rest)
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())
