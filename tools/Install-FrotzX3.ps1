<#
.SYNOPSIS
Copies FrotzX3 into a separate CrossInk checkout and safely integrates its Home entry.
.DESCRIPTION
Resolves the known-good source from this script's parent directory. Checks everything
before copying. Automatic Home patching supports only the recorded known layout;
unrecognized host changes require manual review. Never edits input code or main.cpp.
.PARAMETER Destination
Existing, separate CrossInk source checkout, for example C:\Dev\CrossInk-New.
.PARAMETER DryRun
Inspect and print the plan without writing files or running a build.
.PARAMETER AllowDirtyDestination
Explicitly permit installation into a Git checkout with local changes. Backups are
still required; this does not bypass structural compatibility checks.
.PARAMETER UpdateExistingFrotzX3
Allow replacement of existing FrotzX3-owned files after backing them up. Unexpected
extra files are rejected rather than deleted or mixed with the new installation.
.PARAMETER Build
After installation, run pio run -e default in the destination, without flashing.
.EXAMPLE
.\tools\Install-FrotzX3.ps1 -Destination 'C:\Dev\CrossInk-New' -DryRun
.EXAMPLE
.\tools\Install-FrotzX3.ps1 -Destination 'C:\Dev\CrossInk-New' -Build
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [switch]$DryRun,
    [switch]$AllowDirtyDestination,
    [switch]$UpdateExistingFrotzX3,
    [switch]$Build
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destinationRoot = $Destination
$copied = [Collections.Generic.List[string]]::new()
$modified = [Collections.Generic.List[string]]::new()
$backups = [Collections.Generic.List[string]]::new()
$remaining = [Collections.Generic.List[string]]::new()
$homeStatus = 'Not checked'
$inputStatus = 'Not checked'
$mainStatus = 'Not checked; never modified by this script'
$buildStatus = if ($Build) { 'Requested; not started' } else { 'Not requested' }
$result = 'STOPPED'
$exitCode = 1
$nextAction = 'Correct the reported problem, then rerun with -DryRun.'
$utf8 = [Text.UTF8Encoding]::new($false, $true)

function Read-SourceText([string]$Path) {
    $text = $utf8.GetString([IO.File]::ReadAllBytes($Path))
    return $text.TrimStart([char]0xFEFF).Replace("`r`n", "`n")
}
function Get-TextHash([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($Text)))).Replace('-', '') }
    finally { $sha.Dispose() }
}
function Require-Path([string]$Path, [string]$Type) {
    if (-not (Test-Path -LiteralPath $Path -PathType $Type)) {
        throw "Missing expected $Type path: $Path. Check that you selected the complete CrossInk source checkout."
    }
}
function Assert-NoLinks([string]$Path) {
    # Reject junctions/symlinks, including parent directories, before any copy or write.
    $item = Get-Item -LiteralPath $Path -Force
    while ($null -ne $item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Linked folders/files are not supported safely: $($item.FullName). Use a normal local checkout."
        }
        $item = if ($item -is [IO.DirectoryInfo]) { $item.Parent } else { $item.Directory }
    }
}
function Backup-File([string]$Path) {
    $backup = "$Path.frotzx3-backup-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')-$([Guid]::NewGuid().ToString('N').Substring(0,8))"
    [IO.File]::Copy($Path, $backup, $false)
    $backups.Add($backup)
    Write-Host "Backup: $backup"
    return $backup
}

