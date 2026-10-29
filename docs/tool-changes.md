# What was changed in UEFITool 0.28.0

Base: upstream [LongSoft/UEFITool](https://github.com/LongSoft/UEFITool) tag
`0.28.0`, commit `d9642c53e7b873fcce858ec2bea577f099b966d5`, released by
vit9696. Release notes for that tag:

> * Added 'Do not rebuild' support for volumes
> * Added support to parse/reconstruct LZMAF86 sections
> * Added new EIST lock patches for UEFIPatch
> * Fixed mishandling trailing newline in UEFIPatch

Work here is in **UEFIPatch**, in the **GUI**, and one bounds-check fix in
the shared engine. UEFIReplace, the LZMA and Tiano codecs and the FFS parser
are untouched apart from that fix.

Files changed:

| File | Lines touched |
| --- | --- |
| `UEFIPatch/uefipatch.cpp` | rewritten |
| `UEFIPatch/uefipatch_main.cpp` | rewritten |
| `UEFIPatch/uefipatch.h` | rewritten |
| `ffsengine.cpp` | 1 bounds check |
| `version.h` | version string -> `0.28.0-perf1` |
| `uefitool.cpp`, `uefitool.h` | Tweaks and View menus, preset application |
| `uefitool.pro`, `UEFIPatch/uefipatch.pro` | new sources and the resource |
| `UEFIPatch/uefipatch_main.cpp` | `--analyze` and `--dump` |

Files added:

| File | What it is |
| --- | --- |
| `patchset.h`, `patchset.cpp` | patch file parsing and applying, shared by the GUI and UEFIPatch |
| `presetdialog.h`, `presetdialog.cpp` | the preset list |
| `firmwarereport.h`, `firmwarereport.cpp` | image scanning, platform and vendor detection, the debloat catalogue, the dependency analysis |
| `debloatdialog.h`, `debloatdialog.cpp` | the debloat list |
| `theme.h`, `theme.cpp`, `theme/dark-yellow.qss`, `uefitool.qrc` | the yellow on black theme |

`uefitool.ui` is **not** modified: the Tweaks and View menus are built in
code and inserted before Help, so the form stays exactly as upstream drew it.

## Bugs fixed

Each of these is reproducible against an unmodified 0.28.0 build and is
covered by a case in `tests/run_tests.sh`.

### 1. Segfault on a truncated patch specification

`patchFromFile()` split a patch token on `:` and then read `patchList.at(1)`
and `patchList.at(2)` without checking how many fields came back. A line
missing its replacement pattern crashed the process:

```
$ UEFIPatch test.bin bad.txt -o out.bin
...
Segmentation fault      (exit 139)
```

Now the field count is checked and the line is named:

```
Error: bad.txt line 1: "P:75080FBAE80F89442430" is not "Type:Find:Replace" or "Type:Offset:Replace"
```

The same code also did `patch.type = *(UINT8*)patchList.at(0).constData()`,
which reads a byte out of an empty `QByteArray` when a field is blank.

### 2. An unparseable GUID became the null GUID

`QUuid(list.at(0))` returns a null UUID when it cannot parse its input, and
nothing checked for that. A typo in a GUID silently turned into a search for
`00000000-0000-0000-0000-000000000000`, which matches nothing, and the only
output was `No patches can be applied to input file`. Now:

```
Error: bad.txt line 1: "not-a-guid" is not a usable file GUID
```

The raw GUID bytes were also taken with `QByteArray::fromRawData()` pointing
into a `QUuid` local and relying on `QUuid`'s field layout matching
`EFI_GUID`. That is now built field by field into an owned buffer.

### 3. Offset bounds check underflowed, reading past the section body

`ffsengine.cpp`, `patchViaOffset()`:

```c
if (offset > (UINT32)(body.length() - hexReplacePattern.length() / 2))
    return ERR_PATCH_OFFSET_OUT_OF_BOUNDS;
```

With a replacement longer than the section body, `body.length() - length/2`
is negative, the cast makes it a very large unsigned value, and the check
passes. Two things then happen:

- A literal replacement overwrites past the end of the section, silently
  growing it. The structure around it is now wrong, and the image still gets
  written.
- A replacement containing `..` placeholders calls `body.at(offset + i)`
  beyond the end of the buffer. In a real run this read adjacent heap memory
  and wrote it into the output image — in testing, recognisable heap pointers
  and the firmware volume header of the next region ended up inside a patched
  section.

The fix compares signed lengths before any cast, so an oversized replacement
is rejected:

```
Error: oob.txt line 1: Patch offset out of bounds
```

### 4. A successful patch was reported as a failure

`patchFile()` walked the tree recursively, and on success it did this:

```c
UINT8 result = patchFile(index.child(i, 0), ...);
if (!result)
    break;                      // stop on the first success
...
return ERR_NOTHING_TO_PATCH;    // ...and then return failure anyway
```

The success was thrown away. No caller could tell a hit from a miss, which is
why upstream can only report on the whole image, by comparing the
reconstructed bytes against the input.

The walk now counts the sections it changed and returns that count, which is
what makes everything in the next section possible.

### 5. A half-patched image exited 0 with no indication

This is the consequence of 4, and the one that matters for firmware work.
Given a patch file where the patch you care about does not match but another
one does, upstream prints:

```
Image patched
$ echo $?
0
```

You get a clean exit, an output file, and no way to know that the MSR unlock
you were doing this for never applied. Flashing that image is pure risk for
no change. Now:

```
$ UEFIPatch test.bin partial.txt -o out.bin --strict -v
...
  set default
    [applied] line    2  F7731B4C-58A2-4DF4-8980-5645D39ECE58  sect 10  applies
    [ missed] line    4  299D6F8B-2EC9-4E40-9EC6-DDAA7EBF5FD9  sect 12  never matches

Patches: 1 applied, 1 not matched (1 section(s) changed)
Error: 1 of 2 selected patch line(s) did not match (--strict)
$ echo $?
41
```

## Features added

### Per-line reporting (`-v`, `--verbose`)

Every patch line is reported as `[applied]`, `[ missed]` or `[skipped]`, with
its line number, GUID, section type and a label. The label is the plain
comment above the line in the patch file, so the existing
`# PowerMgmtDxe | Haswell` style comments become the report. A line that
changed more than one section says so.

Without `-v` you still get the counts, and a pointer to `-v` if anything
missed.

### Named patch sets

Three directives in a patch file:

```
#!set  <id>     every patch line after this belongs to set <id>
#!desc <text>   one line description
#!risk <word>   low, medium or high
#!note <text>   a longer caveat, repeat for more lines
```

They start with `#`, so an older UEFIPatch reads them as comments and the
file still works there.

- `--list` prints the sets, with their risk and description, and touches no
  image.
- `--set a,b` applies only those sets.
- `--exclude a,b` skips them.

`patches/performance.txt` is the upstream patch database reorganised this way,
so "apply the MSR 0xE2 unlocks and nothing else" is one flag instead of
editing a file.

### `--strict`

Exit non-zero if any *selected* patch line did not match. Intended to be on
for any run whose output you are going to flash.

### `--dry-run` (`-n`)

Parse, patch in memory, reconstruct the image, report — and write nothing.
Reconstruction is the expensive and failure-prone part, so a dry run that
succeeds tells you the real run will too.

### `--all-copies`

Upstream stops at the first section matching the GUID and section type, which
is also what this build does by default. A real image often carries the same
module in more than one volume, and `--all-copies` patches all of them and
reports the count.

### `--hash`

A SHA-256 of the input and the output image (SHA-1 if built against Qt 4,
which has no SHA-256). When you are about to write one of several similar
files to a flash chip, the digest is the only thing that tells you which.

### Refusing to overwrite the input

`-o` pointing at the input image is a good way to lose the only dump of a
firmware you may need to recover with. That is now refused unless `--force`
is given. Overwriting any other existing file still works as before.

### Patch file lookup

`patches.txt` is looked for in the working directory, then next to the
UEFIPatch executable, so an installed copy finds the database it shipped with.

### Better diagnostics generally

Parse errors name the file, the line number and the offending text. Warnings
are reported separately for things that are suspicious but legal:

- a replacement pattern that is not the same length as the pattern it
  replaces, which in a UEFI module almost always means a typo,
- a section type outside the documented list,
- an unknown `#!` directive.

## GUI changes

Covered at length in [gui-guide.md](gui-guide.md); in summary:

- **Analyze image** under Tweaks, and the same report in the Messages list
  on every open.
- **A yellow on black theme**, in one stylesheet compiled in as a Qt
  resource, with a **View -> Yellow on black theme** toggle that takes effect
  without a restart and is remembered between sessions. Panels, lists, inputs
  and menus use `rgba()` fills so they layer over what is beneath them, and
  corners are rounded throughout.
- **View -> Window opacity**, 100% down to 75%, applied to the dialogs as
  well as the main window. It needs a compositing desktop to show anything.
  The window keeps its native title bar: real translucency in Qt needs a
  frameless window with `WA_TranslucentBackground`, which costs the title bar
  and the window manager behaviour that goes with it. The accent is only
  ever used on a near black background and anything on an accent fill is
  drawn in `#121212`, so contrast stays above 4.5:1 both ways. A ticked
  checkbox on a selected row inverts to a dark square, because an accent
  coloured one would vanish into the accent coloured selection.
- **Tweaks -> Performance presets...** (Ctrl+T), enabled once an image is
  open. Presets are grouped by platform, carry a risk and a description, and
  the pane below the list shows the caveat and which modules a preset
  touches. **Apply tweaks** patches the open image wherever the patterns
  match, then reports how many lines applied and how many did not and offers
  to rebuild and save. When nothing matched it says so rather than implying
  the image changed.
- **Patch every copy of a module** is on by default, so a module present in
  several volumes is patched in all of them.
- **Load preset file...** reads any file in the preset format, so patterns
  you derive yourself appear in the list without rebuilding anything.
- The per-line report goes to the **Messages** list as well as into the
  result box, because a message box's detail pane is a small fixed-size
  area and the report is the thing you want to read and keep.

- The **This image** column, filled from a scan of the open file: *will
  apply*, *module here, pattern differs*, or *not in this image*. The
  **Suggested** button ticks only the first kind. The matching is read only
  and a test pins that evaluating a catalogue leaves the image byte for byte
  unchanged.

Entries with nothing to apply (the AMD and Graphics groups) are shown as
notes, not as tickable presets. There are no upstream byte patterns for
those, and none were invented, so the dialog explains the route that does
work instead of offering a button that would do nothing.

### Scanning

`firmwarereport.cpp` walks a parsed image and works out what it is:

- **Platform**, Intel or AMD, from the region layout, from module names that
  only belong to one vendor's platform code, and from long strings in module
  bodies such as `AGESA!` or `$IBIOSI$`. Evidence for both is reported as
  ambiguous rather than silently resolved, and the evidence is listed rather
  than asserted.
- **Firmware vendor**, from copyright strings with module name prefixes as a
  fallback.
- **Inventory**: every module with its GUID, the name it carries in its own
  user interface section, its size and its volume.
- **Apriori lists**, parsed out of the PEI and DXE apriori files.

It runs when an image is opened, feeds both dialogs, and is reported to the
Messages list and under **Tweaks -> Analyze image**.

### Debloat

`knowledge/debloat.txt` is a catalogue of 108 removable modules in 12
categories, each with a risk, a note on what removing it actually gains, and
the conditions under which it is safe at all. Entries match on a module's
own name by default, because a GUID list is a list of guesses that vary
between vendors while a user interface section is what the image itself says
the module is.

Only categories with something found in the open image are shown. Before
anything can be removed, each candidate is checked three ways:

- **Apriori membership.** A module the dispatcher is explicitly told to run
  at a given point. A hit forces the row to high risk regardless of category.
- **GUID cross reference.** Every other module's body is searched for this
  module's raw 16 byte GUID, and the modules that carry it are named.
- **Last of its file type**, which catches removing the only DXE core.

The dialog states plainly what this cannot do: a dependency expression
records protocol GUIDs rather than file GUIDs, so a clean result means
nothing was found against a module, not that removing it is safe. The check
is sound in one direction only.

The banner across the top says that none of this affects frame rate, because
that is the most common reason people reach for a debloat feature and it is
not true.

**Protected categories.** `#!protected` marks a category as matched and
shown but never removable: it gets no checkbox at all. The compatibility
support module, storage and RAID drivers, the firmware's own flash write
path and AMD platform initialisation are all marked this way. The line is
not about size or how useless something looks, it is about what happens when
the guess is wrong: a slower POST, or a board that needs a clip and a
programmer. A test pins that even ticking everything in the dialog cannot
select a protected module.

### A bug the real image found

The first run against a real AMD B550 image reported the platform as
*"ambiguous, evidence for both Intel and AMD"*. The Intel hint `MeSmm` was
being matched as a substring, and it sits inside `RuntimeSmm` and `NvmeSmm`,
both perfectly ordinary modules on an AMD board. Two false positives were
enough to spoil the verdict.

Hints are matched as name prefixes now, so they identify a module family
rather than turning up by accident in the middle of an unrelated name. A
split that is still lopsided resolves to the majority and says so
("AMD, with some Intel named modules") instead of giving up.

### Scanning and extracting from the command line

- `--analyze` prints the same report the GUI shows, plus which presets fit
  the image, and with `-v` the whole module inventory.
- `--dump <dir>` writes every leaf section body, decompressed, named after
  the module it came from. This is what makes the pattern workflow possible
  with only this repository: a byte scan of a raw dump is close to useless
  because the module bodies are compressed inside their volumes.

On a real 16 MB image the difference is stark: a raw scan found 400
`rdmsr`/`wrmsr` byte pairs and could attribute 40 of them, to numbers like
`0xB2B400E1`. The same image's extracted modules gave 2322 sites with 1338
attributed, to real MSRs in the modules that should have them.
`tools/msr-lock-scan.py --summary` now warns when that ratio looks wrong.

### Shared parsing

Patch file parsing, validation and application moved out of `uefipatch.cpp`
into `patchset.cpp`, which both the GUI and UEFIPatch link. That is what
makes the GUI and the command line tool produce the same bytes from the same
preset, and `tests/run_gui_tests.sh` ends by diffing the two to prove it.

Two directives were added for the GUI, both of which older builds still read
as comments:

```
#!platform <name>   group the sets declared after this under <name>
#!info <text>       an entry with no patch lines, explaining what to do instead
```

## Compatibility

- Old patch files work unchanged; `#!` directives are comments to older
  builds.
- `UEFIPatch image patches.txt -o out` behaves as before.
- `UEFIPatch image -p "GUID 10 P:find:replace" -o out` behaves as before.
- For the stock upstream `patches.txt`, the modified build produces a
  **byte-identical** output image. `tests/run_tests.sh` pins this.
- Builds against Qt 4 and Qt 5. Verified here with Qt 5.15.13; the only
  compiler warnings are the pre-existing upstream `QModelIndex::child()` and
  `qSort()` deprecations in `ffsengine.cpp`, none from the changed files.

## Tests

### Command line

`tests/run_tests.sh` builds UEFIPatch (or takes a binary as its argument) and
runs 37 cases against a synthetic firmware volume built by
`tests/make_test_image.py`. The fixture is a real FFS2 volume — correct
header and file checksums, an 8-byte-aligned file layout, four modules, one
of them a deliberate duplicate so `--all-copies` is actually exercised — so
the engine parses and reconstructs it the way it would a real dump, without
needing anyone's firmware.

```
$ tests/run_tests.sh
Building UEFIPatch with /usr/lib/qt5/bin/qmake ...
Testing .../UEFIPatch
UEFIPatch 0.28.0-perf1 - UEFI image file patching utility

ok    truncated patch spec is rejected, not a crash
...
37 passed, 0 failed
```

### GUI

`tests/run_gui_tests.sh` builds `tests/guicheck`, which links the same
sources as UEFITool but with its own `main()`, and runs 96 checks under Qt's
offscreen platform plugin. No X server and no real firmware are needed. It
covers the theme resource and toggle, the menu layout (including that Help
stays last), presets being disabled until an image is open, platform
grouping, which rows are tickable, and selection reporting. It renders the
screenshots used in the documentation, and finishes by running the same
apply sequence the GUI performs and diffing the result against the image
UEFIPatch produces from the same preset.

The fixture in `tests/make_test_image.py` carries named modules, a DXE
apriori list and a module that references another by GUID, so the scanner
and the dependency check are exercised against contents that are known
exactly rather than against whatever a real dump happens to contain.

```
$ tests/run_gui_tests.sh
...
96 passed, 0 failed

Cross-checking the GUI apply path against UEFIPatch
ok    the GUI and UEFIPatch produce a byte-identical image

GUI tests passed.
```

`tests/screenshot.sh` is the other half: it builds UEFITool, runs the real
binary under Xvfb with openbox and clicks through the preset flow with
xdotool, so the pictures in the documentation are of the program itself.
guicheck's own offscreen renders are named `offscreen-*.png` so the two
can not be confused.

## Tried against a real image

[walkthrough-real-bios.md](walkthrough-real-bios.md) runs the whole flow
against a 16 MB MSI B550 dump: scan, analyze, presets (which correctly find
nothing, because it is an AMD board and every preset is Intel), debloat,
rebuild and save. The rebuilt image was reopened and diffed against the
original from the command line, confirming exactly the three ticked modules
were removed and nothing else changed.

It was never flashed. A rebuilt image that parses cleanly is not a booting
board, and this project has no way to find out which.

## Not done

- `UEFIReplace` is untouched, including its own copy of the GUID handling.
- The preset dialog cannot edit or create presets, only load a file. The
  debloat dialog cannot edit its catalogue either, though both files are
  plain text and are read at startup.
- No byte patterns were added. The scanner makes it clear which of the
  existing ones apply to a given image, which is the honest substitute for
  testing them against hardware this project has no access to.
- Vendor detection has no entry for the OEM behind a board, only for the
  firmware codebase (AMI, Insyde, Phoenix). Board identification lives in
  SMBIOS tables that are populated at runtime, not in the image.
- There is still no AMD preset. Scanning a real AM4 image located boost and
  P-state control in `AmdCcx*Dxe/Smm` and `CbsSetupDxe*`, which is useful to
  know, but AMD has no equivalent of Intel's `CFG_LOCK` and no established
  lock-removal patch for those registers. Writing one from a byte scan with
  no board to test on would be inventing a patch and calling it knowledge.
- The `patchViaPattern()` matcher still builds a hex string of the whole
  section body and runs a `QRegExp` over it, which is slow on large sections.
  It is correct, so it was left alone.
- No new byte patterns. Patterns are specific to a vendor, a board and often a
  single BIOS build, so inventing them would be worse than useless;
  `tools/msr-lock-scan.py` and `docs/pattern-cookbook.md` are there to derive
  your own instead.
