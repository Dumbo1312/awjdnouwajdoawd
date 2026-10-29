#!/usr/bin/env python3
"""Build a minimal, synthetic UEFI firmware volume for UEFIPatch regression tests.

The image is *not* a real BIOS: it is the smallest structure UEFITool's
FfsEngine will parse down to a leaf PE32 section, so that patch application,
reporting and strict-mode behaviour can be exercised without touching
real hardware firmware.

Layout:

    EFI_FIRMWARE_VOLUME_HEADER (FFS2, rev 2, one block map entry)
      EFI_FFS_FILE_HEADER  (EFI_FV_FILETYPE_DRIVER)
        EFI_COMMON_SECTION_HEADER (EFI_SECTION_PE32)
          payload containing the patterns the test patch files look for
      ... one file per fixture module ...
      free space (0xFF)

Usage: python3 make_test_image.py <output.bin>
"""

import struct
import sys
import uuid

FFS2_GUID = uuid.UUID("8c8ce578-8a3d-4f1c-9935-896185c32dd3")
FV_SIGNATURE = b"_FVH"
FV_REVISION = 2
FV_ATTRIBUTES = 0x0004FEFF  # typical Aptio value, erase polarity bit (0x800) set

EFI_FV_FILETYPE_DRIVER = 0x07
EFI_SECTION_PE32 = 0x10
EFI_SECTION_TE = 0x12
EFI_SECTION_RAW = 0x19

FFS_FIXED_CHECKSUM2 = 0xAA
EFI_FILE_HEADER_VALID = 0x02
EFI_FILE_DATA_VALID = 0x04

FV_HEADER_LENGTH = 0x48  # 56 byte header + 2 block map entries of 8 bytes
BLOCK_SIZE = 0x1000
TOTAL_BLOCKS = 4
FV_LENGTH = BLOCK_SIZE * TOTAL_BLOCKS


def guid_bytes(text):
    """EFI_GUID wire layout: data1/data2/data3 little endian, data4 as-is."""
    return uuid.UUID(text).bytes_le


def sum8(data):
    return sum(data) & 0xFF


def sum16(data):
    assert len(data) % 2 == 0
    total = 0
    for i in range(0, len(data), 2):
        total += struct.unpack_from("<H", data, i)[0]
    return total & 0xFFFF


def section(section_type, payload):
    size = 4 + len(payload)
    assert size < 0xFFFFFF
    header = struct.pack("<I", size)[:3] + bytes([section_type])
    body = header + payload
    # sections inside a file are 4 byte aligned
    body += b"\x00" * (-len(body) % 4)
    return body


def ffs_file(file_guid, file_type, sections):
    body = b"".join(sections)
    size = 24 + len(body)
    assert size < 0xFFFFFF, "fixture files must fit a 24 byte header"

    name = guid_bytes(file_guid)
    attributes = 0x00  # no FFS_ATTRIB_CHECKSUM -> fixed data checksum
    state = EFI_FILE_HEADER_VALID | EFI_FILE_DATA_VALID

    # Header checksum is the 8 bit two's complement of the header sum with the
    # Checksum.Header, Checksum.File and State fields excluded.
    partial = (
        sum8(name)
        + FFS_FIXED_CHECKSUM2
        + file_type
        + attributes
        + sum8(struct.pack("<I", size)[:3])
    )
    header_checksum = (0x100 - ((partial - FFS_FIXED_CHECKSUM2) & 0xFF)) & 0xFF

    header = (
        name
        + bytes([header_checksum, FFS_FIXED_CHECKSUM2])
        + bytes([file_type, attributes])
        + struct.pack("<I", size)[:3]
        + bytes([state])
    )
    assert len(header) == 24
    out = header + body
    # files inside a volume are 8 byte aligned
    out += b"\xff" * (-len(out) % 8)
    return out


def fv_header():
    head = (
        b"\x00" * 16                              # ZeroVector
        + FFS2_GUID.bytes_le                      # FileSystemGuid
        + struct.pack("<Q", FV_LENGTH)            # FvLength
        + FV_SIGNATURE                            # Signature
        + struct.pack("<I", FV_ATTRIBUTES)        # Attributes
        + struct.pack("<H", FV_HEADER_LENGTH)     # HeaderLength
        + struct.pack("<H", 0)                    # Checksum placeholder
        + struct.pack("<H", 0)                    # ExtHeaderOffset
        + bytes([0])                              # Reserved
        + bytes([FV_REVISION])                    # Revision
        + struct.pack("<II", TOTAL_BLOCKS, BLOCK_SIZE)
        + struct.pack("<II", 0, 0)                # block map terminator
    )
    assert len(head) == FV_HEADER_LENGTH, len(head)
    checksum = (0x10000 - sum16(head)) & 0xFFFF
    return head[:0x32] + struct.pack("<H", checksum) + head[0x34:]


# Fixture modules. The payloads embed byte sequences that tests/patches-*.txt
# search for, surrounded by filler so that offsets are never 0.
FILLER = bytes(range(0x40))

MODULES = [
    # PowerMgmtDxe, Haswell MSR 0xE2 lock pattern (two occurrences, so the
    # "only the first match is patched" behaviour is observable).
    (
        "F7731B4C-58A2-4DF4-8980-5645D39ECE58",
        EFI_SECTION_PE32,
        FILLER
        + bytes.fromhex("75080FBAE80F89442430")
        + FILLER
        + bytes.fromhex("75080FBAE80F89442430")
        + FILLER,
    ),
    # SiInit, Skylake pattern in a TE section, to cover section type matching.
    (
        "299D6F8B-2EC9-4E40-9EC6-DDAA7EBF5FD9",
        EFI_SECTION_TE,
        FILLER + bytes.fromhex("75080D00800000") + FILLER,
    ),
    # A module with no interesting pattern at all, so that "pattern not found"
    # reporting has something to report.
    (
        "3FFCAE95-23CF-4967-94F5-16352F68E43B",
        EFI_SECTION_PE32,
        FILLER + b"no interesting instruction sequences here" + FILLER,
    ),
    # A second copy of PowerMgmtDxe, the way a real image carries the same
    # module in more than one volume. Without --all-copies only the first copy
    # above is patched and this one keeps its original bytes.
    (
        "F7731B4C-58A2-4DF4-8980-5645D39ECE58",
        EFI_SECTION_PE32,
        FILLER + bytes.fromhex("75080FBAE80F89442430") + FILLER,
    ),
]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_test_image.py <output.bin>")

    body = b"".join(
        ffs_file(guid, EFI_FV_FILETYPE_DRIVER, [section(stype, payload)])
        for guid, stype, payload in MODULES
    )

    image = fv_header() + body
    if len(image) > FV_LENGTH:
        sys.exit("fixture modules no longer fit in %#x bytes" % FV_LENGTH)
    image += b"\xff" * (FV_LENGTH - len(image))

    with open(sys.argv[1], "wb") as handle:
        handle.write(image)
    print("wrote %s (%d bytes, %d modules)" % (sys.argv[1], len(image), len(MODULES)))


if __name__ == "__main__":
    main()
