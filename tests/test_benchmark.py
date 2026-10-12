import copy
import json

import pytest

from knc_rf_lab.benchmark import KERNELS, host_threads, run_one, sweep
from knc_rf_lab.cbin import find_binary
from knc_rf_lab.schema import SchemaError, load_schema, validate


@pytest.fixture(scope="module")
def exe():
    return find_binary("knc_bench")


@pytest.mark.parametrize("kernel", KERNELS)
def test_each_kernel_record_valid(exe, tiny_dataset, kernel):
    prec = "f32"
    r = run_one(exe, kernel, tiny_dataset / "train.kncds", 4, 1, prec, reps=3, warmup=1)
    assert r["status"] == "ok", r
    validate(r, sub="ok_record")
    assert r["simulated"] is False and r["execution_mode"] == "host"
    assert r["timing"]["transfer"] is None
    assert r["input"]["sha256"] == json.loads((tiny_dataset / "train.json").read_text())["sha256"]
    assert r["metadata"]["git_commit"] != "unknown"
    assert r["metadata"]["cpu_model"] != "unknown"
    if kernel != "features":
        assert r["error"]["max_rel"] < 1e-5  # f32 kernels vs double reference


def test_f64_and_synthetic_and_wrap(exe, tiny_dataset):
    r = run_one(exe, "fft", None, 2, 1, "f64", reps=2, warmup=0, samples=512)
    validate(r, sub="ok_record")
    assert r["input"]["kind"] == "synthetic" and r["error"]["max_rel"] < 1e-12
    r = run_one(exe, "magsq", tiny_dataset / "test.kncds", 100, 1, "f32", reps=2, warmup=0)
    assert r["input"]["wrapped"] is True and r["batch"] == 100


def test_bad_invocation_is_unavailable_record(exe, tiny_dataset):
    r = run_one(exe, "fft", None, 2, 1, "f32", reps=2, warmup=0, samples=1000)  # not a power of two
    assert r["status"] == "unavailable" and "power-of-two" in r["reason"]
    validate(r, sub="missing_record")


def test_sweep_document(tiny_dataset):
    doc = sweep(tiny_dataset / "val.kncds", ["meanpow", "psd"], [1, 4], [1, 2], ["f32"], reps=2, warmup=0,
                log=lambda *a: None)
    validate(doc)
    ok = [r for r in doc["records"] if r["status"] == "ok"]
    assert len(ok) == 8
    skipped = {r["threads"] for r in doc["records"] if r["status"] == "skipped"}
    assert skipped == {30, 60, 120, 240}
    modes = {r.get("execution_mode") for r in doc["records"] if r["status"] == "unavailable"}
    assert modes == {"mic-native", "mic-offload"}


def test_schema_rejects_bad_records(exe, tiny_dataset):
    good = run_one(exe, "cmul", tiny_dataset / "train.kncds", 2, 1, "f32", reps=2, warmup=0)
    for mutate in (lambda r: r.update(simulated=True),
                   lambda r: r.update(execution_mode="gpu"),
                   lambda r: r["metadata"].pop("compiler"),
                   lambda r: r["timing"]["kernel"].pop("iqr"),
                   lambda r: r.update(extra=1),
                   lambda r: r["input"].update(sha256="xyz")):
        bad = copy.deepcopy(good)
        mutate(bad)
        with pytest.raises(SchemaError):
            validate(bad, sub="record")
    validate(good, sub="record")
    assert load_schema()["properties"]["schema_version"]["const"] == "1.0"


def test_host_threads():
    assert host_threads(4) == [1, 2, 4]
    assert host_threads(6) == [1, 2, 4, 6]
    assert host_threads(1) == [1]


def test_collect_jsonl(exe, tiny_dataset, tmp_path):
    from knc_rf_lab.benchmark import collect

    r = run_one(exe, "magsq", tiny_dataset / "train.kncds", 2, 1, "f32", reps=2, warmup=0)
    miss = {"schema_version": "1.0", "status": "unavailable", "execution_mode": "mic-native",
            "kernel": "fft", "threads": 240, "reason": "knc_bench failed on mic0 (exit 255)"}
    p = tmp_path / "raw.jsonl"
    p.write_text(json.dumps(r) + "\n" + json.dumps(miss) + "\n")
    doc = collect(p, "test")
    assert len(doc["records"]) == 2
    p.write_text(json.dumps({**r, "simulated": True}) + "\n")
    with pytest.raises(SchemaError):
        collect(p, "test")
