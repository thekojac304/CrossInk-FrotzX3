<#
.SYNOPSIS
  Builds the complete-source ZIP that accompanies a FrotzX3 firmware release.

.DESCRIPTION
  GitHub's automatic "Source code" archives omit submodule contents, so they
  cannot rebuild the firmware. This script exports the release commit of the
  main repository plus every (nested) submodule at its recorded commit into one
  ZIP: FrotzX3-v<Version>-source-complete.zip

  It refuses to run unless the working tree and submodules are clean, HEAD is
  on the release branch, and the FrotzX3 version strings match -Version.
  Files come from `git archive` (committed content only), so .git metadata,
  .pio, build output, saves and local/IDE files are never copied. The staged
  tree is also scanned for forbidden file types before it is zipped. ZIP
  entries use a fixed timestamp (the release commit time) and sorted order, so
  repeated runs on the same commit produce the same SHA-256.

  Nothing is pushed, tagged or published.

.PARAMETER Version
  Release version, without a leading "v". Default: 0.9.0-beta.1

.PARAMETER OutputDir
  Where the ZIP is written. Default: <repo>\dist-publish (git-ignored).

.PARAMETER AllowAnyBranch
  Skip the release/v<Version> branch check (the clean-tree and version checks
  still apply).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\release\New-SourceBundle.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = '0.9.0-beta.1',
    [string]$OutputDir,
    [switch]$AllowAnyBranch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Git {
    param([string]$Repo, [string[]]$GitArgs)
    $out = & git -C $Repo @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "git $($GitArgs -join ' ') failed in $Repo" }
    return $out
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputDir) { $OutputDir = Join-Path $repo 'dist-publish' }
$name = "FrotzX3-v$Version-source-complete"
$zipPath = Join-Path $OutputDir "$name.zip"

# 1. Clean tree -------------------------------------------------------------
Write-Host "Repository: $repo"
$dirty = Invoke-Git $repo @('status', '--porcelain')
if ($dirty) { throw "Working tree is not clean:`n$($dirty -join "`n")" }

# 2. Release branch / version ---------------------------------------------
$branch = (Invoke-Git $repo @('rev-parse', '--abbrev-ref', 'HEAD')).Trim()
$commit = (Invoke-Git $repo @('rev-parse', 'HEAD')).Trim()
if (-not $AllowAnyBranch -and $branch -ne "release/v$Version") {
    throw "On branch '$branch', expected 'release/v$Version' (use -AllowAnyBranch to override)."
}
$frotzCpp = Get-Content (Join-Path $repo 'lib\FrotzX3\src\FrotzX3.cpp') -Raw
$frotzJson = Get-Content (Join-Path $repo 'lib\FrotzX3\library.json') -Raw | ConvertFrom-Json
if ($frotzCpp -notmatch [regex]::Escape("`"FrotzX3 $Version`"") -or $frotzJson.version -ne $Version) {
    throw "FrotzX3 version strings (FrotzX3.cpp / library.json) do not match $Version."
}

