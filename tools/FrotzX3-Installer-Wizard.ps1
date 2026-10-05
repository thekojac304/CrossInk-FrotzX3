<#
.SYNOPSIS
Guided Windows front end for Install-FrotzX3.ps1.
.DESCRIPTION
Run Install-FrotzX3.cmd for the interactive menu. The existing backend remains
responsible for FrotzX3 copying, Home integration, verification and optional building.
.PARAMETER CheckOnly
Developer validation: inspect an existing folder without downloading, patching,
installing, building or flashing. Only the diagnostic report in TEMP is written.
.PARAMETER Destination
Existing folder to inspect with CheckOnly. Not used by the normal interactive menu.
#>
[CmdletBinding()]
param([switch]$CheckOnly, [string]$Destination)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$backend = Join-Path $PSScriptRoot 'Install-FrotzX3.ps1'
$engine = Join-Path $PSHOME 'powershell.exe'
if (-not (Test-Path -LiteralPath $engine)) { $engine = Join-Path $PSHOME 'pwsh.exe' }
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$reportPath = $null
$targetRoot = '(not selected)'
$changes = 'No firmware files were changed.'
$installed = 'NO'
$buildState = 'NOT RUN'
$flashed = 'NO'
$exitCode = 1
$knownInput = 'A103E4408A105DC75702864C457BD0B903BA6729F283F92D529FF57A6DC36926'
$oldInput = '345C8A766C02CE98B0C841C81137B630DEDC47B0F7E647185FD120ED09D82E97'
$knownHeader = 'BF06F227A39BE6A6B622A7D1E96DAA2AEB498A440393C2D6530051C60362F4A7'

