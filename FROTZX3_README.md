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

## Install on Windows

Download the complete FrotzX3 source folder, keep it together, and double-click
**Install-FrotzX3.cmd**. The wizard can download a separate CrossInk checkout,
check compatibility, install FrotzX3, and optionally build and flash an XTEINK
X3. Git for Windows is required; building also requires PlatformIO.
Review prompts before approving installation or flashing.

Read the [installer guide](FROTZX3_INSTALLER_README.md) for prerequisites and
troubleshooting, or the
[advanced installer workflow](FROTZX3_INSTALLER_README.md#advanced-command-line-workflow).
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
