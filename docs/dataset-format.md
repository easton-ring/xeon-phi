# KNCRFDS dataset format, v1.0

One file per split (`train.kncds`, `val.kncds`, `test.kncds`), each with a JSON
sidecar (`<split>.json`) and a per-example impairment table
(`<split>.impairments.csv`).

All integers are little-endian. Writers serialize the header **field by
field** (Python: `struct.pack_into` per field; never a C struct dump).

## Header (64 bytes)

| offset | bytes | type    | field                 | notes |
|-------:|------:|---------|-----------------------|-------|
| 0      | 8     | char[8] | `magic`               | `"KNCRFDS\0"` |
| 8      | 2     | u16     | `version_major`       | 1. Readers reject any other major version. |
| 10     | 2     | u16     | `version_minor`       | 0. Readers accept any minor version of major 1. |
| 12     | 4     | u32     | `header_size`         | must be 64 |
| 16     | 8     | u64     | `num_examples`        | N, may be 0 |
| 24     | 4     | u32     | `samples_per_example` | S > 0 |
| 28     | 4     | u32     | `num_classes`         | C > 0 |
| 32     | 4     | u32     | `sample_format`       | 1 = complex float32, interleaved I/Q |
| 36     | 4     | u32     | `flags`               | must be 0 in v1.0; any set bit is rejected |
| 40     | 8     | u64     | `labels_offset`       | must be 64 |
| 48     | 8     | u64     | `payload_offset`      | `align_up(64 + 8N, 64)` |
| 56     | 4     | u32     | `data_crc32`          | CRC-32 of labels bytes followed by payload bytes |
| 60     | 4     | u32     | `header_crc32`        | CRC-32 of bytes 0..63 with this field zeroed |

CRC-32 is the zlib/IEEE 802.3 polynomial (reflected 0xEDB88320, init and final
XOR 0xFFFFFFFF) — `zlib.crc32` in Python, `csrc/src/crc32.c` in C.

**Change from the proposed brief layout:** field 56 was `payload_crc32`. It is
now `data_crc32` and also covers the labels section, so a flipped label is
detected. The padding between labels and payload is excluded from the CRC but
must be all zero (both readers check it). See `docs/decisions.md` D-07.

## Labels section

At `labels_offset` (= 64): N records of 8 bytes, `{i32 class_id, f32 snr_db}`.
`class_id` must be in `[0, num_classes)`, `snr_db` must be finite.
`snr_db` is the nominal average (pre-fading) SNR — see "Signal model".

## Payload

At `payload_offset`, which is a multiple of 64 (one KNC cache line = one
512-bit vector register). N × S complex samples, each 8 bytes:
`I0 Q0 I1 Q1 …` as float32. Example `i` starts at
`payload_offset + i·S·8`. With S a multiple of 8, every example is also
64-byte aligned.

The file ends exactly at `payload_offset + N·S·8`; trailing bytes are an error.

## Validation (both readers, in this order)

| code            | condition |
|-----------------|-----------|
| `E_IO`          | file cannot be opened or read |
| `E_TRUNCATED`   | file shorter than 64 bytes |
| `E_MAGIC`       | magic mismatch |
| `E_VERSION`     | `version_major != 1` |
| `E_HEADER_SIZE` | `header_size != 64` |
| `E_HEADER_CRC`  | header CRC mismatch |
| `E_FIELD`       | unknown `sample_format`, non-zero `flags`, `S == 0` or `C == 0` |
| `E_OVERFLOW`    | `8N`, `labels_offset + 8N`, `8S`, `N·8S` or `payload_offset + N·8S` overflows u64 (checked arithmetic) |
| `E_OFFSET`      | `labels_offset` or `payload_offset` is past end of file |
| `E_ALIGN`       | `payload_offset % 64 != 0` |
| `E_LAYOUT`      | `labels_offset != 64`, `payload_offset != align_up(64+8N, 64)`, or non-zero padding |
| `E_TRUNCATED`   | payload ends past end of file |
| `E_TRAILING`    | bytes after the payload |
| `E_DATA_CRC`    | labels+payload CRC mismatch |
| `E_LABEL`       | `class_id` out of range or non-finite `snr_db` |

