# Porting FrotzX3 0.9.0-beta.1 to CrossInk v1.6.1

Status: **build-tested, NOT hardware-tested.** Not the public default.

| | |
| --- | --- |
| Upstream | <https://github.com/uxjulia/CrossInk> |
| Tag | `v1.6.1` (lightweight tag; `git rev-parse v1.6.1^{commit}` is the same SHA) |
| Commit | `9914146eeae7b46b300f475a16c32426fc02ec1f` ("Update release manifests for v1.6.1") |
| Commit date | 2026-10-04 02:47:56 +0000 |
| `platformio.ini` `[crossink] version` | `1.6.1` |
| `freeink-sdk` | `699370183fa3a0e33c9cb83a36f701bbb6022095` (old base: `1ff020263cd2202ea79ce3eb811f5ac8489b8cde`) |
| New top-level submodule | `assets/tabler-icons` @ `8ac7d81b72ece11072ef25ea9fd92e80c6f3c9fc` (v3.46.0) |
| Old base | `cab4f24922f05811e7f44be1057f62ea2d978c52` (v1.5.0 + 3 commits) |
| Package | `tools/patches/v0.9.0-beta.1-crossink-1.6.1/` |

(The task text spelled the ref `upstream/v1.6.1`; tags are not remote-tracking refs, so
the verified ref is `v1.6.1`. `git fetch upstream --tags` made it available.)

## How the conflicts were reproduced

Clean clone of the repository, `v1.6.1` checked out, submodules initialised
recursively, then the unmodified v0.9.0-beta.1 patch applied as the installer would:

```
git apply --check --whitespace=nowarn tools/patches/v0.9.0-beta.1/patches/0001-frotzx3-host-integration.patch
```

Result: all three patched files fail (`platformio.ini:87`, `src/MappedInputManager.cpp:606`,
`src/activities/home/HomeActivity.cpp:25`). `git apply --reject` shows the real picture:
12 hunks, 6 rejected, 6 applied (with offsets). Only these three upstream files conflicted;
no other file in the package collided (no overlay file already exists upstream).
`src/main.cpp` is not modified by FrotzX3 in either version.

## Every conflict

| File | Hunk | Old FrotzX3 intent | What upstream v1.6.1 changed | Class |
| --- | --- | --- | --- | --- |
| `platformio.ini` | 1 (`extra_scripts`, ~L87) | Run `pre:scripts/pin_idf_components.py` first so `espressif/mdns` is pinned to 1.14.0 | New `pre:scripts/build_scalable_font_assets.py` line shifted the hunk context | **Line drift** |
| `platformio.ini` | 2 (`[env:default]`, ~L126) | Add `monitor_filters = esp32_exception_decoder` | `[env:default]` now `extends = firmware_tuned` (was `base`) | **Line drift** (context only) |
| `src/MappedInputManager.cpp` | 1 (`wasPressed(Confirm)`) | Arm Power-release suppression when a Confirm press is accepted (physical Confirm and Power-as-Confirm fallback) so the later Power release/long-hold cannot Sleep | The bool `suppressPowerRelease` is gone. Upstream introduced `util/ReleaseSuppression.h` (`releaseSuppression.suppressPower()/isPowerReleaseSuppressed()/...`), an early `if (isPowerReleaseSuppressed()) return false;`, and a `powerAsConfirmInReaderMode` branch | **Semantic** |
| `src/MappedInputManager.cpp` | 2 (`wasReleased(Confirm)`) | Clear `suppressPowerRelease` when an ordinary Confirm releases, so a dedicated-Confirm hardware press cannot swallow a later Power press | `wasReleased(Confirm)` rewritten around `consumeConfirmRelease()`/`consumePowerConfirmRelease()`. `ReleaseSuppression::expireAfterReleaseFrame()` now clears a stale Power suppression on the next frame in which Power is neither held nor released | **Semantic** (hunk dropped; replaced by upstream expiry) |
| `src/MappedInputManager.cpp` | 3 (`isPressed(Power)` returns false while suppressed) | Hide a held, already-consumed Confirm/Power press from the global long-press handler | **Applied textually (offset +233) but is now wrong**: it names `suppressPowerRelease`, which no longer exists, and upstream already has the identical guard (`isPressed`: `button == Power && releaseSuppression.isPowerReleaseSuppressed()`) | **Semantic**: silently "applying" hunk that would not compile. Dropped; upstreamed |
| `src/activities/home/HomeActivity.cpp` | 1 (`#include`) | Include `../frotzx3/FrotzX3Activity.h` | New `#include "GlobalActions.h"` between the neighbouring includes | **Line drift** |
| `src/activities/home/HomeActivity.cpp` | 6 (`getMenuItemCount()`) | Count 4 → 5 for the extra menu entry | Comment/context changed ("Library" replaced "Recents") and a new early return for the Cover Grid theme (`coverGridUi`) with its own fixed menu | **Line drift** for the edit; **semantic note** for Cover Grid (below) |
| `HomeActivity.cpp` | 2-5, 7 (enum value, `kCapacity`, two `push()` sites, activation `case`) | Add `InteractiveFiction` action, 8→9 capacity, the "FrotzX3" entry, `pushActivity(FrotzX3Activity)` | Applied cleanly with small offsets (-27 … +61) | Clean |

