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

## Installation - Beta (`v0.9.0-beta.2`)

**Supported CrossInk base:** official CrossInk **v1.6.1**, exact commit
`9914146eeae7b46b300f475a16c32426fc02ec1f`. This FrotzX3 release was
hardware-validated on an XTEINK X3 on that base only. It is not validated on any
other CrossInk release; every future CrossInk release needs its own port and its
own X3 hardware validation before it is marked supported.

**Recommended:** download the prebuilt, hardware-tested firmware
(`FrotzX3-v0.9.0-beta.2-firmware-x3-x4.bin`) from the GitHub release and install
it with CrossInk's documented firmware-update methods. See **Installation** in
the main [README](README.md).

**Alternative:** the patch installer (`tools/installer/Install-FrotzX3.ps1` in the
FrotzX3 project repository) downloads official CrossInk, checks out the exact
supported commit above, applies the FrotzX3 patch package, and builds a firmware
file locally. It defaults to that supported base. Its documentation in the
project repository states its current test status.

[FROTZX3_INTEGRATION.md](FROTZX3_INTEGRATION.md) covers developer integration,
storage layout, and physical-X3 verification.

**No games included.** Supply legally obtained Z-machine story files directly
in `/adventures` on SD. This project grants no redistribution rights for
commercial Infocom games or other stories.

## Story memory requirements

FrotzX3 allocates a story's dynamic memory as one contiguous block, and the
ESP32-C3 has no PSRAM. Before allocating, it checks that the largest allocatable
block is at least the story's dynamic memory plus a 1,024 B margin. If not, it
refuses cleanly with **"Not enough contiguous memory for this story."** instead of
attempting the allocation.

- Hardware-validated through **Lost Pig at 42,554 B** of dynamic memory (the
  largest story validated on the X3), plus Zork and Varicella.
- The Z-machine format allows up to 65,534 B of dynamic memory. This build does
  **not** guarantee that every story up to that theoretical maximum will fit; a
  story that needs more contiguous memory than is available is refused with the
  message above.

## License and attribution

FrotzX3 is built on MIT-licensed CrossInk and includes modified Frotz source
under **GPL-2.0-or-later**. They remain separate upstream projects. CrossInk's
original MIT [LICENSE](LICENSE) is preserved; the combined FrotzX3 distribution
must comply with applicable GPL terms.

Read [FROTZX3_LICENSE.md](FROTZX3_LICENSE.md),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and [LICENSES/](LICENSES/).
Retain these and upstream notices when redistributing. Prebuilt firmware must
have corresponding source availability satisfying applicable GPL terms.
