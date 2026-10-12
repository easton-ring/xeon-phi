"""Validate dataset files with the Python reader (and optionally the C reader)."""

from __future__ import annotations

import json
from pathlib import Path

from .dsformat import Dataset, DatasetError, sha256_file


def validate_file(path: str | Path) -> dict:
    path = Path(path)
    try:
        ds = Dataset(path, verify_crc=True)
    except DatasetError as e:
        return {"file": str(path), "status": "error", "code": e.code, "message": str(e)}
    out = {"file": str(path), "status": "ok", "num_examples": len(ds),
           "samples_per_example": ds.header.samples_per_example,
           "num_classes": ds.header.num_classes}
    sidecar = path.with_suffix(".json")
    if sidecar.exists():
        meta = json.loads(sidecar.read_text())
        sha = sha256_file(path)
        if meta.get("sha256") != sha:
            return {**out, "status": "error", "code": "E_SIDECAR",
                    "message": "SHA-256 does not match sidecar"}
        if meta.get("num_examples") != len(ds) or len(meta.get("class_map", {})) != ds.header.num_classes:
            return {**out, "status": "error", "code": "E_SIDECAR",
                    "message": "sidecar disagrees with header"}
        out["sha256"] = sha
    return out
