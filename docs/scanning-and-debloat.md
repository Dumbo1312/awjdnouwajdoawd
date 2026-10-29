# Scanning an image, and removing things from it

Two features that only make sense together: the scanner works out what the
image in front of you actually is, and the Debloat dialog uses that to offer
removals that exist in *your* firmware rather than in firmware in general.

![The debloat dialog](images/debloat-dialog.png)

## First, the uncomfortable part

**Removing firmware modules does not increase frame rate.** Not one entry in
the catalogue will give you a single extra FPS. By the time Windows is
running, none of that code is executing. The dialog says so in orange across
the top, because it is the single most common misunderstanding about BIOS
modding and the reason people brick boards for nothing.

What removal actually buys:

- **A shorter POST.** The network boot stack is the big one; on a board that
  probes the network at every boot it can be a second or more.
- **Less code running before the OS**, which is a real security argument.
- **Free space in the BIOS region**, which matters if you are adding
  something.

What it can cost is a machine that does not boot. If you want frame rate,
[bios-performance-guide.md](bios-performance-guide.md) is the document you
want, and the answer there is memory timings, power limits and Resizable BAR
— all settings, none of them removals.

## What the scanner works out

Opening an image runs a scan automatically, and the result goes to the
Messages list. **Tweaks -> Analyze image** shows it again with the full
module inventory.

### Platform

Intel or AMD, with the evidence listed rather than asserted:

- **Regions.** A flash descriptor with a Management Engine region beside it
  is about as Intel as an image gets.
- **Module names.** Modules carry their own name in a user interface section,
  and some names only ever belong to one vendor's platform code: `SiInit`,
  `PchInit`, `SaInit`, `PowerMgmtDxe`, `PpmInitialize`, `IgdOpRegion` on the
  Intel side; anything starting `AmdCpm`, `AmdInit`, `AmdPsp`, `AmdSoc`, plus
  `Agesa` and `FchInit`, on the AMD side.
- **Strings in module bodies.** `AGESA!` and `AMD-AGESA` for AMD, `$IBIOSI$`
  and the Management Engine string for Intel. These are long enough that a
  chance hit is not a realistic worry.

Evidence for both is reported as ambiguous rather than silently picking one.
An image the scanner cannot place says so, and the dialogs then stop
filtering by platform instead of guessing.

### Firmware vendor

AMI Aptio, Insyde or Phoenix, from copyright strings (`American Megatrends`,
`InsydeH2O`, `Phoenix Technologies`) with module name prefixes as a fallback
for images that have had the strings stripped.

### Inventory

Every module, with its GUID, the name it carries itself, its size and which
volume it is in. Plus the contents of any PEI or DXE apriori file, which
matters for the dependency check below.

## How Debloat decides what to offer

Entries come from `knowledge/debloat.txt`, which is a plain text file you can
edit. An entry matches either on a module's **own name** or on a GUID:

```
#!category network-boot
#!label Network boot stack (PXE, HTTP boot, iSCSI)
#!risk medium
#!impact Usually the largest single POST time saving.
#!note Remove only if you never boot this machine from the network.

N UefiPxeBc           UEFI PXE base code, the network boot client
N Ip4Dxe              IPv4 stack
G 12345678-1234-...   a module identified by GUID instead
```

Name matching is the primary mechanism on purpose. A GUID list is a list of
guesses that differ between vendors and BIOS versions; a module's user
interface section is what the image itself says the module is. A name that
matches nothing simply does not appear, which is the safe way to be wrong.

Only categories with something found in the open image are shown, so the
dialog is short and specific rather than a wall of things you do not have.

Categories carry a `#!platform`, and one that does not match the detected
platform is dimmed rather than hidden — if the detection is wrong you can
still see what is there.

## The dependency check

This is the part that earns the feature. Before anything can be removed, each
candidate gets three questions asked of it.

### Is it in an apriori list?

PEI and DXE apriori files are flat arrays of file GUIDs that tell the
dispatcher to run those modules first, in that order. A module listed there
is one the firmware explicitly expects to exist at a particular point.
Removing it without also fixing the apriori list is a classic way to stop a
board booting, so a hit here is shown in amber and pushes the row's risk to
high regardless of what its category says.

### Does anything carry its GUID?

