<#
.SYNOPSIS
  Tests whether the FrotzX3 patch package applies to a candidate CrossInk commit.

.DESCRIPTION
  Fetches the candidate CrossInk commit, tag or branch into a temporary folder and
  checks whether the current FrotzX3 patch package applies to it.

    CLEAN     the patch and overlay files apply without conflicts
    CONFLICT  something no longer applies; the problems are listed

  With -Build, a CLEAN result is also applied and compiled.

  IMPORTANT: CLEAN and even a successful build do NOT mean the CrossInk version is
  supported. Support is only declared after regression and X3 hardware testing and an
  explicit entry in compatibility.json (see UPDATING_CROSSINK.md). This script never
  edits compatibility.json.

  Exit codes: 0 = CLEAN, 2 = CONFLICT, 1 = error (could not run the test).

.PARAMETER CrossInkRef
  Candidate CrossInk commit SHA, tag (for example v1.6.1) or branch.

.PARAMETER FrotzX3Version
  Which FrotzX3 patch package to test. Default: newest in compatibility.json.

.PARAMETER Build
  If CLEAN, also apply the package and build it (slow; needs PlatformIO).

.EXAMPLE
  .\Test-FrotzX3Compatibility.ps1 -CrossInkRef v1.6.1
  .\Test-FrotzX3Compatibility.ps1 -CrossInkRef 9914146e -Build
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CrossInkRef,
    [string]$FrotzX3Version = '',
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
    $release = Select-FxRelease -Compat $compat -Version $FrotzX3Version
    $packageDir = Join-Path $repoRoot ($release.patch_path -replace '/', '\')
    $manifest = Read-FxPackage -PackageDir $packageDir

    Test-FxPrerequisites -NeedBuildTools:$Build
    Test-FxPackageIntegrity -PackageDir $packageDir -Manifest $manifest

    Write-FxStep "Fetching candidate CrossInk '$CrossInkRef'"
    $sha = New-FxUpstreamSource -Url $manifest.upstream.repository -Ref $CrossInkRef -Dest $WorkDir
    Write-FxOk "Candidate commit: $sha"
    $verLine = Select-String -LiteralPath (Join-Path $WorkDir 'platformio.ini') -Pattern '^\s*version\s*=' | Select-Object -First 1
    if ($verLine) { Write-FxInfo ("platformio.ini [crossink]: " + $verLine.Line.Trim()) }
    Write-FxInfo "Tested base for this FrotzX3 release: $($manifest.upstream.commit)"
    if ($sha -eq $manifest.upstream.commit) { Write-FxInfo 'The candidate IS the tested base commit.' }

    Write-FxStep 'Checking component (submodule) pointers'
    $r = Invoke-FxGit -Dir $WorkDir -GitArgs @('ls-tree', 'HEAD', 'freeink-sdk')
    $line = $r.Output | Select-Object -First 1
    $found = if ($line) { ($line -split '\s+')[2] } else { '(missing)' }
    $expectSub = @($manifest.required_submodules)[0].commit
    if ($found -eq $expectSub) { Write-FxOk "freeink-sdk unchanged ($($found.Substring(0,10)))" }
    else { Write-FxInfo "freeink-sdk CHANGED: tested $($expectSub.Substring(0,10)), candidate $found  (expect API-level work)" }

    Write-FxStep 'Trying the FrotzX3 patch package'
    $check = Test-FxPackageApplies -Dest $WorkDir -PackageDir $packageDir -Manifest $manifest
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
        Install-FxPackage -Dest $WorkDir -PackageDir $packageDir -Manifest $manifest -SkipResultCheck:($sha -ne $manifest.upstream.commit)
        $b = Invoke-FxBuild -Dest $WorkDir -Manifest $manifest
        Write-Host ''
        Write-Host 'BUILD: PASS' -ForegroundColor Green
        Write-Host "  $($b.Ram)"
        Write-Host "  $($b.Flash)"
        Write-FxLog "BUILD: PASS $($b.Ram) | $($b.Flash)"
    }
    Write-Host ''
    Write-Host 'NOT SUPPORTED YET: a clean apply and a successful build do not make this CrossInk version supported.' -ForegroundColor Yellow
    Write-Host 'Run the regression and X3 hardware tests in UPDATING_CROSSINK.md before editing compatibility.json.'
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
