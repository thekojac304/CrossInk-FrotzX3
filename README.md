# FrotzX3

**A native-feeling Z-machine Interactive Fiction player, integrated with [CrossInk](https://github.com/uxjulia/CrossInk), for the XTEINK X3.**

FrotzX3 turns the XTEINK X3 into a dedicated e-ink text-adventure machine. It runs
real Z-machine story files (the format used by Infocom games and most modern
parser-based interactive fiction) through an interface designed around the X3's few
physical buttons and its e-ink display.

It is built on CrossInk firmware and the [Frotz](https://github.com/DavidGriffith/frotz)
interpreter. **Current release: `v0.9.0-beta.2`, built on CrossInk `v1.6.1`.**

> **No games are included.** You must supply your own legally obtained Z-machine
> story files. See [Adding games](#adding-games).

> **Public beta: `v0.9.0-beta.2`, for CrossInk v1.6.1.** FrotzX3 runs on a physical XTEINK X3
> and the release firmware has been hardware-tested there, but this is still a beta. Expect rough
> edges and read [Known limitations](#known-limitations).
> The supported CrossInk base is the exact official **v1.6.1** commit
> (`9914146eeae7b46b300f475a16c32426fc02ec1f`); no other CrossInk version is supported.

**Contents:** [Install](#install) · [Adding games](#adding-games) · [Controls](#controls) ·
[Saves and rewind](#saves-and-rewind) · [Compatibility](#compatibility) ·
[Known limitations](#known-limitations) · [Building from source](#building-from-source) ·
[Reporting problems](#reporting-problems) · [Licensing](#licensing-and-attribution)

---

## What it feels like

This is not command-line Frotz squeezed onto an e-reader. FrotzX3 is designed to feel like a native
X3 activity, started from the Home menu like any other CrossInk feature:

- an **e-ink-native interface** with paged transcripts that avoid constant full refreshes;
- **physical-button-first controls**: you can play a whole game with the X3's buttons;
- **T9-style command entry**, with **autocomplete and context suggestions**, plus **action and
  movement menus** for the commands you type most;
- **parser and disambiguation handling**, so "Which do you mean...?" becomes a pick-one list;
- **save, load and rewind** built in, with resume and crash recovery.

---

## Features

- Frotz Z-machine interpreter running natively on the X3 (ESP32-C3).
- **Z3, Z5 and Z8** stories, hardware-tested.
- Game picker for story files on the SD card.
- Native e-ink UI with a **T9-style keyboard** for typing commands.
- **Autocomplete and context suggestions**, including inventory- and room-aware object suggestions.
- Quick **action menus** and a **movement menu** (compass directions, up/down, in/out).
- **Parser feedback and disambiguation**: "Which do you mean...?" questions become a pick-one choice.
- Status-line display for stories that provide one (Z3).
- **Resume** where you left off, **3 manual save slots**, and **rewind** to recent turns.
- **Adventure Log** of recent turns, with rewind.
- **Crash recovery** prompt if a session ended unexpectedly.
- Paged **transcript** designed for e-ink refresh, with long-press page navigation.
- Single-key input for stories that wait for a key press (`READ_CHAR`).
- Clean exit and re-entry without restarting the device.

### Reliability work in beta.2

- **CrossInk v1.6.1 support**, pinned to one exact commit.
- **Large-story memory fix:** big stories such as Lost Pig start reliably on v1.6.1 again.
- **Contiguous-memory guardrail:** a story that cannot be safely loaded is refused with a clear
  message instead of crashing the device.
- **Select/Power false-sleep fix:** pressing Select no longer puts the X3 to sleep by accident;
  a real long Power press still sleeps normally.
- **Safe Home cover cache handling:** Home frees its cover image before a story starts and redraws
  it when you leave.

## Supported hardware

- **XTEINK X3**: developed and physically tested.

The release firmware file is named `...-firmware-x3-x4.bin` because CrossInk builds one shared
X3/X4 image, but **only the X3 has been tested with FrotzX3.** The X4 and other CrossInk devices
are not supported targets, and nothing here implies they work.

---

## Install

FrotzX3 is a CrossInk-based firmware build with the interactive-fiction player added.
Installing it **replaces your current CrossInk firmware** with FrotzX3's build (based on
official CrossInk **v1.6.1**). Your SD card contents are not touched. To go back, install
official CrossInk again.

There are two ways to get FrotzX3 onto your device. Most people should use Path 1.

### Path 1: prebuilt release firmware (recommended)

The easiest option: download the firmware that has been hardware-tested on the X3.

1. Download `FrotzX3-v0.9.0-beta.2-firmware-x3-x4.bin` from the
   [Releases page](https://github.com/thekojac304/CrossInk-FrotzX3/releases).
2. Optional: check that its SHA-256 matches the value in the release notes
   (`Get-FileHash <file> -Algorithm SHA256` in PowerShell).
3. Install it with one of CrossInk's documented firmware-update methods
   (see [docs/installation.md](docs/installation.md)):
   - **SD card update:** copy the `.bin` to the SD card, then on the device go to
     **Settings > System > SD Card Firmware Update** and choose the file.
   - **USB:** flash the same file with `esptool` at offset `0x10000`
     (`esptool.py --chip esp32c3 --port <port> --baud 921600 write_flash 0x10000 <file>`).

   The CrossInk web installer at inky.crossink.dev installs *official* CrossInk, not FrotzX3.
4. After the device restarts, open **FrotzX3** from the Home menu.

### Path 2: FrotzX3 patch installer (builds locally)

Choose this if you would rather build the firmware yourself from official CrossInk source
than trust a prebuilt binary, or if you want to verify what goes into it. The installer path
is hardware-tested: the `0.9.0-beta.2` release firmware is the exact output of a default
installer run, and it passed testing on an X3.

`tools/installer/Install-FrotzX3.ps1` (Windows PowerShell) builds the firmware locally
instead of downloading a prebuilt one. By default it targets the supported base, CrossInk
v1.6.1. It:

1. downloads official CrossInk,
2. checks out the exact supported commit (`9914146eeae7b46b300f475a16c32426fc02ec1f`) and
   refuses any other,
3. verifies and applies the FrotzX3 patch package (every file is checked against its SHA-256),
4. builds locally with PlatformIO, and
5. writes the firmware `.bin` and its SHA-256 (it never flashes your device automatically).
   Install the result with the same SD-card or USB methods as Path 1.

```powershell
powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1
```

Requires Git and PlatformIO. On Windows, a short work folder helps avoid path-length errors
in the ESP-IDF build: add `-WorkDir C:\fx3`. A rebuild will not be byte-identical to the
published firmware (build time is embedded), so its SHA-256 will differ. Details, options and troubleshooting:
[tools/installer/README.md](tools/installer/README.md). A future CrossInk release needs a
new package and its own hardware validation before it is marked supported
([UPDATING_CROSSINK.md](tools/installer/UPDATING_CROSSINK.md)).

An older "transplant" installer (`Install-FrotzX3.cmd`) is still in the repository for
reference; see [FROTZX3_INSTALLER_README.md](FROTZX3_INSTALLER_README.md). Prefer the
two paths above.

---

## Adding games

Create an `adventures` folder at the top of the SD card and copy your story files into it:

```text
/adventures/zork1.z3
/adventures/Planetfall.z5
/adventures/LostPig.z8
```

- The picker lists story files ending in `.z3` to `.z8` (case-insensitive), directly
  inside `/adventures` (subfolders are not searched), **up to 16 stories**.
- Z3, Z5 and Z8 are the tested versions. See [Compatibility](#compatibility).
- Saves are created automatically (see [Saves and rewind](#saves-and-rewind)). The full
  file name, including the extension, forms the save name, so renaming a story starts a new
  set of saves.

FrotzX3 does **not** include Zork, Planetfall, Lost Pig or any other game. Many modern
interactive-fiction games are free to download from sites such as the
[IF Archive](https://ifarchive.org/); commercial games are not free to redistribute. Each
game's own permissions apply.

---

## Controls

FrotzX3 uses four actions everywhere: **Back**, **Select**, **Previous** and **Next**.
Previous/Next are the front Left/Right buttons or the side page buttons, and
selection lists wrap around at both ends. The on-screen hint bar shows what each button does.

| Where | What it does |
| --- | --- |
| Game picker | Previous/Next choose a story; Select opens it; Back leaves FrotzX3. |
| Start prompt | Resume, load a manual save, or New Game (shown when saves exist). |
| Actions menu | Look, Go, Take, Drop, Examine, Open, Close, Inventory, Read, Search, Climb, Type Command. |
| Movement menu | N, NE, E, SE, S, SW, W, NW, Up, Down, In, Out. |
| Object menus | Take/Drop/Examine/Read/Open list objects FrotzX3 found in the room and your inventory, plus a "Type..." entry. |
| Keyboard | T9-style letter groups (ABC, DEF, ...), Space, Suggest, Delete, Enter. Select a group, then a letter. Back steps up; Back on the group row cancels. Up to 63 characters. |
| Suggestions | Up to three completions based on verbs and known objects. |
| Transcript | **Hold** a page button for about half a second to jump to the previous/next transcript page. |
| Game menu | **Hold Back** for about half a second: Save Game, Load Game, Rewind, Adventure Log, Exit Game. |
| Single-key prompts | When a story waits for one key, a key pad appears (digits, Y/N, then More for letters, symbols, arrows, function keys). |

On the X3, CrossInk's defaults let the Power button act as Select. FrotzX3 keeps Select
interaction separate from sleep, so tapping Select (even while another button is held) does not
put the device to sleep, and slow work such as saving waits until the button is released. A real
long Power press still sleeps the device normally. The exact controls may change as the interface
is refined.

---

## Saves and rewind

- **Resume:** choosing **Exit Game** from the game menu saves your place and quits. Next time,
  pick **Resume**. If you start a New Game while a resume save exists, you are asked before it
  is replaced.
- **Manual saves:** three slots, from the game menu. Overwriting and loading ask for confirmation.
- **Rewind:** FrotzX3 keeps checkpoints of your most recent turns (five) on the SD card.
  Use **Rewind** in the game menu for the latest one, or pick an entry marked `[R]` in the
  **Adventure Log** (it lists your last ten turns).
- **Crash recovery:** if a session ended without a clean exit, the next start offers to recover
  the latest checkpoint or ignore it.

All of it lives under `/adventures/saves/` on the SD card:

```text
/adventures/
  Story.z5
  saves/
    Story.z5.sav                  # resume save
    Story.z5.recovery             # crash-recovery marker
    manual/Story.z5.manual1.sav   # manual slots 1-3
    rewind/Story.z5.rewind<N>.sav # rewind checkpoints
```

---

## Compatibility

- **Z3, Z5 and Z8** stories have been physically tested on an XTEINK X3.
- The picker also lists `.z4`, `.z6` and `.z7` files, and the interpreter accepts Z-code
  versions 1 to 8, but Z4 and Z7 are untested and Z6 (graphical) stories are not a supported target.
- Hardware testing on CrossInk v1.6.1 covered Lost Pig (Z8), Zork and Varicella; earlier
  development testing included Zork I (Z3 and Z5), Planetfall, a PunyInform test story and other
  test stories. **This does not mean every game works.** Some unusual
  games may expose interpreter or interface behavior that has not been tested, and output is
  limited to plain text (no graphics, styles or non-ASCII characters).

### Story memory

A story's dynamic memory must fit in one contiguous block of the X3's RAM (the ESP32-C3 has no
PSRAM). Before allocating it, FrotzX3 checks that the largest allocatable block is at least the
story's dynamic memory plus 1,024 B, and otherwise stops with **"Not enough contiguous memory
for this story."** instead of risking the allocation.

- Hardware-validated through **Lost Pig, which needs 42,554 B** (plus Zork and Varicella).
- Larger legal stories may work if enough contiguous RAM is free at launch; the runtime check
  above decides, and refuses cleanly if not.
- The Z-machine format allows up to 65,534 B of dynamic memory. This build does **not**
  guarantee that every story up to that theoretical maximum will fit; larger stories are
  covered by the runtime check, not by a guarantee.

## Known limitations

- Beta software, tested only on the XTEINK X3, and only on CrossInk v1.6.1.
- Stories that need close to the Z-machine maximum of 65,534 B of dynamic memory may be
  refused with "Not enough contiguous memory for this story."; only stories up to Lost Pig's
  42,554 B are hardware-validated.
- Firmware flash space is nearly full on the X3 (the image uses roughly 97% of the OTA
  partition), so there is little room for new features.
- Compatibility is not universal; see above.
- At most 16 story files are listed in the picker, and only from `/adventures`.
- The keyboard has letters only (no digits or punctuation). Number choices use the single-key pad.
- Z4+ stories have no status line display; Z3 stories show room and score/moves in the header.
- Future CrossInk releases need a FrotzX3 compatibility update and their own hardware
  validation before they are supported.

---

## Building from source

**How a release is made.** From `0.9.0-beta.2`, FrotzX3 is not built from this repository's root
tree. The patch installer reconstructs it: it downloads **official CrossInk v1.6.1** (exact commit
`9914146eeae7b46b300f475a16c32426fc02ec1f`), applies the FrotzX3 patch package under
`tools/patches/v0.9.0-beta.2-crossink-1.6.1/`, and builds the result with PlatformIO. The root tree
of this repository is the older CrossInk 1.5.0-based development tree used for `v0.9.0-beta.1`;
building it directly does **not** reproduce the beta.2 release.

**Recommended developer workflow** (needs [Git](https://git-scm.com/) and
[PlatformIO Core](https://platformio.org/install/cli)):

```powershell
git clone https://github.com/thekojac304/CrossInk-FrotzX3.git
cd CrossInk-FrotzX3
powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1 -KeepWorkDir -WorkDir C:\fx3\work
```

`-KeepWorkDir` leaves the reconstructed CrossInk v1.6.1 + FrotzX3 tree in the work folder, where you
can edit and run `pio run -e default` directly. The first build downloads the ESP32 toolchain and
compiles the framework (about 10 minutes on a fast PC). The firmware is written to `dist-installer\`
with its SHA-256. Rebuilds of the same source are not byte-identical (build time strings are
embedded), so compare source revisions, not hashes, when reproducing a release.

**Complete source ZIP.** Every release attaches `FrotzX3-v<version>-source-complete.zip`: the exact
tree that was built, every submodule, the installer and patch package, the license files, and
`SOURCE_BUNDLE_MANIFEST.txt` with the exact revisions and build command. It contains no Git data,
build output, games or saves, and it is the corresponding source for the GPL-covered code in the
firmware. GitHub's automatic "Source code" archives omit submodule contents and do not contain the
CrossInk base, so they are **not** a substitute. See [tools/release/README.md](tools/release/README.md)
for how the ZIP is made and how `espressif/mdns` is pinned to 1.14.0 for reproducible builds.

The root tree is still useful for the older line and for development reference; it is simply not
the beta.2 release source.

For developers: [FROTZX3_INTEGRATION.md](FROTZX3_INTEGRATION.md) explains how FrotzX3 attaches
to CrossInk, and [tools/patches/FROTZX3_DELTA.md](tools/patches/FROTZX3_DELTA.md) lists every
change against official CrossInk. FrotzX3's own code lives in `lib/FrotzX3/` and
`src/activities/frotzx3/`; the CrossInk edits are small.

---

## Reporting problems

Bug reports, compatibility findings and test reports are welcome via
[GitHub Issues](https://github.com/thekojac304/CrossInk-FrotzX3/issues). It helps to include:

- Story file name and Z-machine version.
- What you were doing when the problem happened.
- Whether the game was new or resumed, and whether saving, loading or rewinding was involved.
- Serial log, if you have one. The log prints the FrotzX3 version each time a story starts.
- Your FrotzX3 version (`0.9.0-beta.2`) and your device.

---

## Licensing and attribution

This repository combines code under compatible open-source licenses:

- **CrossInk** is under the **MIT License**. The original [LICENSE](LICENSE) is unchanged.
- **Frotz-derived code** in `lib/FrotzX3/` is under the **GNU GPL, version 2 or later**
  (`GPL-2.0-or-later`), with original notices retained.

Because FrotzX3 firmware contains GPL-covered Frotz-derived code, anyone redistributing the
firmware must also make the corresponding source available under the GPL. That is why every
release ships a matching complete-source ZIP. The root `LICENSE` is the CrossInk MIT license
and does not relicense Frotz code.

Details: [FROTZX3_LICENSE.md](FROTZX3_LICENSE.md),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [LICENSES/](LICENSES/).

**Game files are separate.** FrotzX3 does not include any game or story file, and open-source
licenses on Frotz or CrossInk grant no rights to commercial games. Obtain and use story files legally.

### Acknowledgments

FrotzX3 exists because of:

- **[CrossInk](https://github.com/uxjulia/CrossInk)** and its contributors, who provide the
  device support, rendering, input, SD access, activity framework and firmware infrastructure
  FrotzX3 is built on. For general CrossInk documentation, see the upstream project and the
  [docs/](docs/) folder.
- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**, the open-source
  project CrossInk itself descends from.
- **[Frotz](https://github.com/DavidGriffith/frotz)** and its contributors (Stefan Jokisch, Jim
  Dunleavy, Martin Frost, David Griffith and others). FrotzX3 includes Frotz-derived source
  modified for the X3 and CrossInk: platform integration, SD storage, lifecycle and
  save/restore support, memory behavior suited to the ESP32-C3, and the interfaces used by the
  X3 UI.
- The interactive-fiction community and the open-source tools these projects rely on.

FrotzX3 is an independent project. It is not an official CrossInk, CrossPoint, Frotz or XTEINK
product and is not endorsed by their authors.

## Documentation

- [FrotzX3 overview](FROTZX3_README.md)
- [Integration and porting guide](FROTZX3_INTEGRATION.md)
- [Patch installer](tools/installer/README.md) · [Updating to a new CrossInk](tools/installer/UPDATING_CROSSINK.md) · [Delta vs. official CrossInk](tools/patches/FROTZX3_DELTA.md)
- [Release build and source bundle](tools/release/README.md)
- [Licensing](FROTZX3_LICENSE.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)
- [CHANGELOG](CHANGELOG.md)
- CrossInk documentation under [docs/](docs/)

FrotzX3 is a hobby open-source project. A successful compile is not treated as validation;
testing on a physical X3 is the authoritative test. Issues and test reports (especially other
story files and results on other CrossInk versions) are welcome.
