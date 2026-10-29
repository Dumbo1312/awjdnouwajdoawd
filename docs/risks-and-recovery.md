# Risks and recovery

Read this before you flash anything. The short version is that a bad
firmware write is the one computer mistake that a reinstall cannot fix, and
that on a lot of modern hardware the write will not even be accepted.

## Check first: can this machine boot a modified image at all?

**Intel Boot Guard** and **AMD Platform Secure Boot** verify the first stage
of firmware against a key fused into the chipset or CPU. When they are
enabled — which is the default on most OEM systems and many consumer boards
from roughly 2015 onwards — a modified image fails verification and the
machine either refuses to boot or sits in a reset loop. There is no software
workaround. The fuses are one-way.

So before you plan a reflash:

- Find out whether your platform has it enabled. Vendor documentation, the
  Boot Guard status in a tool like `chipsec` (`chipsec_main -m common.bios_wp`
  and the Boot Guard modules), or simply the experience of other people with
  the same board.
- If it is enabled, **the reflash route is closed**. Use the NVRAM variable
  route described at the end of
  [bios-performance-guide.md](bios-performance-guide.md) instead: it changes
  settings without changing the image, so verification still passes.
- A laptop is almost always in this category, and almost always has no
  recovery header either.

This single check saves more bricked boards than everything else on this page.

## What you need before you write anything

- **A full dump you have verified.** Read the chip, read it a second time,
  and confirm the two files are identical. A dump you have not verified is
  not a backup.
- **A way to recover without the board booting.** One of:
  - a dual-BIOS board with a working switch or automatic fallback,
  - BIOS Flashback / Q-Flash Plus / USB BIOS recovery, which flashes from a
    USB stick with no CPU and no POST,
  - an SPI programmer and a clip (a CH341A with an SOIC-8 clip is the usual
    cheap option) and the knowledge that your chip is reachable — some are
    soldered under a heatsink, some share the bus with something that must be
    powered down first.
  - If you have none of these, stop here. Every other precaution on this page
    assumes you can get back.
- **The vendor's own stock image**, separately from your dump, in case your
  dump turns out to be bad.

## Regions you must not touch

A full SPI dump is not just the BIOS. It is a flash descriptor plus several
regions, and most of them are not yours to edit:

| Region | Edit it? |
| --- | --- |
| Flash descriptor | No. It defines where everything else is and what may write to it. A wrong descriptor is an unrecoverable-without-a-programmer brick. |
| ME / CSME / PSP | No. Vendor signed, and the platform will not run without a valid one. |
| GbE | No. Holds your MAC address, among other things. |
| BIOS | This is the only region any of this applies to. |

UEFITool shows you the regions. If you dumped the whole chip, work on the
BIOS region and leave the rest byte-identical. If a tool offers to "fix" or
rebuild the descriptor, say no.

## What each kind of change can cost you

| Change | Worst realistic outcome |
| --- | --- |
| A setup menu setting | A CMOS clear fixes it. |
| An NVRAM variable write (`setup_var`) | A CMOS clear fixes it. Worst case the variable is in a region a CMOS clear does not reset, and you restore it with the same tool. |
| An MSR lock patch (this project's patch sets) | The image stays structurally valid and boots. What you have changed is that an OS tool can now write a register that was closed, so a bad *setting* afterwards can hang or reboot the machine — a CMOS clear or booting without that tool fixes it. |
| Memory timings far past what the kit does | No POST. CMOS clear. Annoying, not fatal. |
| Replacing microcode | Can be subtly unstable rather than obviously broken, which is the worst failure mode to debug. Can also fail to boot, which needs the recovery path. |
| Removing firmware modules (SMI handlers, drivers) | No boot, needing the recovery path. Other modules depend on code you deleted. |
| Anything touching the descriptor or ME region | Unrecoverable without a hardware programmer. |

The pattern patches in this project sit deliberately at the mild end of that
table: each one replaces an instruction with another instruction of the same
length, so no address in the module moves and no structure changes size.
That is what makes them recoverable. It is not a general property of firmware
editing.

## Flash write protection

Even without Boot Guard, firmware usually protects itself while the OS is
running:

- `BIOSWE` / `BLE` in the `BIOS_CNTL` register, and `SMM_BWP`, make BIOS
  writes either impossible or only possible from SMM.
- The flash descriptor's protected range registers (`PR0`-`PR4`) can lock
  ranges in hardware until reset.
- `FLOCKDN` locks the SPI controller's own configuration.

This is why `flashrom` on a running system often reports the chip as
read-only. The honest answers are: use the vendor's own flashing tool or the
board's USB recovery feature, or use an external programmer. Disabling these
protections from the OS means defeating a security mechanism that exists to
stop firmware malware, and the methods for doing it are platform-specific
exploits, not a configuration step.

## A working order of operations

1. Verify whether Boot Guard / PSB is enabled. If it is, do not plan to
   reflash at all.
2. Dump, twice, and compare.
3. Copy the dump somewhere that is not the machine you are modifying.
4. Make the change with `--dry-run` first, and read the per-line report. A
   patch that reports `[ missed]` has not done anything, and flashing the
   result would be pointless risk for no change.
5. Run it again with `--strict` and `--hash`. Write both digests down.
6. Open the result in UEFITool and look at it. The module you patched should
   be the same size as before, and the structure tree should be unchanged.
7. Flash. Use the vendor's tool or the board's recovery feature if you can.
8. Clear CMOS, then boot and change one setting at a time.
9. Test for hours, not minutes. Memory instability from an aggressive `tREFI`
   appears when the DIMMs are warm, not when they are cold.

## If it does not boot

- **Clear CMOS first.** Most "the mod bricked it" cases are a setting, not
  the image. Pull the battery, use the jumper, hold the clear button.
- **Try the recovery header.** BIOS Flashback and its equivalents work with
  no CPU installed and no display, from a FAT32 stick with the file named
  exactly what the vendor requires.
- **Dual BIOS**: switch to the backup and, once you are up, do not
  immediately copy the broken image over it.
- **External programmer**: clip onto the chip, write the stock image back.
  Read it back afterwards and compare before you power on.
- **Keep notes.** Which digest you flashed, which patch sets were selected,
  what the `--verbose` report said. When you are trying to work out what went
  wrong at two in the morning, that record is the difference between fixing
  it and guessing.

## Finally

None of the images this project produces have been tested on your board.
UEFIPatch's own help text says so, and so does this document. The patterns
that ship in `patches/performance.txt` come from upstream UEFITool and are
known to have worked on the platforms their comments name, which is not the
same as being known to work on yours. That is what `--dry-run` is for, and
why `--strict` exists: so that an image which did not actually get the change
you wanted never reaches the chip.
