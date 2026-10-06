# FrotzX3 release build and source bundle

This describes how an official FrotzX3 firmware binary is built and how its
matching complete source package is produced.

> **No games are included** in the firmware, the source package, or the
> repository. Users supply their own legally obtained Z-machine story files.

## Why a separate source ZIP

GitHub's automatic "Source code (zip / tar.gz)" archives do not include
submodule contents. This repository needs `freeink-sdk` and its nested
`libs/assets/Icons/lucide` submodule to build, so those archives cannot rebuild
the firmware. **Official FrotzX3 binary releases should be paired with the
supplied complete-source ZIP**, `FrotzX3-v<version>-source-complete.zip`. That
ZIP is the intended reproducible source package for that firmware release.

## Official release build

Run from the repository root (from a clone made with `--recurse-submodules`, or
from the extracted source ZIP). Use a short path; deep paths can exceed Windows
limits during the ESP-IDF build.

```powershell
$env:CROSSINK_RELEASE_VERSION = '0.9.0-beta.1'
Remove-Item Env:CROSSINK_RC_HASH -ErrorAction SilentlyContinue   # RC hash would take precedence
pio run -e default
```

- `CROSSINK_RELEASE_VERSION` is read by `scripts/git_branch.py`. Without it the
  default env reports `<crossink version>-dev+<branch>`; with it the firmware
  reports exactly `0.9.0-beta.1`. A leading `v` is stripped. CrossInk's own
  `[crossink] version` in `platformio.ini` is not changed.
- Output: `.pio/build/default/firmware-x3-x4.bin` (identical copies:
  `firmware.bin` and `firmware-x3-x4-v0.9.0-beta.1.bin`).
- The first build downloads the toolchain and libraries and recompiles the
  ESP-IDF libraries (several minutes).
- If redirecting PlatformIO output to a file on Windows, set `PYTHONUTF8=1`;
  otherwise the language table printed by `scripts/gen_i18n.py` can raise a
  `UnicodeEncodeError` in the console encoding.

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

### Measured effect (CrossInk v1.6.1 + FrotzX3 0.9.0-beta.1 only)

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
powershell -ExecutionPolicy Bypass -File tools\release\New-SourceBundle.ps1
```

Defaults: version `0.9.0-beta.1`, output `dist-publish\FrotzX3-v0.9.0-beta.1-source-complete.zip`
(`dist-publish\` is git-ignored). The script:

1. verifies the working tree is clean;
2. verifies the branch is `release/v<version>` and that the FrotzX3 version
   strings match (`-AllowAnyBranch` skips only the branch check);
3. runs `git submodule update --init --recursive` and checks that every
   submodule is clean and at its recorded commit;
4. exports the committed content of the main repository and of each submodule
   with `git archive` into a temporary staging directory;
5. removes Git metadata and build/IDE/cache directories and fails if any
   binary, story, save or credential-type file is present;
6. writes `SOURCE_BUNDLE_MANIFEST.txt` (exact commits and build command) and
   creates the ZIP with a fixed timestamp and sorted entries, so the same
   commit yields the same ZIP;
7. prints the output path and SHA-256.

Publish the printed SHA-256 next to the ZIP. Third-party packages that
PlatformIO downloads at build time (platform, libraries listed in
`platformio.ini`, Arduino-ESP32 and the toolchain) are fetched by version and
are not copied into the ZIP.