# 3. Initialise submodules; they must be clean and sit on the recorded commits
Invoke-Git $repo @('submodule', 'update', '--init', '--recursive') | Out-Null
$subDirty = Invoke-Git $repo @('submodule', 'foreach', '--quiet', '--recursive', 'git status --porcelain')
if ($subDirty) { throw "A submodule is not clean:`n$($subDirty -join "`n")" }
$subs = @()   # @{ Path; Sha }
foreach ($line in (Invoke-Git $repo @('submodule', 'status', '--recursive'))) {
    if ($line -notmatch '^(.)([0-9a-f]{40}) (\S+)') { throw "Unparseable submodule status: $line" }
    if ($Matches[1] -ne ' ') { throw "Submodule $($Matches[3]) is not at its recorded commit (status '$($Matches[1])')." }
    $subs += @{ Path = $Matches[3]; Sha = $Matches[2] }
}
if (-not $subs) { throw 'No submodules found; refusing to build an incomplete bundle.' }

# 4. Export main repo + submodules into a staging directory ----------------
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stage = Join-Path ([IO.Path]::GetTempPath()) ("frotzx3-src-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$root = Join-Path $stage $name
New-Item -ItemType Directory -Path $root | Out-Null
try {
    function Export-Repo([string]$RepoDir, [string]$Dest) {
        New-Item -ItemType Directory -Force -Path $Dest | Out-Null
        $tmp = Join-Path $stage 'export.zip'
        # core.autocrlf=false: export the committed (LF) bytes, not a machine-dependent CRLF conversion
        Invoke-Git $RepoDir @('-c', 'core.autocrlf=false', 'archive', '--format=zip', '-o', $tmp, 'HEAD') | Out-Null
        [IO.Compression.ZipFile]::ExtractToDirectory($tmp, $Dest)
        Remove-Item $tmp
    }
    Export-Repo $repo $root
    foreach ($s in $subs) { Export-Repo (Join-Path $repo $s.Path) (Join-Path $root $s.Path) }

    # 5. Strip anything that must not ship; fail if forbidden files remain ---
    $junkDirs = '.git', '.pio', '.vscode', '.idea', '__pycache__', 'node_modules', 'managed_components', '.dummy', 'dist-publish', 'fs_'
    Get-ChildItem $root -Recurse -Force -Directory | Where-Object { $junkDirs -contains $_.Name } |
        Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
            if (Test-Path $_.FullName) { Remove-Item $_.FullName -Recurse -Force }
        }
    Get-ChildItem $root -Recurse -Force -File -Include '.git' | Remove-Item -Force   # submodule gitlink files
    $forbidden = '*.bin', '*.elf', '*.map', '*.o', '*.a', '*.sav', '*.recovery', '*.zblorb', '*.dblite', '*.pem', '*.key', '*.p12', '.env'
    $forbidden += 0..8 | ForEach-Object { "*.z$_" }
    $bad = Get-ChildItem $root -Recurse -Force -File -Include $forbidden
    if ($bad) { throw "Forbidden files in staged tree:`n$(($bad | ForEach-Object { $_.FullName.Substring($root.Length) }) -join "`n")" }

    # Manifest with exact revisions
    $lines = @("FrotzX3 v$Version - complete source bundle", '', "Main repository commit: $commit")
    foreach ($s in $subs) { $lines += "Submodule $($s.Path): $($s.Sha)" }
    $lines += @('',
        'Build (needs PlatformIO Core and internet for pinned third-party packages):',
        "  PowerShell: `$env:CROSSINK_RELEASE_VERSION='$Version'; pio run -e default",
        '  Output    : .pio/build/default/firmware-x3-x4.bin (firmware.bin is the same image)',
        '',
        'No game/story files are included. See README.md, FROTZX3_LICENSE.md and THIRD_PARTY_NOTICES.md.')
    $manifest = ($lines -join "`n") + "`n"
    [IO.File]::WriteAllText((Join-Path $root 'SOURCE_BUNDLE_MANIFEST.txt'), $manifest, (New-Object Text.UTF8Encoding $false))

    # 6. Deterministic ZIP ---------------------------------------------------
    Add-Type -AssemblyName System.IO.Compression
    $stamp = [DateTimeOffset]::FromUnixTimeSeconds([long](Invoke-Git $repo @('log', '-1', '--format=%ct')).Trim())
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    $files = Get-ChildItem $root -Recurse -Force -File |
        ForEach-Object { [pscustomobject]@{ Full = $_.FullName; Rel = $_.FullName.Substring($stage.Length + 1).Replace('\', '/') } } |
        Sort-Object Rel -Culture ([cultureinfo]::InvariantCulture) -CaseSensitive
    $fs = [IO.File]::Create($zipPath)
    try {
        $zip = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($f in $files) {
                $entry = $zip.CreateEntry($f.Rel, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $stamp
                $in = [IO.File]::OpenRead($f.Full)
                $out = $entry.Open()
                try { $in.CopyTo($out) } finally { $out.Dispose(); $in.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $fs.Dispose() }

    $hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host ''
    Write-Host "Files in bundle : $($files.Count)"
    Write-Host "Release commit  : $commit"
    Write-Host "Output          : $zipPath"
    Write-Host "SHA-256         : $hash"
}
finally {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
}