The scanner reads every other module's body looking for this module's raw
16-byte GUID. A module that loads another by file GUID has that GUID sitting
in its code or data, so a hit usually means a real dependency. The dialog
names which modules it found it in.

### Is it the only module of its file type?

Removing the only DXE core in an image is a specific kind of mistake, so it
is called out separately.

### What this check cannot tell you

A dependency expression records **protocol** GUIDs, not file GUIDs. A module
can be absolutely required and have its file GUID appear nowhere, because
whatever needs it asks for a protocol and the dispatcher works out who
provides it. The dialog says this in as many words when a candidate comes
back clean:

> Nothing found: no apriori entry, and no other module carries this GUID.
> This is the weakest kind of good news. A clean scan means nothing was found
> against it, not that it is safe to remove.

The check is sound in one direction only. It proves a removal is risky; it
never proves one is safe.

## Doing it

1. Open the image. The scan runs and the summary goes to Messages.
2. **Tweaks -> Debloat...** (Ctrl+D).
3. Read the category note and the dependency panel for everything you tick.
   Nothing is ticked for you.
4. **Remove selected** asks again, and says how many of your choices have a
   dependency finding against them. The default answer is No.
5. Nothing is written yet. The modules are marked in the tree and the
   Messages list records every one. Save to write the image.

Then read [risks-and-recovery.md](risks-and-recovery.md) before you flash it,
particularly the Boot Guard section: on a lot of hardware a modified image
will not boot at all, and no amount of care with this dialog changes that.

## What is never offered

Some categories carry `#!protected`. They are matched and shown, so you can
see what your image contains, but they have no checkbox and the tool will not
remove them whatever you click. A test pins that even ticking everything
cannot select one.

| Category | Why |
| --- | --- |
| Compatibility Support Module | CSM is on the boot path. Turning it off in setup gives the same pure UEFI boot without touching the image, and a CMOS clear undoes that. |
| Storage controllers and RAID | Remove the driver for the controller your boot disk is on and there is nothing to boot from and nothing to recover with but a programmer. |
| The firmware's flash write path | How the board updates and recovers itself. Removing it can leave a machine that boots fine and can never be rewritten by software again, which does not show up until the day you need it. |
| AMD platform extras | AMD firmware dispatches modules from tables this tool cannot read, so the dependency check has nothing to go on. |

The line between "offered" and "protected" is not about size or how useless
something looks. It is about what happens when the guess is wrong: a slower
POST, or a board that needs a clip and a programmer.

## Adding your own entries

`knowledge/debloat.txt` is read at startup, so an entry you add appears next
time you open the dialog:

```
#!category my-board
#!label Things my board ships that I do not use
#!risk high
#!note Worked out from my own dump, not tested anywhere else.

N SomeVendorThing   what it is
```

The same applies to `patches/presets.txt` for tweaks. Neither file needs the
program rebuilt.

## Extracting modules

A byte scan of a raw firmware dump is close to useless, because most module
bodies are compressed inside their volumes: the byte pairs that look like
`rdmsr` and `wrmsr` are mostly compressed data. On a real 16 MB image, a raw
scan found 400 sites and could attribute 40 of them, to numbers like
`0xB2B400E1`. Scanning the same image's extracted modules found 2322 sites
and attributed 1338, to real MSRs in the modules that should have them.

So extract first:

```sh
UEFIPatch bios.bin --dump out/
tools/msr-lock-scan.py --summary out/*.bin
```

`--dump` writes every leaf section body, decompressed, named after the module
it came from. `--summary` gives one line per MSR across every file, and warns
when the attributed-to-unattributed ratio says you are looking at compressed
data rather than code.

## Where the scanner is used elsewhere

The preset dialog uses the same scan for its **This image** column:

- **will apply** — the pattern was found, applying will change something
- **module here, pattern differs** — the module exists but this pattern is
  not in it, which is what a different BIOS version usually looks like
- **not in this image** — the module is not there at all

**Suggested** ticks only the first kind. That is as close to "will this work
on my BIOS" as anything can honestly get without flashing it, and it is why
the preset list is worth opening even if you already know which preset you
want.

[walkthrough-real-bios.md](walkthrough-real-bios.md) runs all of this against
a real 16 MB AMD image, including what it looks like when the answer is that
nothing applies.
