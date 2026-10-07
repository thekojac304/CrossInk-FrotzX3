# Shared helpers for the experimental FrotzX3 patch installer.
# Dot-sourced by Install-FrotzX3.ps1 and Test-FrotzX3Compatibility.ps1.
# Written for Windows PowerShell 5.1 and PowerShell 7.

Set-StrictMode -Version 2.0

$script:FxLogWriter = $null
$script:FxLogPath = $null
$script:FxGit = 'git'
$script:FxPio = 'pio'

# ---------------------------------------------------------------- logging / output

function Initialize-FxLog {
    param([Parameter(Mandatory = $true)][string]$Path)
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $script:FxLogPath = $Path
    $script:FxLogWriter = New-Object System.IO.StreamWriter($Path, $false, (New-Object System.Text.UTF8Encoding($false)))
    $script:FxLogWriter.AutoFlush = $true
    Write-FxLog ("FrotzX3 installer log, started {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
}

function Write-FxLog {
    param([string]$Message)
    if ($script:FxLogWriter) { $script:FxLogWriter.WriteLine($Message) }
}

function Close-FxLog {
    if ($script:FxLogWriter) { $script:FxLogWriter.Dispose(); $script:FxLogWriter = $null }
}

function Write-FxStep {
    param([string]$Message)
    Write-Host ''
    Write-Host "== $Message" -ForegroundColor Cyan
    Write-FxLog ''
    Write-FxLog "== $Message"
}

function Write-FxInfo {
    param([string]$Message)
    Write-Host "   $Message"
    Write-FxLog "   $Message"
}

function Write-FxOk {
    param([string]$Message)
    Write-Host "   OK: $Message" -ForegroundColor Green
    Write-FxLog "   OK: $Message"
}

# Prints a plain-English failure and stops. Callers catch the 'FXFAIL' marker at top level.
function Stop-Fx {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [string[]]$Details = @(),
        [string]$NextStep = ''
    )
    Write-Host ''
    Write-Host "FAILED: $Title" -ForegroundColor Red
    Write-FxLog "FAILED: $Title"
    foreach ($d in $Details) { Write-Host "  $d"; Write-FxLog "  $d" }
    if ($NextStep) { Write-Host ''; Write-Host "  $NextStep"; Write-FxLog "  $NextStep" }
    if ($script:FxLogPath) { Write-Host "  Detailed log: $($script:FxLogPath)" }
    throw 'FXFAIL'
}

# ---------------------------------------------------------------- process helpers

function Invoke-FxNative {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = '',
        [switch]$Ticker
    )
    $lines = New-Object System.Collections.Generic.List[string]
    Write-FxLog ("> {0} {1}   (in {2})" -f $Exe, ($Arguments -join ' '), $(if ($WorkingDirectory) { $WorkingDirectory } else { (Get-Location).Path }))
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $pushed = $false
    $count = 0
    try {
        if ($WorkingDirectory) { Push-Location -LiteralPath $WorkingDirectory; $pushed = $true }
        & $Exe @Arguments 2>&1 | ForEach-Object {
            $t = "$_"
            $lines.Add($t)
            Write-FxLog $t
            $count++
            if ($Ticker -and ($count % 150 -eq 0)) { Write-Host -NoNewline '.' }
        }
        $code = $LASTEXITCODE
    } finally {
        if ($pushed) { Pop-Location }
        $ErrorActionPreference = $prevEap
    }
    if ($Ticker -and $count -ge 150) { Write-Host '' }
    Write-FxLog "< exit code $code"
    return [pscustomobject]@{ ExitCode = $code; Output = $lines.ToArray() }
}

function Invoke-FxGit {
    param(
        [Parameter(Mandatory = $true)][string[]]$GitArgs,
        [string]$Dir = ''
    )
    $all = @('-c', 'core.autocrlf=false', '-c', 'core.longpaths=true', '-c', 'core.safecrlf=false') + $GitArgs
    return Invoke-FxNative -Exe $script:FxGit -Arguments $all -WorkingDirectory $Dir
}

