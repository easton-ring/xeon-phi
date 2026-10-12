"""KNCRFDS v1 binary dataset format: writer, validating reader.

The layout is specified in docs/dataset-format.md. The C reader in
csrc/src/kncds.c implements the same checks and reports the same error codes.
"""

from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAGIC = b"KNCRFDS\0"
VERSION_MAJOR = 1
VERSION_MINOR = 0
HEADER_SIZE = 64
PAYLOAD_ALIGN = 64
SAMPLE_FORMAT_CF32 = 1
LABEL_RECORD_SIZE = 8  # i32 class_id + f32 snr_db
BYTES_PER_SAMPLE = 8  # complex float32, interleaved I/Q
U64_MAX = (1 << 64) - 1

# Field layout: (name, struct code, offset). Little-endian, written one by one.
_FIELDS = (
    ("magic", "8s", 0),
    ("version_major", "<H", 8),
    ("version_minor", "<H", 10),
    ("header_size", "<I", 12),
    ("num_examples", "<Q", 16),
    ("samples_per_example", "<I", 24),
    ("num_classes", "<I", 28),
    ("sample_format", "<I", 32),
    ("flags", "<I", 36),
    ("labels_offset", "<Q", 40),
    ("payload_offset", "<Q", 48),
    ("data_crc32", "<I", 56),
    ("header_crc32", "<I", 60),
)
FIELD_OFFSETS = {name: off for name, _, off in _FIELDS}
FIELD_CODES = {name: code for name, code, _ in _FIELDS}

# Error codes shared with the C reader (csrc/include/knc/kncds.h).
ERROR_CODES = (
    "E_IO",
    "E_MAGIC",
    "E_VERSION",
    "E_HEADER_SIZE",
    "E_HEADER_CRC",
    "E_FIELD",
    "E_OVERFLOW",
    "E_LAYOUT",
    "E_OFFSET",
    "E_ALIGN",
    "E_TRUNCATED",
    "E_TRAILING",
    "E_DATA_CRC",
    "E_LABEL",
)


class DatasetError(Exception):
    """Validation failure. ``code`` is one of ERROR_CODES."""

    def __init__(self, code: str, message: str):
        assert code in ERROR_CODES, code
        super().__init__(f"{code}: {message}")
        self.code = code


@dataclass(frozen=True)
class Header:
    version_major: int
    version_minor: int
    header_size: int
    num_examples: int
    samples_per_example: int
    num_classes: int
    sample_format: int
    flags: int
    labels_offset: int
    payload_offset: int
    data_crc32: int
    header_crc32: int


def align_up(x: int, a: int) -> int:
    return (x + a - 1) // a * a


def _checked_mul(a: int, b: int) -> int:
    r = a * b
    if r > U64_MAX:
        raise DatasetError("E_OVERFLOW", f"{a} * {b} overflows u64")
    return r


def _checked_add(a: int, b: int) -> int:
    r = a + b
    if r > U64_MAX:
        raise DatasetError("E_OVERFLOW", f"{a} + {b} overflows u64")
    return r


def pack_header(fields: dict) -> bytes:
    """Serialize a header field by field; computes header_crc32 last."""
    buf = bytearray(HEADER_SIZE)
    for name, code, off in _FIELDS:
        if name == "header_crc32":
            continue
        val = MAGIC if name == "magic" else fields[name]
        struct.pack_into(code, buf, off, val)
    crc = zlib.crc32(bytes(buf)) & 0xFFFFFFFF  # header_crc32 bytes are zero here
    struct.pack_into("<I", buf, 60, crc)
    return bytes(buf)


def header_crc(raw: bytes) -> int:
    b = bytearray(raw[:HEADER_SIZE])
    b[60:64] = b"\0\0\0\0"
    return zlib.crc32(bytes(b)) & 0xFFFFFFFF


def write_dataset(path: str | Path, iq: np.ndarray, class_ids: np.ndarray,
                  snr_db: np.ndarray, num_classes: int) -> str:
    """Write a dataset file. ``iq`` is complex (N, S). Returns SHA-256 hex."""
    iq = np.ascontiguousarray(iq, dtype="<c8")
    n, s = iq.shape
    class_ids = np.asarray(class_ids, dtype="<i4")
    snr_db = np.asarray(snr_db, dtype="<f4")
    if class_ids.shape != (n,) or snr_db.shape != (n,):
        raise ValueError("label arrays must have shape (num_examples,)")
    labels = np.empty(n, dtype=[("class_id", "<i4"), ("snr_db", "<f4")])
    labels["class_id"] = class_ids
    labels["snr_db"] = snr_db
    labels_bytes = labels.tobytes()
    payload_bytes = iq.tobytes()
    labels_offset = HEADER_SIZE
    payload_offset = align_up(labels_offset + len(labels_bytes), PAYLOAD_ALIGN)
    pad = b"\0" * (payload_offset - labels_offset - len(labels_bytes))
    crc = zlib.crc32(labels_bytes)
    crc = zlib.crc32(payload_bytes, crc) & 0xFFFFFFFF
    header = pack_header(dict(
        version_major=VERSION_MAJOR, version_minor=VERSION_MINOR,
        header_size=HEADER_SIZE, num_examples=n, samples_per_example=s,
        num_classes=num_classes, sample_format=SAMPLE_FORMAT_CF32, flags=0,
        labels_offset=labels_offset, payload_offset=payload_offset,
        data_crc32=crc,
    ))
    blob = header + labels_bytes + pad + payload_bytes
    Path(path).write_bytes(blob)
    return hashlib.sha256(blob).hexdigest()