## What the port does (smallest possible changes)

- `platformio.ini`: both lines re-added at their new locations. Unchanged in effect.
- `HomeActivity.cpp`: the same seven edits as before, re-applied. The new Cover Grid Home
  theme builds its own fixed menu (`getMenuItemCount()` returns before the line FrotzX3
  edits, and `activateCoverGridSelection()` is index based), so **FrotzX3 is not offered
  in the Cover Grid theme**. This is deliberate scope: it is a new upstream UI mode; the
  other themes are unchanged. Select any other Home theme to reach FrotzX3.
  The minimal-theme activation `switch` still has no `InteractiveFiction` case. That was
  already true in the old integration (the minimal menu lists FrotzX3 but does not open
  it); it is not changed here.
- `MappedInputManager.cpp`: the *intent* is kept using upstream's own mechanism:
  - physical Confirm accepted in `wasPressed()` → `releaseSuppression.suppressPower()`;
  - Power-as-Confirm fallback accepted → `releaseSuppression.suppressPower()` (the fallback
    expression is stored in a local, then returned);
  - the old "clear on Confirm release" hunk is not reapplied: `expireAfterReleaseFrame()`
    already drops a stale Power suppression one frame later when Power is not held, and
    keeps it armed while a shared Confirm/Power control is physically held;
  - the `isPressed(Power)` hunk is not reapplied: upstream contains it.

Runtime changes beyond the old integration: none other than adapting to the new
suppression API. One build-tooling change was needed (next section). FrotzX3-owned files (`lib/FrotzX3/`, `src/activities/frotzx3/`,
`scripts/pin_idf_components.py`, licences/notices) are the unmodified v0.9.0-beta.1
overlay, byte for byte.

## Build-tooling change: `scripts/git_branch.py` (Windows)

Official v1.6.1 does **not** build on Windows with the pinned toolchain (pioarduino platform
55.03.37, PlatformIO Core 6.1.19). `pio run -e default` stops after ~6 s:

```
*** Two environments with different actions were specified for the same target:
    ...\.pio\build\default\FrameworkArduino\ColorFormat.c.o
```

Reproduced on a pristine `v1.6.1` checkout with **no FrotzX3 files**, so it is upstream's, not the
port's. Cause: v1.6.1's `git_branch.py` registers `env.AddBuildMiddleware(..., '*src/util/BuildInfo.cpp')`;
pioarduino's `builder/frameworks/arduino.py` (Windows only, `smart_include_length_shorten`) replays all
registered middlewares for every framework source and ignores the pattern, so FrameworkArduino objects
get two different command lines. The port patches `git_branch.py` to use its own existing fallback
(`env.Append(CPPDEFINES=scoped)`) when `os.name == 'nt'`. Effect: `CROSSINK_GIT_SHA`,
`CROSSINK_GIT_DIRTY`, `CROSSINK_VERSION` are defined for all translation units on Windows (the
pre-middleware behavior of the old base); only `src/util/BuildInfo.cpp` reads them. No runtime code changes.
Upstream should be told; until fixed, Windows builds of v1.6.1 need this.

## Behaviour difference to verify on hardware

Upstream's `wasPressed(Confirm)` returns `false` once a Power-release suppression is armed
(the new early return). The old FrotzX3 code returned the same `true` every time
`wasPressed(Confirm)` was called during the frame. With the port, when Confirm arrives via
the Power **fallback**, only the first `wasPressed(Confirm)` call in that frame returns
`true`. `FrotzX3Activity::loop()` reads Confirm in mutually exclusive mode branches, so a
second call in the same frame is not expected, but this is the one place where behaviour
could differ and it is the first thing to check (see the hardware checklist in the final
report and in `tools/patches/v0.9.0-beta.1-crossink-1.6.1/README.md`). A physical-Confirm
press is unaffected (that branch returns `true` before the guard).

## Source preservation check (FrotzX3 0.9.0-beta.1 approved source vs v1.6.1 port)

| File | Result | Classification |
| --- | --- | --- |
| `src/activities/frotzx3/FrotzX3Activity.cpp/.h` | byte-identical (SHA-256 equals the v0.9.0-beta.1 package and repo HEAD) | no change |
| `lib/FrotzX3/**` (`FrotzX3.cpp/.h`, `FrotzPlatform.c`, Frotz core, ...) | byte-identical, all 39 overlay files | no change |
| `scripts/pin_idf_components.py`, licences, notices | byte-identical | no change |
| `src/main.cpp` | not modified by FrotzX3 in either version; v1.6.1's file used as is | n/a |
| `src/MappedInputManager.cpp` | Confirm accept arms Power-release suppression via `releaseSuppression.suppressPower()` on both paths; old "clear on release" and `isPressed(Power)` hunks dropped | upstream adaptation (API rename + upstreamed/replaced behavior); one **to verify on hardware** item: Power-fallback `wasPressed(Confirm)` is one-shot per frame because of upstream's new guard |
| `src/activities/home/HomeActivity.cpp` | same 7 edits as before (only the `getMenuItemCount()` comment text differs: "Library" instead of "Recents") | upstream adaptation only; Cover Grid theme does not list FrotzX3 (new upstream mode) |
| `platformio.ini` | same two added lines | unchanged intent |
| `scripts/git_branch.py` | new, 8 lines | intentional compatibility fix (build tooling) |
| unintended behavior changes | none found | |

