<#
.SYNOPSIS
  EXPERIMENTAL: builds FrotzX3 firmware from official CrossInk source plus the FrotzX3 patch package.

.DESCRIPTION
  1. Downloads official CrossInk (https://github.com/uxjulia/CrossInk) into a temporary folder.
  2. Checks out the exact CrossInk commit this FrotzX3 release was built against.
  3. Downloads CrossInk's required components (submodules) and verifies their commits.
  4. Applies the FrotzX3 patch package and verifies every changed file.
  5. Builds the firmware with PlatformIO (pio run -e default).
  6. Copies the firmware .bin to dist-installer\ and prints its SHA-256.

  It never flashes a device. Your own folders are not modified; all work happens in a
  temporary folder that is deleted on success.

  This installer has NOT been hardware-tested as an install method. For the
  hardware-tested firmware, use the prebuilt .bin from the GitHub release.

.PARAMETER FrotzX3Version
  FrotzX3 version to build. Default: newest entry in compatibility.json.

.PARAMETER TargetCrossInkVersion
  Opt in to a FrotzX3 package built for a specific, NON-default CrossInk release
  (for example 1.6.1). Such targets are build-tested but not hardware-tested and are never
  selected unless you pass this parameter. The value must exactly match a "crossink_target"
  in compatibility.json; the nearest version is never substituted.

.PARAMETER OutputDir
  Where the firmware and log are written. Default: <repository>\dist-installer

.PARAMETER WorkDir
  Temporary build folder (must not exist). Default: a new folder under %TEMP%.
  Keep this path short; very long paths can break the ESP-IDF build on Windows.

.PARAMETER CheckOnly
  Download and verify CrossInk and test that the patch applies, but do not apply or build.

.PARAMETER SkipBuild
  Apply FrotzX3 and verify it, but do not build. Implies -KeepWorkDir.

.PARAMETER KeepWorkDir
  Do not delete the temporary folder when finished.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\installer\Install-FrotzX3.ps1
#>
[CmdletBinding()]
param(
    [string]$FrotzX3Version = '',
    [string]$TargetCrossInkVersion = '',
    [string]$OutputDir = '',
    [string]$WorkDir = '',
    [switch]$CheckOnly,
    [switch]$SkipBuild,
    [switch]$KeepWorkDir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'FrotzX3.Common.ps1')

$repoRoot = Get-FxRepoRoot
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot 'dist-installer' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $WorkDir) { $WorkDir = Join-Path ([IO.Path]::GetTempPath()) "FrotzX3-build-$stamp" }
if ($SkipBuild) { $KeepWorkDir = $true }

