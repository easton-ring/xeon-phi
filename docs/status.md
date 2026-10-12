# Status matrix

Legend: **verified here** = tested in the cloud container (not the lab host);
**verified on hardware** = run on the real Xeon Phi 5110P with captured output;
**unverified** = written but not executed against its real target.

| component | status | evidence |
|-----------|--------|----------|
| Packaging (`pip install -e ".[dev]"`) | verified here | `.venv/bin/pip install -e ".[dev]"` |
| Config loading / validation | verified here | `pytest tests/test_dataset.py::test_config_validation` |
| Built-in generator, byte-identical | verified here | `pytest tests/test_dataset.py` |
| Python reader + corruption cases | verified here | `pytest tests/test_dataset.py -k corruption` |

(Updated at the end of every phase.)
