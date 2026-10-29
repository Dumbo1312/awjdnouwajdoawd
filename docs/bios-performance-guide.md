# BIOS and firmware changes that actually affect frame rate

This is the part of the project that is not code. It ranks the firmware-side
changes that move frame rate on a gaming PC, says roughly how much each one
is worth, and says which ones need a modified firmware image at all.

Read [risks-and-recovery.md](risks-and-recovery.md) before you flash
anything. In particular, read the Boot Guard section: on a lot of hardware
made after about 2015 a modified image simply will not boot, and the whole
reflashing route is closed before it starts.

## The short version

Almost all of the frame rate in a BIOS comes from three things:

1. **Memory running at its rated speed with tightened subtimings.** On a
   CPU-bound game this is the single biggest firmware-side lever there is.
2. **Power and current limits that let the CPU hold its boost clock** instead
   of dropping back after a few seconds.
3. **Resizable BAR**, where the GPU and the title benefit from it.

Everything else is either a frametime-consistency fix worth a few percent, or
a myth. Patching the firmware binary — what UEFIPatch does — is **not** on
that list: its job is to remove locks so that the three things above become
reachable at all. On a board whose setup menu already exposes memory
timings and power limits, you do not need to patch anything.

So the honest order of operations is:

```
  settings you already have   ->   settings the vendor hid   ->   binary patches
  (free, reversible)               (NVRAM write, reversible)      (reflash, risky)
```

Work top to bottom and stop as soon as you have what you need.

## Tier A - memory

This is where the frame rate is, on both AMD and Intel, in any game that is
not purely GPU-bound.

| Change | Typical effect in a CPU-bound game |
| --- | --- |
| Turning on XMP / EXPO at all, instead of the JEDEC default | 10-25% |
| Getting the memory and fabric clocks into a 1:1 ratio | 3-10% |
| Tightening primary timings (tCL, tRCD, tRP, tRAS) | 2-6% |
| Raising tREFI and lowering tRFC | 2-5% |
| Command rate 1T, Gear Down Mode off (AM4) | 1-3% |

Notes that matter more than the exact numbers:

- **Ratios beat raw frequency.** On AM4, DDR4-3600 with FCLK 1800 (1:1) beats
  DDR4-4000 with FCLK 1900 and a desynchronised fabric, because the
  mismatch adds a clock-domain crossing to every memory access. On AM5, keep
  UCLK:MCLK at 1:1 for as long as the memory controller allows it, which is
  usually somewhere around DDR5-6000 to 6400. On Intel 12th to 14th gen the
  equivalent is Gear 1 versus Gear 2.
- **tREFI is the cheapest win nobody uses.** It sets how often the controller
  stops to refresh the DRAM. Raising it means fewer interruptions. It is also
  temperature-sensitive: a value that is stable at 35C can corrupt memory at
  50C, so test it with the case closed and under a long load, not for five
  minutes on an open bench.
- **Measure latency, not just the frequency number.** AIDA64's memory latency
  figure or Intel MLC gives you something to compare. Rough targets:
  under about 60ns on AM4, under about 65ns on AM5, under about 55ns on a
  tuned Intel setup. If a change lowered your latency, it probably helped the
  frame rate; if it did not, it did not.
- Every one of these is a **setup menu setting**. If your board exposes them,
  no firmware modification is involved. If it does not, that is a
  hidden-settings problem, covered below.

## Tier B - power, current and clock limits

A CPU that boosts to 5.4 GHz for eight seconds and then sits at 4.6 GHz for
the rest of the match is a firmware configuration problem, not a silicon one.

**Intel**

- `PL1` and `PL2`, and the `Tau` window between them. Raising them, within
  what your cooling can actually remove, is what converts a short boost into
  a sustained one.
- `IccMax` (the current limit). Often the real limiter on a 13th/14th gen part
  before either power or temperature is.
- `AC Loadline` / `DC Loadline`. Lowering AC_LL reduces how much extra voltage
  the CPU asks for under load, which lowers temperature, which raises the
  clock it can hold. This is one of the highest-value settings on recent Intel
  and one of the most often hidden.
- `Ring` / cache ratio. Worth a couple of percent in CPU-bound titles, and
  usually left well below what the chip can do.
- On 13th/14th gen, `CEP` (Current Excursion Protection) will fight an
  undervolt by clock-stretching, so it gets turned off together with any AC_LL
  work, not before.

**AMD**