# ---------------------------------------------------------------- prerequisites

function Find-FxPio {
    $cmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command platformio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @(
            (Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'),
            (Join-Path $env:USERPROFILE '.platformio/penv/bin/pio'))) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return $null
}

function Test-FxPrerequisites {
    param([switch]$NeedBuildTools)
    Write-FxStep 'Checking prerequisites'
    $missing = New-Object System.Collections.Generic.List[string]

    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) {
        $script:FxGit = $git.Source
        $ver = (Invoke-FxNative -Exe $script:FxGit -Arguments @('--version')).Output | Select-Object -First 1
        Write-FxOk "Git found: $ver"
    } else {
        $missing.Add('Git for Windows (https://git-scm.com/download/win)')
    }

    if ($NeedBuildTools) {
        $pio = Find-FxPio
        if ($pio) {
            $script:FxPio = $pio
            $ver = (Invoke-FxNative -Exe $pio -Arguments @('--version')).Output | Select-Object -First 1
            Write-FxOk "PlatformIO found: $ver"
        } else {
            $missing.Add('PlatformIO Core (https://platformio.org/install/cli). Python is only needed to install it; PlatformIO brings its own.')
        }
    }

    if ($missing.Count -gt 0) {
        Stop-Fx -Title 'A required tool is not installed.' `
            -Details (@('Missing:') + ($missing | ForEach-Object { "  - $_" })) `
            -NextStep 'Install the tool(s) above, open a NEW PowerShell window, and run this script again. Nothing was changed.'
    }
}

function Get-FxFreeSpaceGb {
    param([string]$Path)
    try {
        $root = [System.IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $Path).Path)
        $drive = Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -eq $root } | Select-Object -First 1
        if ($drive) { return [math]::Round($drive.Free / 1GB, 1) }
    } catch { }
    return $null
}

# ---------------------------------------------------------------- manifest / package

function Get-FxRepoRoot {
    # tools/installer -> tools -> repository root
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}

function Get-FxCompatibility {
    $path = Join-Path $PSScriptRoot 'compatibility.json'
    if (-not (Test-Path -LiteralPath $path)) {
        Stop-Fx -Title 'The compatibility list is missing.' -Details @("Expected file: $path") `
            -NextStep 'Re-download FrotzX3 and run the script from the complete folder.'
    }
    try { return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json) }
    catch { Stop-Fx -Title 'The compatibility list could not be read.' -Details @($_.Exception.Message) }
}

# Releases that carry "opt_in_only": true (for example a build-tested port to a newer CrossInk that has not
# been flashed yet) are never chosen by default. They are selected only by an explicit -Target that matches
# the entry's "crossink_target" exactly (no nearest-version matching).
function Select-FxRelease {
    param($Compat, [string]$Version, [string]$Target = '')
    $releases = @($Compat.releases)
    $isOptIn = { param($r) ($r.PSObject.Properties.Name -contains 'opt_in_only') -and $r.opt_in_only }
    if ($Target) {
        $match = @($releases | Where-Object { ($_.PSObject.Properties.Name -contains 'crossink_target') -and $_.crossink_target -eq $Target })
        if ($Version) { $match = @($match | Where-Object { $_.frotzx3_version -eq $Version.TrimStart('v') }) }
        if ($match.Count -eq 0) {
            $known = @($releases | Where-Object { $_.PSObject.Properties.Name -contains 'crossink_target' } | ForEach-Object { $_.crossink_target })
            Stop-Fx -Title "No FrotzX3 package is listed for CrossInk target '$Target'." `
                -Details @('Targets with a package: ' + ($known -join ', '), 'Targets must match exactly; the nearest version is never substituted.') `
                -NextStep 'Nothing was changed.'
        }
        return $match[0]
    }
    $standard = @($releases | Where-Object { -not (& $isOptIn $_) })
    if ($Version) {
        $match = @($standard | Where-Object { $_.frotzx3_version -eq $Version.TrimStart('v') })
        if ($match.Count -eq 0) {
            Stop-Fx -Title "FrotzX3 version '$Version' is not in the compatibility list." `
                -Details @('Available: ' + (($standard | ForEach-Object { $_.frotzx3_version }) -join ', '))
        }
        return $match[0]
    }
    $usable = @($standard | Where-Object { $_.status -ne 'unsupported' })
    if ($usable.Count -eq 0) { Stop-Fx -Title 'The compatibility list has no usable FrotzX3 release.' }
    return $usable[0]   # newest first
}

