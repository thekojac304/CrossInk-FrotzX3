<#
.SYNOPSIS
  Builds the complete-source ZIP for a FrotzX3 firmware that was built by the patch installer.

.DESCRIPTION
  The FrotzX3 firmware is built from official CrossInk plus the FrotzX3 patch package, not from the
  root tree of this repository. GitHub's automatic source archives therefore contain neither the
  CrossInk base nor its submodules. This script packages the tree that was actually built:

    <name>/                      official CrossInk at the supported commit with FrotzX3 applied,
                                 plus the full source of every (nested) submodule
    <name>/frotzx3-build-tools/  the installer, the patch package, the compatibility list and the
                                 tests/tools needed to reproduce that tree
    <name>/SOURCE_BUNDLE_MANIFEST.txt   exact revisions, build command, checks performed

  Point -SourceTree at the temporary folder the installer built in (run the installer with
  -KeepWorkDir, or -SkipBuild, to keep it). Before exporting, the script verifies that the tree is
  exactly what the package describes: HEAD is the supported CrossInk commit, submodules are clean and at
  their recorded commits, every patched file matches its recorded git blob, every overlay file matches
  its SHA-256, and no other file differs. Files are exported from the working tree (LF bytes; the
  installer's checkout has core.autocrlf=false); .git data, build output and IDE/cache folders are
  never copied, and the staged tree is scanned for binaries, story files, saves and credentials.
  ZIP entries use a fixed timestamp and sorted order, so the same inputs give the same SHA-256.

  Nothing is pushed, tagged or published. The repository holding this script must be clean.

.PARAMETER SourceTree
  The folder the installer built in (contains the patched CrossInk tree, optionally with .pio).

.PARAMETER OutputDir
  Where the ZIP is written. Default: <repo>\dist-publish (git-ignored).

.PARAMETER FirmwarePath
  Optional: the firmware .bin this source produced; its SHA-256 and size are recorded in the manifest.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\release\New-InstallerSourceBundle.ps1 -SourceTree C:\fxi\work
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceTree,
    [string]$OutputDir = '',
    [string]$FirmwarePath = ''
)

$ErrorActionPreference = 'Stop'
$installerDir = Join-Path $PSScriptRoot '..\installer'
. (Join-Path $installerDir 'FrotzX3.Common.ps1')

