# FrotzX3 source notices

This directory includes modified Frotz interpreter sources and CrossInk port
integration. Frotz-derived code is GPL-2.0-or-later. Preserve original headers.
See the [licensing guide](../../FROTZX3_LICENSE.md),
[GPL text](../../LICENSES/Frotz-GPL-2.0-or-later.txt), and
[attribution](../../THIRD_PARTY_NOTICES.md).

The 2026-09-10 audit compared interpreter files with local Frotz reference
`fe66e2ef738354f5e2fe37c7c01b1781ef659acd`. Existing copyright/GPL headers were
retained. `fastmem.c`, `frotz.h`, `object.c`, `process.c`, `quetzal.c`, `screen.c`,
and `text.c` differ from that reference and carry dated port notices. The date
records this notice update, not an inferred date for earlier functional changes.
`setup.h` and `unused.h` match the reference and retain their brief comments.
`FrotzGlobals.c` includes globals derived from upstream `main.c`; its upstream
notice is reproduced without alteration.

Port-specific bridge/API files had no upstream license headers to preserve.
Confirm contributor permissions and historical modification dates before release
as described in the licensing guide.

No stories are included. When transplanting this library, also carry the
top-level licensing documents and `LICENSES/`; the installer does not copy
those documents automatically.