- `PBO` limits: `PPT`, `TDC`, `EDC`. Same idea as PL1/PL2.
- `Curve Optimizer`. Negative per-core offsets let the part hit a higher clock
  at the same voltage. Typical stable range on Zen 3 and Zen 4 is -10 to -30
  per core, and it varies per core, so it needs per-core tuning and a long
  stability test. This is usually the largest single AMD gain after memory.
- On AM4, `Power Supply Idle Control` set to `Typical Current Idle` fixes a
  family of idle reboots and WHEA errors that otherwise look like a bad
  overclock.

Everything in this tier is also a setup menu setting. On an enthusiast
motherboard they are all exposed. On an OEM prebuilt or a laptop, most of
them are not, which is the real reason people start modifying firmware.

## Tier C - frametime consistency

These rarely change the average frame rate much. They change the 1% low, which
is what actually feels like stutter.

- **Resizable BAR**, with **Above 4G Decoding** on and **CSM** off. Worth
  anywhere from nothing to about 10-15% depending on the title and the GPU.
  Needs all three together: Resizable BAR without Above 4G Decoding does
  nothing, and leaving CSM on blocks it.
- **Pure UEFI boot, CSM off.** Removes legacy traps from the boot path and is
  a prerequisite for the above.
- **C-state behaviour.** Deep package C-states save power and allow higher
  short-term boost, but add wake latency. For the most consistent frametimes,
  limiting the package C-state (to C2, say) or turning C-states off is worth
  trying — and measuring, because on a modern part the lost boost headroom
  can cost more than the latency gains. **This is the one tier-C item that
  can require a binary patch**: if firmware set the lock bit in
  `MSR_PKG_CST_CONFIG_CONTROL` (0xE2) the register is read-only until the
  next reset, so no OS-side tool can change it. That is what the
  `msr-e2-unlock` set in [../patches/performance.txt](../patches/performance.txt)
  is for.
- **SMI load.** A System Management Interrupt stops every core and runs
  firmware code in SMM. Fan control, hardware monitoring and some ACPI paths
  are implemented as periodic SMIs, and a long SMI handler shows up directly
  as a frametime spike. Where the setup menu exposes hardware-monitor polling
  or an OEM "thermal SMI" interval, lengthening or disabling it is a real
  frametime win. Removing the handlers from the firmware itself is possible
  and is how some "latency-optimised" BIOS mods are built, but it is deep
  water: you are deleting code the rest of the firmware expects to exist.
- **Turning off hardware you do not use** — a second NIC, onboard audio when
  you use USB audio, unused SATA controllers, serial and LPC debug ports,
  Thunderbolt. Fewer devices means fewer interrupt sources. The effect on
  average frame rate is small, the effect on interrupt latency is measurable,
  and it shortens POST.
- **`TME` (Total Memory Encryption) off**, where present. Encrypting all of
  DRAM costs a few percent of effective bandwidth.

Two things people do in this tier that generally do not help:

- **Disabling SMT or hyperthreading.** On a modern scheduler this is neutral
  at best and a loss in anything that uses the threads. The handful of titles
  where it helped are years old.
- **Disabling E-cores.** Same story. There are a few old anti-cheat and
  scheduler-bug cases, but the default is that you lose multithreaded
  performance for nothing.

## Tier D - things that trade something real away

These work. They also cost you something you may care about. They are listed
so the trade is explicit, not because they are recommended.

- **Rolling the CPU microcode back** to a revision from before a speculative
  execution mitigation. The gain depends heavily on the generation: small on
  recent parts, larger on Skylake-era chips where the mitigations cost the
  most. You give up the fix for a real, exploitable vulnerability, and you may
  give up stability fixes and errata workarounds that shipped in the same
  update. This is one of the few genuine uses for firmware binary editing
  (UEFIReplace against the microcode section), and it is the one most likely
  to make a machine subtly unstable rather than obviously broken.
- **Virtualisation-based security off.** `VT-d`/`SVM` plus `IOMMU` in
  firmware, and Memory Integrity / HVCI in Windows. Commonly 2-8% in games.
  Most of this is an operating system setting rather than a firmware one, and
  the firmware half of it breaks GPU passthrough and some sandboxing.
- **BCLK overclocking on a locked part.** It works on some Intel non-K chips.
  It also desynchronises the timers that thermal and power management rely on,
  so sensors read wrong and AVX offsets misbehave. High risk of an unstable
  machine that benchmarks well.

## What does not give you frame rate

