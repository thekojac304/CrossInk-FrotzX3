# FrotzX3 delta against official CrossInk

This lists every file that differs between official CrossInk and FrotzX3
`0.9.0-beta.1`, and how each is handled in the portable patch package
(`tools/patches/v0.9.0-beta.1/`).

- **Upstream base:** `cab4f24922f05811e7f44be1057f62ea2d978c52`
  (`chore: add x4pro device type to bug report`, 2026-08-18) from
  <https://github.com/uxjulia/CrossInk>. It is CrossInk `v1.5.0` plus three
  commits (`git describe`: `v1.5.0-3-gcab4f249`; `platformio.ini` says `1.5.0`) and is
  contained in `v1.5.1`, `v1.6.0` and `v1.6.1`.
- **FrotzX3 source commit:** `606122d188b97410619e5051e2c964598ea88af3`. Runtime code is
  unchanged from the hardware-validated build (`a045d1fd`).
- **History:** FrotzX3 history descends directly from the base
  (`git merge-base HEAD cab4f249` is `cab4f249`). The 17 commits after the base touch
  only the files below, so **no upstream CrossInk changes are mixed into the delta**
  (category D is empty).
- `freeink-sdk` (submodule) is **not** modified; FrotzX3 uses the commit CrossInk pins
  (`1ff020263cd2202ea79ce3eb811f5ac8489b8cde`).

## Categories

| Code | Meaning | In patch package? |
| --- | --- | --- |
| A | FrotzX3-owned code (new files, never exist upstream) | Yes, overlay `files/` |
| B | CrossInk integration change (edits an upstream file) | Yes, git patch |
| C | Docs / release / installer files | Docs and licensing: overlay. Others: no |
| D | Upstream CrossInk changes that must not be in the patch | none present |
| E | Generated / build artifacts | none tracked (ignored by `.gitignore`) |

Conflict risk is the expected chance that the file needs manual work when CrossInk
moves to a newer release: **None** (path does not exist upstream), **Low**, **Medium**,
**High**. For A-files the risk is not textual conflict but *API drift*: they compile
against CrossInk/FreeInk APIs (see "Fragile integration points" below).

## B. CrossInk integration changes (git patch)

| File | Why FrotzX3 changes it | Conflict risk |
| --- | --- | --- |
| `src/activities/home/HomeActivity.cpp` | Adds the Home menu entry that launches `FrotzX3Activity`: include, `InteractiveFiction` action, `kCapacity` 8→9, entries in `appendHomeMenuItems()` and `buildMinimalMenuItems()`, `getMenuItemCount()` +1, launch case in `loop()`. | **High.** Home is one of CrossInk's most actively changed files (menu items, themes, carousel). The context lines around the enum, capacity constant and menu lists change often; also the count in `getMenuItemCount()` must stay in step with the entries even when the patch applies cleanly. |
| `src/MappedInputManager.cpp` | Generic Confirm/Power fix for X3: arms `suppressPowerRelease` when Confirm is accepted, clears it on Confirm release, hides a consumed Power hold in `isPressed(Power)`. Prevents Select from also triggering Sleep. No FrotzX3-specific logic. | **Medium-High.** Input mapping is touched by device additions (X4 Pro, touch). Recheck whether upstream already fixed this; if so drop the hunk. |
| `platformio.ini` | Adds `pre:scripts/pin_idf_components.py` to `extra_scripts` (pins `espressif/mdns` 1.14.0 for reproducible builds) and an optional `monitor_filters = esp32_exception_decoder` for debugging. | **Medium.** `extra_scripts` and `[env:default]` are edited by upstream regularly; the hunks are tiny, so conflicts are easy to resolve. |

## A. FrotzX3-owned code (overlay)

All paths are new; the installer refuses to continue if one already exists in the
CrossInk source.

