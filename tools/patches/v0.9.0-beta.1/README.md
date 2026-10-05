# FrotzX3 0.9.0-beta.1 patch package

Turns official CrossInk into FrotzX3 `0.9.0-beta.1`.

| | |
| --- | --- |
| Patch schema | 1 |
| Official CrossInk | <https://github.com/uxjulia/CrossInk> |
| Supported CrossInk commit | `cab4f24922f05811e7f44be1057f62ea2d978c52` (v1.5.0 + 3 commits) |
| Required submodule | `freeink-sdk` @ `1ff020263cd2202ea79ce3eb811f5ac8489b8cde` (with nested submodules) |
| PlatformIO environment | `default` (XTEINK X3 / X4 image) |
| Release version env | `CROSSINK_RELEASE_VERSION=0.9.0-beta.1`, `CROSSINK_RC_HASH` unset |

## Contents

```text
manifest.json                         versions, SHA-256 of every file, expected results
patches/0001-frotzx3-host-integration.patch
                                      git patch: HomeActivity.cpp, MappedInputManager.cpp, platformio.ini
files/                                new FrotzX3-owned files, copied verbatim (overlay)
```

There are no binaries, story files, saves, local settings or credentials in this
package. See [../FROTZX3_DELTA.md](../FROTZX3_DELTA.md) for why each file is included.

## Applying by hand

From a clean checkout of the supported CrossInk commit (with
`git config core.autocrlf false`):

```powershell
git apply --check --whitespace=nowarn <path>\patches\0001-frotzx3-host-integration.patch
git apply         --whitespace=nowarn <path>\patches\0001-frotzx3-host-integration.patch
Copy-Item -Recurse -Force <path>\files\* .
git submodule update --init --recursive
$env:CROSSINK_RELEASE_VERSION = '0.9.0-beta.1'
pio run -e default
```

The installer (`tools/installer/Install-FrotzX3.ps1`) does exactly this, plus
verification. After applying, each patched file must match the
`expected_results` git blob IDs in `manifest.json`.

## Line endings

`tools/patches/.gitattributes` marks this folder `-text`, so Git never rewrites line
endings and the SHA-256 values stay valid. Apply the patch to a checkout with
`core.autocrlf=false` (the installer does this); on a CRLF checkout the patch will
not apply.

## Regenerating

```powershell
tools\patches\New-PatchPackage.ps1 -Version 0.9.0-beta.1 `
  -BaseCommit cab4f24922f05811e7f44be1057f62ea2d978c52 -SourceCommit 606122d1
```

`606122d1` is the FrotzX3 commit whose files this package reproduces (it is recorded
in `manifest.json` as `frotzx3_source_commit`).
