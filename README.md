# UEFITool 0.28.0, modified, with a firmware performance toolkit

UEFITool 0.28.0 from [LongSoft/UEFITool](https://github.com/LongSoft/UEFITool)
with a reworked **UEFIPatch**, a patch database organised into named sets, a
helper for finding your own patch patterns, and documentation on which BIOS
changes actually move frame rate.

Base commit: `d9642c53e7b873fcce858ec2bea577f099b966d5` (tag `0.28.0`).

## Kratko (hrvatski)

- `UEFITool-0.28.0/` je originalni source s izmjenama. Mijenjan je samo
  **UEFIPatch** plus jedan popravak granica u `ffsengine.cpp`.
- **Popravljeno 5 pravih bugova u originalu**, uključujući segfault i
  čitanje izvan heapa koje je upisivalo smeće iz memorije u BIOS sliku.
  Detalji i reprodukcija: [`docs/tool-changes.md`](docs/tool-changes.md).
- **Najvažnije novo**: original je mogao reći "Image patched" i vratiti
  exit 0 čak i kad se patch koji ti treba nije primijenio. Sad `-v` pokaže
  svaku liniju kao `[applied]` ili `[ missed]`, a `--strict` vrati grešku.
- Što zapravo daje FPS: [`docs/bios-performance-guide.md`](docs/bios-performance-guide.md).
  Kratko — memorija (XMP/EXPO + timinzi), power limiti (PL1/PL2, Curve
  Optimizer) i Resizable BAR. Patchanje binarnog BIOS-a **nije** na toj
  listi; ono samo otključava registre da bi to troje bilo dostupno.
- **Pročitaj [`docs/risks-and-recovery.md`](docs/risks-and-recovery.md) prije
  flashanja.** Ako ploča ima Intel Boot Guard (većina OEM-a i dosta
  consumer ploča od ~2015.), modificirani BIOS se **neće** bootati i nema
  softverskog zaobilaženja — tada ide ruta preko NVRAM varijabli, bez
  flashanja.

## Build

Needs Qt 4 or Qt 5. Verified here with Qt 5.15.

```sh
cd UEFITool-0.28.0/UEFIPatch
qmake uefipatch.pro && make
```

The GUI (`UEFITool-0.28.0/uefitool.pro`) and `UEFIReplace` build the same way
and are unchanged.

## Using it

```sh
# what is in the patch database
UEFIPatch --list patches/performance.txt

# try the MSR 0xE2 unlocks against a dump, write nothing
UEFIPatch bios.bin patches/performance.txt \
          --set msr-e2-unlock --strict --verbose --dry-run --hash

# do it for real
UEFIPatch bios.bin patches/performance.txt \
          --set msr-e2-unlock --strict --hash -o bios-modded.bin
```

`--dry-run` does everything including reconstructing the image, so a dry run
that succeeds means the real run will. `--strict` is the one you want on for
anything you intend to flash: it turns "some patches applied, the one you
needed did not" from exit 0 into an error.

### Finding patterns for your own board

```sh
UEFIExtract bios.bin
tools/msr-lock-scan.py bios.bin.dump/<module>/.../body.bin
```

The scanner locates every `rdmsr`/`wrmsr`, attributes it to an MSR number,
prints the surrounding bytes, and flags nearby bit instructions that look
like a lock being set. It is a byte scanner, not a disassembler: confirm
every hit before patching.
[`docs/pattern-cookbook.md`](docs/pattern-cookbook.md) walks through the rest.

## What is here

```
UEFITool-0.28.0/     upstream source, with the UEFIPatch changes
patches/
  performance.txt    upstream patch database, reorganised into named sets
docs/
  bios-performance-guide.md   which firmware changes affect frame rate
  pattern-cookbook.md         deriving UEFIPatch patterns for your own board
  risks-and-recovery.md       Boot Guard, protected regions, getting back
  tool-changes.md             every change to the tool, with reproductions
tools/
  msr-lock-scan.py   find candidate MSR lock sites in an extracted module
tests/
  run_tests.sh       37 regression cases
  make_test_image.py builds the synthetic firmware volume they run against
```

## What changed in the tool

Full detail with reproductions in [`docs/tool-changes.md`](docs/tool-changes.md).

**Bugs fixed** (all reproducible against an unmodified 0.28.0, all covered by
a test):

1. Segfault on a patch line missing its replacement pattern.
2. An unparseable GUID silently became the null GUID and matched nothing.
3. The offset bounds check underflowed on unsigned types, so an oversized
   replacement grew the section — and with `..` placeholders read past the end
   of the buffer and wrote adjacent heap memory into the output image.
4. A successful recursive patch returned `ERR_NOTHING_TO_PATCH` anyway, so no
   caller could tell a hit from a miss.
5. As a result of 4, a run where some patches applied and the important one
   did not printed `Image patched` and exited 0.

**Added**: `--verbose` per-line reporting, `#!set`/`#!desc`/`#!risk`/`#!note`
directives with `--list`/`--set`/`--exclude`, `--strict`, `--dry-run`,
`--all-copies`, `--hash`, a guard against writing the output over the input,
patch file lookup next to the executable, and parse diagnostics that name the
file, line and offending text.

**Compatibility**: old patch files and both old command forms work unchanged,
`#!` directives are comments to older builds, and for the stock upstream
`patches.txt` this build produces a byte-identical output image. A test pins
that.

## Tests

```sh
tests/run_tests.sh              # builds UEFIPatch itself
tests/run_tests.sh /path/to/UEFIPatch   # or test an existing binary
```

37 cases against a synthetic FFS2 volume built by `tests/make_test_image.py`
— real headers and checksums, four modules, one a deliberate duplicate — so
nothing needs anyone's real firmware.

## Honest scope

- **No new byte patterns were invented.** Every live patch line in
  `patches/performance.txt` comes from upstream UEFITool. Patterns depend on
  the vendor, the board and often the exact BIOS build, so a fabricated one
  is worse than none. The scanner and the cookbook are there to derive your
  own.
- **Patching firmware is not where the frame rate is.** Memory timings, power
  limits and Resizable BAR are, and on most boards those are settings you
  already have. Binary patching only removes locks that stop you reaching
  them. The guide says this at more length because it is the part that gets
  skipped.
- **Nothing here has been tested on your board.** The tests cover the tool,
  against a synthetic image. They say nothing about whether a given patch is
  right for your firmware, which is what `--dry-run` and `--strict` are for.

## Licence

Upstream UEFITool is BSD licensed; see
[`UEFITool-0.28.0/LICENSE.md`](UEFITool-0.28.0/LICENSE.md). The modifications,
the documentation, the tests and `tools/msr-lock-scan.py` are offered under
the same terms.
