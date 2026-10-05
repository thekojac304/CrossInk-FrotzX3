# FrotzX3 patch installer (experimental)

> **Status: experimental.** This builds a firmware file from official CrossInk plus
> the FrotzX3 patch package. It has **not** been hardware-tested as an install
> method. The recommended beta install is the **prebuilt, hardware-tested firmware**
> attached to the GitHub release. This installer does **not** flash your device.

## What it does

1. Checks that Git and PlatformIO are installed.
2. Downloads official CrossInk (<https://github.com/uxjulia/CrossInk>) into a temporary folder.
3. Checks out the exact CrossInk commit this FrotzX3 release supports, and verifies it.
4. Downloads CrossInk's required components (submodules) and verifies their commits.
5. Checks the FrotzX3 patch package against its checksums, then applies it.
6. Builds the firmware with `pio run -e default` and `CROSSINK_RELEASE_VERSION` set.
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
| `Test-FrotzX3Compatibility.ps1` | Developer tool: does the patch apply to a candidate CrossInk version? |
| `UPDATING_CROSSINK.md` | How to move FrotzX3 to a newer CrossInk release |

The patch packages live in [`tools/patches/`](../patches/).
