#!/usr/bin/env python3
"""Find candidate MSR lock sites in an extracted UEFI module.

Byte patterns for UEFIPatch are specific to one vendor, one board and often
one BIOS build, so a generic pattern list can not exist. This script does the
first step of deriving your own: it locates every rdmsr/wrmsr in a module,
attributes each one to an MSR number where it can, and prints the surrounding
bytes so you can pick a find pattern out of them.

It is a byte scanner, not a disassembler. Every result is a candidate that you
confirm in a real disassembler before you patch anything.

Typical use:

    # 1. get the modules out of a dump
    UEFIExtract bios.bin
    # 2. scan the power management module's PE32 section
    tools/msr-lock-scan.py bios.bin.dump/.../body.bin
    # 3. confirm the site in a disassembler, then write the patch line
    UEFIPatch bios.bin -p "<guid> 10 P:<find>:<replace>" -n -v

Usage:
    msr-lock-scan.py <file> [<file>...] [--context N] [--all] [--arch 32|64]
"""

import argparse
import sys

# MSRs worth a closer look when hunting for firmware locks. The comment is
# why the register matters, not a claim that patching it is a good idea.
INTERESTING_MSRS = {
    0x0CE: "MSR_PLATFORM_INFO - ratio limits, programmable ratio and TDP lock bits",
    0x0E2: "MSR_PKG_CST_CONFIG_CONTROL - package C-state limit plus CFG_LOCK in bit 15",
    0x0E4: "MSR_PMG_IO_CAPTURE_BASE - C-state IO redirection",
    0x194: "MSR_FLEX_RATIO - the ratio firmware hands the OS",
    0x198: "MSR_IA32_PERF_STATUS",
    0x199: "MSR_IA32_PERF_CTL - the ratio and voltage request the OS makes",
    0x19C: "MSR_IA32_THERM_STATUS",
    0x1A0: "MSR_IA32_MISC_ENABLE - EIST, turbo disable, prefetcher bits",
    0x1A2: "MSR_TEMPERATURE_TARGET - TCC offset",
    0x1AA: "MSR_MISC_PWR_MGMT - EIST hardware coordination and its lock bit",
    0x1AD: "MSR_TURBO_RATIO_LIMIT - per core count turbo ratios",
    0x1FC: "MSR_POWER_CTL - C1E and energy efficient turbo",
    0x610: "MSR_PKG_POWER_LIMIT - PL1 and PL2, with a lock bit in 63",
    0x614: "MSR_PKG_POWER_INFO - TDP window",
    0x618: "MSR_DRAM_POWER_LIMIT",
    0x64C: "MSR_TURBO_ACTIVATION_RATIO",
    0x150: "MSR_OC_MAILBOX - undervolt and overclock mailbox on many client parts",
}

RDMSR = b"\x0f\x32"
WRMSR = b"\x0f\x30"

# 0F BA /r ib is the immediate form of the bit instructions. The /r field of
# the ModRM byte picks which one, and the imm8 is the bit number: firmware
# lock bits are very often set with one of these, so a BTS of bit 15 right
# before a wrmsr of 0xE2 is the classic CFG_LOCK site.
BIT_OPS = {4: "bt", 5: "bts", 6: "btr", 7: "btc"}
REG_NAMES = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]


def find_all(haystack, needle):
    offset = haystack.find(needle)
    while offset >= 0:
        yield offset
        offset = haystack.find(needle, offset + 1)


def attribute_msr(data, site, lookback):
    """Walk back from an rdmsr/wrmsr looking for 'mov ecx, imm32' (B9 imm32).

    Returns (msr_number, distance) for the closest plausible load, or
    (None, None). A B9 byte can of course be part of something else, which is
    what the distance is for: a load two or three bytes before the msr access
    is almost always the real one, a load 50 bytes earlier may not be.
    """
    start = max(0, site - lookback)
    best = None
    for offset in range(site - 5, start - 1, -1):
        if data[offset] != 0xB9:
            continue
        if offset + 5 > len(data):
            continue
        value = int.from_bytes(data[offset + 1:offset + 5], "little")
        best = (value, site - offset)
        break
    return best if best else (None, None)


