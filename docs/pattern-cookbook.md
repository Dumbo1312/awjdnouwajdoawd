# Deriving your own UEFIPatch patterns

A UEFIPatch line is a byte pattern. The compiler that built your firmware
chose those bytes, so a pattern that works on one vendor's Skylake build
often does not match another's, and almost never matches a different
microarchitecture. That is why
[../patches/performance.txt](../patches/performance.txt) only ships patterns
that came from upstream UEFITool, and why there is no generic pattern for
anything else.

This is how to find your own.

## What you are looking for

Firmware locks a register by setting a lock bit and writing the register
back. For model-specific registers that is an `rdmsr` / modify / `wrmsr`
sequence, and the lock bit is almost always set with one of the `0F BA`
bit instructions.

The classic `MSR_PKG_CST_CONFIG_CONTROL` lock looks like this:

```
    B9 E2 00 00 00      mov    ecx, 0E2h        ; which MSR
    0F 32               rdmsr                   ; read it into edx:eax
    0F BA E8 0F         bts    eax, 0Fh         ; set bit 15, the CFG_LOCK bit
    0F 30               wrmsr                   ; write it back, now locked
```

The useful thing about the `0F BA` encoding is that the operation lives in
three bits of the ModRM byte, so changing the operation is a one-nibble edit:

| Operation | What it does | ModRM reg field | Example ModRM |
| --- | --- | --- | --- |
| `bt` | tests the bit, writes nothing | 4 | `E0` |
| `bts` | sets the bit | 5 | `E8` |
| `btr` | clears the bit | 6 | `F0` |
| `btc` | flips the bit | 7 | `F8` |

So `0F BA E8 0F` (`bts eax, 15`) becomes `0F BA E0 0F` (`bt eax, 15`): the
instruction is the same length, the flags still get set so any following
branch still behaves, and the bit is simply never written. That is exactly
what the upstream Haswell-E patch does:

```
F7731B4C-58A2-4DF4-8980-5645D39ECE58 10 P:0FBA6C24380F:0FBA7424380F
```

`6C` is `bts` on a memory operand, `74` is `btr` on the same operand — set
becomes clear.

The other common shape is a conditional jump over the locking code:

```
F7731B4C-58A2-4DF4-8980-5645D39ECE58 10 P:75080FBAE80F89442430:EB080FBAE80F89442430
```

`75 08` is `jnz +8`, `EB 08` is `jmp +8`. The patch turns "skip the lock if
the flag is clear" into "always skip the lock". Same length, no relocation
needed.

That is the whole trick, and it is why these patches are relatively safe as
firmware edits go: every one of them replaces an instruction with another
instruction of exactly the same length, so nothing in the module moves.

## Step by step

### 1. Dump your firmware and keep the dump

`flashrom -p <programmer> -r backup.bin`, Intel FPT, AFUWIN, or an SPI
programmer. Verify you can read it back identically. This file is your
recovery image.

### 2. Extract the modules

```
UEFIExtract backup.bin
```

This gives you `backup.bin.dump/` with one directory per firmware file. The
power management code is usually in one of:

| Module | GUID | Where it shows up |
| --- | --- | --- |
| `PowerMgmtDxe` | `F7731B4C-58A2-4DF4-8980-5645D39ECE58` | Haswell and Haswell-E |
| `PowerManagement` | `8C783970-F02A-4A4D-AF09-8797A51EEC8D` | Sandy Bridge, Ivy Bridge |
| `CpuPei` | `2BB5AFA9-FF33-417B-8497-CB773C2B93BF` | older Sandy Bridge-E |
| `PpmInitialize` | `3FFCAE95-23CF-4967-94F5-16352F68E43B` | Broadwell-E, Skylake-X, server chipsets |
| `SiInit` | `299D6F8B-2EC9-4E40-9EC6-DDAA7EBF5FD9` | Skylake, Kaby Lake, Gemini Lake |
| `CpuInitPei` | `01359D99-9446-456D-ADA4-50A711C03ADA` | Skylake-X |

If none of those are in your dump, `UEFIFind` will locate the module by
content instead:

```
UEFIFind find all 8 0F320FBAE80F0F30 backup.bin
```

### 3. Find the candidate sites

```
tools/msr-lock-scan.py backup.bin.dump/<module>/.../body.bin
```

The scanner finds every `rdmsr` and `wrmsr`, attributes each to an MSR number
by looking backwards for the `mov ecx, imm32` that loaded it, prints the
surrounding bytes, and flags any `0F BA` bit instruction nearby — with a
marker when the bit number is 15, which is the usual lock bit. Output looks
like this:

```
  wrmsr at 0x0001a4c
      MSR     0x0e2  MSR_PKG_CST_CONFIG_CONTROL - package C-state limit plus CFG_LOCK in bit 15
      loaded  11 bytes before the access
      bytes   1FB9E20000000F320FBAE80F [0F30] 4889442430488B4424
      bitop   bts eax, 15 (-4)  <-- bit 15, the usual lock bit
```