The old and new integration patches were compared line by line; the only differences are the
`MappedInputManager.cpp` hunks and comment text listed above.

## Verification record, first port build (`LOG_LEVEL=1`; superseded for the build, kept as history)

This record was taken before release builds moved to `LOG_LEVEL=0` (see the next record). Its image/flash figures are the `LOG_LEVEL=1` figures; RAM is unchanged by the log level.

| Item | v0.9.0-beta.1 (old base `cab4f249`) | v1.6.1 port |
| --- | --- | --- |
| `pio run -e default` | PASS (earlier installer log) | **PASS** (`CROSSINK_RELEASE_VERSION=0.9.0-beta.1-ci161-test`) |
| RAM | 25.1% (82,396 / 327,680 B) | 27.3% (89,488 / 327,680 B), +7,092 B |
| Flash | 96.9% (6,351,421 / 6,553,600 B) | 97.5% (6,392,623 / 6,553,600 B), +41,202 B |
| OTA headroom (partition - used flash) | 202,179 B | 160,977 B (6,406,752 B image; 146,848 B below the partition size) |
| Lines containing `warning:` | 45 | 39 |
| Of those: FrotzX3 `err.c` | 32 | 32 (identical, pre-existing `-Wdiscarded-qualifiers`) |
| Of those: upstream code | 13 (SD_MMC, rmaker) | 7 (narrowing conversions in `FileBrowserActivity.cpp`, `EpubReaderDrawerActivity.cpp`, `main.cpp`) |
| Warnings in files touched by the port | none new | none new |

The size increase is the upstream v1.6.1 delta plus nothing from FrotzX3 (FrotzX3 sources are
identical); a pure-v1.6.1 control build was not run, so the split is not measured.

Installer (`Install-FrotzX3.ps1 -TargetCrossInkVersion 1.6.1`, clean clone of upstream, no files
copied from the development repository except the package): PASS in ~10 min. Firmware
`FrotzX3-v0.9.0-beta.1-ci161-test-firmware-x3-x4.bin`, 6,406,752 bytes, SHA-256
`0617e95808ae547a54c4bcf6fdd1193b036717af02fdf9e8e3a5cfbfe57a9714`. espressif/mdns 1.14.0 verified.
The binary is not byte-identical to the scratch-checkout build (embedded git SHA/dirty flag differ).

Source reconstruction: the installer's work tree (v1.6.1 + package), staged with `git add -A`, has tree
`47b4088a54ceeacb5b20022231c92387a347a6c4`, identical to the port source commit's tree, i.e. FrotzX3 files
byte-identical, 4 patched files as intended, no unexpected files, submodule gitlinks
`freeink-sdk 69937018...` and `assets/tabler-icons 8ac7d81b...` correct.

## Verification record, release logging `LOG_LEVEL=0` (2026-10-05)

`[env:default]` in the package's `platformio.ini` patch now uses `-DENABLE_SERIAL_LOG -DLOG_LEVEL=0`
(ERR logs, crash-report ring buffer and USB serial setup kept; INF/DBG compiled out). No runtime
source changed. Package regenerated; only these manifest fields moved: `frotzx3_source_commit`
(`18349ee7...`), the patch SHA-256, the `platformio.ini` expected blob (`6324d039...`) and the
release version string / output name (`0.9.0-beta.1-ci161-hwtest`, the hardware-test build).

| Item (CrossInk v1.6.1 + FrotzX3, clean builds) | `LOG_LEVEL=1` | `LOG_LEVEL=0` |
| --- | --- | --- |
| Firmware image | 6,406,752 B | 6,389,616 B (-17,136 B) |
| RAM | 89,488 B | 89,488 B |
| App-slot headroom (6,553,600 B slot) | 146,848 B | 163,984 B |

Installer build of the hardware-test firmware (`Install-FrotzX3.ps1 -TargetCrossInkVersion 1.6.1`,
fresh clone, package only): PASS. `FrotzX3-v0.9.0-beta.1-ci161-hwtest-firmware-x3-x4.bin`,
6,389,616 B, SHA-256 `913dcdd47d7471404249a4056e13c47932a7251d0958262370b5ba67ea7f3dd0`; PlatformIO flash
6,375,493 B (97.3%); RAM 89,488 B. The installer work tree staged with `git add -A` has tree
`d511f8c69928c0b7b6627bc4a0cd9d9fb08c776f`, identical to the port source commit's tree. Still **not
hardware-tested**.
