# FrotzX3 release build and source bundle

This describes how an official FrotzX3 firmware binary is built and how its
matching complete source package is produced.

> **No games are included** in the firmware, the source package, or the
> repository. Users supply their own legally obtained Z-machine story files.

## Why a separate source ZIP

From `0.9.0-beta.2`, the firmware is built from **official CrossInk v1.6.1** plus the FrotzX3 patch
package (`tools/patches/v0.9.0-beta.2-crossink-1.6.1/`), not from the root tree of this repository (the
older CrossInk 1.5.0-based development tree). GitHub's automatic "Source code (zip / tar.gz)" archives
therefore contain neither the CrossInk base nor the submodules (`freeink-sdk` with its nested
`libs/assets/Icons/lucide`, and `assets/tabler-icons`), and cannot rebuild the firmware. **Official
FrotzX3 binary releases must be paired with the complete-source ZIP**,
`FrotzX3-v<version>-source-complete.zip`: the exact tree that was built, every submodule, the installer and
package, and `SOURCE_BUNDLE_MANIFEST.txt`. That ZIP is the corresponding source for the GPL.

## Official release build

The release firmware is produced by the patch installer so that the distributed file and its source are
the same thing. Run from a clean clone of this repository, with `-KeepWorkDir` so the built tree can be
packaged. Use a short path for the work folder; deep paths can exceed Windows limits during the ESP-IDF build.

```powershell
powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1 `
  -KeepWorkDir -WorkDir C:\fx3\work -OutputDir C:\fx3\out
```

- The installer sets `CROSSINK_RELEASE_VERSION` from the package manifest (`0.9.0-beta.2`) and unsets
  `CROSSINK_RC_HASH`; `scripts/git_branch.py` then reports exactly that version (a leading `v` is stripped).
  Without it the default env reports `<crossink version>-dev+<branch>`. Environment variables such as
  `PLATFORMIO_BUILD_FLAGS` are ignored for the run so they cannot change the firmware.
- Output: `FrotzX3-v<version>-firmware-x3-x4.bin` (built from `.pio/build/default/firmware-x3-x4.bin`).
- The first build downloads the toolchain and libraries and recompiles the ESP-IDF libraries (several minutes).
- Rebuilding the same source does **not** reproduce the same SHA-256 (build-time strings such as `__TIME__`
  are embedded). The released binary is therefore the exact file that was hardware-tested, not a later rebuild.
- If redirecting PlatformIO output to a file on Windows, set `PYTHONUTF8=1`; otherwise the language table
  printed by `scripts/gen_i18n.py` can raise a `UnicodeEncodeError` in the console encoding.

## Logging in release vs. debug builds

Release (`env:default`) builds use `-DENABLE_SERIAL_LOG -DLOG_LEVEL=0`:

- `LOG_ERR` output is compiled in, the crash-report log ring buffer is kept, and
  the USB serial transport is set up exactly as before.
- `LOG_INF` and `LOG_DBG` output (and their format strings) is compiled out.
  This includes FrotzX3's startup, load and save/restore `LOG_INF` lines;
  fatal interpreter errors are `LOG_ERR` and are kept.
- `ENABLE_SERIAL_LOG` stays enabled. Removing it was only measured separately
  and is **not** the release policy: it would also drop `LOG_ERR`, leave the
  "Last logs" section of crash reports empty and skip USB serial setup.

For verbose logs build `pio run -e debug` (`LOG_LEVEL=2`), or override the
level in an untracked `platformio.local.ini`. To also enable FrotzX3's
development diagnostics add `-DFROTZX3_DEBUG_LOG`; they log through `LOG_INF`,
so the build needs `LOG_LEVEL >= 1` (`env:debug` qualifies):

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-DFROTZX3_DEBUG_LOG'
pio run -e debug
```

### Measured effect (CrossInk v1.6.1 + FrotzX3 only)

Identical v1.6.1 + FrotzX3 source, clean builds, 6,553,600 B app slot:

| `LOG_LEVEL` | Image | RAM | App-slot headroom |
| --- | --- | --- | --- |
| 1 (previous) | 6,406,752 B | 89,488 B | 146,848 B |
| 0 (release) | 6,389,616 B | 89,488 B | 163,984 B |

`LOG_LEVEL=0` saves 17,136 B. These figures apply to the v1.6.1 port only; the
older base measures differently and its numbers must not be quoted for v1.6.1.

## Build dependency pinning

Most dependencies in `platformio.ini` are pinned (platform, libraries,
JPEGDEC commit). The ESP-IDF managed components that pioarduino pulls in for the
Arduino framework are declared with ranges by Arduino-ESP32 itself, so they are
resolved when the build runs. `scripts/pin_idf_components.py` pins
`espressif/mdns` to `1.14.0` (previously it floated between `1.11.3` and
`1.14.0` on different machines). After a build, confirm with:

```powershell
Select-String -Path managed_components\espressif__mdns\idf_component.yml -Pattern '^version:'
```

Other managed components still use ranges from the framework manifest. The
verified builds resolved `esp-dsp` 1.8.2, `esp_modem` 2.1.0, `esp-modbus`
1.0.18, `libsodium` 1.0.22~1 and `joltwallet/littlefs` 1.22.3; if a later build
resolves different versions, pin them the same way in `PINNED_COMPONENTS`.

## Creating the complete source bundle

```powershell
powershell -ExecutionPolicy Bypass -File tools\release\New-InstallerSourceBundle.ps1 `
  -SourceTree C:\fx3\work -FirmwarePath C:\fx3\out\FrotzX3-v0.9.0-beta.2-firmware-x3-x4.bin
```

`-SourceTree` is the work folder the installer built in. Output:
`dist-publish\FrotzX3-v<version>-source-complete.zip` (`dist-publish\` is git-ignored). The script:

1. checks that this repository is clean and verifies the package against its manifest checksums;
2. verifies that `-SourceTree` is exactly the supported CrossInk commit plus the package: `HEAD` equals the
   manifest commit, submodules are clean and at their recorded commits, every patched file matches its
   recorded git blob, every overlay file matches its SHA-256, and no other file differs;
3. records the git tree hash of that source state;
4. copies the working-tree files (patched files included) of the main tree and of every submodule, and the
   committed installer, package, compatibility list and tests into `frotzx3-build-tools/`;
5. removes Git metadata and build/IDE/cache directories and fails if any binary, story, save, log or
   credential-type file is present;
6. writes `SOURCE_BUNDLE_MANIFEST.txt` (exact commits, tree hash, firmware SHA-256 if given, build
   commands) and creates the ZIP with a fixed timestamp and sorted entries;
7. prints the output path and SHA-256.

Publish the printed SHA-256 next to the ZIP (for example in `SHA256SUMS.txt`). Third-party packages that
PlatformIO downloads at build time (platform, libraries listed in `platformio.ini`, Arduino-ESP32 and the
toolchain) are fetched by version and are not copied into the ZIP.

`New-SourceBundle.ps1` is the earlier script for `v0.9.0-beta.1`, whose firmware was built directly from
this repository's root tree (CrossInk 1.5.0 base). It does not describe the 1.6.1-based firmware and is
kept only for that release.
