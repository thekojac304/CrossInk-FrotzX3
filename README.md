# FrotzX3

**A native-feeling Z-machine Interactive Fiction player, integrated with [CrossInk](https://github.com/uxjulia/CrossInk), for the XTEINK X3.**

FrotzX3 turns the XTEINK X3 into a dedicated e-ink text-adventure machine. It runs
real Z-machine story files (the format used by Infocom games and most modern
parser-based interactive fiction) through an interface designed around the X3's few
physical buttons and its e-ink display.

It is built on CrossInk firmware and the [Frotz](https://github.com/DavidGriffith/frotz)
interpreter.

> **No games are included.** You must supply your own legally obtained Z-machine
> story files. See [Adding games](#adding-games).

> **Public beta: `v0.9.0-beta.1`.** FrotzX3 runs on a physical XTEINK X3, but this is
> a first public beta. Expect rough edges and read [Known limitations](#known-limitations).

**Contents:** [Install](#install) · [Adding games](#adding-games) · [Controls](#controls) ·
[Saves and rewind](#saves-and-rewind) · [Compatibility](#compatibility) ·
[Known limitations](#known-limitations) · [Building from source](#building-from-source) ·
[Reporting problems](#reporting-problems) · [Licensing](#licensing-and-attribution)

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

## Supported hardware

- **XTEINK X3**: developed and physically tested.

CrossInk supports other devices, and the firmware image is the shared X3/X4 build, but
**only the X3 has been tested with FrotzX3.** Other devices are not supported targets.

---

## Install

FrotzX3 is a CrossInk-based firmware build with the interactive-fiction player added.
Installing it **replaces your current CrossInk firmware** with FrotzX3's build (based on
CrossInk 1.5.0 plus three upstream commits). Your SD card contents are not touched. To go
back, install official CrossInk again.

### Recommended beta install

Use the prebuilt, hardware-tested firmware.

1. Download `FrotzX3-v0.9.0-beta.1-firmware-x3-x4.bin` from the
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

### Experimental patch installer

> Experimental: **not** hardware-tested as an install method and not the recommended path yet.

`tools/installer/Install-FrotzX3.ps1` (Windows PowerShell) builds the firmware locally
instead of downloading a prebuilt one. It:

1. downloads official CrossInk,
2. checks out the exact supported CrossInk commit,
3. applies the FrotzX3 patch package,
4. builds locally with PlatformIO, and
5. writes the firmware `.bin` and its SHA-256 (it does **not** flash your device).

```powershell
powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1
```

Requires Git and PlatformIO. Details, options and troubleshooting:
[tools/installer/README.md](tools/installer/README.md). This architecture is intended to
make future CrossInk updates easier: a new CrossInk release only needs a small patch update
and a compatibility entry ([UPDATING_CROSSINK.md](tools/installer/UPDATING_CROSSINK.md)).

An older "transplant" installer (`Install-FrotzX3.cmd`) is still in the repository for
reference; see [FROTZX3_INSTALLER_README.md](FROTZX3_INSTALLER_README.md). Prefer the
steps above.

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

On the X3, Select can share its button with Power. FrotzX3 defers saves and other slow work
until Select is released, so a Select press does not put the device to sleep. The exact
controls may change as the interface is refined.

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
- Development testing included Zork I (Z3 and Z5), Planetfall, Lost Pig (Z8), a PunyInform
  test story and other test stories. **This does not mean every game works.** Some unusual
  games may expose interpreter or interface behavior that has not been tested, and output is
  limited to plain text (no graphics, styles or non-ASCII characters).

## Known limitations

- Beta software, tested only on the XTEINK X3.
- Firmware flash space is nearly full on the X3 (the image uses roughly 97% of the OTA
  partition), so there is little room for new features.
- Compatibility is not universal; see above.
- At most 16 story files are listed in the picker, and only from `/adventures`.
- The keyboard has letters only (no digits or punctuation). Number choices use the single-key pad.
- Z4+ stories have no status line display; Z3 stories show room and score/moves in the header.
- The experimental patch installer is not yet the default install method and has not been hardware-tested.
- Future CrossInk releases may need a small FrotzX3 compatibility update.

---

## Building from source

You need [Git](https://git-scm.com/) and [PlatformIO Core](https://platformio.org/install/cli).
The firmware depends on a submodule (`freeink-sdk`, which has its own submodule), so clone
with submodules:

```powershell
git clone --recurse-submodules https://github.com/thekojac304/CrossInk-FrotzX3.git
cd CrossInk-FrotzX3
pio run -e default
```

If you already cloned without submodules, run `git submodule update --init --recursive` first.
The first build downloads the ESP32 toolchain and compiles the framework (about 10 minutes
on a fast PC). The image is written to `.pio/build/default/firmware-x3-x4.bin`
(`firmware.bin` is the same image). To flash over USB with PlatformIO:
`pio run -e default -t upload`. If `pio` is not on your PATH, use
`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`.

GitHub's automatic "Source code" archives omit submodule contents and cannot be built on
their own. Use `git clone --recurse-submodules`, or the **complete-source ZIP** attached to
each release.

### Official release builds

```powershell
$env:CROSSINK_RELEASE_VERSION = '0.9.0-beta.1'
Remove-Item Env:CROSSINK_RC_HASH -ErrorAction SilentlyContinue
pio run -e default
```

Each release pairs the firmware with `FrotzX3-v<version>-source-complete.zip` (the
repository plus submodules at the release commit; no Git data, build output, games or saves).
See [tools/release/README.md](tools/release/README.md) for how it is made and how
`espressif/mdns` is pinned to 1.14.0 for reproducible builds.

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
- Your FrotzX3 version (`0.9.0-beta.1`) and your device.

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
- **[Frotz](https://github.com/DavidGriffith/frotz)** and its contributors (Stefan Jokisch, Jim
  Dunleavy, Martin Frost, David Griffith and others). FrotzX3 includes Frotz-derived source
  modified for the X3 and CrossInk: platform integration, SD storage, lifecycle and
  save/restore support, memory behavior suited to the ESP32-C3, and the interfaces used by the
  X3 UI.
- The interactive-fiction community and the open-source tools both projects rely on.

## Documentation

- [FrotzX3 overview](FROTZX3_README.md)
- [Integration and porting guide](FROTZX3_INTEGRATION.md)
- [Patch installer](tools/installer/README.md) · [Updating to a new CrossInk](tools/installer/UPDATING_CROSSINK.md) · [Delta vs. official CrossInk](tools/patches/FROTZX3_DELTA.md)
- [Release build and source bundle](tools/release/README.md)
- [Licensing](FROTZX3_LICENSE.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)
- [CHANGELOG](CHANGELOG.md)
- CrossInk documentation under [docs/](docs/)

FrotzX3 is a hobby open-source project. A successful compile is not treated as validation;
testing on a physical X3 is the authoritative test.
