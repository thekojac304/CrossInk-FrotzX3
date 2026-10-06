<#
.SYNOPSIS
  Tests whether the FrotzX3 patch package applies to a candidate CrossInk commit.

.DESCRIPTION
  Fetches the candidate CrossInk commit, tag or branch into a temporary folder and
  checks whether a FrotzX3 patch package applies to it.

  The package is chosen by EXACT manifest match: the candidate's full commit SHA must equal
  a package's upstream.commit. A package is never picked because its CrossInk version is
  "nearest". To test a specific package against a different commit on purpose (for
  example while porting), name it with -Target or -FrotzX3Version; the result is then
  MISMATCH, with an informational apply check, and the exit code is never 0.

    CLEAN     exact manifest match, and the patch and overlay files apply without conflicts
    CONFLICT  exact manifest match, but something no longer applies; problems are listed
    MISMATCH  no package targets this exact commit (or the named package is for another one)

  With -Build, a CLEAN result is also applied and compiled.

  IMPORTANT: CLEAN and even a successful build do NOT mean the CrossInk version is
  supported. Support is only declared after regression and X3 hardware testing and an
  explicit entry in compatibility.json (see UPDATING_CROSSINK.md). This script never
  edits compatibility.json.

  Exit codes: 0 = CLEAN, 2 = CONFLICT or MISMATCH, 1 = error (could not run the test),
  3 = CLEAN but the -Build failed.

.PARAMETER CrossInkRef
  Candidate CrossInk commit SHA, tag (for example v1.6.1) or branch.

.PARAMETER FrotzX3Version
  Restrict to a FrotzX3 version. With -Target or alone this explicitly names the package.

.PARAMETER Target
  Explicitly name the package by its "crossink_target" in compatibility.json (for example
  1.6.1). Required exact match; an explicit package whose base commit differs from the
  candidate yields MISMATCH.

.PARAMETER Build
  If CLEAN, also apply the package and build it (slow; needs PlatformIO).

.EXAMPLE
  .\Test-FrotzX3Compatibility.ps1 -CrossInkRef v1.6.1                 # CLEAN via the v1.6.1 package
  .\Test-FrotzX3Compatibility.ps1 -CrossInkRef cab4f249                # CLEAN via the old-base package
  .\Test-FrotzX3Compatibility.ps1 -CrossInkRef v1.6.1 -FrotzX3Version 0.9.0-beta.1   # old package vs v1.6.1 -> MISMATCH
  .\Test-FrotzX3Compatibility.ps1 -CrossInkRef 9914146e -Build
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CrossInkRef,
    [string]$FrotzX3Version = '',
    [string]$Target = '',
    [string]$WorkDir = '',
    [switch]$Build,
    [switch]$KeepWorkDir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'FrotzX3.Common.ps1')