| File(s) | Why | Conflict risk |
| --- | --- | --- |
| `src/activities/frotzx3/FrotzX3Activity.cpp`, `.h` | The native e-ink UI: picker, keyboard, suggestions, menus, transcript, saves, rewind, Adventure Log. Uses CrossInk `Activity`, `RenderLock`, `MappedInputManager`, `GfxRenderer`, `UITheme`, `fontIds.h`, `HalStorage`. | None textually. **High API drift risk:** any change to those classes or font IDs can break the build. |
| `lib/FrotzX3/src/FrotzX3.cpp`, `FrotzX3.h`, `FrotzX3Paths.h` | C++ bridge: Frotz task, input hand-off, SD paths, Quetzal bridge, status info. | None. Low API drift (uses HAL storage, ESP-IDF heap/FreeRTOS). |
| `lib/FrotzX3/src/FrotzPlatform.c`, `FrotzGlobals.c` | Frotz platform layer / globals for ESP32 (text-only output capture, key input). | None. Low. |
| `lib/FrotzX3/src/{buffer,err,fastmem,files,hotkey,input,math,object,process,quetzal,random,redirect,screen,sound,stream,table,text,variable}.c`, `frotz.h`, `setup.h`, `unused.h`, `git_hash.h` | Frotz interpreter core, modified for the X3 (HAL-backed storage, memory behavior, platform hooks). GPL-2.0-or-later. | None. Independent of CrossInk. |
| `lib/FrotzX3/library.json`, `README.md` | PlatformIO library metadata (carries the FrotzX3 version) and library readme. | None. |
| `scripts/pin_idf_components.py` | PlatformIO pre-build script that pins `espressif/mdns` to 1.14.0. | None textually. **Medium:** depends on how pioarduino/Arduino-ESP32 builds the IDF libs; revisit on platform upgrades. |

## C. Docs, licensing and release files

| File | In package? | Why | Conflict risk |
| --- | --- | --- | --- |
| `FROTZX3_README.md`, `FROTZX3_INTEGRATION.md`, `FROTZX3_LICENSE.md`, `THIRD_PARTY_NOTICES.md` | Yes (overlay) | Licensing and integration docs that must travel with any FrotzX3 build. | None |
| `LICENSES/CrossInk-MIT.txt`, `Frotz-AUTHORS.txt`, `Frotz-GPL-2.0-or-later.txt` | Yes (overlay) | License texts required by GPL/MIT redistribution. | None unless upstream adds a `LICENSES/` folder |
| `README.md` | No | FrotzX3 landing page (replaces CrossInk's README in this repository). | n/a (this repository only) |
| `CHANGELOG.md` | No | FrotzX3 entries added to the top. | n/a |
| `.gitignore` | No | Adds `/vendor/frotz/`, `.agents/` (local conveniences) and `dist-installer/` (installer output). Not needed to build. | n/a |
| `FROTZX3_INSTALLER_README.md`, `Install-FrotzX3.cmd`, `tools/Install-FrotzX3.ps1`, `tools/FrotzX3-Installer-Wizard.ps1` | No | Legacy "transplant" installer (copies files into an existing checkout). Superseded by the patch installer; kept for reference. | n/a |
| `tools/release/New-SourceBundle.ps1`, `tools/release/README.md` | No | Builds the complete-source ZIP for each release. | n/a |
| `tools/patches/**`, `tools/installer/**` | No | The patch package, generator, installer and compatibility tools themselves. | n/a |

## D. Upstream CrossInk changes

None. `git diff cab4f249..HEAD --name-only` contains only files classified above.

## E. Generated / build artifacts

None are tracked. Ignored and never packaged: `.pio/`, `managed_components/`, `.dummy/`,
`sdkconfig.*`, `lib/I18n/I18n*.{h,cpp}`, `src/network/html/*.generated.h`, `vendor/`,
`dist-publish/`, `dist-installer/`.

## Fragile integration points

1. **`HomeActivity.cpp`** — menu capacity, entry counts and the launch case; textual
   conflicts are likely and semantic breakage is possible without a conflict.
2. **`FrotzX3Activity.cpp` vs CrossInk APIs** — `Activity`, `RenderLock`, `GfxRenderer`,
   `UITheme`, `MappedInputManager` and `HalStorage`. A rename or signature change
   breaks the build but not the patch.
3. **Confirm/Power semantics** in `MappedInputManager.cpp` — wrong behavior builds
   fine but can put the X3 to sleep on Select. Needs hardware retest.
4. **`freeink-sdk` pointer** — if CrossInk moves it, FreeInk APIs used by the activity
   may change. Upstream submodule commits can also become unfetchable; the
   complete-source ZIP is the fallback.
5. **Flash headroom** — the image uses roughly 97% of the OTA partition; a larger
   CrossInk release can push it over the limit without any code conflict.
6. **IDF component pin** (`pin_idf_components.py`) — tied to how pioarduino builds the
   Arduino libraries; other floating managed components are not pinned.