# Version string compiled into the firmware (CROSSINK_RELEASE_VERSION). Packages may override the
# FrotzX3 version with a test-only string via build.release_version_env.
function Get-FxReleaseVersion {
    param([Parameter(Mandatory = $true)]$Manifest)
    $rv = $Manifest.build.release_version_env
    if ($rv -and ($rv.PSObject.Properties.Name -contains 'CROSSINK_RELEASE_VERSION') -and $rv.CROSSINK_RELEASE_VERSION) {
        return [string]$rv.CROSSINK_RELEASE_VERSION
    }
    return [string]$Manifest.frotzx3_version
}

function Get-FxSha256 {
    param([string]$Path)
    # .NET directly: Get-FileHash was not resolvable in some hidden/child PowerShell 5.1 sessions.
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}

function Read-FxPackage {
    param([Parameter(Mandatory = $true)][string]$PackageDir)
    $manifestPath = Join-Path $PackageDir 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        Stop-Fx -Title 'The FrotzX3 patch package is missing or incomplete.' -Details @("Expected: $manifestPath") `
            -NextStep 'Re-download FrotzX3 and run the script from the complete folder.'
    }
    $m = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($m.schema_version -ne 1) {
        Stop-Fx -Title 'This patch package uses a newer format than this installer understands.' `
            -Details @("Package schema: $($m.schema_version); installer supports: 1")
    }
    return $m
}