try {
    Require-Path $sourceRoot Container
    Assert-NoLinks $sourceRoot
    foreach ($relative in @('lib\FrotzX3', 'src\activities\frotzx3')) {
        Require-Path (Join-Path $sourceRoot $relative) Container
    }
    foreach ($relative in @('FROTZX3_INTEGRATION.md', 'platformio.ini',
        'lib\FrotzX3\library.json', 'lib\FrotzX3\src\FrotzX3.h',
        'lib\FrotzX3\src\FrotzX3Paths.h', 'src\activities\frotzx3\FrotzX3Activity.cpp',
        'src\activities\frotzx3\FrotzX3Activity.h')) {
        Require-Path (Join-Path $sourceRoot $relative) Leaf
    }
    $destinationRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination).TrimEnd('\', '/')
    Require-Path $destinationRoot Container
    Assert-NoLinks $destinationRoot
    $sameRoot = $destinationRoot.Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase)
    if (-not $sameRoot -and ($destinationRoot.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $sourceRoot.StartsWith($destinationRoot + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw 'Source and destination must be separate, non-nested checkouts.'
    }
    if ($sameRoot -and -not $DryRun) { throw 'Refusing to install over the known-good source repo. Choose C:\Dev\CrossInk-New instead.' }
    foreach ($relative in @('src', 'lib')) { Require-Path (Join-Path $destinationRoot $relative) Container }
    foreach ($relative in @('src\activities\home\HomeActivity.cpp', 'src\MappedInputManager.cpp', 'src\main.cpp', 'platformio.ini')) {
        $path = Join-Path $destinationRoot $relative
        Require-Path $path Leaf
        Assert-NoLinks $path
    }
    if ((Read-SourceText (Join-Path $destinationRoot 'platformio.ini')) -notmatch '(?m)^\[crossink\]') {
        throw "CrossInk identity could not be confirmed in $destinationRoot\platformio.ini. Check the selected checkout."
    }
    Write-Host "Known-good source: $sourceRoot"
    Write-Host "Future firmware destination: $destinationRoot"
    $git = Get-Command git -ErrorAction SilentlyContinue
    $dirty = $false
    if ($git) {
        # Windows PowerShell can treat native stderr as a terminating error. A ZIP
        # checkout is valid too; a failed Git probe should only produce a warning.
        try {
            $ErrorActionPreference = 'Continue'
            $inside = & $git.Source --no-optional-locks -C $destinationRoot rev-parse --is-inside-work-tree 2>$null
            $probeExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = 'Stop' }
        if ($probeExit -eq 0 -and $inside -eq 'true') {
            $branch = & $git.Source --no-optional-locks -C $destinationRoot symbolic-ref --short -q HEAD
            if ($LASTEXITCODE -ne 0) { $branch = '(detached HEAD)' }
            Write-Host "Destination Git branch: $branch"
            $status = @(& $git.Source --no-optional-locks -C $destinationRoot status --porcelain --untracked-files=all)
            if ($LASTEXITCODE -ne 0) { throw 'Git status failed; destination safety could not be checked.' }
            $dirty = $status.Count -gt 0
            if ($dirty) { Write-Warning 'Destination has uncommitted changes. Commit or back them up first, or explicitly use -AllowDirtyDestination.' }
        } else { Write-Warning 'Destination is not a Git working tree. Backups will be your recovery mechanism.' }
    } else { Write-Warning 'Git was not found. Branch and uncommitted-change checks are unavailable.' }

    $manifest = [Collections.Generic.List[object]]::new()
    foreach ($relative in @('lib\FrotzX3', 'src\activities\frotzx3', 'FROTZX3_INTEGRATION.md')) {
        $from = Join-Path $sourceRoot $relative
        $to = Join-Path $destinationRoot $relative
        Write-Host "COPY FROM: $from"
        Write-Host "       TO: $to"
        Assert-NoLinks $from
        $items = if (Test-Path -LiteralPath $from -PathType Container) {
            @(Get-ChildItem -LiteralPath $from -Recurse -Force)
        } else { @(Get-Item -LiteralPath $from) }
        foreach ($item in $items) {
            Assert-NoLinks $item.FullName
            if ($item.PSIsContainer) { continue }
            $rel = $item.FullName.Substring($sourceRoot.Length + 1)
            $target = Join-Path $destinationRoot $rel
            $ancestor = $target
            while (-not (Test-Path -LiteralPath $ancestor)) { $ancestor = Split-Path -Parent $ancestor }
            Assert-NoLinks $ancestor
            if (Test-Path -LiteralPath $target -PathType Container) { throw "A folder occupies an expected file path: $target" }
            $manifest.Add([pscustomobject]@{ Source = $item.FullName; Target = $target })
            Write-Host "  FILE: $($item.FullName) -> $target"
        }
    }
    # Refuse stale extras rather than remove them or leave duplicate activity sources.
    foreach ($relative in @('lib\FrotzX3', 'src\activities\frotzx3')) {
        $targetDir = Join-Path $destinationRoot $relative
        if (Test-Path -LiteralPath $targetDir) {
            Require-Path $targetDir Container
            Assert-NoLinks $targetDir
            foreach ($item in Get-ChildItem -LiteralPath $targetDir -Recurse -Force) {
                Assert-NoLinks $item.FullName
                if (-not $item.PSIsContainer -and $item.FullName -notin $manifest.Target) {
                    $remaining.Add("Unexpected existing file: $($item.FullName). Back up and reconcile this owned folder manually before installation.")
                }
            }
        }
    }
    $existing = @($manifest | Where-Object { Test-Path -LiteralPath $_.Target })
    if ($existing.Count -gt 0 -and -not $UpdateExistingFrotzX3) {
        $remaining.Add('FrotzX3-owned destination files already exist. Review them, then use -UpdateExistingFrotzX3 to back up and replace them.')
    }
    if ($dirty -and -not $AllowDirtyDestination) { $remaining.Add('Commit destination changes, or explicitly supply -AllowDirtyDestination after reviewing them.') }

    $homePath = Join-Path $destinationRoot 'src\activities\home\HomeActivity.cpp'
    $homeText = Read-SourceText $homePath
    $homeHash = Get-TextHash $homeText
    # Hash gates cover the whole host file (only CRLF/LF and UTF-8 BOM are ignored).
    # This intentionally rejects unfamiliar future layouts, even if a few anchors match.
    $plainHomeHash = 'D08C83F82EB888E91DE0336F3367E6F15B666AFFCEDA252700BF7551E49E9E23'
    $integratedHomeHash = '8A7CCDCC9BF7F08443362072ACBB300D91441562BFEA61DF1E93F3DA8FB30263'
    $knownInputHash = 'A103E4408A105DC75702864C457BD0B903BA6729F283F92D529FF57A6DC36926'
    $patches = @(
        @('#include "ClippingStore.h"', "#include `"ClippingStore.h`"`n#include `"../frotzx3/FrotzX3Activity.h`""),
        @("  Bookmarks,`n  FileTransfer,", "  Bookmarks,`n  InteractiveFiction,`n  FileTransfer,"),
        @('static constexpr int kCapacity = 8;', 'static constexpr int kCapacity = 9;'),
        @('  items.push({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});', "  items.push({`"FrotzX3`", Book, HomeMenuAction::InteractiveFiction});`n  items.push({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});"),
        @('int count = 4;  // File Browser, Recents, File transfer, Settings', 'int count = 5;  // File Browser, Recents, Interactive Fiction, File transfer, Settings'),
        @("`n      case HomeMenuAction::FileTransfer:", "`n      case HomeMenuAction::InteractiveFiction:`n        activityManager.pushActivity(`n            std::make_unique<FrotzX3Activity>(renderer, mappedInput));`n        break;  `n      case HomeMenuAction::FileTransfer:")
    )
    $plannedHome = $homeText
    if ($homeHash -eq $integratedHomeHash) {
        $homeStatus = 'Already integrated: exact supported layout; no Home edit needed'
    } elseif ($homeHash -eq $plainHomeHash) {
        foreach ($patch in $patches) {
            $expected = if ($patch[0].StartsWith('  items.push')) { 2 } else { 1 }
            if ([regex]::Matches($plannedHome, [regex]::Escape($patch[0])).Count -ne $expected) { throw 'Home anchor verification failed. Manual integration required.' }
            $plannedHome = $plannedHome.Replace($patch[0], $patch[1])
        }
        if ((Get-TextHash $plannedHome) -ne $integratedHomeHash) { throw 'Home patch did not produce the verified integration. No files have been changed.' }
        $homeStatus = 'Compatible: will add include, action, two menu entries, capacity/count, and launch case'
        Write-Host "HOST FILE TO MODIFY: $homePath"
    } else {
        $homeStatus = 'Manual integration required: unfamiliar or partially integrated Home layout'
        $remaining.Add("Manually edit ${homePath}: add the FrotzX3Activity include, menu action, entries in both menu builders, adjust capacity/count, and add the pushActivity launch case. See $sourceRoot\FROTZX3_INTEGRATION.md for exact examples. Do not replace HomeActivity.cpp wholesale.")
    }
    $inputPath = Join-Path $destinationRoot 'src\MappedInputManager.cpp'
    if ((Get-TextHash (Read-SourceText $inputPath)) -eq $knownInputHash) {
        $inputStatus = 'Known-good generic Confirm/Power behavior recognized; no input patch needed'
    } else {
        $inputStatus = 'Unverified: manual generic input review required (not necessarily missing)'
        $remaining.Add("Review $inputPath with the integration guide: accepting Confirm arms Power-release suppression; ordinary Confirm release clears it; suppressed Power release is consumed; Power hold is hidden after Confirm acceptance. Preserve the existing suppression APIs in MappedInputManager.h. Port only missing generic behavior; never add Frotz-specific conditionals. This installer does not patch input code or bypass this check.")
    }
    $mainPath = Join-Path $destinationRoot 'src\main.cpp'
    $mainHash = (Get-FileHash -LiteralPath $mainPath -Algorithm SHA256).Hash
    $mainStatus = 'No FrotzX3-specific main.cpp modification is currently required.'
    Write-Host $mainStatus
    Write-Host "Home: $homeStatus"
    Write-Host "Input: $inputStatus"

    if ($DryRun) {
        $result = 'DRY RUN COMPLETE - no files changed'
        $buildStatus = 'Not run (DryRun always takes precedence over Build)'
        if ($sameRoot) { $remaining.Add('This inspected the source itself only. Actual installation must target a separate checkout.') }
        $nextAction = if ($remaining.Count) { "Read '$sourceRoot\FROTZX3_INSTALLER_README.md', section 'Manual integration required', and resolve the listed checks." } else { ".\tools\Install-FrotzX3.ps1 -Destination '$destinationRoot' -Build" }
        $exitCode = if ($remaining.Count) { 2 } else { 0 }
    } else {
        if ($remaining.Count) { throw 'Preflight requires manual action. Nothing has been copied or patched.' }
        foreach ($entry in $manifest) {
            if (Test-Path -LiteralPath $entry.Target) {
                if (-not $UpdateExistingFrotzX3) { throw "A destination file appeared after preflight: $($entry.Target). Refusing to overwrite it." }
                Assert-NoLinks $entry.Target
                $null = Backup-File $entry.Target
            }
            $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $entry.Target))
            Assert-NoLinks (Split-Path -Parent $entry.Target)
            [IO.File]::Copy($entry.Source, $entry.Target, [bool]$UpdateExistingFrotzX3)
            $copied.Add($entry.Target)
            if ((Get-FileHash -LiteralPath $entry.Source).Hash -ne (Get-FileHash -LiteralPath $entry.Target).Hash) {
                throw "Copy verification failed: $($entry.Target). Do not build until this file is checked."
            }
        }
        if ($plannedHome -cne $homeText) {
            if ((Get-TextHash (Read-SourceText $homePath)) -ne $homeHash) { throw 'Home changed during installation. Refusing to patch it; copied files are listed below.' }
            $backup = Backup-File $homePath
            try {
                $originalBytes = [IO.File]::ReadAllBytes($homePath)
                $hasBom = $originalBytes.Length -ge 3 -and $originalBytes[0] -eq 239 -and $originalBytes[1] -eq 187 -and $originalBytes[2] -eq 191
                $originalText = $utf8.GetString($originalBytes)
                $output = if ($originalText.Contains("`r`n")) { $plannedHome.Replace("`n", "`r`n") } else { $plannedHome }
                [IO.File]::WriteAllText($homePath, $output, [Text.UTF8Encoding]::new($hasBom))
                if ((Get-TextHash (Read-SourceText $homePath)) -ne $integratedHomeHash) { throw 'Written Home integration failed verification.' }
                $modified.Add($homePath)
                $homeStatus = 'Patched and verified: all expected integration elements present'
            } catch {
                [IO.File]::Copy($backup, $homePath, $true)
                throw "Home patch failed; original restored from $backup. $($_.Exception.Message)"
            }
        }
        if ((Get-FileHash -LiteralPath $mainPath -Algorithm SHA256).Hash -ne $mainHash) { throw 'main.cpp changed externally during installation; inspect before building.' }
        $result = 'INSTALL COMPLETE'
        $nextAction = "Build in '$destinationRoot', then flash and smoke-test on physical X3 using FROTZX3_INTEGRATION.md."
        if ($Build) {
            $pio = Get-Command pio -ErrorAction SilentlyContinue
            $pioPath = if ($pio) { $pio.Source } else { Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe' }
            Require-Path $pioPath Leaf
            $savedUtf8 = $env:PYTHONUTF8
            $savedEncoding = $env:PYTHONIOENCODING
            $firstError = $null
            $buildStatus = 'Running'
            Push-Location $destinationRoot
            try {
                $env:PYTHONUTF8 = '1'
                $env:PYTHONIOENCODING = 'utf-8'
                # Native stderr warnings must not abort the build under Windows PowerShell.
                $ErrorActionPreference = 'Continue'
                & $pioPath run -e default 2>&1 | ForEach-Object {
                    $line = $_.ToString()
                    Write-Host $line
                    if (-not $firstError -and $line -match '(?i)(fatal error:|\berror[: ]|Exception:|Error:|\*\*\*.*Error)') { $firstError = $line }
                }
                $buildExit = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = 'Stop'
                $env:PYTHONUTF8 = $savedUtf8
                $env:PYTHONIOENCODING = $savedEncoding
                Pop-Location
            }
            if ($buildExit -ne 0) {
                $buildStatus = "FAILED (exit $buildExit)"
                if ($firstError) { Write-Host "First reported build error: $firstError" }
                else { Write-Host 'No specific error was exposed. Review the output above; installer output may have been suppressed by PlatformIO.' }
                throw 'Build failed. No automatic source fixes were attempted; installed files and backups remain available.'
            }
            $buildStatus = 'PASSED: pio run -e default'
            $nextAction = 'Flash the destination build to your X3 and run the integration guide smoke tests. Physical X3 testing is authoritative.'
        }
        $exitCode = 0
    }
} catch {
    $result = 'STOPPED - review the error and any partial changes listed below'
    if ($buildStatus -eq 'Running') { $buildStatus = 'FAILED while launching or reading build output' }
    $nextAction = "Read the error below and '$sourceRoot\FROTZX3_INSTALLER_README.md'. Review any listed partial changes/backups before retrying."
    $remaining.Add($_.Exception.Message)
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    Write-Host "`n=== $result ==="
    Write-Host "Source repo: $sourceRoot"
    Write-Host "Destination repo: $destinationRoot"
    foreach ($section in @(@('Files copied', $copied), @('Host files modified', $modified), @('Backup files created', $backups))) {
        Write-Host "$($section[0]):"
        if ($section[1].Count -eq 0) { Write-Host '  None' } else { foreach ($item in $section[1]) { Write-Host "  $item" } }
    }
    Write-Host "HomeActivity integration: $homeStatus"
    Write-Host "MappedInputManager compatibility: $inputStatus"
    Write-Host "main.cpp: $mainStatus"
    Write-Host "Build: $buildStatus"
    Write-Host 'Remaining manual steps:'
    if ($remaining.Count -eq 0) { Write-Host '  Physical X3 smoke testing after building/flashing.' }
    foreach ($item in $remaining) { Write-Host "  $item" }
    Write-Host "Next action: $nextAction"
}
exit $exitCode
