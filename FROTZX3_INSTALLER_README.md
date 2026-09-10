# Install FrotzX3 into another CrossInk checkout

## Recommended: Double-click Install-FrotzX3.cmd

Open your known-good FrotzX3 folder in File Explorer (for example `C:\Dev\CrossInk`) and double-click **Install-FrotzX3.cmd**. Keep the entire folder together; the launcher finds the app files and existing installer automatically. You do not need to open PowerShell or type installer switches. The window stays open until you press a key at the end.

1. Choose **1**, or press Enter: **Download a fresh CrossInk and install FrotzX3**.
2. Press Enter to accept a new folder such as `C:\Dev\CrossInk-FrotzX3-20260910`, or paste your preferred location. It must not already exist and must be separate from the original folder.
3. Wait while CrossInk and its required components download. This downloads upstream's current default branch, not a guaranteed tagged release. To use a particular release already downloaded with its components, choose option 2 instead.
4. The wizard checks the Home menu and X3 button handling. A known, missing generic button fix is planned automatically, with a backup; unfamiliar code stops for developer review. Nothing is patched until you approve installation.
5. Answer **Y** to **Everything looks compatible. Install FrotzX3 now?**
6. Press Enter to build the firmware as well (recommended), or answer N to install without building.
7. After a successful build, the wizard asks separately whether to flash a connected XTEINK X3. Answer **Y only when you want to write the firmware to that device**. Connect only the intended X3. Answer N to leave the device unchanged.

Success is shown by the large **FrotzX3 INSTALL COMPLETE** message. It lists the new folder, installation result, build result, and whether the X3 was flashed. Follow the single recommended next action at the bottom. **Your original FrotzX3 folder is not changed. Physical X3 testing remains authoritative.**

Option **2** accepts a pasted existing CrossInk folder path, with or without quotes. If that folder has changes that have not been saved to Git, the wizard stops to protect them. Choose option 1 with a new folder rather than trying to bypass the warning. Existing app files are also left for review; the beginner wizard is intended for fresh installations, not forced updates.

Git for Windows must be installed to download and check folders. PlatformIO must be installed if you choose to build. The wizard finds both automatically and gives a plain-English message if either is missing; it does not silently install system tools. If Windows security or an organization policy blocks the launcher, ask for help reviewing and allowing the trusted script rather than disabling machine-wide protections. The launcher uses a process-only PowerShell execution-policy setting.

When a release needs manual review, no guessed patch is applied. A compatibility report is saved under `%TEMP%\FrotzX3-Installer\`; the exact filename is shown in the window. Give that report to ChatGPT/Codex or a developer. It contains local paths, checks, backup paths and command output; review it before sharing publicly. A failed download can leave a new partial folder. An interrupted install/build can leave files in the destination; the report records what happened and lists backend backups. The known-good folder remains untouched.

The wizard keeps the advanced backend below unchanged. Its only additional source patch is the already-tested generic Confirm/Power update, limited to a recognized input layout and verified against the expected result. It does not add Frotz-specific input conditions or edit `main.cpp`. It refuses unrelated unsaved changes even when applying its own compatibility fix.

## Advanced command-line workflow

This Windows PowerShell installer copies the tested FrotzX3 app into a **separate** CrossInk source checkout. It can add the Home menu integration when the destination matches the supported layout. It does not flash your device, edit `main.cpp`, or automatically change input behavior.

## Keep two separate folders

| Purpose | Example folder |
| --- | --- |
| Known-good FrotzX3 source, including this installer | `C:\Dev\CrossInk` |
| Fresh future CrossInk firmware to receive FrotzX3 | `C:\Dev\CrossInk-New` |

Keep the known-good folder intact. The script resolves its source from its own location (`tools\Install-FrotzX3.ps1`), not from the destination. Never copy this script into the fresh checkout and run it there. Installation over the source itself, nested checkouts, and linked folders/junctions are refused.

Download a complete future source checkout, not just a firmware `.bin`. The currently configured upstream is `https://github.com/uxjulia/CrossInk.git`. With Git installed, you can create a separate checkout using:

```powershell
git clone --recurse-submodules https://github.com/uxjulia/CrossInk.git C:\Dev\CrossInk-New
```

