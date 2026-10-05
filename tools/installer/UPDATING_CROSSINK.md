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

Prefer a release tag (for example `v1.6.1`) over a branch tip. Record its full SHA.

## 3. Try the current patch

```powershell
tools\installer\Test-FrotzX3Compatibility.ps1 -CrossInkRef v1.6.1
```

- **CLEAN** (exit code 0): the patch and overlay apply. Go on to step 5.
- **CONFLICT** (exit code 2): the failing hunks are listed. Go to step 4.
- Exit code 1: the test itself could not run (network, Git).

The output also says whether `freeink-sdk` moved. If it did, expect API-level work in
`FrotzX3Activity.cpp` even when the patch is clean.

## 4. Resolve conflicts only where needed

1. Make a working branch from the candidate commit:
   `git checkout -b frotzx3/crossink-1.6.1 <candidate sha>` (a FrotzX3 checkout, not the installer's temp folder).
2. Bring in FrotzX3: copy `tools/patches/v<old>/files/*` to the tree, then
   `git apply --3way tools/patches/v<old>/patches/*.patch`. Fix only the hunks that fail,
   following `FROTZX3_INTEGRATION.md` (Home menu steps, Confirm/Power semantics).
3. Check semantic items that can break without a textual conflict:
   - `HomeMenuEntries::kCapacity` is at least the number of entries (including FrotzX3).
   - `getMenuItemCount()` matches the entries built for each Home layout.
   - Upstream may already include an equivalent Confirm/Power fix; if so drop that hunk
     instead of duplicating it.
   - `platformio.ini` still lists `pre:scripts/pin_idf_components.py`.
4. Do **not** change interpreter, input, task, allocation or save behavior to make it fit.

## 5. Build

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

```powershell
tools\patches\New-PatchPackage.ps1 -Version <new frotzx3 version> `
  -BaseCommit <crossink sha> -SourceCommit <frotzx3 commit> `
  -UpstreamVersionNote "<crossink version/tag>"
```

Update `FROTZX3_DELTA.md` if the classification changed. Run
`Install-FrotzX3.ps1 -CheckOnly` and then a full install to confirm the package
reconstructs the tested source.

## 9. Update the compatibility manifest

Add a new entry at the **top** of `releases` in
[`compatibility.json`](compatibility.json) with the actual SHA, CrossInk version,
patch path, and honest test flags. Keep earlier entries so older releases stay
reproducible.

## 10. Publish

Publish a new FrotzX3 release with the firmware, the complete-source ZIP
(`tools/release/New-SourceBundle.ps1`), SHA-256 values and notes naming the CrossInk
version it is based on.
