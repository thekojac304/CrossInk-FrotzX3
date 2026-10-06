# Moving FrotzX3 to a newer CrossInk release

FrotzX3 is an overlay of new files plus small edits to three CrossInk files
(see [`tools/patches/FROTZX3_DELTA.md`](../patches/FROTZX3_DELTA.md)). A new CrossInk
release is **never** declared supported just because the patch applies or the firmware
compiles. Support is declared only after the steps below, ending with hardware testing.

## 1. Fetch the new upstream release

```powershell
git fetch upstream --tags
git log --oneline v1.5.1..upstream/main      # what changed upstream
```

Read upstream's `CHANGELOG.md` for the new version, especially anything touching Home
menus, input mapping (`MappedInputManager`), `Activity`/rendering APIs, the `freeink-sdk`
pointer, the partition table, or firmware size.

## 2. Choose a candidate commit

Prefer a release tag (for example `v1.6.1`) over a branch tip. Record its full SHA
(`git rev-parse v1.6.1^{commit}`) and the `[crossink] version` in its `platformio.ini`.
Tags are not remote-tracking refs: use `v1.6.1`, not `upstream/v1.6.1`.

## 3. Try the current patch

```powershell
tools\installer\Test-FrotzX3Compatibility.ps1 -CrossInkRef v1.6.1
```

The tester selects a package only by **exact match** of the candidate commit SHA against
a package's `upstream.commit`. It never falls back to the "nearest" version.

- **CLEAN** (exit code 0): exact package match and it applies. Go on to step 5.
- **CONFLICT** (exit code 2): exact package match, but hunks fail; they are listed. Go to step 4.
- **MISMATCH** (exit code 2): no package targets this commit. This is the normal result for a new
  upstream release. To probe an existing package against it while porting, name the package
  explicitly (`-Target 1.6.1` or `-FrotzX3Version 0.9.0-beta.1`): the result is still MISMATCH,
  with an informational apply check, and never exit code 0.
- Exit code 1: the test itself could not run (network, Git).

The output also says whether `freeink-sdk` moved. If it did, expect API-level work in
`FrotzX3Activity.cpp` even when the patch is clean.

## 4. Resolve conflicts only where needed

