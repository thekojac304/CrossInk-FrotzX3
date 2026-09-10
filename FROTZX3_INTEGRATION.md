# FrotzX3 integration

FrotzX3 is a source package compiled into CrossInk, not a separately installed runtime app. Transplant from a known-good, physical-X3-tested checkpoint. Adapt host integration to the destination release rather than replacing whole CrossInk host files.

## Files to copy

- Entire `lib/FrotzX3/`, including `library.json`, all modified interpreter sources, and `src/FrotzX3Paths.h`.
- Entire `src/activities/frotzx3/`, currently `FrotzX3Activity.cpp` and `FrotzX3Activity.h`.
- This document for future maintenance.

Do not substitute upstream Frotz files for this port's modified sources. The ignored `vendor/frotz/` reference tree is not needed to build or run the app. Story files and existing saves must be supplied separately on SD.

## Required host integration

The only existing host file requiring Frotz-specific edits is `src/activities/home/HomeActivity.cpp`. It supplies the Home menu entry and launches the activity through the normal activity manager:

1. Include `../frotzx3/FrotzX3Activity.h` (currently line 28).
2. Add `InteractiveFiction` to `HomeMenuAction` (line 61). This historical action name remains in use; the activity class is `FrotzX3Activity`.
3. Allow space for the extra entry: current `HomeMenuEntries::kCapacity` is 9 (line 73).
4. Add the following entry in both `appendHomeMenuItems()` and `buildMinimalMenuItems()` (lines 279 and 304), before File Transfer:

   ```cpp
   items.push({"FrotzX3", Book, HomeMenuAction::InteractiveFiction});
   ```

5. Account for the added entry in `getMenuItemCount()` (line 600; current base count is 5).
6. Add the launch case in `HomeActivity::loop()` (line 1662):

   ```cpp
   case HomeMenuAction::InteractiveFiction:
     activityManager.pushActivity(
         std::make_unique<FrotzX3Activity>(renderer, mappedInput));
     break;
   ```

Line numbers and counts describe this snapshot. Recalculate capacity/counts against the future release's menus; do not overwrite its other entries. No separate activity-manager registration is currently required.

**`src/main.cpp` requires no FrotzX3-specific changes.** Its existing global Power handling runs before the activity loop, which matters for the input contract below.

## Generic CrossInk input prerequisite

`src/MappedInputManager.cpp` contains generic Confirm/Power behavior, not Frotz-specific registration or interpreter logic. Preserve equivalent behavior in the destination:

- `wasPressed(Confirm)` arms `suppressPowerRelease` when a mapped Confirm press or eligible Power-as-Confirm fallback is accepted (currently lines 606–648).
- An ordinary Confirm release clears suppression so a later unrelated Power press is not swallowed (lines 691–702).
- `wasReleased(Power)` consumes and clears a suppressed Power release (lines 732–743).
- `isPressed(Power)` hides a Power hold once that press has been consumed as Confirm (lines 757–777).

This prevents Select on shared Confirm/Power hardware from also triggering a global sleep action. The activity also defers blocking work until button release and uses the existing `suppressNextPowerRelease()` and `suppressNextPowerConfirmRelease()` APIs in `MappedInputManager.h`.

If the future release already implements equivalent semantics, no input edit is needed. Otherwise port the minimal generic fix into `MappedInputManager.cpp`; do not replace the file wholesale or add a Frotz-specific branch. No Frotz-specific header changes are currently required.

## Build and runtime contracts