Each case has a test: `tests/corruptions.py` builds the corrupted file, and
`tests/test_dataset.py` (Python reader) and `tests/test_c_core.py` (C reader,
via `knc_validate`) assert the same error code. `csrc/tests/test_kncds.c`
checks the C reader without Python.

The Python reader (`knc_rf_lab.dsformat.Dataset`) exposes the payload as a
read-only `numpy.memmap` of dtype `<c8` and shape (N, S). The C reader
(`csrc/src/kncds.c`) validates the whole file once (streaming CRC) and then
reads batches of examples with `fseeko`/`fread` into caller buffers.

## JSON sidecar (`<split>.json`)

Deterministic (no timestamps) so it is byte-identical for a fixed config and
software stack. Keys:

- `format`, `format_version`, `split`, `file`, `sha256` (of the `.kncds` file)
- `num_examples`, `samples_per_example`, `sample_format`, `sample_rate_hz`
- `class_map` (name → id)
- `generator` (`name`, `version`), `config` (the full resolved config)
- `seeds` (master seed, split spawn key, bit generator, derivation)
- `software` (knc_rf_lab, numpy, python versions)
- `impairments` (CSV file name, its SHA-256, column list)
- `header_field_offsets`

## Impairment table (`<split>.impairments.csv`)

Always written (decision D-08), one row per example in file order:
`index, class_id, class_name, snr_db, freq_offset, phase_offset, fading,
fading_gain_re, fading_gain_im, sps, rrc_beta, timing_offset, start_offset,
mod_index, ofdm_qam_order`. `nan` means "not applicable to this class".

## Signal model (built-in generator)

```
x[n] = h · s[n] · exp(j(2π f n + φ)) + w[n],   n = 0 … S-1
```

- `s`: clean modulated signal, scaled to unit mean power over the S samples.
  - PSK/QAM: random symbols, RRC pulse (roll-off `rrc_beta`, `sps` samples per
    symbol, span 8 symbols), fractional timing offset `timing_offset` samples
    applied by evaluating the RRC at shifted instants; filter transient
    skipped. Constellations have unit average energy (QPSK at π/4).
  - 2/4-FSK: continuous-phase FSK, rectangular frequency pulse, modulation
    index `mod_index`, random symbol-boundary offset `start_offset`.
  - OFDM: 64-point IFFT, 16-sample CP, 52 active subcarriers (DC unused),
    QPSK or 16-QAM per example, random symbol-boundary offset.
- `h`: `none` → 1; `flat` → real gain from `flat_gain_db`; `rayleigh` →
  CN(0, 1) (block fading, constant over the example).
- `f`: frequency offset in cycles/sample; `φ`: phase offset.
- `w`: complex AWGN, variance `10^(-snr_db/10)`.

## Reproducibility

- Per example: `Generator(PCG64(child))` with
  `child = SeedSequence(master_seed, spawn_key=(crc32(split),)).spawn(1+N)[1+i]`;
  child 0 drives the class-order permutation. Splits use disjoint spawn keys.
- Byte-identical output for a fixed config is tested
  (`test_generate_byte_identical`) for one NumPy build on one machine.
- Across NumPy versions: the `SeedSequence` → `PCG64` state derivation and raw
  bit stream are stable by NumPy policy (NEP 19), but `Generator` methods
  (`uniform`, `integers`, `choice`, `standard_normal`, `permutation`) may change
  their algorithms between feature releases, and transcendental functions
  (`exp`, `sin`, `cos`) can differ in the last ulp between builds/CPUs (SIMD
  dispatch). So: identical within one NumPy version on one CPU family; expect
  small numeric differences across versions or machines. **The SHA-256 in the
  sidecar is the identity of a dataset**, not the config. Benchmark records
  carry it.