# Checks every file in the package against the SHA-256 values recorded in its manifest.
function Test-FxPackageIntegrity {
    param([Parameter(Mandatory = $true)][string]$PackageDir, [Parameter(Mandatory = $true)]$Manifest)
    $bad = New-Object System.Collections.Generic.List[string]
    foreach ($p in @($Manifest.patches)) {
        $f = Join-Path $PackageDir ($p.path -replace '/', '\')
        if (-not (Test-Path -LiteralPath $f)) { $bad.Add("missing: $($p.path)"); continue }
        if ((Get-FxSha256 $f) -ne $p.sha256) { $bad.Add("changed: $($p.path)") }
    }
    foreach ($o in @($Manifest.overlay_files)) {
        $f = Join-Path $PackageDir ('files\' + ($o.path -replace '/', '\'))
        if (-not (Test-Path -LiteralPath $f)) { $bad.Add("missing: files/$($o.path)"); continue }
        if ((Get-FxSha256 $f) -ne $o.sha256) { $bad.Add("changed: files/$($o.path)") }
    }
    if ($bad.Count -gt 0) {
        Stop-Fx -Title 'The FrotzX3 patch package is damaged or has been edited.' `
            -Details (@("$($bad.Count) file(s) do not match the checksums in manifest.json:") + ($bad | Select-Object -First 10 | ForEach-Object { "  - $_" })) `
            -NextStep 'Re-download FrotzX3. If you changed the package on purpose, regenerate it with tools\patches\New-PatchPackage.ps1.'
    }
    Write-FxOk ("Patch package verified ({0} overlay files, {1} patch)" -f @($Manifest.overlay_files).Count, @($Manifest.patches).Count)
}

# ---------------------------------------------------------------- CrossInk source

# Fetches an upstream CrossInk ref (commit, tag or branch) into $Dest and checks it out.
# Returns the full commit SHA that is checked out.
function New-FxUpstreamSource {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Ref,
        [Parameter(Mandatory = $true)][string]$Dest
    )
    $env:GIT_TERMINAL_PROMPT = '0'
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    $r = Invoke-FxGit -Dir $Dest -GitArgs @('init', '--quiet'); if ($r.ExitCode -ne 0) { Stop-Fx -Title 'Could not create a Git repository in the build folder.' -Details @($Dest) }
    Invoke-FxGit -Dir $Dest -GitArgs @('config', 'core.autocrlf', 'false') | Out-Null
    Invoke-FxGit -Dir $Dest -GitArgs @('config', 'core.longpaths', 'true') | Out-Null
    Invoke-FxGit -Dir $Dest -GitArgs @('remote', 'add', 'origin', $Url) | Out-Null

    # Shallow fetch of exactly the wanted commit; fall back to a full fetch if the server refuses.
    $r = Invoke-FxGit -Dir $Dest -GitArgs @('fetch', '--depth', '1', '--quiet', 'origin', $Ref)
    $viaFetchHead = ($r.ExitCode -eq 0)
    if (-not $viaFetchHead) {
        Write-FxInfo 'Shallow download was refused; downloading full history (slower)...'
        $r = Invoke-FxGit -Dir $Dest -GitArgs @('fetch', '--tags', '--quiet', 'origin', '+refs/heads/*:refs/remotes/origin/*')
        if ($r.ExitCode -ne 0) {
            Stop-Fx -Title 'Could not download the official CrossInk source.' `
                -Details @("Repository: $Url", 'Check your internet connection and that the address is reachable.') `
                -NextStep 'No files were changed in your own folders. Try again in a few minutes.'
        }
    }
    # After a direct fetch FETCH_HEAD is the requested ref; after the full-history fallback it is
    # NOT (it points at whichever branch was fetched last), so resolve the name instead.
    $sha = ''
    $candidates = if ($viaFetchHead) { @('FETCH_HEAD') } else { @($Ref, "refs/remotes/origin/$Ref", "refs/tags/$Ref") }
    foreach ($c in $candidates) {
        $q = Invoke-FxGit -Dir $Dest -GitArgs @('rev-parse', '--verify', '--quiet', "$c^{commit}")
        if ($q.ExitCode -eq 0 -and $q.Output.Count -gt 0) { $sha = ($q.Output | Select-Object -Last 1).Trim(); break }
    }
    if (-not $sha) {
        Stop-Fx -Title 'The requested CrossInk version was not found.' -Details @("Repository: $Url", "Requested: $Ref")
    }
    $co = Invoke-FxGit -Dir $Dest -GitArgs @('checkout', '--quiet', '--detach', $sha)
    if ($co.ExitCode -ne 0) { Stop-Fx -Title 'Could not check out the requested CrossInk version.' -Details @("Requested: $Ref ($sha)") }
    return $sha
}

function Initialize-FxSubmodules {
    param([Parameter(Mandatory = $true)][string]$Dest)
    $r = Invoke-FxGit -Dir $Dest -GitArgs @('submodule', 'update', '--init', '--recursive', '--depth', '1', '--quiet')
    if ($r.ExitCode -ne 0) {
        Write-FxInfo 'Retrying submodule download without shallow mode...'
        $r = Invoke-FxGit -Dir $Dest -GitArgs @('submodule', 'update', '--init', '--recursive', '--quiet')
    }
    if ($r.ExitCode -ne 0) {
        Stop-Fx -Title 'Could not download one of CrossInk''s required components (submodules).' `
            -Details @('CrossInk needs the freeink-sdk component, which has its own nested components.',
                'If a component commit was removed upstream, use the complete-source ZIP from the FrotzX3 release instead.') `
            -NextStep 'No files were changed in your own folders.'
    }
}

function Test-FxSubmoduleCommits {
    param([Parameter(Mandatory = $true)][string]$Dest, [Parameter(Mandatory = $true)]$Manifest)
    foreach ($s in @($Manifest.required_submodules)) {
        $r = Invoke-FxGit -Dir $Dest -GitArgs @('ls-tree', 'HEAD', $s.path)
        $line = $r.Output | Select-Object -First 1
        $found = if ($line) { ($line -split '\s+')[2] } else { '(missing)' }
        if ($found -ne $s.commit) {
            Stop-Fx -Title "The $($s.path) component is not the version FrotzX3 was tested with." `
                -Details @("Expected: $($s.commit)", "Found:    $found") -NextStep 'No files were changed.'
        }
        $st = Invoke-FxGit -Dir $Dest -GitArgs @('submodule', 'status', '--recursive')
        $uninit = @($st.Output | Where-Object { $_ -match '^-' })
        if ($uninit.Count -gt 0) {
            Stop-Fx -Title 'A required CrossInk component did not download completely.' -Details ($uninit | Select-Object -First 5)
        }
        Write-FxOk "$($s.path) is at the expected commit ($($s.commit.Substring(0,10)))"
    }
    # Nested submodules (recorded by newer packages) must also match exactly.
    if ($Manifest.PSObject.Properties.Name -contains 'nested_submodules') {
        $st = Invoke-FxGit -Dir $Dest -GitArgs @('submodule', 'status', '--recursive')
        foreach ($n in @($Manifest.nested_submodules)) {
            $line = @($st.Output | Where-Object { $_ -match ('\s' + [regex]::Escape($n.path) + '(\s|$)') }) | Select-Object -First 1
            $got = if ($line -and $line -match '^[ +U-]?([0-9a-f]{40})') { $Matches[1] } else { '(missing)' }
            if ($got -ne $n.commit) {
                Stop-Fx -Title "The nested component $($n.path) is not the version FrotzX3 was tested with." `
                    -Details @("Expected: $($n.commit)", "Found:    $got") -NextStep 'No files were changed.'
            }
            Write-FxOk "$($n.path) is at the expected commit ($($n.commit.Substring(0,10)))"
        }
    }
}

