<#
.SYNOPSIS
  Generates a FrotzX3 patch package (manifest + git patch + overlay files).

.DESCRIPTION
  Compares an official CrossInk commit (-BaseCommit) with a FrotzX3 source commit
  (-SourceCommit) and writes tools/patches/v<Version>/:

    manifest.json     what the package is for, plus SHA-256 values
    patches/*.patch   git patch for upstream CrossInk files FrotzX3 modifies
    files/            FrotzX3-owned files that do not exist upstream (overlay)

  Every file that differs between the two commits must be classified below as
  overlay, patch or excluded. An unclassified file stops the script, so a new
  integration change cannot be forgotten silently.

  Files are read from Git objects (not the working tree), so the output is
  identical regardless of line-ending settings on the machine running this.

.EXAMPLE
  .\New-PatchPackage.ps1 -Version 0.9.0-beta.1 -BaseCommit cab4f249 -SourceCommit HEAD
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$BaseCommit,
    [string]$SourceCommit = 'HEAD',
    [string]$UpstreamUrl = 'https://github.com/uxjulia/CrossInk.git',
    [string]$UpstreamVersionNote = ''
)

$ErrorActionPreference = 'Stop'

# --- Classification -------------------------------------------------------------
# New FrotzX3-owned files copied verbatim into the CrossInk checkout.
$OverlayRoots = @(
    'lib/FrotzX3/',
    'src/activities/frotzx3/',
    'scripts/pin_idf_components.py',
    'LICENSES/',
    'FROTZX3_README.md',
    'FROTZX3_INTEGRATION.md',
    'FROTZX3_LICENSE.md',
    'THIRD_PARTY_NOTICES.md'
)
# Existing CrossInk files FrotzX3 modifies (applied as a git patch).
$PatchFiles = @(
    'platformio.ini',
    'src/MappedInputManager.cpp',
    'src/activities/home/HomeActivity.cpp'
)
# Differences that are intentionally NOT part of the portable package.
$ExcludedRoots = @(
    'README.md',                      # this project's own landing page
    'CHANGELOG.md',                   # this project's own changelog
    '.gitignore',                     # local convenience entries only
    'FROTZX3_INSTALLER_README.md',    # legacy transplant installer
    'Install-FrotzX3.cmd',            # legacy transplant installer
    'tools/'                          # release/installer/patch tooling itself
)

function Invoke-Git {
    param([string[]]$GitArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git @GitArgs 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $prev }
    if ($code -ne 0) { throw "git $($GitArgs -join ' ') failed ($code): $($out -join "`n")" }
    return $out
}
function Test-UnderRoots([string]$Path, [string[]]$Roots) {
    foreach ($r in $Roots) {
        if ($r.EndsWith('/')) { if ($Path.StartsWith($r)) { return $true } }
        elseif ($Path -eq $r) { return $true }
    }
    return $false
}
function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

$repoRoot = (Invoke-Git @('-C', $PSScriptRoot, 'rev-parse', '--show-toplevel') | Select-Object -First 1).Trim()
Set-Location -LiteralPath $repoRoot
$base = (Invoke-Git @('rev-parse', '--verify', "$BaseCommit^{commit}") | Select-Object -First 1).Trim()
$src  = (Invoke-Git @('rev-parse', '--verify', "$SourceCommit^{commit}") | Select-Object -First 1).Trim()
Invoke-Git @('merge-base', '--is-ancestor', $base, $src) | Out-Null   # throws unless base is an ancestor

$pkgDir = Join-Path $PSScriptRoot "v$Version"
$filesDir = Join-Path $pkgDir 'files'
$patchDir = Join-Path $pkgDir 'patches'
if (Test-Path -LiteralPath $filesDir) { Remove-Item -LiteralPath $filesDir -Recurse -Force }
if (Test-Path -LiteralPath $patchDir) { Remove-Item -LiteralPath $patchDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $filesDir, $patchDir | Out-Null

# --- Classify every changed file ------------------------------------------------
$overlay = New-Object System.Collections.Generic.List[string]
$patched = New-Object System.Collections.Generic.List[string]
$excluded = New-Object System.Collections.Generic.List[string]
$problems = New-Object System.Collections.Generic.List[string]
foreach ($line in (Invoke-Git @('diff', '--name-status', '--no-renames', $base, $src))) {
    if (-not $line) { continue }
    $status, $path = $line -split "`t", 2
    if (Test-UnderRoots $path $OverlayRoots) {
        if ($status -eq 'A') { $overlay.Add($path) } else { $problems.Add("$path has status $status but overlay files must be new (A)") }
    } elseif (Test-UnderRoots $path $PatchFiles) {
        if ($status -eq 'M') { $patched.Add($path) } else { $problems.Add("$path has status $status but patched files must be modified (M)") }
    } elseif (Test-UnderRoots $path $ExcludedRoots) {
        $excluded.Add($path)
    } else {
        $problems.Add("$path ($status) is not classified; add it to OverlayRoots, PatchFiles or ExcludedRoots")
    }
}
foreach ($p in $PatchFiles) { if (-not $patched.Contains($p)) { $problems.Add("$p is listed as patched but is unchanged between the commits") } }
if ($problems.Count -gt 0) { throw ("Cannot build package:`n  " + ($problems -join "`n  ")) }

# --- Overlay: export exact committed bytes --------------------------------------
$zip = Join-Path ([IO.Path]::GetTempPath()) ("frotzx3-overlay-{0}.zip" -f [Guid]::NewGuid().ToString('N'))
try {
    $archiveArgs = @('-c', 'core.autocrlf=false', 'archive', '--format=zip', "--output=$zip", $src, '--') + $overlay.ToArray()
    Invoke-Git $archiveArgs | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $filesDir)
} finally { if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force } }

$overlayEntries = @()
foreach ($p in ($overlay | Sort-Object)) {
    $f = Join-Path $filesDir ($p -replace '/', [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $f)) { throw "Overlay file missing after export: $p" }
    $overlayEntries += [ordered]@{ path = $p; sha256 = (Get-Sha256 $f); bytes = (Get-Item -LiteralPath $f).Length }
}

# --- Patch: modified upstream files ---------------------------------------------
$patchName = '0001-frotzx3-host-integration.patch'
$patchFile = Join-Path $patchDir $patchName
$diffArgs = @('diff', '--binary', '--full-index', '--no-renames', "--output=$patchFile", $base, $src, '--') + $patched.ToArray()
Invoke-Git $diffArgs | Out-Null

$expected = @()
foreach ($p in ($patched | Sort-Object)) {
    $blob = (Invoke-Git @('rev-parse', "$src`:$p") | Select-Object -First 1).Trim()
    $expected += [ordered]@{ path = $p; git_blob = $blob }
}

# --- Version facts ---------------------------------------------------------------
$sub = (Invoke-Git @('ls-tree', $base, 'freeink-sdk') | Select-Object -First 1)
$subSha = ($sub -split '\s+')[2]
$baseSubject = (Invoke-Git @('log', '-1', '--format=%s', $base) | Select-Object -First 1).Trim()
$baseDate = (Invoke-Git @('log', '-1', '--format=%cs', $base) | Select-Object -First 1).Trim()

$manifest = [ordered]@{
    schema_version     = 1
    frotzx3_version    = $Version
    frotzx3_source_commit = $src
    frotzx3_source_note = 'Commit whose FrotzX3-owned files and host edits this package reproduces. Excluded paths (README.md, CHANGELOG.md, .gitignore, tools/, legacy installer) are intentionally not part of the package.'
    upstream           = [ordered]@{
        repository = $UpstreamUrl
        commit     = $base
        commit_subject = $baseSubject
        commit_date = $baseDate
        version_note = $UpstreamVersionNote
    }
    required_submodules = @([ordered]@{
        path = 'freeink-sdk'
        commit = $subSha
        url = 'https://github.com/Free-Ink/freeink-sdk.git'
        recursive = $true
    })
    build = [ordered]@{
        platformio_environment = 'default'
        release_version_env = [ordered]@{ CROSSINK_RELEASE_VERSION = $Version }
        unset_env = @('CROSSINK_RC_HASH')
        firmware_output = '.pio/build/default/firmware-x3-x4.bin'
        output_name = "FrotzX3-v$Version-firmware-x3-x4.bin"
        expected_mdns_version = '1.14.0'
    }
    patches = @([ordered]@{
        path = "patches/$patchName"
        sha256 = (Get-Sha256 $patchFile)
        modifies = @($patched | Sort-Object)
    })
    expected_results = $expected
    overlay_files = $overlayEntries
}
$json = $manifest | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $pkgDir 'manifest.json'), ($json -replace "`r`n", "`n") + "`n", (New-Object Text.UTF8Encoding($false)))

Write-Host "Package:   $pkgDir"
Write-Host "Upstream:  $base"
Write-Host "Source:    $src"
Write-Host ("Overlay:   {0} files" -f $overlayEntries.Count)
Write-Host ("Patched:   {0}" -f ($patched -join ', '))
Write-Host ("Excluded:  {0} files" -f $excluded.Count)
Write-Host "Patch SHA: $($manifest.patches[0].sha256)"