Use the release/tag you intend to transplant into; the command above initially checks out upstream's default branch. Do not run it over an existing folder. A GitHub source ZIP is another option, but submodule contents such as `freeink-sdk` must also be supplied as required by that release. A Git clone with submodules is less error-prone. Install the destination release's normal PlatformIO prerequisites before building.

## Open PowerShell and preview

Open Start, type **PowerShell**, and open Windows PowerShell or PowerShell 7. Administrator access is not normally needed. Run:

```powershell
cd C:\Dev\CrossInk
Get-Help .\tools\Install-FrotzX3.ps1 -Detailed
.\tools\Install-FrotzX3.ps1 -Destination "C:\Dev\CrossInk-New" -DryRun
```

If Windows blocks a trusted, reviewed local script because of execution policy, you can run this in the same window, then repeat the command. It affects only that PowerShell session; do not change machine-wide policy:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

The preview prints each full source and destination filename and the host compatibility checks. **DryRun never copies, backs up, patches, or builds**, even when combined with `-Build`. Git checks use no optional locks. Exit code 0 means the preview has no blockers; 2 means the preview completed but manual action is required; 1 means validation failed. Check the final summary rather than assuming any completed preview means installation is safe.

The exact copy roots are:

```text
C:\Dev\CrossInk\lib\FrotzX3\
  -> C:\Dev\CrossInk-New\lib\FrotzX3\
C:\Dev\CrossInk\src\activities\frotzx3\
  -> C:\Dev\CrossInk-New\src\activities\frotzx3\
C:\Dev\CrossInk\FROTZX3_INTEGRATION.md
  -> C:\Dev\CrossInk-New\FROTZX3_INTEGRATION.md
```

The only host source the installer can patch is `C:\Dev\CrossInk-New\src\activities\home\HomeActivity.cpp`. It adds the activity include, action, both menu entries, capacity/count adjustment, and launch case. It first backs up Home and verifies the complete resulting integration. A failed Home verification restores that backup automatically.

## Install

After a preview with no blockers, choose **one** command:

```powershell
# Install only:
.\tools\Install-FrotzX3.ps1 -Destination "C:\Dev\CrossInk-New"
```

```powershell
# Install and build:
.\tools\Install-FrotzX3.ps1 -Destination "C:\Dev\CrossInk-New" -Build
```

Success shows `INSTALL COMPLETE`. With `-Build`, also require `Build: PASSED: pio run -e default`. The summary lists actual copies, host edits, backups, and remaining actions. A build failure leaves the installation in place for review; the script does not invent source fixes, merge branches, commit, push, or flash.

For existing FrotzX3 files, the default is to stop. After reviewing them, an explicit update creates a backup beside every replaced file:

```powershell
.\tools\Install-FrotzX3.ps1 -Destination "C:\Dev\CrossInk-New" -UpdateExistingFrotzX3 -DryRun
.\tools\Install-FrotzX3.ps1 -Destination "C:\Dev\CrossInk-New" -UpdateExistingFrotzX3 -Build
```

If Git reports local changes, preferably commit or separately back them up first. `-AllowDirtyDestination` explicitly permits those changes to remain while installing; it does **not** bypass Home/input compatibility checks. It can be added to the commands above only after you have reviewed the destination. Unexpected extra files in app-owned folders are never deleted automatically. This includes older activity names and previous backup files: archive and reconcile them outside those folders before a later update.

## Manual integration required

The current installer deliberately recognizes **exact recorded host files**, ignoring only UTF-8 BOM and CRLF/LF differences. Even an unrelated edit or formatting change can require manual review. It is conservative rather than a general C++ parser.

- An unfamiliar `HomeActivity.cpp` stops installation before copying. Use `FROTZX3_INTEGRATION.md` for the include, menu action, menu-building entries, capacity/count, and launch case. Preserve the future release's other code; never replace the whole host file.
- An unfamiliar `MappedInputManager.cpp` is reported as **unverified**, not necessarily broken. Its required Confirm/Power suppression is generic CrossInk behavior. A knowledgeable reviewer must check Confirm acceptance, suppression clearing, consumed Power release, hidden Power hold, and the existing header APIs. Port only missing generic behavior; never add Frotz-specific input conditionals.
- `main.cpp` currently needs no Frotz-specific modifications.

Open the guide with:

```powershell
notepad C:\Dev\CrossInk\FROTZX3_INTEGRATION.md
```