function Fail([string]$Message) { throw $Message }
function Git([string]$Dir, [string[]]$GitArgs) {
    $r = Invoke-FxGit -Dir $Dir -GitArgs $GitArgs
    if ($r.ExitCode -ne 0) { Fail "git $($GitArgs -join ' ') failed in ${Dir}:`n$($r.Output -join "`n")" }
    return $r.Output
}

$repo = Get-FxRepoRoot
$SourceTree = (Resolve-Path -LiteralPath $SourceTree).Path
if (-not $OutputDir) { $OutputDir = Join-Path $repo 'dist-publish' }

# 1. This repository (the installer and package) must be clean and its package intact ----------------
if (Git $repo @('status', '--porcelain')) { Fail 'The FrotzX3 repository working tree is not clean; commit or stash first.' }
$repoCommit = (Git $repo @('rev-parse', 'HEAD') | Select-Object -First 1).Trim()
$compat = Get-FxCompatibility
$release = Select-FxRelease -Compat $compat -Version '' -Target ''
$packageDir = Join-Path $repo ($release.patch_path -replace '/', '\')
$manifest = Read-FxPackage -PackageDir $packageDir
Test-FxPackageIntegrity -PackageDir $packageDir -Manifest $manifest
$version = [string]$manifest.frotzx3_version
$name = "FrotzX3-v$version-source-complete"
$zipPath = Join-Path $OutputDir "$name.zip"

# 2. The source tree must be exactly base + package ---------------------------------------------------
$head = (Git $SourceTree @('rev-parse', 'HEAD') | Select-Object -First 1).Trim()
if ($head -ne $manifest.upstream.commit) { Fail "SourceTree HEAD is $head, expected $($manifest.upstream.commit)." }
Test-FxSubmoduleCommits -Dest $SourceTree -Manifest $manifest
$subDirty = Git $SourceTree @('submodule', 'foreach', '--quiet', '--recursive', 'git status --porcelain')
if ($subDirty) { Fail "A submodule is not clean:`n$($subDirty -join "`n")" }

$expectedPaths = @{}
foreach ($e in @($manifest.expected_results)) {
    $expectedPaths[$e.path] = 'M'
    $blob = (Git $SourceTree @('hash-object', '--no-filters', ($e.path -replace '/', '\')) | Select-Object -First 1).Trim()
    if ($blob -ne $e.git_blob) { Fail "$($e.path) does not match the recorded result of the package." }
}
foreach ($o in @($manifest.overlay_files)) {
    $expectedPaths[$o.path] = '?'
    $f = Join-Path $SourceTree ($o.path -replace '/', '\')
    if (-not (Test-Path -LiteralPath $f)) { Fail "Overlay file missing from SourceTree: $($o.path)" }
    if ((Get-FxSha256 $f) -ne $o.sha256) { Fail "Overlay file differs from the package: $($o.path)" }
}
# Overlay files are untracked; list individual files (not collapsed directories) and compare.
foreach ($line in (Git $SourceTree @('status', '--porcelain', '--untracked-files=all', '--ignore-submodules=none'))) {
    if ($line.Length -lt 4) { continue }
    $code = $line.Substring(0, 2).Trim(); $path = $line.Substring(3).Trim('"')
    $kind = if ($code -eq '??') { '?' } else { 'M' }
    if (-not $expectedPaths.ContainsKey($path) -or $expectedPaths[$path] -ne $kind) {
        Fail "SourceTree differs from base + package at: $line"
    }
}
Write-Host "Source tree verified: $head + package $($release.patch_path) (no other differences)"

# Tree identity: hash of the exact source state (temporary index, nothing in the tree is changed).
$idx = Join-Path ([IO.Path]::GetTempPath()) ("fxidx-" + [Guid]::NewGuid().ToString('N'))
$treeHash = ''
try {
    $env:GIT_INDEX_FILE = $idx
    Git $SourceTree @('add', '-A') | Out-Null
    $treeHash = (Git $SourceTree @('write-tree') | Select-Object -First 1).Trim()
} finally {
    Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $idx) { Remove-Item -LiteralPath $idx -Force }
}

# 3. Collect files: tracked (incl. submodules, from the working tree) + overlay ---------------------
$rel = New-Object System.Collections.Generic.HashSet[string]
foreach ($l in (Git $SourceTree @('ls-files', '--recurse-submodules'))) { if ($l) { [void]$rel.Add($l.Trim('"')) } }
foreach ($o in @($manifest.overlay_files)) { [void]$rel.Add($o.path) }
$subs = @()
foreach ($l in (Git $SourceTree @('submodule', 'status', '--recursive'))) {
    if ($l -notmatch '^(.)([0-9a-f]{40}) (\S+)') { Fail "Unparseable submodule status: $l" }
    if ($Matches[1] -ne ' ') { Fail "Submodule $($Matches[3]) is not at its recorded commit." }
    $subs += @{ Path = $Matches[3]; Sha = $Matches[2] }
}
if (-not $subs) { Fail 'No submodules found; refusing to build an incomplete bundle.' }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stage = Join-Path ([IO.Path]::GetTempPath()) ("frotzx3-src-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$root = Join-Path $stage $name
New-Item -ItemType Directory -Path $root | Out-Null
try {
    foreach ($r in ($rel | Sort-Object)) {
        $src = Join-Path $SourceTree ($r -replace '/', '\')
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { continue }   # e.g. a gitlink path
        $dst = Join-Path $root ($r -replace '/', '\')
        $dir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst
    }

    # Build tools from the committed (LF) state of this repository.
    $toolPaths = @('tools/installer', 'tools/patches', 'tools/release', 'tools/tests', 'FROTZX3_INSTALLER_README.md')
    $toolPaths = @($toolPaths | Where-Object { Test-Path -LiteralPath (Join-Path $repo $_) })
    $tmpZip = Join-Path $stage 'tools.zip'
    $a = Git $repo (@('archive', '--format=zip', '-o', $tmpZip, 'HEAD', '--') + $toolPaths)
    $toolsDir = Join-Path $root 'frotzx3-build-tools'
    [IO.Compression.ZipFile]::ExtractToDirectory($tmpZip, $toolsDir)
    Remove-Item -LiteralPath $tmpZip
    # Only the current package is needed; older packages belong to other CrossInk bases and stay in the repository.
    $patchRoot = Join-Path $toolsDir 'tools\patches'
    foreach ($d in (Get-ChildItem -LiteralPath $patchRoot -Directory)) {
        if ($d.Name -ne (Split-Path -Leaf $packageDir)) { Remove-Item -LiteralPath $d.FullName -Recurse -Force }
    }

    # 4. Strip anything that must not ship; fail if forbidden files remain -------------------------
    $junkDirs = '.git', '.pio', '.vscode', '.idea', '__pycache__', 'node_modules', 'managed_components', '.dummy', 'dist-installer', 'dist-publish', 'fs_'
    Get-ChildItem $root -Recurse -Force -Directory | Where-Object { $junkDirs -contains $_.Name } |
        Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
            if (Test-Path $_.FullName) { Remove-Item $_.FullName -Recurse -Force }
        }
    $gitFiles = @(Get-ChildItem $root -Recurse -Force -File | Where-Object { $_.Name -eq '.git' })
    $gitFiles | Remove-Item -Force   # submodule gitlink files
    $forbidden = '*.bin', '*.elf', '*.map', '*.o', '*.a', '*.sav', '*.recovery', '*.zblorb', '*.blb', '*.blorb', '*.dblite', '*.pem', '*.key', '*.p12', '.env', '*.log'
    $forbidden += 0..8 | ForEach-Object { "*.z$_" }
    $bad = @(Get-ChildItem $root -Recurse -Force -File -Include $forbidden)
    if ($bad) { Fail "Forbidden files in staged tree:`n$(($bad | ForEach-Object { $_.FullName.Substring($root.Length) }) -join "`n")" }

    # 5. Manifest ---------------------------------------------------------------------------------------
    $lines = @("FrotzX3 v$version - complete source bundle", '',
        "Built from official CrossInk: $($manifest.upstream.repository)",
        "CrossInk commit (tag $($manifest.upstream.tag)): $($manifest.upstream.commit)",
        "FrotzX3 patch package: $($release.patch_path) (frotzx3-build-tools/$($release.patch_path))",
        "Source tree identity (git tree hash, base + package, submodules as gitlinks): $treeHash",
        "FrotzX3 repository commit that holds the installer and package: $repoCommit")
    foreach ($s in $subs) { $lines += "Submodule $($s.Path): $($s.Sha)" }
    if ($FirmwarePath) {
        $fp = (Resolve-Path -LiteralPath $FirmwarePath).Path
        $lines += @('', "Firmware built from this source: $(Split-Path -Leaf $fp)",
            "  SHA-256: $(Get-FxSha256 $fp)", "  Size:    $((Get-Item -LiteralPath $fp).Length) bytes")
    }
    $lines += @('',
        'Verified before packaging: HEAD equals the CrossInk commit above; submodules are clean and at the',
        'recorded commits; patched files and overlay files equal the package results; no other file differs.',
        '',
        'Rebuild (needs Git, PlatformIO Core and internet for pinned third-party packages), either:',
        '  1. Run frotzx3-build-tools\tools\installer\Install-FrotzX3.ps1 from a FrotzX3 repository checkout; it',
        '     downloads CrossInk at the commit above and applies the package; or',
        '  2. Build this tree directly:',
        "       PowerShell: `$env:CROSSINK_RELEASE_VERSION='$($manifest.build.release_version_env.CROSSINK_RELEASE_VERSION)'; Remove-Item Env:CROSSINK_RC_HASH -ErrorAction SilentlyContinue; pio run -e $($manifest.build.platformio_environment)",
        "       Output: $($manifest.build.firmware_output)",
        '     Use a short path (for example C:\fx3\src); set PYTHONUTF8=1 if redirecting PlatformIO output.',
        'Rebuilds are not byte-identical (build-time strings are embedded); compare source revisions.',
        '',
        'No game/story files are included. See FROTZX3_README.md, FROTZX3_LICENSE.md, THIRD_PARTY_NOTICES.md and LICENSES/.')
    [IO.File]::WriteAllText((Join-Path $root 'SOURCE_BUNDLE_MANIFEST.txt'), (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))

    # 6. Deterministic ZIP --------------------------------------------------------------------------------
    $stamp = [DateTimeOffset]::FromUnixTimeSeconds([long](Git $repo @('log', '-1', '--format=%ct') | Select-Object -First 1).Trim())
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

    Write-Host ''
    Write-Host "Files in bundle : $($files.Count)"
    Write-Host "Source tree     : $treeHash"
    Write-Host "Output          : $zipPath"
    Write-Host "SHA-256         : $(Get-FxSha256 $zipPath)"
}
finally {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue }
}
