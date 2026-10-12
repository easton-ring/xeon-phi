"""Build corrupted copies of a valid dataset file; shared by Python and C reader tests.

Each case maps to the error code both readers must report. Field corruptions
recompute the header CRC so the intended check (not E_HEADER_CRC) fires.
"""

from __future__ import annotations

import struct
from pathlib import Path

from knc_rf_lab.dsformat import FIELD_CODES, FIELD_OFFSETS, header_crc


def _set(raw: bytearray, field: str, value: int, fix_crc: bool = True) -> bytearray:
    struct.pack_into(FIELD_CODES[field], raw, FIELD_OFFSETS[field], value)
    if fix_crc:
        struct.pack_into("<I", raw, 60, header_crc(bytes(raw)))
    return raw


def _flip_payload(raw: bytearray) -> bytearray:
    raw[-5] ^= 0x01
    return raw


def _flip_label(raw: bytearray) -> bytearray:
    raw[64 + 4] ^= 0x80  # sign bit region of first snr_db -> CRC must catch it
    return raw


def _bad_label(raw: bytearray) -> bytearray:
    # Out-of-range class id with a *recomputed* data CRC: only the label check catches it.
    import zlib
    struct.pack_into("<i", raw, 64, 999)
    n = struct.unpack_from("<Q", raw, 16)[0]
    off = struct.unpack_from("<Q", raw, 48)[0]
    crc = zlib.crc32(bytes(raw[64:64 + 8 * n]))
    crc = zlib.crc32(bytes(raw[off:]), crc) & 0xFFFFFFFF
    return _set(raw, "data_crc32", crc)


# name -> (mutator(bytearray) -> bytearray, expected error code)
CASES = {
    "bad_magic": (lambda r: (r.__setitem__(slice(0, 8), b"KNCRFDX\0"), r)[1], "E_MAGIC"),
    "bad_version": (lambda r: _set(r, "version_major", 2), "E_VERSION"),
    "bad_header_size": (lambda r: _set(r, "header_size", 128), "E_HEADER_SIZE"),
    "header_crc": (lambda r: _set(r, "num_classes", 9, fix_crc=False), "E_HEADER_CRC"),
    "bad_sample_format": (lambda r: _set(r, "sample_format", 7), "E_FIELD"),
    "unknown_flags": (lambda r: _set(r, "flags", 1), "E_FIELD"),
    "zero_samples": (lambda r: _set(r, "samples_per_example", 0), "E_FIELD"),
    "overflow_num_examples": (lambda r: _set(r, "num_examples", (1 << 62) + 3), "E_OVERFLOW"),
    "overflow_payload": (lambda r: _set(r, "num_examples", (1 << 64) // (8 * 256) + 1), "E_OVERFLOW"),
    "overflow_offset": (lambda r: _set(r, "payload_offset", (1 << 64) - 64), "E_OVERFLOW"),
    "offset_past_eof": (lambda r: _set(r, "payload_offset", 1 << 40), "E_OFFSET"),
    "labels_offset_past_eof": (lambda r: _set(r, "labels_offset", 1 << 40), "E_OFFSET"),
    "misaligned_payload": (lambda r: _set(r, "payload_offset", struct.unpack_from("<Q", r, 48)[0] + 4), "E_ALIGN"),
    "bad_layout": (lambda r: _set(r, "payload_offset", struct.unpack_from("<Q", r, 48)[0] + 64), "E_LAYOUT"),
    "truncated_payload": (lambda r: r[:-100], "E_TRUNCATED"),
    "truncated_header": (lambda r: r[:40], "E_TRUNCATED"),
    "trailing_bytes": (lambda r: r + b"\0" * 8, "E_TRAILING"),
    "payload_crc": (_flip_payload, "E_DATA_CRC"),
    "label_crc": (_flip_label, "E_DATA_CRC"),
    "label_out_of_range": (_bad_label, "E_LABEL"),
}


def make_case(src: Path, dst: Path, name: str) -> str:
    mut, code = CASES[name]
    raw = mut(bytearray(src.read_bytes()))
    dst.write_bytes(bytes(raw))
    return code
