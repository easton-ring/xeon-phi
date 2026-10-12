import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))


@pytest.fixture(scope="session")
def tiny_dataset(tmp_path_factory):
    """Generate the tiny config once per session; returns the output directory."""
    from knc_rf_lab.generator import generate

    out = tmp_path_factory.mktemp("tiny")
    generate(ROOT / "configs" / "tiny.yaml", out)
    return out
