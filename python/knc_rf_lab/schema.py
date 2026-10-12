"""Minimal JSON Schema validator for the subset used by our schemas.

Supports: type (incl. lists), const, enum, required, properties,
additionalProperties (bool), items, minimum, pattern, oneOf, $ref to
"#/$defs/...". See docs/decisions.md D-03 for why this is not `jsonschema`.
"""

from __future__ import annotations

import json
import re
from importlib import resources

_TYPES = {
    "object": dict, "array": list, "string": str, "boolean": bool, "null": type(None),
}


class SchemaError(ValueError):
    pass


def _is_type(v, t: str) -> bool:
    if t == "integer":
        return isinstance(v, int) and not isinstance(v, bool)
    if t == "number":
        return isinstance(v, (int, float)) and not isinstance(v, bool)
    return isinstance(v, _TYPES[t])


def _check(v, s: dict, root: dict, path: str) -> None:
    if "$ref" in s:
        ref = s["$ref"]
        assert ref.startswith("#/$defs/"), ref
        return _check(v, root["$defs"][ref[len("#/$defs/"):]], root, path)
    if "oneOf" in s:
        ok = 0
        errs = []
        for sub in s["oneOf"]:
            try:
                _check(v, sub, root, path)
                ok += 1
            except SchemaError as e:
                errs.append(str(e))
        if ok != 1:
            raise SchemaError(f"{path}: matched {ok} of oneOf branches: {errs}")
        return
    if "type" in s:
        ts = s["type"] if isinstance(s["type"], list) else [s["type"]]
        if not any(_is_type(v, t) for t in ts):
            raise SchemaError(f"{path}: expected {ts}, got {type(v).__name__}")
    if "const" in s and v != s["const"]:
        raise SchemaError(f"{path}: expected {s['const']!r}, got {v!r}")
    if "enum" in s and v not in s["enum"]:
        raise SchemaError(f"{path}: {v!r} not in {s['enum']}")
    if "minimum" in s and isinstance(v, (int, float)) and v < s["minimum"]:
        raise SchemaError(f"{path}: {v} < {s['minimum']}")
    if "pattern" in s and isinstance(v, str) and not re.search(s["pattern"], v):
        raise SchemaError(f"{path}: {v!r} does not match {s['pattern']}")
    if isinstance(v, dict):
        for k in s.get("required", []):
            if k not in v:
                raise SchemaError(f"{path}: missing required key {k!r}")
        props = s.get("properties", {})
        for k, sub in props.items():
            if k in v:
                _check(v[k], sub, root, f"{path}.{k}")
        if s.get("additionalProperties") is False:
            extra = set(v) - set(props)
            if extra:
                raise SchemaError(f"{path}: unexpected keys {sorted(extra)}")
    if isinstance(v, list) and "items" in s:
        for i, item in enumerate(v):
            _check(item, s["items"], root, f"{path}[{i}]")


def load_schema(name: str = "benchmark.schema.json") -> dict:
    return json.loads(resources.files("knc_rf_lab").joinpath("schemas", name).read_text())


def validate(doc, schema: dict | None = None, sub: str | None = None) -> None:
    """Raise SchemaError if invalid. ``sub`` validates against ``$defs/<sub>``."""
    schema = schema or load_schema()
    s = schema["$defs"][sub] if sub else schema
    _check(doc, s, schema, "$")
