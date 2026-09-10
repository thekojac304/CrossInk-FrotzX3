# FrotzX3

**Frotz / Z-machine interactive fiction for the XTEINK X3, integrated into CrossInk.**

FrotzX3 turns the XTEINK X3 into a dedicated e-ink text-adventure machine.

It runs real Z-machine story files through a native-feeling interface designed around the X3's limited physical controls, e-ink display, and ESP32-C3 hardware.

FrotzX3 is built on top of [CrossInk](https://github.com/uxjulia/CrossInk) and the [Frotz](https://github.com/DavidGriffith/frotz) Z-machine interpreter.

> **No games are included.**
>
> You must provide your own legally obtained Z-machine story files.

---

## Quick Start

### Recommended Windows installation

1. Download or clone this repository.
2. Double-click `Install-FrotzX3.cmd`.
3. Choose **Download a fresh CrossInk and install FrotzX3**.
4. Follow the guided installer.

The installer can:

- Download a fresh CrossInk source tree.
- Check whether the CrossInk version is compatible.
- Apply the small X3 input compatibility change when needed.
- Install FrotzX3.
- Build the firmware.
- Optionally flash a connected XTEINK X3.

Your original known-good FrotzX3 source folder is not modified.

For detailed installer information, see [FROTZX3_INSTALLER_README.md](FROTZX3_INSTALLER_README.md).

---

## What FrotzX3 Can Do

FrotzX3 currently includes:

- Real Frotz Z-machine interpreter integration.
- Z3, Z5, and Z8 story support.
- Game picker for story files stored on the SD card.
- Resume and New Game startup choices.
- Automatic save / recovery support.
- Manual save slots and named saves.
- Rewind checkpoints.
- Adventure Log with rewind integration.
- Native X3 command entry.
- T9-style keyboard input.
- Autocomplete and context-aware word suggestions.
- Inventory-aware command suggestions.
- Context-sensitive object/action menus.
- Quick-command menus.
- Compass-style movement menu including diagonals, up, and down.
- Parser feedback integration.
- Z-machine status-line handling.
- Transcript pagination.
- Long-press page navigation.
- Numeric input support for in-game menus.
- Single-key input support for games that use `READ_CHAR`.
- Clean exit and re-entry without restarting the entire device.

The goal is not merely to make Frotz run on the X3, but to make interactive fiction feel like a native X3 application.

---

## Tested Games

Development and testing have included:

- **Zork I** — Z3
- **Zork I** — Z5
- **Planetfall** — Z5
- **Lost Pig** — Z8
- **FrotzX3 Test Lab / PunyInform test story**
- Additional Z-machine test stories

Z3, Z5, and Z8 have all been physically tested on the XTEINK X3.

Z6 graphics-focused games are not currently a primary target.

---

## Tested Hardware

FrotzX3 has been developed and physically tested on:

- **XTEINK X3**

CrossInk itself may support additional devices, but those should not be considered tested FrotzX3 targets unless explicitly documented here.

---

## Screenshots

Screenshots and device photos coming soon.

---

## SD Card Layout

Place Z-machine story files in:

```text
/adventures/
```

For example:

```text
/adventures/zork1.z3
/adventures/Planetfall.z5
/adventures/LostPig.z8
```

FrotzX3 manages save data under:

```text
/adventures/saves/
```

including manual saves and rewind checkpoints.

The game picker recognizes Z-machine story files from `.z3` through `.z8`.

---

## FrotzX3 Controls

FrotzX3 is designed specifically around the X3's limited physical controls.

The interface includes:

- Native menu navigation.
- T9-style text entry.
- Context-aware command suggestions.
- Quick actions for common interactive-fiction verbs.
- Directional movement menu.
- Long-press actions where useful.
- Transcript paging designed for e-ink refresh behavior.

The exact controls may evolve as the interface continues to be refined.

---

## Installing Into Future CrossInk Releases

One of the goals of FrotzX3 is to remain reasonably portable across future CrossInk releases.

The guided installer handles the normal migration process.

For developers or manual integration, see [FROTZX3_INTEGRATION.md](FROTZX3_INTEGRATION.md).

The FrotzX3-specific source is primarily contained in:

```text
lib/FrotzX3/
src/activities/frotzx3/
```

Only a small amount of CrossInk host integration is required outside those directories.

---

## Advanced Installation

The beginner-friendly installer is:

```text
Install-FrotzX3.cmd
```

Advanced users can directly use the PowerShell backend:

```text
tools/Install-FrotzX3.ps1
```

The backend supports compatibility checking, dry runs, existing installations, backups, building, and other migration options.

See [FROTZX3_INSTALLER_README.md](FROTZX3_INSTALLER_README.md).

---

## Building Manually

CrossInk uses PlatformIO.

From the repository root:

```powershell
pio run -e default
```

If `pio` is not on your PATH:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e default
```

To upload using PlatformIO:

```powershell
pio run -e default -t upload
```

The guided installer can perform the build and optionally the upload for you.

---

## Project Goals

FrotzX3 prioritizes:

1. Stability.
2. Z-machine compatibility.
3. Reliable save and restore.
4. Performance on the ESP32-C3.
5. Comfortable X3 controls.
6. Interactive-fiction quality-of-life features.
7. Portability to future CrossInk releases.

The X3 has limited RAM, so changes are intentionally conservative with memory usage.

A successful compile is not considered sufficient validation; physical-device testing is the authoritative test.

---

## Current Limitations

- FrotzX3 is currently tested only on the XTEINK X3.
- Z6 graphics are not a primary supported use case.
- Some unusual games may expose interpreter or UI behavior that has not yet been tested.
- Future CrossInk releases may occasionally require a small compatibility update.
- The Windows guided installer currently expects the required development tools to already be available; automatic prerequisite installation may be added later.
- No commercial or copyrighted story files are distributed with this project.

---

## About CrossInk

FrotzX3 is built on [CrossInk](https://github.com/uxjulia/CrossInk), an open-source firmware project for e-ink devices including the XTEINK family.

CrossInk provides the underlying:

- Device support.
- Display rendering.
- Input handling.
- SD-card access.
- Activity/application framework.
- Power management.
- Firmware infrastructure.

FrotzX3 adds the Z-machine interpreter, interactive-fiction UI, save systems, command helpers, and related integration on top of that foundation.

For general CrossInk documentation, visit the [upstream CrossInk project](https://github.com/uxjulia/CrossInk).

---

## About Frotz

[Frotz](https://github.com/DavidGriffith/frotz) is a long-running open-source interpreter for Infocom-style Z-machine interactive fiction.

FrotzX3 includes modified Frotz-derived source adapted for the XTEINK X3 / CrossInk environment.

The modifications include platform integration, storage handling, lifecycle support, save/restore integration, memory behavior suitable for the ESP32-C3, and APIs used by the native X3 interface.

---

## Licensing

This repository contains code under multiple compatible open-source licenses.

### CrossInk

CrossInk is distributed under the **MIT License**.

The original CrossInk license and copyright notices are retained.

### Frotz

Frotz-derived code is distributed under the **GNU General Public License, version 2 or later (GPL-2.0-or-later)**.

Original Frotz copyright and license notices are retained.

### FrotzX3 distribution

Because FrotzX3 includes GPL-covered Frotz-derived code, redistribution of firmware or combined source containing FrotzX3 must comply with the applicable GPL terms.

If you redistribute compiled firmware containing FrotzX3, the corresponding source used to build that firmware should also be made available as required by the GPL.

For full details, see:

- [FROTZX3_LICENSE.md](FROTZX3_LICENSE.md)
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- [LICENSES/](LICENSES/)

The root [LICENSE](LICENSE) remains the original CrossInk MIT license and should not be interpreted as relicensing GPL-covered Frotz code.

---

## Game Files and Copyright

FrotzX3 does **not** include Zork, Planetfall, Lost Pig, or any other game/story file.

Users are responsible for obtaining and using story files legally.

The fact that Frotz is open-source does not imply that every game playable with Frotz is freely redistributable.

---

## Documentation

FrotzX3 documentation:

- [FrotzX3 Public README](FROTZX3_README.md)
- [Guided Installer](FROTZX3_INSTALLER_README.md)
- [Integration / Porting Guide](FROTZX3_INTEGRATION.md)
- [Licensing](FROTZX3_LICENSE.md)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)

CrossInk documentation remains available under `docs/` and from the upstream CrossInk project.

---

## Development

FrotzX3 is a hobby/open-source project built for the fun of making the XTEINK X3 a genuinely good interactive-fiction device.

Bug reports, compatibility findings, and testing reports are welcome.

When reporting a problem, it is especially useful to include:

- Story filename and Z-machine version.
- What action triggered the problem.
- Whether the game was newly started or resumed.
- Whether save/restore was involved.
- Serial logs if available.
- CrossInk/FrotzX3 version or commit.

---

## Credits

FrotzX3 would not exist without the work of:

- The **CrossInk** developers and contributors.
- The **Frotz** developers and contributors.
- The broader interactive-fiction community.
- The open-source tools and libraries used by both projects.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSES/](LICENSES/) for formal attribution.

---

## Status

FrotzX3 is under active hobby development.

The core text-adventure experience — including real Z-machine execution, Z3/Z5/Z8 support, saves, rewind, command entry, parser integration, and X3-native controls — is working on physical XTEINK X3 hardware.

Expect continued polish, compatibility testing, and quality-of-life improvements.
