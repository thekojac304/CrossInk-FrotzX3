# FrotzX3 patch installer

> **Status: tested.** This builds a firmware file from official CrossInk v1.6.1 plus the
> FrotzX3 patch package. The 0.9.0-beta.2 release firmware is the exact output of this installer
> (default options, fresh clone) and passed XTEINK X3 hardware testing
> (`patch_installer_hardware_tested: true` in `compatibility.json`). The prebuilt firmware attached
> to the GitHub release is the easiest install; use this installer if you prefer to build locally.
> It does **not** flash your device. Rebuilds are not byte-identical to the release file because
> build time is embedded.

## What it does

1. Checks that Git and PlatformIO are installed.
2. Downloads official CrossInk (<https://github.com/uxjulia/CrossInk>) into a temporary folder.
3. Checks out the exact CrossInk commit this FrotzX3 release supports (v1.6.1,
   `9914146eeae7b46b300f475a16c32426fc02ec1f`), and verifies it. Any other commit stops the run.
4. Downloads CrossInk's required components (submodules) and verifies their commits.
5. Checks the FrotzX3 patch package against its checksums, then applies it.
6. Builds the firmware with `pio run -e default` and `CROSSINK_RELEASE_VERSION` set. Environment
   variables that would alter the build (such as `PLATFORMIO_BUILD_FLAGS`) are ignored for the run.
7. Verifies the build (version string, `espressif/mdns` 1.14.0, FrotzX3 present).
8. Copies `FrotzX3-v<version>-firmware-x3-x4.bin` to `dist-installer\` and prints its SHA-256.

Your own folders are never modified. Everything happens in a temporary folder that is
deleted on success. If any check fails, the script stops with a plain-English
message; a detailed log is written next to the firmware (`dist-installer\FrotzX3-install-<time>.log`).

## Requirements

- Windows 10/11 with PowerShell 5.1 or 7
- [Git for Windows](https://git-scm.com/download/win)
- [PlatformIO Core](https://platformio.org/install/cli) (`pio`). The script also finds
  `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. PlatformIO brings its own Python.
- Internet access, roughly 6 GB free disk space, and about 10-15 minutes for a first build

## Usage

From the repository folder (or the extracted complete-source ZIP):

```powershell
powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1
```

Useful options:

| Option | Meaning |
| --- | --- |
| `-CheckOnly` | Download CrossInk, verify it, and test that the patch applies. No build. Takes seconds. |
| `-SkipBuild` | Apply FrotzX3 and verify it, keep the folder, do not build. |
| `-KeepWorkDir` | Keep the temporary build folder. |
| `-WorkDir <path>` | Use this (new) folder. Keep it short, e.g. `C:\fx3`; very long paths can break the ESP-IDF build on Windows. |
| `-OutputDir <path>` | Where the firmware and log go (default `dist-installer\`). |
| `-FrotzX3Version <v>` | Choose a release from `compatibility.json` (default: newest). |
| `-TargetCrossInkVersion <v>` | Select a package by exact `crossink_target` in `compatibility.json` (for example `1.6.1`); the nearest version is never substituted. |

## Supported target: CrossInk v1.6.1

`compatibility.json` lists one package: FrotzX3 `0.9.0-beta.2` for official CrossInk **v1.6.1**
(`tools/patches/v0.9.0-beta.2-crossink-1.6.1/`), status `tested`. It is the default, so a plain run
selects it and checks out commit `9914146eeae7b46b300f475a16c32426fc02ec1f`. The firmware built from
this source passed XTEINK X3 hardware validation, including the contiguous-memory guard (see
`PORTING_TO_CROSSINK_1.6.1.md` and `MEMORY_BUDGET.md`). The installer path itself was
hardware-tested (`patch_installer_hardware_tested: true`). **No other CrossInk version is
supported**; a future CrossInk release needs its own package and its own hardware validation before it
is marked supported. To name the target explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1 -TargetCrossInkVersion 1.6.1
```

The firmware carries the clean release version string (`0.9.0-beta.2`) and is written as
`FrotzX3-v0.9.0-beta.2-firmware-x3-x4.bin`. An existing file of that name in the output folder is replaced.
No game files are involved at any point.

## After the build

The firmware is **not** flashed. To install it, use CrossInk's documented methods
(see `docs/installation.md`): copy the `.bin` to the SD card and use
**Settings > System > SD Card Firmware Update**, or flash over USB. Compare the
SHA-256 printed by the script with the one in the GitHub release notes if you want to
check that your build matches the published firmware (builds from the same source
are expected to match closely but are not guaranteed to be byte-identical).

## If it fails

| Message | Meaning |
| --- | --- |
| "A required tool is not installed" | Install Git and/or PlatformIO, open a new PowerShell window, run again. |
| "Could not download the official CrossInk source" | Network problem or GitHub unavailable. Nothing changed. |
| "FrotzX3 could not be applied to this CrossInk source" | The CrossInk commit is not the supported one, or a file no longer matches. The message shows expected and found values. Nothing changed. |
| "Could not download one of CrossInk's required components" | A submodule could not be fetched. Use the complete-source ZIP from the release instead. |
| "The firmware build failed" | See the end of the console output and the log file. |

## Files

| File | Purpose |
| --- | --- |
| `Install-FrotzX3.ps1` | The installer / builder |
| `FrotzX3.Common.ps1` | Shared helpers (used by both scripts) |
| `compatibility.json` | Which CrossInk commit each FrotzX3 release supports, and its test status |
| `Test-FrotzX3Compatibility.ps1` | Developer tool: does the package for this exact CrossInk commit apply? (exact manifest match only) |
| `UPDATING_CROSSINK.md` | How to move FrotzX3 to a newer CrossInk release |

The patch packages live in [`tools/patches/`](../patches/).