- PlatformIO discovers the local library through `<FrotzX3.h>` and compiles the activity under `src/`. No Frotz-specific `platformio.ini` edit is currently needed. The exception-decoder monitor filter is optional debugging tooling.
- Preserve compatible CrossInk APIs/includes: `Activity`, `RenderLock`, `MappedInputManager`, `GfxRenderer`, `UITheme`, `fontIds.h`, `Logging.h`, and `HalStorage`/`HalFile`. Library metadata does not declare a complete independent dependency bundle.
- This port requires ESP32 FreeRTOS and `esp_heap_caps.h`. Native simulator support cannot be assumed without compatible shims.
- There is one persistent Frotz task and shared global interpreter state. Preserve input-wait handoff, task shutdown, and output ownership; this is not a multiple-instance interface.
- `FrotzX3::startStory()` calls `init_memory()` through `frotz_try_init_memory()` **before task creation**. Large stories need a contiguous allocation; changing this order can cause allocation failure despite sufficient total free heap.
- The actual Frotz task stack is **8192 bytes (8 KB)** in the ESP32 `xTaskCreate()` call. Older comments mentioning 16 KB do not describe the current value.
- Preserve the existing HAL-backed story/save I/O and modified Quetzal bridge. Available heap and host memory usage must still support the tested stories.
- Explicit in-app exits save/stop before finishing. `FrotzX3Activity` currently has no `onExit()` override; generic activity destruction alone does not perform Frotz shutdown. Recheck forced navigation and sleep behavior if the host lifecycle changes.
- **Physical XTEINK X3 testing is authoritative.** A successful build or simulator run does not establish input, SD, memory, or save compatibility on the device.

## Storage and SD layout

The four directory definitions live only in `lib/FrotzX3/src/FrotzX3Paths.h`:

| Definition | Runtime path |
| --- | --- |
| `FROTZX3_STORIES_DIR` | `/adventures` |
| `FROTZX3_SAVES_DIR` | `/adventures/saves` |
| `FROTZX3_MANUAL_SAVES_DIR` | `/adventures/saves/manual` |
| `FROTZX3_REWIND_SAVES_DIR` | `/adventures/saves/rewind` |

Place story files directly in `/adventures`. The current picker recognizes `.z3` through `.z8` case-insensitively and lists up to 16 games. Recognition is not a guarantee that every story's features are supported.

```text
/adventures/
  Story.z5
  saves/
    Story.z5.sav                       # resume
    Story.z5.recovery                  # recovery marker
    manual/
      Story.z5.manual1.sav             # manual slots 1–3
    rewind/
      Story.z5.rewind<N>.sav           # app-managed checkpoints
```

The app ensures the save directories exist. Keep story filenames, save filenames, and directory paths unchanged when transplanting existing data. The full story filename, including extension, forms the save basename. Retain the existing legacy manual/rewind migration logic; do not manually reorganize saves as an installation step.

## Minimal install checklist

- [ ] Start from a clean destination checkout and record its revision plus the tested FrotzX3 source revision.
- [ ] Copy the two FrotzX3 directories listed above.
- [ ] Apply only the Home menu include, action, entries, capacity/count, and launch changes.
- [ ] Compare the destination's generic Confirm/Power semantics; port the input fix only if equivalent behavior is absent.
- [ ] Confirm host API/include compatibility, ESP32 tooling, and unchanged task/allocation/storage contracts.
- [ ] Supply the SD story directory and preserve existing saves.
- [ ] Run `pio run -e default`; inspect compilation, linking, and firmware-size checks. On Windows terminals with encoding errors, use process-local `PYTHONUTF8=1` and `PYTHONIOENCODING=utf-8`.
- [ ] Review the diff for unintended changes, then perform the physical-X3 smoke tests below.

## Smoke tests after transplant

- [ ] Home shows FrotzX3 in the applicable menu layouts; selection opens the picker and Back returns normally.
- [ ] Existing stories are discovered. Start a new game and resume an existing save; include a previously tested large story such as Lost Pig.
- [ ] Check startup logs: memory initialization precedes task creation, with no allocation or task-start failure.
- [ ] Check short and held Select, navigation, Back, keyboard commands, and single-key story prompts. Select must not accidentally trigger sleep; intentional Power behavior must still work.
- [ ] Test manual save/load, overwrite confirmation, resume, rewind, and recovery using the existing SD paths. Verify persistence after exit/re-entry and reboot.
- [ ] Exit a running story and start another; check for a stuck task, stale output, or failed storage access.
- [ ] Test intentional sleep/wake and automatic sleep against the known-good device behavior, including recovery and any host Home gesture.

Do not treat a portability transplant as permission to change interpreter, input, task, allocation, or save behavior. Investigate differences against the tested checkpoint before expanding scope.