1. Make a working branch from the candidate commit:
   `git checkout -b frotzx3/crossink-1.6.1 <candidate sha>` (a FrotzX3 checkout, not the installer's temp folder).
2. Bring in FrotzX3: copy `tools/patches/v<old>/files/*` to the tree, then
   `git apply --3way tools/patches/v<old>/patches/*.patch`. Fix only the hunks that fail,
   following `FROTZX3_INTEGRATION.md` (Home menu steps, Confirm/Power semantics).
   Beware: `git apply --reject` can report a hunk as "applied" (at an offset) when its
   context still matches although the code it refers to no longer exists. Read every hunk
   that is not a plain include/menu line (v1.6.1: the `isPressed(Power)` hunk).
3. Check semantic items that can break without a textual conflict:
   - `HomeMenuEntries::kCapacity` is at least the number of entries (including FrotzX3).
   - `getMenuItemCount()` matches the entries built for each Home layout.
   - Upstream may already include an equivalent Confirm/Power fix; if so drop that hunk
     instead of duplicating it.
   - `platformio.ini` still lists `pre:scripts/pin_idf_components.py`.
4. Do **not** change interpreter, input, task, allocation or save behavior to make it fit.

## 5. Build

On Windows also confirm that a **pristine** checkout of the candidate builds before blaming the
port (v1.6.1 did not; see the worked example below).

```powershell
$env:CROSSINK_RELEASE_VERSION = '<frotzx3 version>'
Remove-Item Env:CROSSINK_RC_HASH -ErrorAction SilentlyContinue
pio run -e default
```

Or let the tool do it on a clean apply:
`Test-FrotzX3Compatibility.ps1 -CrossInkRef <ref> -Build`.

Check for: no warnings-as-errors, flash use (currently about 97% of the OTA partition;
`pio` prints it), the `espressif/mdns` 1.14.0 pin, and the managed components that
resolved (see `tools/release/README.md`).

## 6. Regression test

Re-run the FrotzX3 smoke tests in `FROTZX3_INTEGRATION.md` plus a normal CrossInk
check (open a book, Settings, File Transfer). Confirm CrossInk's own features were not
disturbed by the Home menu change.

## 7. Hardware test

Flash a physical XTEINK X3 and run the full FrotzX3 test list: Z3/Z5/Z8 stories, game
picker, Select and power behavior (Select must not trigger Sleep), T9 keyboard,
suggestions, transcript paging, manual save/load, rewind, Adventure Log, exit and
re-entry, resume. **Hardware results are authoritative.**

## 8. Generate the new patch package

Never overwrite the previous package. Use a new folder for each CrossInk base:

```powershell
tools\patches\New-PatchPackage.ps1 -Version <new frotzx3 version> `
  -PackageName v<frotzx3 version>-crossink-<x.y.z> -RepoDir <port checkout> `
  -BaseCommit <crossink sha> -SourceCommit <port commit> -UpstreamTag v<x.y.z> `
  -PackageStatus build-tested -ReleaseVersion <test-only version string> `
  [-ExtraPatchFiles scripts/git_branch.py]
```

Update `FROTZX3_DELTA.md` if the classification changed. Run
`Install-FrotzX3.ps1 -CheckOnly` and then a full install to confirm the package
reconstructs the tested source.

## 9. Update the compatibility manifest

Add a new entry at the **top** of `releases` in
[`compatibility.json`](compatibility.json) with the actual SHA, CrossInk version,
patch path, and honest test flags. Keep earlier entries so older releases stay
reproducible.

Until X3 hardware testing passes, mark the entry `"status": "build-tested"`,
`"opt_in_only": true`, `"firmware_hardware_tested": false` and give it a `crossink_target`
(for example `"1.6.1"`). `Install-FrotzX3.ps1` then builds it only with
`-TargetCrossInkVersion 1.6.1`; the default run keeps choosing the newest entry that is not
`opt_in_only`. Promote it (status `tested`, drop `opt_in_only`) only after step 7.

## 10. Publish

Publish a new FrotzX3 release with the firmware, the complete-source ZIP
(`tools/release/New-SourceBundle.ps1`), SHA-256 values and notes naming the CrossInk
version it is based on.

---

# Worked example: CrossInk v1.5.0+3 to v1.6.1 (FrotzX3 0.9.0-beta.1)

This is the template. Details: [`PORTING_TO_CROSSINK_1.6.1.md`](PORTING_TO_CROSSINK_1.6.1.md).

**Scale of the jump** (`git diff --stat` old base to `v1.6.1`, `9914146e`, 2026-10-04): `HomeActivity.cpp`
~1250 changed lines, `main.cpp` ~1150, `MappedInputManager.cpp` ~470, `platformio.ini` ~320;
`freeink-sdk` moved to `69937018` and a new top-level submodule `assets/tabler-icons` appeared.
Despite that, the port was small because FrotzX3's host footprint is three files.

| Question | Answer |
| --- | --- |
| Files that conflicted | `platformio.ini` (2 hunks), `src/MappedInputManager.cpp` (2 hunks + 1 stale "applied" hunk), `src/activities/home/HomeActivity.cpp` (2 hunks). 6 of 12 hunks rejected. |
| Trivial / line drift | both `platformio.ini` hunks, `HomeActivity.cpp` include and `getMenuItemCount()` text |
| Needed semantic changes | `MappedInputManager.cpp`: upstream replaced the `suppressPowerRelease` bool with `util/ReleaseSuppression.h`; the FrotzX3 intent was re-expressed with `releaseSuppression.suppressPower()`, and two old hunks were dropped (one replaced by upstream's frame expiry, one now upstream). |
| New upstream feature touching FrotzX3 | The Cover Grid Home theme has its own fixed menu; FrotzX3 is not offered there (documented, not changed). |
| FrotzX3-owned code changed? | **No.** All 39 overlay files are byte-identical to 0.9.0-beta.1; `FrotzX3Activity.cpp`, `FrotzX3.cpp/.h`, `FrotzPlatform.c` are untouched and compiled against the new `freeink-sdk` with zero edits. |
| Extra build-tooling change | `scripts/git_branch.py` skips upstream's build middleware on Windows. A pristine v1.6.1 does not build on Windows: pioarduino's wrapper replays that middleware on every framework source. |
| `main.cpp` | Not modified by FrotzX3 in either version. |
| Effort | Small for the code (three files, one careful semantic port in `MappedInputManager`). Most of the time went to build infrastructure: the first build after the base switch spent ~8 min rebuilding the Arduino/IDF libs and then failed on the Windows middleware issue; builds after that took ~2.5 min incremental. The tooling work (exact-match tester, opt-in target, all-submodule manifest) took longer than the port and is now reusable. |

**Fragile areas to check first next time**

1. `MappedInputManager.cpp` Confirm/Power suppression (`wasPressed`, `wasReleased`,
   `isPressed`, `ReleaseSuppression`, `expireReleaseSuppressions`). Any rewrite of upstream's
   suppression logic invalidates the FrotzX3 hunks silently; re-read, do not just re-apply.
   Re-check that `wasPressed(Confirm)` is not one-shot per frame on the Power-fallback path.
2. `HomeActivity.cpp` menu construction: `kCapacity`, `getMenuItemCount()`, each Home theme's own
   menu (new themes may build menus separately, as Cover Grid does) and every `switch` over
   `HomeMenuAction`.
3. `platformio.ini` extra-scripts block and `[env:default]` inheritance, and whether
   `custom_sdkconfig` or the espressif/mdns constraint changed (`pin_idf_components.py` depends
   on `.dummy/idf_component.yml`).
4. Build scripts that interact with pioarduino on Windows (`git_branch.py` middleware).
   Switching between bases triggers an Arduino-libs reinstall (many minutes).
5. Submodule set: record **every** gitlink (the manifest now lists all top-level submodules and
   the nested ones); the installer verifies them.
6. Flash headroom: the v1.6.1 build uses 97.5% of the OTA partition (old base 96.9%); little room remains.
