# FrotzX3 licensing

FrotzX3 integrates modified Frotz code into CrossInk. The combined FrotzX3
source/firmware distribution must comply with the applicable GPL terms.
The top-level MIT license alone does not describe this combined distribution.

## Component licenses

- **CrossInk:** MIT. The original [LICENSE](LICENSE), including its
  `Copyright (c) 2025 Dave Allie` notice, is unchanged. An exact copy is in
  [LICENSES/CrossInk-MIT.txt](LICENSES/CrossInk-MIT.txt).
- **Frotz-derived code in `lib/FrotzX3`:** GNU GPL version 2 or, at your option,
  any later version (`GPL-2.0-or-later`). The complete, unmodified GPL version 2
  text is in [LICENSES/Frotz-GPL-2.0-or-later.txt](LICENSES/Frotz-GPL-2.0-or-later.txt).
  The permission to use later versions comes from Frotz's source notices;
  there is no separate "or later" license text.
- **Other dependencies and assets:** retain their own licenses and notices,
  including [FreeInk SDK's license](freeink-sdk/LICENSE) and
  [notices](freeink-sdk/NOTICE), and its nested third-party notices.
  These primary license copies are not an exhaustive dependency inventory.

CrossInk remains a separate MIT-licensed upstream project. This documentation
does not claim ownership of CrossInk or relicense all its independent code.
Original notices remain applicable. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Distributing source or prebuilt firmware

Retain original notices, this guide, `THIRD_PARTY_NOTICES.md`, and `LICENSES/`
with redistributions, together with applicable dependency notices. Modified
Frotz files must carry prominent change notices and dates under GPLv2 section 2(a).

If distributing prebuilt firmware containing FrotzX3, make the complete
corresponding source used to build it available under the applicable GPL terms.
A practical approach is to accompany each binary with its matching source
archive: port modifications, required modules and interfaces, build/install
scripts, configuration, and required dependency or submodule sources. Record
exact revisions and build instructions. Unmodified upstream Frotz or a moving
branch is not the matching source.

GitHub's automatic source archives omit submodule contents. Official FrotzX3
binary releases are therefore paired with a complete-source ZIP,
`FrotzX3-v<version>-source-complete.zip`, created by
`tools/release/New-SourceBundle.ps1` (see `tools/release/README.md`).

Follow GPLv2 section 3 (or the applicable later version) for your distribution
method. If using a written source offer instead of accompanying source, satisfy
its requirements, including duration and eligible recipients; a general promise
is insufficient. The included license controls; see also the
[GNU GPLv2 reference](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).

## Games are separate

No game/story files are included. Users must supply legally obtained Z-machine
story files. These software licenses grant no redistribution rights to commercial
Infocom titles or other third-party stories. Each story's own permissions apply.

## Release review

Before publishing binaries, review all dependency/asset licenses and the source
delivery method for that build. Confirm provenance, historical modification
dates, and contributor permissions for port-specific files previously lacking
explicit notices. Local Frotz reference commit
`fe66e2ef738354f5e2fe37c7c01b1781ef659acd` was used for this notice audit; this is
comparison evidence, not proof every port file originated at that revision.
This guide is not a certification of legal compliance.
