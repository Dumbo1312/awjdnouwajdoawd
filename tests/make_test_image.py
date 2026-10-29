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
EFI_FV_FILETYPE_FREEFORM = 0x02
EFI_SECTION_PE32 = 0x10
EFI_SECTION_TE = 0x12
EFI_SECTION_USER_INTERFACE = 0x15
EFI_SECTION_RAW = 0x19

FFS_FIXED_CHECKSUM2 = 0xAA
EFI_FILE_HEADER_VALID = 0x02
EFI_FILE_DATA_VALID = 0x04

FV_HEADER_LENGTH = 0x48  # 56 byte header + 2 block map entries of 8 bytes
BLOCK_SIZE = 0x1000
TOTAL_BLOCKS = 16
FV_LENGTH = BLOCK_SIZE * TOTAL_BLOCKS

# The DXE apriori file's well known GUID, from the PI specification.
DXE_APRIORI_GUID = "fc510ee7-ffdc-11d4-bd41-0080c73c8881"

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


def ui_section(name):
    """A user interface section: the module's own name, UTF-16LE, terminated."""
    return section(EFI_SECTION_USER_INTERFACE, name.encode("utf-16-le") + b"\x00\x00")


# Fixture modules. The payloads embed byte sequences that the patch files
# search for, surrounded by filler so that offsets are never 0.
#
# GUIDs below fall into two groups. The first four are the real GUIDs of the
# Intel power management modules the upstream patch patterns target, because
# the patch tests have to find them. Everything after that is a made up GUID
# for a module that only exists in this fixture; they are deliberately
# obvious so nobody mistakes them for something real.
FILLER = bytes(range(0x40))

HASWELL_PATTERN = bytes.fromhex("75080FBAE80F89442430")
SKYLAKE_PATTERN = bytes.fromhex("75080D00800000")

FAKE_LOGO_GUID = "aaaa0001-0000-4000-8000-000000000001"
FAKE_CSM_GUID = "aaaa0002-0000-4000-8000-000000000002"
FAKE_BOOTMGR_GUID = "aaaa0003-0000-4000-8000-000000000003"

# (guid, file type, [(section type, payload), ...])
MODULES = [
    # Intel power management, Haswell. Two copies, the way a real image carries
    # the same module in more than one place, so --all-copies is observable.
    (
        "F7731B4C-58A2-4DF4-8980-5645D39ECE58",
        EFI_FV_FILETYPE_DRIVER,
        [
            ("ui", "PowerMgmtDxe"),
            (EFI_SECTION_PE32,
             FILLER + HASWELL_PATTERN + FILLER + HASWELL_PATTERN + FILLER),
        ],
    ),
    # SiInit in a TE section: covers section type matching, and its name is
    # what makes the scanner call this an Intel image.
    (
        "299D6F8B-2EC9-4E40-9EC6-DDAA7EBF5FD9",
        EFI_FV_FILETYPE_DRIVER,
        [
            ("ui", "SiInit"),
            (EFI_SECTION_TE, FILLER + SKYLAKE_PATTERN + FILLER),
        ],
    ),
    # A module with no interesting pattern, so "pattern not found" reporting
    # has something to report.
    (
        "3FFCAE95-23CF-4967-94F5-16352F68E43B",
        EFI_FV_FILETYPE_DRIVER,
        [
            ("ui", "PpmInitialize"),
            (EFI_SECTION_PE32, FILLER + b"no interesting instruction sequences here" + FILLER),
        ],
    ),
    # The second copy of PowerMgmtDxe.
    (
        "F7731B4C-58A2-4DF4-8980-5645D39ECE58",
        EFI_FV_FILETYPE_DRIVER,
        [
            ("ui", "PowerMgmtDxe"),
            (EFI_SECTION_PE32, FILLER + HASWELL_PATTERN + FILLER),
        ],
    ),

    # Debloat fodder. Named so the catalogue in knowledge/debloat.txt matches
    # them, which is what the debloat dialog is driven by.
    (
        "aaaa0010-0000-4000-8000-000000000010",
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "UefiPxeBcDxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    (
        "aaaa0011-0000-4000-8000-000000000011",
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "Ip4Dxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    (
        "aaaa0012-0000-4000-8000-000000000012",
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "Dhcp4Dxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    (
        "aaaa0013-0000-4000-8000-000000000013",
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "Mtftp4Dxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    (
        "aaaa0014-0000-4000-8000-000000000014",
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "StatusCodeHandlerDxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    # Referenced by BootManagerDxe below, so the cross reference check has a
    # module to find something against.
    (
        FAKE_LOGO_GUID,
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "LogoDxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    # Listed in the apriori file below, so the apriori check has one too.
    (
        FAKE_CSM_GUID,
        EFI_FV_FILETYPE_DRIVER,
        [("ui", "CsmDxe"), (EFI_SECTION_PE32, FILLER)],
    ),
    # Carries LogoDxe's GUID in its body, the way a module that loads another
    # by file GUID does.
    (
        FAKE_BOOTMGR_GUID,
        EFI_FV_FILETYPE_DRIVER,
        [
            ("ui", "BootManagerDxe"),
            (EFI_SECTION_PE32, FILLER + guid_bytes(FAKE_LOGO_GUID) + FILLER),
        ],
    ),
    # The DXE apriori list: a flat array of file GUIDs in dispatch order.
    (
        DXE_APRIORI_GUID,
        EFI_FV_FILETYPE_FREEFORM,
        [(EFI_SECTION_RAW, guid_bytes(FAKE_CSM_GUID))],
    ),
]


def build_sections(spec):
    """Turns the section shorthand above into real section bytes."""
    out = []
    for section_type, payload in spec:
        if section_type == "ui":
            out.append(ui_section(payload))
        else:
            out.append(section(section_type, payload))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_test_image.py <output.bin>")

    body = b"".join(
        ffs_file(guid, file_type, build_sections(spec))
        for guid, file_type, spec in MODULES
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
