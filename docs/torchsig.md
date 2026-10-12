# TorchSig adapter

Module: `python/knc_rf_lab/torchsig_adapter.py` (the only module that imports
`torchsig`). Select it with `generator: torchsig` in a config, for example
`configs/torchsig_tiny.yaml`.

## Status

| item | status | evidence |
|------|--------|----------|
| TorchSig 2.2.0 + torch 2.14.1 install on Python 3.13 | verified here | `pip install torchsig==2.2.0` in a separate venv (exit 0) |
| Adapter generates a balanced, deterministic dataset | verified here, with 2.2.0 only | `tests/test_torchsig.py`, log in `results/examples/torchsig_adapter_test.log` |
| Any other TorchSig version | UNVERIFIED | the adapter refuses any version other than 2.2.0 |
| In the main dev venv | skipped | `torchsig` isn't installed there; `pytest.importorskip` gives the reason |

### Why it is not in the default dev environment

- `download.pytorch.org` (the CPU-only wheel index) returns **403** through
  this container's proxy:
  `Tunnel connection failed: 403 Forbidden ... /whl/cpu/torch/`.
- So the only torch available is the PyPI wheel `torch 2.14.1+cu130`. It pulls
  about 3 GB of `nvidia-*` CUDA wheels, even though it runs on CPU here.
- TorchSig 2.2.0 also requires torchaudio, lightning, opencv-python, numba,
  scipy, h5py and soundfile.

That is too heavy for `[dev]`. The `[torchsig]` extra pins `torchsig==2.2.0`.
On the lab host, install CPU-only torch first:
`pip install torch --index-url https://download.pytorch.org/whl/cpu`.

## How the real API is used (read from the installed source)

Paths are relative to `site-packages/torchsig/` (2.2.0):

- `signals/signal_lists.py`: the real class names. Mapping:
  bpsk→`bpsk`, qpsk→`qpsk`, 8psk→`8psk`, 16qam→`16qam`, 64qam→`64qam`,
  2fsk→`2fsk`, 4fsk→`4fsk`, ofdm→`ofdm-64`.
- `utils/defaults.py`: `TorchSigDefaults().default_dataset_metadata`, a dict
  that we update with `num_iq_samples_dataset`, `num_signals_min/max = 1`,
  `snr_db_min/max`, `signal_duration_in_samples_min/max = N`,
  `signal_center_freq_min/max = 0`, `bandwidth_min/max`, `fft_size`.
- `datasets/datasets.py`: `TorchSigIterableDataset(signal_generators=[name],
  metadata=md, target_labels=None, seed=seed)`. `next(ds)` returns a `Signal`.
  `.data` is complex64 of shape (N,), and `.component_signals[0]` carries
  `class_name`, `snr_db` (rounded to 0.1 dB), `bandwidth`, `center_freq` and
  `sample_rate` (10 MHz default).
- There is no "narrowband" preset in 2.2.0. "Narrowband" here means one
  signal, centered at 0 Hz, filling the whole window.
- Balance: one iterable dataset per class, each seeded from
  `SeedSequence(master, spawn_key=(crc32(split), 1, class_idx))`. The class
  order is shuffled from a separate stream. Same seed → identical bytes
  (tested).

## Differences from the built-in generator

- Only `snr_db` and `samples_per_example` are passed through. Our frequency,
  phase and timing offsets and fading are **not** applied, and TorchSig's own
  impairment levels are not enabled.
- TorchSig's stored `bandwidth` is re-estimated from a coarse 64-point FFT;
  treat it as approximate.
- SNR definitions differ. TorchSig draws a target SNR (rounded to 0.1 dB) and
  rescales the signal using an SNR estimated from its spectrogram
  (`utils/dsp.py:1418-1456`, `update_signal_snr_bandwidth`). Ours is the
  average whole-band SNR (docs/dataset-format.md), so `snr_db` values are not
  directly comparable between the two generators.
