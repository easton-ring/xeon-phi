import json
import zlib

import numpy as np
import pytest

from corruptions import CASES, make_case
from knc_rf_lab.config import load_config
from knc_rf_lab.dsformat import Dataset, DatasetError, align_up, write_dataset
from knc_rf_lab.generator import generate, generate_split, split_seed_sequence
from knc_rf_lab.validate import validate_file

from conftest import ROOT


def test_generate_byte_identical(tiny_dataset, tmp_path):
    generate(ROOT / "configs" / "tiny.yaml", tmp_path)
    for name in ("train.kncds", "val.kncds", "test.kncds", "train.json", "train.impairments.csv"):
        assert (tmp_path / name).read_bytes() == (tiny_dataset / name).read_bytes(), name


def test_different_seed_differs(tiny_dataset, tmp_path):
    generate(ROOT / "configs" / "tiny.yaml", tmp_path, overrides={"master_seed": 999})
    assert (tmp_path / "train.kncds").read_bytes() != (tiny_dataset / "train.kncds").read_bytes()


def test_reader_roundtrip(tiny_dataset):
    ds = Dataset(tiny_dataset / "train.kncds")
    cfg = load_config(ROOT / "configs" / "tiny.yaml")
    assert len(ds) == 6 * 8
    assert ds.iq.shape == (48, 256) and ds.iq.dtype == np.complex64
    assert ds.header.payload_offset % 64 == 0
    assert ds.header.payload_offset == align_up(64 + 8 * 48, 64)
    # balanced classes
    assert np.all(np.bincount(ds.class_id, minlength=8) == 6)
    iq, cid, snr, _ = generate_split(cfg, "train")
    np.testing.assert_array_equal(ds.iq, iq)
    np.testing.assert_array_equal(ds.class_id, cid)
    np.testing.assert_array_equal(ds.snr_db, snr)


def test_sidecar(tiny_dataset):
    meta = json.loads((tiny_dataset / "train.json").read_text())
    assert meta["class_map"]["bpsk"] == 0 and len(meta["class_map"]) == 8
    assert meta["config"]["master_seed"] == 12345
    assert validate_file(tiny_dataset / "train.kncds")["status"] == "ok"
    lines = (tiny_dataset / "train.impairments.csv").read_text().splitlines()
    assert len(lines) == 1 + 48


def test_split_seed_streams_disjoint():
    keys = {split_seed_sequence(1, s).spawn_key for s in ("train", "val", "test")}
    assert len(keys) == 3
    states = {tuple(split_seed_sequence(1, s).generate_state(4)) for s in ("train", "val", "test")}
    assert len(states) == 3


def test_examples_unique(tiny_dataset):
    ds = [Dataset(tiny_dataset / f"{s}.kncds") for s in ("train", "val", "test")]
    rows = np.concatenate([np.asarray(d.iq) for d in ds])
    hashes = {zlib.crc32(r.tobytes()) for r in rows}
    assert len(hashes) == len(rows)


def test_unit_power_and_snr():
    cfg = load_config(ROOT / "configs" / "tiny.yaml",
                      {"snr_db": [30.0, 30.0], "examples_per_class": {"train": 4}})
    iq, cid, snr, params = generate_split(cfg, "train")
    for x, p in zip(iq, params):
        g2 = p["fading_gain_re"] ** 2 + p["fading_gain_im"] ** 2
        assert np.mean(np.abs(x) ** 2) == pytest.approx(g2 + 1e-3, rel=0.05)


@pytest.mark.parametrize("case", sorted(CASES))
def test_corruption_rejected(tiny_dataset, tmp_path, case):
    bad = tmp_path / f"{case}.kncds"
    code = make_case(tiny_dataset / "train.kncds", bad, case)
    with pytest.raises(DatasetError) as ei:
        Dataset(bad)
    assert ei.value.code == code


def test_empty_dataset(tmp_path):
    write_dataset(tmp_path / "e.kncds", np.zeros((0, 16), np.complex64), [], [], 3)
    assert len(Dataset(tmp_path / "e.kncds")) == 0


def test_config_validation():
    with pytest.raises(ValueError):
        load_config(None, {"classes": ["bpsk", "nope"]})
    with pytest.raises(ValueError):
        load_config(None, {"snr_db": [5, -5]})
    with pytest.raises(ValueError):
        load_config(None, {"bogus": 1})