Worth stating plainly, because a lot of "performance BIOS mod" advice is
made of these:

- **Removing the vendor logo, or deleting unused DXE drivers to "slim" the
  firmware.** Shortens POST. Does nothing once the OS is running, because
  none of that code is executing any more.
- **Disabling or neutering the Intel Management Engine.** No frame rate
  effect. Breaks features, and on many boards breaks booting.
- **Flashing a modded BIOS from a forum because the thread title promises
  more FPS.** Whatever gains it has come from memory and power settings that
  you can set yourself. What you are taking on is someone else's untested
  image built for someone else's board revision.
- **Changing the FFS compression, rebuilding volumes, "optimising" the
  firmware layout.** Nothing. The firmware is decompressed into RAM at boot.
- **Fan curves.** Nothing, unless you were thermally throttling, in which case
  the fan curve was not the problem.

## Reaching settings the vendor hid

This is the actual work on an OEM board or a laptop, and it usually does not
need a modified firmware image at all.

Every setup menu question is backed by a variable in NVRAM. The menu being
hidden does not mean the variable is gone; it means the setup browser was
told not to draw it. So you can often write the variable directly and get the
behaviour without touching the firmware image.

**Step 1 - get a dump.** `flashrom`, Intel FPT, AFUWIN, or an SPI programmer.
Keep this file. It is your recovery image, and the whole exercise depends on
having it.

**Step 2 - find the setup module.** Open the dump in UEFITool and look for
`Setup`, `SetupUtility`, or `AMITSE`. Extract its PE32 section body.

**Step 3 - read the IFR.** Run the extracted body through
[IFRExtractor-RS](https://github.com/LongSoft/IFRExtractor-RS). You get a text
dump of every question in the setup browser: its text, its VarStore, its
offset within that VarStore, its possible values, and the `SuppressIf` and
`GrayOutIf` conditions that hide it.

Example of what you are looking for, in IFR terms:

```
    OneOf Prompt: "Above 4GB MMIO BIOS assignment", Help: "...",
      QuestionFlags: 0x10, QuestionId: 0x4F2, VarStoreId: 0x1, VarOffset: 0x1A3,
      Flags: 0x10, Size: 8, Min: 0x0, Max 0x1, Step: 0x0
```

`VarStoreId: 0x1` and `VarOffset: 0x1A3` is the address you need: byte 0x1A3 of
that VarStore, which is usually the one named `Setup`.

**Step 4 - pick a route.**

- **Write the variable (preferred).** From a UEFI shell, `setup_var.efi` or
  `setup_var_3.efi` from [grub-mod-setup_var](https://github.com/datasone/grub-mod-setup_var)
  writes that offset directly. No reflash, no modified image, and a CMOS clear
  undoes it. This route also works on hardware where Boot Guard makes
  reflashing impossible, which is most of why it is the recommended one.
- **Modify the image and reflash.** Change the access flags in the setup
  module, or use AMIBCP on an AMI firmware, then flash. This gets you a real
  visible menu rather than a blind variable write, which is genuinely nicer to
  work with — and it requires that your platform lets you boot a modified
  image at all, plus a working recovery path when it does not.

**Step 5 - only now, binary patches.** What is left after all of the above is
the locks: registers that firmware closed before handing control to the OS, so
that no setting and no OS tool can reach them. That is what UEFIPatch and
[../patches/performance.txt](../patches/performance.txt) are for, and
[pattern-cookbook.md](pattern-cookbook.md) covers deriving the patterns for
your own board.

## How to tell whether any of it worked

Set a baseline before you change anything, and use the same scene every time.

- **Memory:** AIDA64 memory latency, or Intel MLC. A change that did not lower
  latency did not help.
- **Sustained clocks:** HWiNFO64 logging the effective clock, package power and
  temperature through a ten-minute load. You are looking for the clock after
  the boost window, not the peak.
- **Frame rate and frametimes:** CapFrameX or PresentMon, and look at the 1%
  and 0.1% low figures, not the average. Tier-C changes show up almost
  entirely in the lows.
- **Stability:** anything touching memory timings needs a long test. TM5 with
  the `absolut` or `1usmus_v3` config, or Karhu, for several hours, plus
  OCCT or y-cruncher for the CPU side. Memory instability caused by an
  aggressive tREFI often appears only after the DIMMs are properly warm.

Change one thing at a time. This is dull and it is the only way to know which
change was worth the risk you took for it.