def decode_bit_ops(data, start, end):
    """Report 0F BA bit instructions in [start, end), with their bit number."""
    found = []
    for offset in find_all(data[start:end], b"\x0f\xba"):
        absolute = start + offset
        if absolute + 4 > len(data):
            continue
        modrm = data[absolute + 2]
        mod = modrm >> 6
        reg = (modrm >> 3) & 0x7
        rm = modrm & 0x7
        if reg not in BIT_OPS:
            continue
        # Only the register-direct form has the imm8 at a fixed offset; the
        # memory forms carry a displacement first, so their bit number is
        # reported as unknown rather than guessed.
        if mod == 0x3:
            bit = data[absolute + 3]
            target = REG_NAMES[rm]
        else:
            bit = None
            target = "memory"
        found.append((absolute, BIT_OPS[reg], target, bit))
    return found


def hexs(data):
    return data.hex().upper()


def report_site(data, site, kind, args):
    msr, distance = attribute_msr(data, site, args.lookback)

    if msr is not None and not args.all and msr not in INTERESTING_MSRS:
        return False
    if msr is None and not args.all:
        return False

    name = INTERESTING_MSRS.get(msr)
    print("  %s at %#08x" % (kind, site))
    if msr is None:
        print("      MSR     unknown (no 'mov ecx, imm32' within %d bytes)" % args.lookback)
    else:
        print("      MSR     %#05x%s" % (msr, "  " + name if name else ""))
        print("      loaded  %d bytes before the access" % distance)

    window_start = max(0, site - args.context)
    window_end = min(len(data), site + 2 + args.context)
    before = data[window_start:site]
    after = data[site + 2:window_end]

    print("      bytes   %s [%s] %s" % (hexs(before), hexs(data[site:site + 2]), hexs(after)))

    bit_ops = decode_bit_ops(data, window_start, window_end)
    if bit_ops:
        for offset, op, target, bit in bit_ops:
            where = "%+d" % (offset - site)
            if bit is None:
                print("      bitop   %s %s, <memory form, bit number not decoded> (%s)"
                      % (op, target, where))
            else:
                marker = "  <-- bit 15, the usual lock bit" if bit == 15 else ""
                print("      bitop   %s %s, %d (%s)%s" % (op, target, bit, where, marker))
        print("      hint    turning a 'bts' into a 'bt' leaves the bit alone: the ModRM")
        print("              reg field goes from 5 to 4, so 0FBAE80F becomes 0FBAE00F")
    print()
    return True


def scan(path, args):
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        print("%s: %s" % (path, error), file=sys.stderr)
        return 0

    print("%s (%d bytes)" % (path, len(data)))

    sites = []
    for offset in find_all(data, RDMSR):
        sites.append((offset, "rdmsr"))
    for offset in find_all(data, WRMSR):
        sites.append((offset, "wrmsr"))
    sites.sort()

    if not sites:
        print("  no rdmsr or wrmsr found\n")
        return 0

    shown = 0
    for offset, kind in sites:
        if report_site(data, offset, kind, args):
            shown += 1

    if shown == 0:
        print("  %d rdmsr/wrmsr found, none of them on a known interesting MSR"
              % len(sites))
        print("  pass --all to see every one\n")
    else:
        print("  %d of %d rdmsr/wrmsr sites reported\n" % (shown, len(sites)))
    return shown


def main():
    parser = argparse.ArgumentParser(
        description="Find candidate MSR lock sites in an extracted UEFI module.")
    parser.add_argument("files", nargs="+", metavar="FILE",
                        help="module body, PE32 section or whole firmware dump")
    parser.add_argument("--context", type=int, default=12, metavar="N",
                        help="bytes of context to print on each side (default 12)")
    parser.add_argument("--lookback", type=int, default=32, metavar="N",
                        help="how far back to look for 'mov ecx, imm32' (default 32)")
    parser.add_argument("--all", action="store_true",
                        help="report every rdmsr/wrmsr, not just the interesting MSRs")
    args = parser.parse_args()

    total = 0
    for path in args.files:
        total += scan(path, args)

    if total == 0:
        print("Nothing to report. Remember that these are candidates either way:")
        print("confirm every site in a disassembler before writing a patch line.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