# ---------------------------------------------------------------- applying the package

# Dry run: reports problems without touching any file. Returns @{ Clean = bool; Problems = string[] }.
function Test-FxPackageApplies {
    param(
        [Parameter(Mandatory = $true)][string]$Dest,
        [Parameter(Mandatory = $true)][string]$PackageDir,
        [Parameter(Mandatory = $true)]$Manifest
    )
    $problems = New-Object System.Collections.Generic.List[string]

    foreach ($o in @($Manifest.overlay_files)) {
        $target = Join-Path $Dest ($o.path -replace '/', '\')
        if (Test-Path -LiteralPath $target) {
            $problems.Add("File already exists in CrossInk (FrotzX3 expected to add it): $($o.path)")
        }
    }
    foreach ($p in @($Manifest.patches)) {
        $patchFile = Join-Path $PackageDir ($p.path -replace '/', '\')
        $r = Invoke-FxGit -Dir $Dest -GitArgs @('apply', '--check', '--whitespace=nowarn', $patchFile)
        if ($r.ExitCode -ne 0) {
            foreach ($l in $r.Output) { if ($l -match '^error:') { $problems.Add($l) } }
            if (-not ($r.Output | Where-Object { $_ -match '^error:' })) { $problems.Add("git apply --check failed for $($p.path)") }
        }
    }
    return [pscustomobject]@{ Clean = ($problems.Count -eq 0); Problems = $problems.ToArray() }
}

function Install-FxPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Dest,
        [Parameter(Mandatory = $true)][string]$PackageDir,
        [Parameter(Mandatory = $true)]$Manifest,
        [switch]$SkipResultCheck   # for candidate CrossInk versions: patched files legitimately differ from the tested blobs
    )
    foreach ($p in @($Manifest.patches)) {
        $patchFile = Join-Path $PackageDir ($p.path -replace '/', '\')
        $r = Invoke-FxGit -Dir $Dest -GitArgs @('apply', '--whitespace=nowarn', $patchFile)
        if ($r.ExitCode -ne 0) { Stop-Fx -Title 'Applying the FrotzX3 changes failed part-way.' -Details $r.Output -NextStep 'The temporary build folder is left in place for inspection.' }
    }
    foreach ($o in @($Manifest.overlay_files)) {
        $from = Join-Path $PackageDir ('files\' + ($o.path -replace '/', '\'))
        $to = Join-Path $Dest ($o.path -replace '/', '\')
        $toDir = Split-Path -Parent $to
        if (-not (Test-Path -LiteralPath $toDir)) { New-Item -ItemType Directory -Force -Path $toDir | Out-Null }
        Copy-Item -LiteralPath $from -Destination $to
    }
    # Verify the result byte-for-byte.
    $bad = New-Object System.Collections.Generic.List[string]
    if (-not $SkipResultCheck) {
        foreach ($e in @($Manifest.expected_results)) {
            $r = Invoke-FxGit -Dir $Dest -GitArgs @('hash-object', '--no-filters', ($e.path -replace '/', '\'))
            $got = ($r.Output | Select-Object -First 1)
            if ($got -ne $e.git_blob) { $bad.Add("$($e.path) differs from the tested result") }
        }
    }
    foreach ($o in @($Manifest.overlay_files)) {
        $to = Join-Path $Dest ($o.path -replace '/', '\')
        if ((Get-FxSha256 $to) -ne $o.sha256) { $bad.Add("$($o.path) was not copied correctly") }
    }
    if ($bad.Count -gt 0) {
        Stop-Fx -Title 'FrotzX3 was applied, but the result is not what was tested.' `
            -Details ($bad | Select-Object -First 10) -NextStep 'Do not build this folder. Report this problem with the log file.'
    }
    Write-FxOk 'FrotzX3 applied; every changed file matches the tested result'
}

# ---------------------------------------------------------------- build

function Invoke-FxBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Dest,
        [Parameter(Mandatory = $true)]$Manifest
    )
    $version = Get-FxReleaseVersion -Manifest $Manifest
    $envName = [string]$Manifest.build.platformio_environment

    # PlatformIO reads these from the environment and merges them into the build, so a leftover
    # PLATFORMIO_BUILD_FLAGS (for example -DFROTZX3_DEBUG_LOG) would silently change the firmware.
    $buildInfluencing = @(
        'PLATFORMIO_BUILD_FLAGS', 'PLATFORMIO_BUILD_UNFLAGS', 'PLATFORMIO_SRC_BUILD_FLAGS',
        'PLATFORMIO_BUILD_SRC_FLAGS', 'PLATFORMIO_BUILD_SRC_UNFLAGS', 'PLATFORMIO_SRC_FILTER',
        'PLATFORMIO_BUILD_SRC_FILTER', 'PLATFORMIO_BUILD_DIR', 'PLATFORMIO_EXTRA_SCRIPTS',
        'PLATFORMIO_LIB_EXTRA_DIRS', 'PLATFORMIO_DEFAULT_ENVS', 'PLATFORMIO_ENV_DEFAULT')
    $saved = @{}
    foreach ($n in (@('CROSSINK_RELEASE_VERSION', 'CROSSINK_RC_HASH', 'PYTHONUTF8', 'PYTHONIOENCODING') + $buildInfluencing)) {
        $saved[$n] = [Environment]::GetEnvironmentVariable($n, 'Process')
    }
    try {
        foreach ($n in $buildInfluencing) {
            if ($saved[$n]) {
                Write-FxInfo "Ignoring environment variable $n for this build (it would change the firmware)."
                [Environment]::SetEnvironmentVariable($n, $null, 'Process')
            }
        }
        $env:CROSSINK_RELEASE_VERSION = $version
        Remove-Item Env:CROSSINK_RC_HASH -ErrorAction SilentlyContinue   # an RC hash would override the release version
        $env:PYTHONUTF8 = '1'
        $env:PYTHONIOENCODING = 'utf-8'
        Write-FxInfo "Building: pio run -e $envName   (CROSSINK_RELEASE_VERSION=$version)"
        Write-FxInfo 'A first build downloads the toolchain and compiles the framework; this can take 10+ minutes. Progress dots follow.'
        $r = Invoke-FxNative -Exe $script:FxPio -Arguments @('run', '-e', $envName) -WorkingDirectory $Dest -Ticker
    } finally {
        foreach ($n in $saved.Keys) { [Environment]::SetEnvironmentVariable($n, $saved[$n], 'Process') }
    }

    $ram = ($r.Output | Where-Object { $_ -match '^\s*RAM:\s' } | Select-Object -Last 1)
    $flash = ($r.Output | Where-Object { $_ -match '^\s*Flash:\s' } | Select-Object -Last 1)
    $ok = ($r.ExitCode -eq 0) -and ($r.Output | Where-Object { $_ -match '\[SUCCESS\]' })
    if (-not $ok) {
        $tail = @($r.Output | Select-Object -Last 15)
        Stop-Fx -Title 'The firmware build failed.' `
            -Details (@('Last lines of build output:') + ($tail | ForEach-Object { "  $_" })) `
            -NextStep 'See the log file for the full output. The temporary build folder was kept for inspection.'
    }
    $bin = Join-Path $Dest ($Manifest.build.firmware_output -replace '/', '\')
    if (-not (Test-Path -LiteralPath $bin)) {
        Stop-Fx -Title 'The build reported success but the firmware file is missing.' -Details @("Expected: $bin")
    }
    return [pscustomobject]@{ Firmware = $bin; Ram = $(if ($ram) { $ram.Trim() } else { '(not reported)' }); Flash = $(if ($flash) { $flash.Trim() } else { '(not reported)' }) }
}

function Test-FxBuildResult {
    param(
        [Parameter(Mandatory = $true)][string]$Dest,
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$Firmware
    )
    $version = Get-FxReleaseVersion -Manifest $Manifest

    # 1. espressif/mdns pinned version actually used by the build
    $mdnsYml = Join-Path $Dest 'managed_components\espressif__mdns\idf_component.yml'
    $mdns = '(not found)'
    if (Test-Path -LiteralPath $mdnsYml) {
        $m = Select-String -LiteralPath $mdnsYml -Pattern '^version:\s*"?([^"\s]+)"?' | Select-Object -First 1
        if ($m) { $mdns = $m.Matches[0].Groups[1].Value }
    }
    if ($mdns -ne [string]$Manifest.build.expected_mdns_version) {
        Stop-Fx -Title 'The build used an unexpected espressif/mdns version.' `
            -Details @("Expected: $($Manifest.build.expected_mdns_version)", "Found:    $mdns") `
            -NextStep 'Do not use this firmware. The build dependency pin did not apply.'
    }
    Write-FxOk "espressif/mdns $mdns (pinned)"

    # 2. version string embedded in the firmware image
    $bytes = [System.IO.File]::ReadAllBytes($Firmware)
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    if ($text.IndexOf($version, [StringComparison]::Ordinal) -lt 0) {
        Stop-Fx -Title 'The firmware does not contain the expected version string.' -Details @("Expected: $version")
    }
    $devForm = $text.IndexOf("$version-dev", [StringComparison]::Ordinal)
    if ($devForm -ge 0) { Stop-Fx -Title 'The firmware is marked as a development build.' -Details @("Found: $version-dev...") }
    Write-FxOk "Firmware version string '$version' present"

    # 3. FrotzX3 sources are in the image
    foreach ($needle in 'FrotzX3') {
        if ($text.IndexOf($needle, [StringComparison]::Ordinal) -lt 0) { Stop-Fx -Title 'The firmware does not appear to contain FrotzX3.' }
    }
    Write-FxOk 'FrotzX3 present in firmware image'
    return $mdns
}

function Copy-FxFirmwareOutput {
    param([string]$Firmware, [string]$OutputDir, $Manifest)
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $dest = Join-Path $OutputDir $Manifest.build.output_name
    Copy-Item -LiteralPath $Firmware -Destination $dest -Force
    return $dest
}

function Remove-FxWorkDir {
    param([string]$Path)
    if ($Path -and (Test-Path -LiteralPath $Path)) {
        try { Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop }
        catch { Write-FxInfo "Could not fully remove the temporary folder: $Path" }
    }
}
