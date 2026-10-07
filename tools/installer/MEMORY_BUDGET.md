# FrotzX3 contiguous-memory budget

FrotzX3 keeps a story's whole dynamic memory in RAM as **one contiguous block**
(`init_memory()` in `lib/FrotzX3/src/fastmem.c`). On the ESP32-C3 that block, not total free
heap, is what limits which stories can start. This page records the limits, the runtime guard
and what a new CrossInk base must show before it is marked supported.

## What a story needs

- Dynamic memory runs from byte `$00000` to the byte before the **base of static memory**,
  header word `$0E` (Z-Machine Standard 1.1, section 1.1). Frotz reads that word into
  `h_dynamic_size` (`fastmem.c`, `LOW_WORD (H_DYNAMIC_SIZE, h_dynamic_size)`) and allocates
  exactly that many bytes.
- Legal range, the same for Z3, Z5 and Z8: at least 64 bytes ("Dynamic memory must contain at
  least 64 bytes"), and dynamic plus static memory "must not exceed 64K" ("64K minus 2 bytes"),
  so at most **65,534 B**. The version only changes the story-file limit (128 / 256 / 512 KB),
  which lives on the SD card here.
- This port adds no lower ceiling: `h_dynamic_size` is a 16-bit `zword` (65,535 representable)
  and the only check is `< 64`.

Known stories (header word `$0E`):

| Story | Version | Dynamic memory |
| --- | --- | --- |
| Lost Pig (`LostPig.z8`, r2/080406) | Z8 | **42,554 B** (largest known) |
| `crashme.z5` (Frotz test suite) | Z5 | 34,287 B |
| Beyond Zork (`BeyondZork.z5`, r60/880610) | Z5 | 32,130 B |
| Planetfall (`Planetfall.z5`, r10/880531) | Z5 | 17,435 B |
| Zork I (`Zork1.z5`, r52/871125) | Z5 | 15,166 B |
| Zork III (`Zork3.z3`, r25/860811) | Z3 | 12,548 B |
| Zork II (`Zork2.z3`, r63/860811) | Z3 | 11,767 B |
| `FrotzX3TestLab_Puny.z3` | Z3 | 2,604 B |
| Frotz `etude`, `gntests`, `random`, `strictz`, `unicode` (Z5) | Z5 | 1,216 to 1,553 B |

## What the allocator reports

`heap_caps_get_largest_free_block()` on ESP-IDF 5.5 already returns a **request size**, not a
raw block size:

- `multi_heap.c` rounds the largest free block down with `tlsf_fit_size()`: TLSF searches by
  size class and rounds a request up to its class, so only a request no larger than the
  rounded-down value is guaranteed to be found. For 32 to 64 KiB the class step is 1 KiB, for
  64 to 128 KiB it is 2 KiB.
- `multi_heap_poisoning.c` (`CONFIG_HEAP_POISONING_LIGHT=y` in the C3 Arduino libs) subtracts
  the 12 B head/tail canary overhead.

The X3 measurement fits this exactly: a 62,364 B raw free block reports 61,440 - 12 =
**61,428 B**; Lost Pig's 42,554 B request becomes a 43,008 B block. So comparing the story's
request directly with the reported value is correct; no extra rounding is needed. `malloc()`
finally searches the `MALLOC_CAP_DEFAULT` heaps (`heap_caps_malloc_default()`), so the guard
queries `MALLOC_CAP_DEFAULT`.

## Runtime guard

`init_memory()` reads the header, then, **before** the dynamic-memory `malloc()`:

```text
largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)
start only if h_dynamic_size + FROTZX3_DYNMEM_MARGIN <= largest     (margin 1,024 B)
```

On failure it does not call `malloc()`. It logs
`dynmem preflight: need <bytes> + margin <bytes>, largest <bytes>` at error level (kept at
`LOG_LEVEL=0`), then calls `os_fatal("Not enough contiguous memory for this story.")`. That
takes the existing startup error path: `startStory()` returns false, frees the 64-byte header,
closes the story and shows the reason under **FROTZ START FAILED**. If it passes, startup is
unchanged. The decision is `frotzx3_dynmem_fits()` in `lib/FrotzX3/src/FrotzX3MemGuard.h`, with a
host test in `tools/tests/frotzx3_mem_guard/`.

**Why 1,024 B.** The check runs right before `malloc()` on the same task, so the margin only has
to absorb small allocations another task (for example the render task) might make in between.
1 KiB is one allocator size class at these sizes and 1.7% of the measured budget. It rejects no
known story (Lost Pig needs 43,578 of 61,428 B). If the heap still changes enough between check
and `malloc()`, the `malloc()`-failure path shows the same user-facing message (no allocator
numbers on screen) and logs `dynmem malloc failed: need ..., free ..., largest ...` at error level.

## Two questions, two thresholds

| Question | Rule | v1.6.1 on X3 |
| --- | --- | --- |
| A. Will this story start now? (runtime) | `h_dynamic_size + 1,024 <= largest` | Any story up to **60,404 B** |
| B. Is this build comfortably supported? (validation) | `largest >= R + 1,024 + 8,192`, where `R` = 42,554 B (Lost Pig, the largest supported test story) and 8,192 B is the Frotz task stack `startStory()` allocates right after, i.e. `largest >= 51,770 B` | 61,428 B: passes by 9,658 B |

## Limit: not every legal story fits

A story at the legal maximum (65,534 B) needs a reported largest block of 66,558 B with the margin,
which takes a raw free block of at least 67,584 B. The measured v1.6.1 block is 62,364 B. **FrotzX3 on
CrossInk v1.6.1 cannot guarantee every legal Z3/Z5/Z8 story.** It guarantees stories up to
42,554 B (the validated Lost Pig budget). Stories up to about 60 KB start if the heap looks as
measured. Bigger stories are refused with the message above instead of failing in `malloc()`.
Claim support as "Z3/Z5/Z8 stories with up to 42,554 B dynamic memory (hardware-validated)", never
"all Z5/Z8 stories".

## Hardware validation (CrossInk v1.6.1, XTEINK X3)

| Test | Result |
| --- | --- |
| Normal guardrail firmware: Lost Pig, Zork, Varicella start normally; no regressions | **PASS** |
| Forced-failure probe firmware (`-DFROTZX3_DYNMEM_MARGIN=65535`): stories refused before allocation, FROTZ START FAILED screen shows "Not enough contiguous memory for this story."; no crash, watchdog reset, corruption or partial startup | **PASS** |

Recorded values: runtime margin 1,024 B; hardware-validated story requirement floor 42,554 B (Lost Pig);
theoretical Z-machine maximum 65,534 B; measured largest allocatable request on v1.6.1 about 61,428 B.
v1.6.1 does **not** guarantee every theoretical near-64K story. Future CrossInk releases must repeat
this contiguous-memory validation before being marked supported.

## Requirement for a future CrossInk base

A new CrossInk release cannot be marked `tested` in `compatibility.json` until all of these hold
on an X3:

1. The integration package applies and `pio run -e default` builds (`UPDATING_CROSSINK.md` steps 3 to 5).
2. The full hardware checklist passes (step 7).
3. The contiguous-memory budget is **measured**: the reported largest block just before Lost Pig's
   dynamic-memory allocation, entering FrotzX3 from Home after normal use (book opened, cover
   shown), lowest of at least three entries.
4. Lost Pig (or a larger supported test story) starts and plays.
5. The measured value meets rule B (`>= 51,770 B` for Lost Pig). Record it in the port notes.
   If it is lower, the base is not supported until the host-side cause is found; do not lower
   the margin or the rule to pass.

The render-stack high-water mark rule from the v1.6.1 port (at least 3,072 B left) still applies.

### Measuring without diagnostics code

The guard prints the reported largest block when it refuses a story, so a test build that
refuses every story measures the budget at the exact point of the allocation, with no extra
code:

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-DFROTZX3_DYNMEM_MARGIN=65535'
$env:CROSSINK_RELEASE_VERSION = '<version>-memprobe-hwtest'
pio run -e default
Remove-Item Env:PLATFORMIO_BUILD_FLAGS
```

Flash it, open Lost Pig from Home and read the serial log:
`[<ms>] [ERR] [FROTZ] dynmem preflight: need 42554 + margin 65535, largest <bytes>` (61,428 on v1.6.1). The screen must show
**FROTZ START FAILED** with "Not enough contiguous memory for this story." below it, and
leaving and re-entering must work. This also tests the failure path on hardware. Never ship this
build. Release builds leave `FROTZX3_DYNMEM_MARGIN` at its default.