$succeeded = $false
$exitCode = 1
try {
    Initialize-FxLog -Path (Join-Path $OutputDir "FrotzX3-install-$stamp.log")
    Write-Host ''
    Write-Host 'FrotzX3 patch installer  (EXPERIMENTAL - builds a firmware file only; never flashes)' -ForegroundColor Yellow
    Write-Host 'For the hardware-tested firmware, use the prebuilt .bin from the GitHub release instead.'

    Write-FxStep 'Selecting FrotzX3 release'
    $compat = Get-FxCompatibility
    $release = Select-FxRelease -Compat $compat -Version $FrotzX3Version -Target $TargetCrossInkVersion
    $packageDir = Join-Path $repoRoot ($release.patch_path -replace '/', '\')
    $manifest = Read-FxPackage -PackageDir $packageDir
    Write-FxInfo "FrotzX3 version:        $($manifest.frotzx3_version)"
    Write-FxInfo "Official CrossInk repo: $($manifest.upstream.repository)"
    Write-FxInfo "CrossInk commit:        $($manifest.upstream.commit)"
    Write-FxInfo "Status in compatibility list: $($release.status)"
    if ($release.status -ne 'tested') {
        Write-Host '   NOTE: this installer path has not been hardware-tested; treat the result as experimental.' -ForegroundColor Yellow
    }
    if ($TargetCrossInkVersion -and $release.status -ne 'tested') {
        Write-Host "   OPT-IN TARGET: CrossInk $TargetCrossInkVersion. Package status '$($release.status)': it compiles, but it has NOT been flashed or tested on an X3." -ForegroundColor Yellow
        Write-Host '   Do not distribute the resulting firmware as a FrotzX3 release.' -ForegroundColor Yellow
    }
    if ($manifest.frotzx3_version -ne $release.frotzx3_version -or $manifest.upstream.commit -ne $release.crossink_commit) {
        Stop-Fx -Title 'compatibility.json and the patch package disagree.' `
            -Details @("compatibility.json: FrotzX3 $($release.frotzx3_version) on $($release.crossink_commit)",
                "manifest.json:       FrotzX3 $($manifest.frotzx3_version) on $($manifest.upstream.commit)") `
            -NextStep 'Nothing was changed. Re-download FrotzX3.'
    }

    Test-FxPrerequisites -NeedBuildTools:(-not ($CheckOnly -or $SkipBuild))
    Test-FxPackageIntegrity -PackageDir $packageDir -Manifest $manifest

    if (Test-Path -LiteralPath $WorkDir) {
        Stop-Fx -Title 'The temporary build folder already exists.' -Details @($WorkDir) -NextStep 'Choose a new -WorkDir, or delete that folder first.'
    }
    $free = Get-FxFreeSpaceGb -Path ([IO.Path]::GetTempPath())
    if ($free -ne $null -and $free -lt 6) {
        Write-Host "   WARNING: only $free GB free on the build drive; a first build needs roughly 6 GB." -ForegroundColor Yellow
    }

    Write-FxStep 'Downloading official CrossInk'
    Write-FxInfo "Build folder: $WorkDir"
    $sha = New-FxUpstreamSource -Url $manifest.upstream.repository -Ref $manifest.upstream.commit -Dest $WorkDir
    if ($sha -ne $manifest.upstream.commit) {
        Stop-Fx -Title 'FrotzX3 could not be applied to this CrossInk source.' `
            -Details @("Expected CrossInk commit: $($manifest.upstream.commit)", "Found:                  $sha") `
            -NextStep 'No files were changed.'
    }
    Write-FxOk "CrossInk commit verified: $sha"

    Write-FxStep 'Downloading CrossInk components (submodules)'
    Initialize-FxSubmodules -Dest $WorkDir
    Test-FxSubmoduleCommits -Dest $WorkDir -Manifest $manifest

    $dirty = (Invoke-FxGit -Dir $WorkDir -GitArgs @('status', '--porcelain', '--ignore-submodules=none')).Output
    if (@($dirty | Where-Object { $_ }).Count -gt 0) {
        Stop-Fx -Title 'The downloaded CrossInk source is not clean.' -Details @($dirty | Select-Object -First 5) -NextStep 'No files were changed in your own folders.'
    }

    Write-FxStep 'Checking that FrotzX3 fits this CrossInk source'
    $check = Test-FxPackageApplies -Dest $WorkDir -PackageDir $packageDir -Manifest $manifest
    if (-not $check.Clean) {
        Stop-Fx -Title 'FrotzX3 could not be applied to this CrossInk source.' `
            -Details (@("Expected CrossInk commit: $($manifest.upstream.commit)", "Found:                  $sha", 'Problems:') + ($check.Problems | ForEach-Object { "  - $_" })) `
            -NextStep 'No files were changed.'
    }
    Write-FxOk 'The FrotzX3 patch applies cleanly'

    if ($CheckOnly) {
        Write-FxStep 'Check-only run finished'
        Write-FxInfo 'Everything verified; nothing was applied or built (-CheckOnly).'
        $succeeded = $true; $exitCode = 0
        return
    }

    Write-FxStep 'Applying FrotzX3'
    Install-FxPackage -Dest $WorkDir -PackageDir $packageDir -Manifest $manifest

    if ($SkipBuild) {
        Write-FxStep 'Skipping build (-SkipBuild)'
        Write-FxInfo "FrotzX3 is applied in: $WorkDir"
        $succeeded = $true; $exitCode = 0
        return
    }

    Write-FxStep 'Building firmware'
    $build = Invoke-FxBuild -Dest $WorkDir -Manifest $manifest
    Write-FxOk 'Build succeeded'
    Write-FxInfo $build.Ram
    Write-FxInfo $build.Flash

    Write-FxStep 'Verifying firmware'
    $mdns = Test-FxBuildResult -Dest $WorkDir -Manifest $manifest -Firmware $build.Firmware

    $out = Copy-FxFirmwareOutput -Firmware $build.Firmware -OutputDir $OutputDir -Manifest $manifest
    $hash = Get-FxSha256 $out
    $size = (Get-Item -LiteralPath $out).Length

    Write-Host ''
    Write-Host '==================== FrotzX3 build complete ====================' -ForegroundColor Green
    Write-Host "CrossInk base:    $($manifest.upstream.repository) @ $sha"
    Write-Host "FrotzX3 version:  $($manifest.frotzx3_version)"
    Write-Host "espressif/mdns:   $mdns"
    Write-Host "Firmware:         $out"
    Write-Host "Size:             $size bytes"
    Write-Host "SHA-256:          $hash"
    Write-Host "Log:              $($script:FxLogPath)"
    Write-Host ''
    Write-Host 'This file was NOT flashed. To install it, use CrossInk''s documented SD-card'
    Write-Host 'firmware update (Settings > System > SD Card Firmware Update) or a USB flasher.'
    Write-Host 'This build path is experimental; the prebuilt release firmware is the hardware-tested one.'
    Write-FxLog "RESULT: firmware=$out sha256=$hash size=$size ram='$($build.Ram)' flash='$($build.Flash)' mdns=$mdns crossink=$sha"
    $succeeded = $true; $exitCode = 0
}
catch {
    if ($_.Exception.Message -ne 'FXFAIL') {
        Write-Host ''
        Write-Host 'FAILED: An unexpected problem occurred.' -ForegroundColor Red
        Write-Host "  $($_.Exception.Message)"
        Write-FxLog "UNEXPECTED: $($_ | Out-String)"
        if ($script:FxLogPath) { Write-Host "  Detailed log: $($script:FxLogPath)" }
    }
    $exitCode = 1
}
finally {
    if ($succeeded -and -not $KeepWorkDir) { Remove-FxWorkDir -Path $WorkDir }
    elseif (-not $succeeded -and (Test-Path -LiteralPath $WorkDir)) { Write-Host "  Temporary build folder kept for inspection: $WorkDir" }
    Close-FxLog
    exit $exitCode   # in finally so early 'return' paths also report the right code
}
