# FrotzX3 0.9.0-beta.1 patch package for CrossInk v1.6.1

Turns official CrossInk **v1.6.1** into a FrotzX3 `0.9.0-beta.1` build.

> **Status: hardware-validated on an XTEINK X3 (`compatibility.json` status `tested`).**
> The first hardware test (`FrotzX3-v0.9.0-beta.1-crossink-1.6.1-hwtest.bin`, SHA-256
> `913dcdd47d7471404249a4056e13c47932a7251d0958262370b5ba67ea7f3dd0`) failed on large stories.
> **Root cause:** Home's ~16 KB cover cache sometimes spilled into the retention RAM that Lost Pig's
> 42,554 B contiguous dynamic-memory block needs (the 16,384 B C3 render stack was a second consumer).
> **Permanent fix:** release the Home cover (`invalidateCoverCache()` under `RenderLock`) before
> pushing FrotzX3, a 12,288 B (was 16,384 B) C3 render stack in `src/main.cpp`, and a failure-screen
> title/error overprint fix. **Measured on an X3:** Lost Pig allocator headroom about 19 KB over its
> rounded block (~19,356 B; retention free block ~62,364 B), lowest render-stack high-water mark
> 5,048 B remaining (peak use ~7,240 B).
> The cleaned final build (`...-crossink-1.6.1-final-hwtest.bin`, SHA-256
> `1e63d288ea94800cb9b1662a05fde7a701d8bb70c580865c8a29f5752ecd685e`) passed the full hardware
> checklist with no regressions, and this package reproduces its source exactly.
> **Validated for CrossInk v1.6.1 only; future CrossInk releases still require explicit hardware
> validation before being marked supported.** Details:
> [`tools/installer/PORTING_TO_CROSSINK_1.6.1.md`](../../installer/PORTING_TO_CROSSINK_1.6.1.md).

| | |
| --- | --- |
| Patch schema | 1 |
| Official CrossInk | <https://github.com/uxjulia/CrossInk> |
| CrossInk tag / commit | `v1.6.1` / `9914146eeae7b46b300f475a16c32426fc02ec1f` |
| Device | XTEINK X3 (the `default` image is shared with X4) |
| PlatformIO environment | `default` |
| Required submodules | `freeink-sdk` @ `699370183fa3a0e33c9cb83a36f701bbb6022095`, `assets/tabler-icons` @ `8ac7d81b72ece11072ef25ea9fd92e80c6f3c9fc`; nested `freeink-sdk/libs/assets/Icons/lucide` @ `c81680e066f45b640743ca78ae36cdedda3f0318` |
| `espressif/mdns` | pinned to `1.14.0` by `scripts/pin_idf_components.py` (overlay file), listed as a `pre:` extra script by the patch; the installer fails the build if another version resolves |
| Release logging | `[env:default]` is patched to `-DLOG_LEVEL=0` (ERR only; `ENABLE_SERIAL_LOG`, serial setup and the crash-report ring buffer unchanged). Image 6,389,616 B, RAM 89,488 B vs. 6,406,752 B at `LOG_LEVEL=1`; see `tools/release/README.md` |
| Release version env | `CROSSINK_RELEASE_VERSION=0.9.0-beta.1-ci161-hwtest` (test-only string so the firmware cannot be mistaken for a release), `CROSSINK_RC_HASH` unset |

## Contents

```text
manifest.json                         versions, SHA-256 of every file, expected results
patches/0001-frotzx3-host-integration.patch
                                      git patch: HomeActivity.cpp (menu entry + cover release), MappedInputManager.cpp,
                                      main.cpp (C3 render stack 12,288 B), platformio.ini (two FrotzX3 lines + LOG_LEVEL=0),
                                      scripts/git_branch.py (Windows build fix, see below)
files/                                FrotzX3-owned files, copied verbatim (overlay)
```

The 39 overlay files are the v0.9.0-beta.1 package files, except `FrotzX3Activity.cpp`, which carries
the failure-screen fix. The host-integration edits were re-made against
v1.6.1 (plus the cover release and 12 KiB render stack), and one build-tooling fix (`scripts/git_branch.py`, below); see [`tools/installer/PORTING_TO_CROSSINK_1.6.1.md`](../../installer/PORTING_TO_CROSSINK_1.6.1.md).

## Windows build fix (`scripts/git_branch.py`)

Official CrossInk v1.6.1 does not build on Windows with the pinned toolchain
(pioarduino platform 55.03.37, PlatformIO Core 6.1.19): SCons stops after ~6 s with
`Two environments with different actions were specified for the same target ...
FrameworkArduino\ColorFormat.c.o`. Cause: v1.6.1's `git_branch.py` registers a build
middleware scoped to `src/util/BuildInfo.cpp`, but pioarduino's Windows-only include-length
wrapper replays every registered middleware for every framework source, ignoring that
pattern. This was reproduced on a pristine v1.6.1 checkout with no FrotzX3 files. The
package makes `git_branch.py` use its existing global-defines fallback when `os.name == 'nt'`
(the pre-middleware behavior, which the old base used). It changes only which translation units
receive `CROSSINK_GIT_SHA`/`CROSSINK_GIT_DIRTY`/`CROSSINK_VERSION`; only `BuildInfo.cpp` reads them.
Builds on Linux/macOS are unaffected.

## Applying by hand

From a clean checkout of `v1.6.1` (with `git config core.autocrlf false`):

```powershell
git apply --check --whitespace=nowarn <path>\patches\0001-frotzx3-host-integration.patch
git apply         --whitespace=nowarn <path>\patches\0001-frotzx3-host-integration.patch
Copy-Item -Recurse -Force <path>\files\* .
git submodule update --init --recursive
$env:CROSSINK_RELEASE_VERSION = '0.9.0-beta.1-ci161-hwtest'
pio run -e default
```

Prefer `tools\installer\Install-FrotzX3.ps1 -TargetCrossInkVersion 1.6.1`, which does this
plus verification. The patch does not apply to any other CrossInk commit; the compatibility
tester refuses to treat a different base as compatible.

## Line endings

`tools/patches/.gitattributes` marks this folder `-text`, so Git never rewrites line
endings and the SHA-256 values stay valid.

## Regenerating

The package is generated from a working checkout of official v1.6.1 that has the port
committed on top (the source commit is recorded in `manifest.json`):

```powershell
tools\patches\New-PatchPackage.ps1 -Version 0.9.0-beta.1 `
  -PackageName v0.9.0-beta.1-crossink-1.6.1 -RepoDir <port checkout> `
  -BaseCommit 9914146eeae7b46b300f475a16c32426fc02ec1f -SourceCommit HEAD `
  -UpstreamTag v1.6.1 -PackageStatus build-tested -ReleaseVersion 0.9.0-beta.1-ci161-hwtest
```

## Support policy

This package is promoted (`status: tested`, no `opt_in_only`) for CrossInk v1.6.1 `9914146e` only. Do not
reuse it for another CrossInk commit: every new CrossInk release needs its own port and its own X3 hardware
checklist (`tools/installer/UPDATING_CROSSINK.md`) before it is marked supported.