It is a byte scanner, not a disassembler, so treat every hit as a candidate.

### 4. Confirm in a disassembler

Load the same module into a disassembler and look at the site the scanner
pointed at. You are checking three things:

- The `mov ecx` really is the load for this `wrmsr` and not a coincidence —
  a `B9` byte turns up inside other instructions and inside data all the time.
- The bit instruction operates on the value that came out of `rdmsr`, and not
  on something unrelated that happens to sit between them.
- Nothing jumps into the middle of the bytes you are about to replace.

This step is not optional. The scanner's job is to save you from reading the
whole module; it is not evidence.

### 5. Choose the pattern

Two rules decide whether a pattern is any good:

- **It has to be unique within the section.** UEFIPatch replaces *every*
  match inside a section body, so a short pattern that appears five times
  patches five places. Include enough context — the `mov ecx` and the
  `rdmsr` along with the bit instruction, typically 8 to 12 bytes.
- **The replacement has to be the same length as the pattern.** Nothing in a
  UEFI module can move: addresses are baked in. UEFIPatch warns when the
  two lengths differ, and under `--strict` that warning is the point where
  you stop and reconsider.

Use `.` for a nibble that varies between builds — a register number, a stack
offset, part of a displacement:

```
3FFCAE95-23CF-4967-94F5-16352F68E43B 10 P:4889442430....0FBAE8:4889442430....0FBAE0
```

The `....` matches any two bytes, which is how that pattern survives across
several chipsets that differ only in a displacement.

### 6. Dry run it

```
UEFIPatch backup.bin -p "<guid> 10 P:<find>:<replace>" -n -v --hash
```

`-n` does everything except write the file. `-v` tells you whether the line
matched and how many sections it changed. If it says `[ missed]`, the pattern
is wrong — and finding that out here costs you nothing.

Check the section count too. If one pattern changed three sections you either
want `--all-copies` deliberately, or your pattern is not as unique as you
thought.

### 7. Add it to a patch file with a set name

```
#!set my-board-e2-unlock
#!desc MSR 0xE2 CFG_LOCK removal for <board> BIOS <version>
#!risk medium
#!note derived from my own dump, confirmed in a disassembler, not tested on any other board

# PpmInitialize | <board> F<version>
3FFCAE95-23CF-4967-94F5-16352F68E43B 10 P:<find>:<replace>
```

Then run it the way you will run it for real, with `--strict`, so that a
pattern which stops matching after a BIOS update fails loudly instead of
quietly producing a half-patched image:

```
UEFIPatch backup.bin patches/mine.txt --set my-board-e2-unlock --strict -v --hash -o modded.bin
```

Record the digests. When you later wonder which file you flashed, that is the
only thing that will tell you.

## MSRs worth knowing about

The scanner flags these. Knowing what they are is the difference between a
deliberate patch and poking at a register because a forum post said to.

| MSR | Name | Why it comes up |
| --- | --- | --- |
| `0xCE` | `MSR_PLATFORM_INFO` | ratio limits, programmable ratio and TDP-limit lock bits |
| `0xE2` | `MSR_PKG_CST_CONFIG_CONTROL` | package C-state limit, `CFG_LOCK` in bit 15 |
| `0x194` | `MSR_FLEX_RATIO` | the ratio firmware presents to the OS |
| `0x199` | `MSR_IA32_PERF_CTL` | the ratio and voltage the OS asks for |
| `0x1A0` | `MSR_IA32_MISC_ENABLE` | EIST, turbo disable, prefetcher controls |
| `0x1A2` | `MSR_TEMPERATURE_TARGET` | TCC offset |
| `0x1AA` | `MSR_MISC_PWR_MGMT` | EIST hardware coordination plus its lock bit |
| `0x1AD` | `MSR_TURBO_RATIO_LIMIT` | per-core-count turbo ratios |
| `0x1FC` | `MSR_POWER_CTL` | C1E, energy efficient turbo |
| `0x610` | `MSR_PKG_POWER_LIMIT` | PL1 and PL2, with a lock bit in 63 |
| `0x150` | `MSR_OC_MAILBOX` | the undervolt and overclock mailbox on many client parts |

## When not to patch

If the setting you want is reachable any other way, use the other way.

- The setup menu has it, even buried: use the menu.
- The menu entry exists but is hidden: write the NVRAM variable, as described
  at the end of [bios-performance-guide.md](bios-performance-guide.md). No
  reflash, and a CMOS clear undoes it.
- An OS-side tool can set it: use the tool. ThrottleStop, XTU, Ryzen Master
  and `MSR` utilities reach everything that is not locked.

Binary patching is for what is left over: a register the firmware closed
before the OS ever ran. It is the only case where modifying the image buys
you something a setting cannot, and it is the only reason this part of the
project exists.