def parse_header(raw: bytes) -> Header:
    vals = {}
    for name, code, off in _FIELDS:
        if name == "magic":
            continue
        vals[name] = struct.unpack_from(code, raw, off)[0]
    return Header(**vals)


def validate_header(raw: bytes, file_size: int) -> Header:
    """Check everything that can be checked from the header and file size."""
    if len(raw) < HEADER_SIZE:
        raise DatasetError("E_TRUNCATED", f"file is {len(raw)} bytes, header needs {HEADER_SIZE}")
    if raw[:8] != MAGIC:
        raise DatasetError("E_MAGIC", f"bad magic {raw[:8]!r}")
    h = parse_header(raw)
    if h.version_major != VERSION_MAJOR:
        raise DatasetError("E_VERSION", f"unsupported version {h.version_major}.{h.version_minor}")
    if h.header_size != HEADER_SIZE:
        raise DatasetError("E_HEADER_SIZE", f"header_size {h.header_size} != {HEADER_SIZE}")
    if header_crc(raw) != h.header_crc32:
        raise DatasetError("E_HEADER_CRC", "header CRC mismatch")
    if h.sample_format != SAMPLE_FORMAT_CF32:
        raise DatasetError("E_FIELD", f"unknown sample_format {h.sample_format}")
    if h.flags != 0:
        raise DatasetError("E_FIELD", f"unknown flags 0x{h.flags:x}")
    if h.samples_per_example == 0 or h.num_classes == 0:
        raise DatasetError("E_FIELD", "samples_per_example and num_classes must be > 0")
    labels_bytes = _checked_mul(h.num_examples, LABEL_RECORD_SIZE)
    labels_end = _checked_add(h.labels_offset, labels_bytes)
    example_bytes = _checked_mul(h.samples_per_example, BYTES_PER_SAMPLE)
    payload_bytes = _checked_mul(h.num_examples, example_bytes)
    payload_end = _checked_add(h.payload_offset, payload_bytes)
    if h.labels_offset > file_size or h.payload_offset > file_size:
        raise DatasetError("E_OFFSET", "section offset past end of file")
    if h.payload_offset % PAYLOAD_ALIGN != 0:
        raise DatasetError("E_ALIGN", f"payload_offset {h.payload_offset} not {PAYLOAD_ALIGN}-byte aligned")
    if h.labels_offset != HEADER_SIZE or h.payload_offset != align_up(labels_end, PAYLOAD_ALIGN):
        raise DatasetError("E_LAYOUT", "labels must follow the header and payload must start "
                           "at the first 64-byte boundary after labels")
    if payload_end > file_size:
        raise DatasetError("E_TRUNCATED", f"payload ends at {payload_end}, file is {file_size} bytes")
    if payload_end < file_size:
        raise DatasetError("E_TRAILING", f"{file_size - payload_end} unexpected trailing bytes")
    return h


class Dataset:
    """Validated, memory-mapped dataset split.

    ``iq`` is a read-only complex64 memmap of shape (N, S). ``class_id`` and
    ``snr_db`` are in-memory arrays.
    """

    def __init__(self, path: str | Path, verify_crc: bool = True):
        self.path = Path(path)
        size = self.path.stat().st_size
        with open(self.path, "rb") as f:
            raw = f.read(HEADER_SIZE)
            h = validate_header(raw, size)
            f.seek(h.labels_offset)
            lab_raw = f.read(h.num_examples * LABEL_RECORD_SIZE)
            pad = f.read(h.payload_offset - h.labels_offset - len(lab_raw))
            if any(pad):
                raise DatasetError("E_LAYOUT", "non-zero padding before payload")
            if verify_crc:
                crc = zlib.crc32(lab_raw)
                while chunk := f.read(1 << 22):
                    crc = zlib.crc32(chunk, crc)
                if (crc & 0xFFFFFFFF) != h.data_crc32:
                    raise DatasetError("E_DATA_CRC", "labels+payload CRC mismatch")
        labels = np.frombuffer(lab_raw, dtype=[("class_id", "<i4"), ("snr_db", "<f4")])
        self.class_id = labels["class_id"].astype(np.int32)
        self.snr_db = labels["snr_db"].astype(np.float32)
        if np.any(self.class_id < 0) or np.any(self.class_id >= h.num_classes):
            raise DatasetError("E_LABEL", "class_id out of range")
        if not np.all(np.isfinite(self.snr_db)):
            raise DatasetError("E_LABEL", "non-finite snr_db")
        self.header = h
        if h.num_examples:
            self.iq = np.memmap(self.path, dtype="<c8", mode="r", offset=h.payload_offset,
                                shape=(h.num_examples, h.samples_per_example))
        else:
            self.iq = np.zeros((0, h.samples_per_example), dtype="<c8")

    def __len__(self) -> int:
        return self.header.num_examples


def sha256_file(path: str | Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 22):
            h.update(chunk)
    return h.hexdigest()