function Read-NormalText([string]$Path) {
    return $utf8.GetString([IO.File]::ReadAllBytes($Path)).TrimStart([char]0xFEFF).Replace("`r`n", "`n")
}
function Text-Hash([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($Text))).Replace('-', '') }
    finally { $sha.Dispose() }
}
function Write-Report([string]$Text) {
    if ($reportPath) { [IO.File]::AppendAllText($reportPath, $Text + [Environment]::NewLine, $utf8) }
}
function Assert-NormalPath([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    while ($null -ne $item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "This folder uses a link or junction: $($item.FullName). Choose a normal, separate folder." }
        $item = if ($item -is [IO.DirectoryInfo]) { $item.Parent } else { $item.Directory }
    }
}
function Resolve-Target([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { throw 'No folder was selected.' }
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Value.Trim().Trim('"')).TrimEnd('\', '/')
    if ($resolved.Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $resolved.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $sourceRoot.StartsWith($resolved + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'That is your original FrotzX3 folder, or a folder inside/around it. Choose a separate new folder so the original stays safe.'
    }
    $ancestor = $resolved
    while (-not (Test-Path -LiteralPath $ancestor)) {
        $ancestor = Split-Path -Parent $ancestor
        if (-not $ancestor) { throw 'That folder location could not be resolved. Choose an ordinary local Windows folder.' }
    }
    Assert-NormalPath $ancestor
    return $resolved
}
function Ask-Yes([string]$Question, [bool]$DefaultYes = $false) {
    $answer = Read-Host $Question
    if ([string]::IsNullOrWhiteSpace($answer)) { return $DefaultYes }
    return $answer.Trim() -match '^(?i)y(es)?$'
}
function Invoke-Recorded([string]$Program, [string[]]$Arguments, [switch]$StdoutOnly) {
    $lines = [Collections.Generic.List[string]]::new()
    $priorUtf8 = $env:PYTHONUTF8
    $priorEncoding = $env:PYTHONIOENCODING
    $priorPreference = $ErrorActionPreference
    Write-Report ("RUN: " + $Program + ' ' + ($Arguments -join ' '))
    try {
        $env:PYTHONUTF8 = '1'
        $env:PYTHONIOENCODING = 'utf-8'
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            if (-not $StdoutOnly -or $_ -isnot [Management.Automation.ErrorRecord]) { $lines.Add($line) }
            Write-Report $line
        }
        $code = $LASTEXITCODE
    } finally {
        $env:PYTHONUTF8 = $priorUtf8
        $env:PYTHONIOENCODING = $priorEncoding
        $ErrorActionPreference = $priorPreference
    }
    return [pscustomobject]@{ Code = $code; Text = ($lines -join "`n") }
}
function Get-GitChanges([string]$Root) {
    $probe = Invoke-Recorded $gitPath @('--no-optional-locks', '-C', $Root, 'rev-parse', '--show-toplevel') -StdoutOnly
    if ($probe.Code -ne 0) { throw 'This folder cannot be checked safely with Git. Choose option 1 to download a complete fresh copy, or ask a developer to check the folder. Details are in the report.' }
    $gitRoot = [IO.Path]::GetFullPath($probe.Text.Trim()).TrimEnd('\', '/')
    if (-not $gitRoot.Equals($Root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Select the main CrossInk folder, not a subfolder inside another Git checkout.' }
    $status = Invoke-Recorded $gitPath @('--no-optional-locks', '-C', $Root, 'status', '--porcelain', '--untracked-files=all', '--ignore-submodules=none') -StdoutOnly
    if ($status.Code -ne 0) { throw 'The safety check for existing changes failed. Nothing will be installed until a developer checks the report.' }
    return $status.Text
}
function Assert-Clean([string]$Root) {
    if (-not [string]::IsNullOrWhiteSpace((Get-GitChanges $Root))) {
        throw 'This CrossInk folder contains changes that have not been saved to Git. Automatic installation stopped so those changes are not damaged. Choose option 1 with a new folder.'
    }
}
function Plan-Input([string]$Root) {
    $path = Join-Path $Root 'src\MappedInputManager.cpp'
    $header = Join-Path $Root 'src\MappedInputManager.h'
    $text = Read-NormalText $path
    $hash = Text-Hash $text
    if ((Text-Hash (Read-NormalText $header)) -ne $knownHeader) { throw 'This CrossInk release changed its input interface. Developer review is needed before installation.' }
    if ($hash -eq $knownInput) { return [pscustomobject]@{ Needed = $false; Text = $text; Before = $hash } }
    if ($hash -ne $oldInput) { throw 'This CrossInk release changed its button handling. Developer review is needed; no automatic input patch was attempted.' }
    $reference = Read-NormalText (Join-Path $sourceRoot 'src\MappedInputManager.cpp')
    if ((Text-Hash $reference) -ne $knownInput) { throw 'The original input reference is not the supported tested version. Developer review is needed.' }
    # Replace only the three known function bodies, never the entire source file.
    # Whole-file hashes on both sides prevent unrelated changes being overwritten.
    foreach ($name in @('wasPressed', 'wasReleased', 'isPressed')) {
        $pattern = '(?ms)^bool MappedInputManager::' + $name + '\(const Button button\) const \{.*?^\}'
        $from = [regex]::Matches($text, $pattern)
        $to = [regex]::Matches($reference, $pattern)
        if ($from.Count -ne 1 -or $to.Count -ne 1) { throw 'The known button update could not be matched exactly. Developer review is needed.' }
        $text = $text.Substring(0, $from[0].Index) + $to[0].Value + $text.Substring($from[0].Index + $from[0].Length)
    }
    if ((Text-Hash $text) -ne $knownInput) { throw 'The planned button update did not match the tested version. No input file was changed.' }
    return [pscustomobject]@{ Needed = $true; Text = $text; Before = $hash }
}
function Apply-Input([string]$Root, $Plan) {
    $path = Join-Path $Root 'src\MappedInputManager.cpp'
    Assert-NormalPath $path
    if ((Text-Hash (Read-NormalText $path)) -ne $Plan.Before) { throw 'The input file changed while the wizard was open. Installation stopped.' }
    $backup = "$path.frotzx3-backup-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$([Guid]::NewGuid().ToString('N'))"
    [IO.File]::Copy($path, $backup, $false)
    Write-Report "Input backup: $backup"
    Write-Host "Safety backup: $backup"
    try {
        $bytes = [IO.File]::ReadAllBytes($path)
        $original = $utf8.GetString($bytes)
        $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191
        $newText = if ($original.Contains("`r`n")) { $Plan.Text.Replace("`n", "`r`n") } else { $Plan.Text }
        [IO.File]::WriteAllText($path, $newText, [Text.UTF8Encoding]::new($bom))
        if ((Text-Hash (Read-NormalText $path)) -ne $knownInput) { throw 'Button update verification failed.' }
    } catch {
        [IO.File]::Copy($backup, $path, $true)
        throw "The input update failed and its original was restored. Backup: $backup. $($_.Exception.Message)"
    }
    return $backup
}

try {
    # Reports are outside both firmware folders so they do not affect Git checks.
    $logDir = Join-Path ([IO.Path]::GetTempPath()) 'FrotzX3-Installer'
    $null = [IO.Directory]::CreateDirectory($logDir)
    $reportPath = Join-Path $logDir ("compatibility-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$([Guid]::NewGuid().ToString('N')).txt")
    Write-Report "FrotzX3 guided installer`nOriginal folder: $sourceRoot"
    if (-not (Test-Path -LiteralPath $backend -PathType Leaf)) { throw "The installer is incomplete: $backend is missing. Obtain the complete known-good FrotzX3 folder." }
    Assert-NormalPath $sourceRoot
    Write-Host 'FrotzX3 guided installer'
    Write-Host "Original FrotzX3 folder: $sourceRoot"
    Write-Host 'Your current known-good FrotzX3 folder will not be changed.'
    $option = '2'
    if (-not $CheckOnly) {
        Write-Host '[1] Download a fresh CrossInk and install FrotzX3 (recommended)'
        Write-Host '[2] Install FrotzX3 into an existing CrossInk source folder'
        Write-Host '[3] Exit'
        $option = Read-Host 'Choose 1, 2 or 3 [Enter = 1]'
        if ([string]::IsNullOrWhiteSpace($option)) { $option = '1' }
        if ($option -eq '3') { Write-Host 'Closed without changing firmware files.'; exit 0 }
        if ($option -notin @('1', '2')) { throw 'That menu choice was not recognized. Open the wizard again and choose 1, 2 or 3.' }
    }
    $git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $git) { throw 'Git is missing. Install Git for Windows from https://git-scm.com/download/win, then reopen this wizard. No firmware files were changed.' }
    $gitPath = $git.Source
    if ($option -eq '1') {
        $defaultFolder = "C:\Dev\CrossInk-FrotzX3-$(Get-Date -Format 'yyyyMMdd')"
        $answer = Read-Host "Where should the new firmware folder go? [Enter = $defaultFolder]"
        if ([string]::IsNullOrWhiteSpace($answer)) { $answer = $defaultFolder }
        $targetRoot = Resolve-Target $answer
        if (Test-Path -LiteralPath $targetRoot) { throw 'That folder already exists. Choose a different new folder name; nothing will be overwritten.' }
        Write-Report "New folder: $targetRoot"
        Write-Host 'Downloading current CrossInk and its required components. This may take a few minutes.'
        $changes = "A download was attempted into $targetRoot. It may contain a partial download; no existing folder was overwritten."
        $clone = Invoke-Recorded $gitPath @('clone', '--recurse-submodules', '--', 'https://github.com/uxjulia/CrossInk.git', $targetRoot)
        if ($clone.Code -ne 0) { throw 'CrossInk could not be downloaded. Check your connection and the report, then retry with a new folder name.' }
        $changes = "Fresh CrossInk downloaded to $targetRoot. No compatibility changes have been applied."
    } else {
        if (-not $CheckOnly) {
            Write-Host 'A fresh download (option 1) is safest. Existing folders with unsaved changes will be left alone.'
            $Destination = Read-Host 'Paste the full CrossInk folder path (for example C:\Dev\CrossInk-New)'
        }
        $targetRoot = Resolve-Target $Destination
    }
    Write-Report "Destination: $targetRoot"
    foreach ($relative in @('platformio.ini', 'src\main.cpp', 'src\activities\home\HomeActivity.cpp', 'src\MappedInputManager.cpp', 'src\MappedInputManager.h')) {
        $path = Join-Path $targetRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "This is not a complete CrossInk folder. Missing: $path. Choose option 1 for a complete download." }
        Assert-NormalPath $path
    }
    Assert-Clean $targetRoot
    Write-Host '[OK] CrossInk source found'
    Write-Host 'Checking whether this version can be installed safely...'
    $analysis = Invoke-Recorded $engine @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $backend, '-Destination', $targetRoot, '-DryRun')
    if ($analysis.Text -notmatch '(?m)^HomeActivity integration: (Already integrated:|Compatible:)') {
        throw 'This CrossInk release changed its Home menu code, or its source is incomplete. FrotzX3 needs a small manual compatibility update before it can be installed safely. No automatic source patch was attempted.'
    }
    Write-Host '[OK] Home menu compatible'
    $inputPlan = Plan-Input $targetRoot
    # Do not turn the backend's other blockers into a blanket override.
    $manual = [regex]::Match($analysis.Text, '(?ms)^Remaining manual steps:\r?\n(.*?)^Next action:')
    if (-not $manual.Success -or $analysis.Code -notin @(0, 2)) { throw 'The installer could not complete its safety checks. Give the compatibility report to a developer.' }
    foreach ($line in ($manual.Groups[1].Value -split '\r?\n')) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line.Trim() -eq 'Physical X3 smoke testing after building/flashing.') { continue }
        if ($inputPlan.Needed -and $line.Trim().StartsWith("Review $targetRoot\src\MappedInputManager.cpp with the integration guide:")) { continue }
        throw 'This folder needs additional manual review (for example, FrotzX3 files may already exist). Choose option 1 with a new folder, or share the report with a developer.'
    }
    if ($inputPlan.Needed) { Write-Host '[OK] X3 input handling compatible with a known small update (backup will be made)' }
    else { Write-Host '[OK] X3 input handling compatible' }
    Write-Host 'No FrotzX3-specific main.cpp modification is currently required.'
    if ($CheckOnly) {
        Write-Host '[OK] Compatibility checks complete. No input patch, install, build or flash was performed.'
        Write-Host "Report: $reportPath"
        exit 0
    }
    if (-not (Ask-Yes 'Everything looks compatible. Install FrotzX3 now? [Y/N]')) {
        Write-Host "Installation cancelled. $changes"
        Write-Host 'Next action: Reopen the wizard when you are ready to install.'
        exit 0
    }
    $doBuild = Ask-Yes 'Build the firmware after installation? [Y/N, Enter = Y]' $true
    $pioPath = $null
    if ($doBuild) {
        $pio = Get-Command pio -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $pioPath = if ($pio) { $pio.Source } else { Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe' }
        if (-not (Test-Path -LiteralPath $pioPath -PathType Leaf)) { throw 'PlatformIO, the firmware build tool, is missing. Ask for help installing PlatformIO, then reopen this wizard. Installation has not started.' }
    }
    Assert-Clean $targetRoot
    $backendArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $backend, '-Destination', $targetRoot)
    if ($inputPlan.Needed) {
        $changes = "The known generic input update was attempted in $targetRoot. Its backup and outcome are in the report."
        $inputBackup = Apply-Input $targetRoot $inputPlan
        $allowedBackup = '?? src/' + [IO.Path]::GetFileName($inputBackup)
        $statusLines = @((Get-GitChanges $targetRoot) -split '\r?\n' | Where-Object { $_ })
        if ($statusLines.Count -ne 2 -or ' M src/MappedInputManager.cpp' -notin $statusLines -or $allowedBackup -notin $statusLines) {
            throw 'Other changes appeared while the wizard was running. Installation stopped to protect them. The input backup is listed in the report.'
        }
        # Only the exact edit and backup just created above may bypass the dirty guard.
        $backendArguments += '-AllowDirtyDestination'
    }
    if ($doBuild) { $backendArguments += '-Build'; $buildState = 'REQUESTED' }
    $changes = "Installation was started in $targetRoot. It may contain copied files and backed-up host edits; see the report for the exact list."
    Write-Host 'Installing FrotzX3. If building, this may take several minutes. Please keep this window open.'
    Write-Host "Detailed progress is recorded in: $reportPath"
    $run = Invoke-Recorded $engine $backendArguments
    if ($run.Text -match 'HomeActivity integration: (Patched and verified:|Already integrated:)') { $installed = 'YES (check build status)' }
    if ($run.Code -ne 0) {
        if ($doBuild) { $buildState = 'NOT PASSED' }
        $first = [regex]::Match($run.Text, '(?im)^.*(?:fatal error:|error:|Exception:).*$').Value
        throw "Installation or building stopped. No speculative repair was attempted. First reported error: $first. Give the report to a developer before retrying."
    }
    $installed = 'YES'
    if ($doBuild) {
        if ($run.Text -notmatch 'Build: PASSED: pio run -e default') { throw 'The build result could not be confirmed. Do not flash; have a developer review the report.' }
        $buildState = 'PASSED'
        Write-Host '[OK] FIRMWARE BUILD PASSED' -ForegroundColor Green
        Write-Host 'Flashing writes this firmware to your device. Connect only the intended XTEINK X3 with a data cable.'
        if (Ask-Yes 'Would you like to flash the connected XTEINK X3 now? [Y/N]') {
            Push-Location $targetRoot
            try { $upload = Invoke-Recorded $pioPath @('run', '-e', 'default', '-t', 'upload') }
            finally { Pop-Location }
            if ($upload.Code -ne 0) { $flashed = 'FAILED / device state unverified'; throw 'The upload failed. The new firmware folder is still available. Follow the X3 recovery/flashing instructions with a developer before retrying.' }
            $flashed = 'YES'
        }
    } else { $buildState = 'SKIPPED BY USER' }
    Write-Host "`n=====================================" -ForegroundColor Green
    Write-Host '       FrotzX3 INSTALL COMPLETE' -ForegroundColor Green
    Write-Host '=====================================' -ForegroundColor Green
    Write-Host "New firmware folder:`n$targetRoot"
    Write-Host "FrotzX3 installed: $installed"
    Write-Host "Firmware build: $buildState"
    Write-Host "X3 flashed: $flashed"
    Write-Host 'Your original FrotzX3 folder was not changed.'
    Write-Host "Report: $reportPath"
    if ($flashed -eq 'YES') { Write-Host "Next action: Run the physical X3 smoke tests in $targetRoot\FROTZX3_INTEGRATION.md." }
    elseif ($doBuild) { Write-Host "Next action: Follow the Flash and test section of $sourceRoot\FROTZX3_INSTALLER_README.md." }
    else { Write-Host "Next action: Follow the Build manually section of $sourceRoot\FROTZX3_INSTALLER_README.md." }
    $exitCode = 0
} catch {
    Write-Host "`nInstallation stopped safely: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "Files: $changes"
    Write-Host "FrotzX3 installed: $installed | Firmware build: $buildState | X3 flashed: $flashed"
    Write-Host 'Your original FrotzX3 folder was not changed.'
    Write-Host "Compatibility report: $reportPath"
    Write-Host 'Next action: Share this report with a developer to resolve the reported problem.'
    Write-Report "STOPPED: $($_.Exception.Message)"
} finally {
    Write-Report "Files: $changes`nInstalled: $installed`nBuild: $buildState`nFlashed: $flashed"
}
exit $exitCode
