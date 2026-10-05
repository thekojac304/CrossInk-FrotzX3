# FrotzX3

FrotzX3 integrates the Frotz Z-machine interpreter into CrossInk firmware for
XTEINK devices, letting you play text adventures on an e-ink reader.
Documented physical testing is on **XTEINK X3**. Other CrossInk devices are not
thereby verified for FrotzX3; builds or simulator runs alone do not establish
hardware compatibility.

## Features

- Launch from CrossInk Home and choose a story from SD.
- Read text output and enter commands using the on-screen keyboard.
- Resume stories, use three manual save slots, and return to rewind checkpoints.
- Keep stories and saves on SD under `/adventures`.

The picker recognizes `.z3` through `.z8`, listing up to 16 stories. This does
not promise compatibility with every story or every Z-machine feature.

## Installation - Beta (`v0.9.0-beta.1`)

**Recommended:** download the prebuilt, hardware-tested firmware
(`FrotzX3-v0.9.0-beta.1-firmware-x3-x4.bin`) from the GitHub release and install
it with CrossInk's documented firmware-update methods. See **Installation** in
the main [README](README.md).

**Experimental:** the patch installer (`tools/installer/Install-FrotzX3.ps1`)
downloads official CrossInk, checks out the exact supported commit, applies the
FrotzX3 patch package, and builds a firmware file locally. It has not been
hardware-tested as an install method and is not the recommended path yet.
Building from a source checkout also works; see the main README.

[FROTZX3_INTEGRATION.md](FROTZX3_INTEGRATION.md) covers developer integration,
storage layout, and physical-X3 verification.

**No games included.** Supply legally obtained Z-machine story files directly
in `/adventures` on SD. This project grants no redistribution rights for
commercial Infocom games or other stories.

## License and attribution

FrotzX3 is built on MIT-licensed CrossInk and includes modified Frotz source
under **GPL-2.0-or-later**. They remain separate upstream projects. CrossInk's
original MIT [LICENSE](LICENSE) is preserved; the combined FrotzX3 distribution
must comply with applicable GPL terms.

Read [FROTZX3_LICENSE.md](FROTZX3_LICENSE.md),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and [LICENSES/](LICENSES/).
Retain these and upstream notices when redistributing. Prebuilt firmware must
have corresponding source availability satisfying applicable GPL terms.
