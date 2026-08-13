"""Shared binary container framing for SatView catalog assets.

Every SatView binary catalog uses the same little-endian container framing,
mirrored by the C++ reader in
``plugins/satview/product/include/draxul/satview/satview_catalog_container.h``:

  prologue (16 bytes, common to every catalog)::

      8s  magic          format tag, e.g. b"DXSTAR1\\0", b"DXCLINE1", b"DXCBND01"
      u32 version        format version (all shipped catalogs are version 1)
      u32 header_size    total header size in bytes; payload follows it

  single-table header (32 bytes, DXSTAR1 / DXCLINE1)::

      prologue + u32 record_size, u32 record_count, u32 flags, u32 source_id

  Multi-table catalogs (DXCBND01) append their own u32 fields to the
  prologue: per-table record sizes, counts and offsets, and string blob
  bounds. They are written with :func:`pack_header` plus
  :func:`pack_records` per table.

Compatibility and limits:

* Byte order is little-endian and structures are packed (no padding).
* Loaders reject tables with more than ``MAX_RECORD_COUNT`` records and
  string blobs larger than ``MAX_STRING_BYTES``; the writers here enforce
  the same limits so a bad catalog fails at build time, not at load time.
* Offsets and sizes are u32, so a container must stay below 4 GiB.
* The shipped assets use version 1 of each format. Do not bump a version or
  rewrite shipped assets without a deterministic migration plan that
  preserves provenance/attribution (see
  ``kanban/done/32 catalog-container-format -refactor.md``).
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Iterable, Sequence

MAGIC_SIZE = 8
PROLOGUE = struct.Struct("<8sII")
SINGLE_TABLE_HEADER_SIZE = 32

# Mirrors kCatalogContainerMaximumRecordCount / kCatalogContainerMaximumStringBytes
# in the C++ loader helper; keep the values in sync.
MAX_RECORD_COUNT = 1_000_000
MAX_STRING_BYTES = 1_000_000


def _validated_magic(magic: bytes) -> bytes:
    if not isinstance(magic, bytes) or len(magic) != MAGIC_SIZE:
        raise ValueError(f"catalog magic must be exactly {MAGIC_SIZE} bytes, got {magic!r}")
    return magic


def pack_prologue(magic: bytes, version: int, header_size: int) -> bytes:
    """Pack the 16-byte prologue shared by every catalog container."""
    return PROLOGUE.pack(_validated_magic(magic), version, header_size)


def pack_header(magic: bytes, version: int, header_size: int, fields: Sequence[int]) -> bytes:
    """Pack a full catalog header: the prologue plus little-endian u32 fields.

    ``header_size`` is validated against the packed size so a header whose
    declared size disagrees with its actual layout fails at build time.
    """
    header = pack_prologue(magic, version, header_size)
    header += struct.pack(f"<{len(fields)}I", *fields)
    if len(header) != header_size:
        raise ValueError(
            f"catalog header packs to {len(header)} bytes but header_size says {header_size}")
    return header


def pack_single_table_header(
    magic: bytes,
    version: int,
    record_size: int,
    record_count: int,
    *,
    flags: int = 0,
    source_id: int = 0,
) -> bytes:
    """Pack the 32-byte header used by single-table catalogs."""
    if record_count > MAX_RECORD_COUNT:
        raise ValueError(f"catalog has {record_count} records, limit is {MAX_RECORD_COUNT}")
    return pack_header(
        magic,
        version,
        SINGLE_TABLE_HEADER_SIZE,
        [record_size, record_count, flags, source_id],
    )


def pack_records(records: Iterable[bytes], record_size: int, table_name: str) -> bytes:
    """Concatenate fixed-size records, validating size and count."""
    payload = bytearray()
    count = 0
    for record in records:
        if len(record) != record_size:
            raise ValueError(
                f"{table_name} record {count} is {len(record)} bytes, expected {record_size}")
        payload += record
        count += 1
    if count > MAX_RECORD_COUNT:
        raise ValueError(f"{table_name} has {count} records, limit is {MAX_RECORD_COUNT}")
    return bytes(payload)


def write_single_table_catalog(
    path: Path,
    *,
    magic: bytes,
    version: int,
    record_size: int,
    records: Sequence[bytes],
    source_id: int,
    flags: int = 0,
) -> None:
    """Write a complete single-table catalog (DXSTAR1 / DXCLINE1 framing)."""
    payload = pack_records(records, record_size, "catalog")
    header = pack_single_table_header(
        magic, version, record_size, len(records), flags=flags, source_id=source_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(header)
        output.write(payload)


def unpack_single_table_header(data: bytes) -> tuple[bytes, int, int, int, int, int, int]:
    """Unpack (magic, version, header_size, record_size, record_count, flags,
    source_id) from the first 32 bytes of a single-table catalog."""
    if len(data) < SINGLE_TABLE_HEADER_SIZE:
        raise ValueError(f"catalog is {len(data)} bytes, smaller than the 32-byte header")
    return struct.unpack("<8sIIIIII", data[:SINGLE_TABLE_HEADER_SIZE])


def validate_single_table_catalog(
    data: bytes, *, magic: bytes, version: int, record_size: int
) -> int:
    """Validate the framing of a packed single-table catalog.

    Checks magic, version, header size, record size, record count limit, and
    payload bounds — the same rules as the C++ loader. Returns the record
    count. Raises ValueError when the framing is invalid.
    """
    header = unpack_single_table_header(data)
    (found_magic, found_version, header_size, found_record_size, record_count, _flags, _src) = header
    if found_magic != _validated_magic(magic):
        raise ValueError(f"catalog magic is {found_magic!r}, expected {magic!r}")
    if found_version != version:
        raise ValueError(f"catalog version is {found_version}, expected {version}")
    if header_size < SINGLE_TABLE_HEADER_SIZE:
        raise ValueError(f"catalog header_size {header_size} is smaller than 32")
    if found_record_size != record_size:
        raise ValueError(f"catalog record_size is {found_record_size}, expected {record_size}")
    if record_count > MAX_RECORD_COUNT:
        raise ValueError(f"catalog has {record_count} records, limit is {MAX_RECORD_COUNT}")
    if header_size + record_count * record_size > len(data):
        raise ValueError(
            f"catalog payload ({record_count} x {record_size} bytes at offset {header_size}) "
            f"does not fit in {len(data)} bytes")
    return record_count