$repoRoot = Get-FxRepoRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $WorkDir) { $WorkDir = Join-Path ([IO.Path]::GetTempPath()) "FrotzX3-compat-$stamp" }
$logDir = Join-Path $repoRoot 'dist-installer'
$result = 'ERROR'
$exitCode = 1
try {
    Initialize-FxLog -Path (Join-Path $logDir "FrotzX3-compat-$stamp.log")
    $compat = Get-FxCompatibility
    Test-FxPrerequisites -NeedBuildTools:$Build

    Write-FxStep "Fetching candidate CrossInk '$CrossInkRef'"
    $sha = New-FxUpstreamSource -Url $compat.upstream_repository -Ref $CrossInkRef -Dest $WorkDir
    Write-FxOk "Candidate commit: $sha"

    Write-FxStep 'Selecting the FrotzX3 package (exact commit match only)'
    $explicit = [bool]($Target -or $FrotzX3Version)
    $release = $null
    if ($explicit) {
        $release = Select-FxRelease -Compat $compat -Version $FrotzX3Version -Target $Target
        Write-FxInfo "Explicitly requested package: $($release.patch_path)"
    } else {
        foreach ($r in @($compat.releases)) {
            $m = Read-FxPackage -PackageDir (Join-Path $repoRoot ($r.patch_path -replace '/', '\'))
            if ($m.upstream.commit -eq $sha) { $release = $r; break }
        }
    }
    if (-not $release) {
        $result = 'MISMATCH'; $exitCode = 2
        Write-Host ''
        Write-Host "RESULT: MISMATCH  (no FrotzX3 package targets CrossInk commit $sha)" -ForegroundColor Red
        Write-Host '  Known package base commits:'
        foreach ($r in @($compat.releases)) {
            $m = Read-FxPackage -PackageDir (Join-Path $repoRoot ($r.patch_path -replace '/', '\'))
            Write-Host "    $($m.upstream.commit)  $($r.patch_path)"
        }
        Write-Host '  The nearest version is never substituted. To probe a specific package against this commit'
        Write-Host '  while porting, pass -Target or -FrotzX3Version.'
        Write-FxLog "RESULT: MISMATCH (no package) $sha"
        return
    }
    $packageDir = Join-Path $repoRoot ($release.patch_path -replace '/', '\')
    $manifest = Read-FxPackage -PackageDir $packageDir
    Test-FxPackageIntegrity -PackageDir $packageDir -Manifest $manifest
    $exact = ($sha -eq $manifest.upstream.commit)
    $verLine = Select-String -LiteralPath (Join-Path $WorkDir 'platformio.ini') -Pattern '^\s*version\s*=' | Select-Object -First 1
    if ($verLine) { Write-FxInfo ("platformio.ini [crossink]: " + $verLine.Line.Trim()) }
    Write-FxInfo "Tested base for this FrotzX3 release: $($manifest.upstream.commit)"
    if ($exact) { Write-FxInfo 'The candidate IS the package base commit (exact match).' }

    Write-FxStep 'Checking component (submodule) pointers'
    $r = Invoke-FxGit -Dir $WorkDir -GitArgs @('ls-tree', 'HEAD', 'freeink-sdk')
    $line = $r.Output | Select-Object -First 1
    $found = if ($line) { ($line -split '\s+')[2] } else { '(missing)' }
    $expectSub = (@($manifest.required_submodules | Where-Object { $_.path -eq 'freeink-sdk' }))[0].commit
    if ($found -eq $expectSub) { Write-FxOk "freeink-sdk unchanged ($($found.Substring(0,10)))" }
    else { Write-FxInfo "freeink-sdk CHANGED: tested $($expectSub.Substring(0,10)), candidate $found  (expect API-level work)" }

    Write-FxStep 'Trying the FrotzX3 patch package'
    $check = Test-FxPackageApplies -Dest $WorkDir -PackageDir $packageDir -Manifest $manifest
    if (-not $exact) {
        $result = 'MISMATCH'; $exitCode = 2
        Write-Host ''
        Write-Host "RESULT: MISMATCH  (package $($release.patch_path) is for CrossInk $($manifest.upstream.commit); candidate is $sha)" -ForegroundColor Red
        if ($check.Clean) { Write-Host '  (informational) the patch hunks would apply textually, but the package is still NOT valid for this commit.' }
        else {
            Write-Host '  (informational) the patch also fails to apply to this commit:'
            foreach ($p in $check.Problems) { Write-Host "    - $p" }
        }
        Write-FxLog "RESULT: MISMATCH package-base=$($manifest.upstream.commit) candidate=$sha clean=$($check.Clean)"
        return
    }
    if (-not $check.Clean) {
        $result = 'CONFLICT'; $exitCode = 2
        Write-Host ''
        Write-Host "RESULT: CONFLICT  (FrotzX3 $($manifest.frotzx3_version) vs CrossInk $sha)" -ForegroundColor Red
        foreach ($p in $check.Problems) { Write-Host "  - $p" }
        Write-Host ''
        Write-Host 'Next: resolve only the listed hunks by hand (see UPDATING_CROSSINK.md), then regenerate the package.'
        Write-FxLog "RESULT: CONFLICT $sha"
        return
    }
    $result = 'CLEAN'; $exitCode = 0
    Write-Host ''
    Write-Host "RESULT: CLEAN  (FrotzX3 $($manifest.frotzx3_version) applies to CrossInk $sha)" -ForegroundColor Green
    Write-FxLog "RESULT: CLEAN $sha"

    if ($Build) {
        Write-FxStep 'Downloading components and building (-Build)'
        Initialize-FxSubmodules -Dest $WorkDir
        Install-FxPackage -Dest $WorkDir -PackageDir $packageDir -Manifest $manifest
        $b = Invoke-FxBuild -Dest $WorkDir -Manifest $manifest
        Write-Host ''
        Write-Host 'BUILD: PASS' -ForegroundColor Green
        Write-Host "  $($b.Ram)"
        Write-Host "  $($b.Flash)"
        Write-FxLog "BUILD: PASS $($b.Ram) | $($b.Flash)"
    }
    Write-Host ''
    if ($release.status -eq 'tested') {
        Write-Host "Package status in compatibility.json: tested." -ForegroundColor Green
    } else {
        Write-Host "NOT HARDWARE-TESTED: package status is '$($release.status)'. A clean apply and a successful build do not make a CrossInk version supported." -ForegroundColor Yellow
        Write-Host 'Run the regression and X3 hardware tests in UPDATING_CROSSINK.md before promoting it in compatibility.json.'
    }
}
catch {
    if ($_.Exception.Message -ne 'FXFAIL') {
        Write-Host "FAILED: unexpected problem: $($_.Exception.Message)" -ForegroundColor Red
        Write-FxLog "UNEXPECTED: $($_ | Out-String)"
    }
    $exitCode = 1
    if ($Build -and $result -eq 'CLEAN') { Write-Host 'BUILD: FAIL' -ForegroundColor Red; $exitCode = 3 }
}
finally {
    if (-not $KeepWorkDir -and $exitCode -ne 3) { Remove-FxWorkDir -Path $WorkDir }
    elseif (Test-Path -LiteralPath $WorkDir) { Write-Host "Work folder kept: $WorkDir" }
    Close-FxLog
    exit $exitCode   # in finally so early 'return' paths also report the right code
}