Ask a developer to apply that guide to the future checkout if you are unsure. There is no force-compatibility switch. After legitimate future host changes, rerunning this installer may still stop because those files are not a recorded layout. The developer must finish the transplant manually (copy only the three owned paths shown above and integrate the host), or separately review and extend installer support for that release. Do not replace future host code with old code just to satisfy the checks.

## Build manually

In PowerShell, use the destination folder. This works whether or not `pio` is on PATH and restores the process-local Unicode settings afterward:

```powershell
cd C:\Dev\CrossInk-New
$frotzPio = Get-Command pio -ErrorAction SilentlyContinue
$frotzPioPath = if ($frotzPio) { $frotzPio.Source } else { "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" }
$frotzOldUtf8 = $env:PYTHONUTF8
$frotzOldEncoding = $env:PYTHONIOENCODING
try {
    $env:PYTHONUTF8 = '1'
    $env:PYTHONIOENCODING = 'utf-8'
    & $frotzPioPath run -e default
} finally {
    $env:PYTHONUTF8 = $frotzOldUtf8
    $env:PYTHONIOENCODING = $frotzOldEncoding
}
```

If the fallback executable is missing, install/configure PlatformIO according to the destination release's instructions. On failure, retain the first reported error and full build output. Some PlatformIO setup failures suppress their underlying error; the installer cannot recover text that PlatformIO never prints. Do not change firmware dependencies speculatively to fix tooling.

## Flash and test

Back up your SD stories and saves. Follow the selected CrossInk release's X3 flashing instructions. For a supported USB upload setup, connect the X3 with a data cable and use the same PowerShell window after the manual-build block:

```powershell
cd C:\Dev\CrossInk-New
& $frotzPioPath run -e default -t upload
```

If upload needs a port or a device-specific boot procedure, follow that release's instructions rather than guessing. For SD flashing, use its documented procedure with the successfully built `.pio\build\default\firmware-x3-x4.bin` when that artifact is provided. The installer never uploads automatically.

Keep stories directly in `/adventures` and preserve `/adventures/saves`, its `manual` and `rewind` subfolders, and all existing filenames. See the integration guide for the full layout and tests. Verify Home launch, new game/resume, Select without accidental sleep, commands/single-key input, manual save/load, rewind/recovery, exit/re-entry, and intentional sleep/wake. Include a previously tested large story. **Physical XTEINK X3 testing is authoritative; build success is not device verification.**

## Restore a backup

Stop builds and close editors first. Every backup path is printed and includes a timestamp plus a unique suffix. For example, to restore Home, replace the example backup name below with the **exact printed filename**:

```powershell
Copy-Item -LiteralPath "C:\Dev\CrossInk-New\src\activities\home\HomeActivity.cpp.frotzx3-backup-YYYYMMDD-HHMMSS-fff-XXXXXXXX" -Destination "C:\Dev\CrossInk-New\src\activities\home\HomeActivity.cpp" -Force
```

This intentionally overwrites the current Home file with its saved original; save any newer edits separately first. Restore replaced app-owned files the same way using their corresponding backups. A stopped copy can leave partial files; the summary lists completed copies and backups. Do not assume the whole installation was rolled back: only failed Home patch verification has automatic restoration.

For files that did not exist before installation, there is no original backup. Review the printed copy list before manually removing anything. On a disposable fresh checkout, starting again in another new folder is often simpler. Never run `git reset --hard`, delete the known-good repo, or discard unrelated work as a recovery shortcut.

## Validation scope

The scripts pass syntax checks in Windows PowerShell 5.1 and PowerShell 7. The advanced backend's dry run can safely inspect `C:\Dev\CrossInk` itself, but real installation there is refused; the wizard rejects that folder even for inspection. The wizard has been exercised with temporary test checkouts, including compatibility checks, protected-folder and unsaved-change refusals, and installation with the known generic input fix and backups. Current host snapshots are recognized; future layouts require review. Live upstream downloading, building and flashing through the wizard still require real-world validation. No hardware was flashed during wizard validation.

Developers can inspect a separate existing checkout with `powershell -NoProfile -File .\tools\FrotzX3-Installer-Wizard.ps1 -CheckOnly -Destination "C:\Dev\CrossInk-TestInstall"`. This skips the menu and performs no download, source patch, installation, build or upload; only a diagnostic report is written under `%TEMP%`.
