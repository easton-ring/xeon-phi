"""TorchSig adapter tests. Skipped unless torchsig 2.2.0 is importable."""

import pytest

from conftest import ROOT

torchsig = pytest.importorskip("torchsig", reason="torchsig not installed (optional [torchsig] extra; "
                               "CPU-only torch index is unreachable here, see docs/torchsig.md)")


def test_torchsig_dataset_roundtrip(tmp_path):
    import numpy as np

    from knc_rf_lab.dsformat import Dataset
    from knc_rf_lab.generator import generate

    a = generate(ROOT / "configs" / "torchsig_tiny.yaml", tmp_path / "a")
    generate(ROOT / "configs" / "torchsig_tiny.yaml", tmp_path / "b")
    assert (tmp_path / "a/train.kncds").read_bytes() == (tmp_path / "b/train.kncds").read_bytes()
    ds = Dataset(tmp_path / "a/train.kncds")
    assert ds.iq.shape == (32, 1024)
    assert np.all(np.bincount(ds.class_id) == 4)
    assert a[0]["generator"]["name"] == "torchsig"
