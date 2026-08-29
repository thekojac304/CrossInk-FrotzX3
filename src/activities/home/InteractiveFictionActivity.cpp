#include "InteractiveFictionActivity.h"

#include <cstdio>
#include <cstring>

#include <GfxRenderer.h>

#include <HalStorage.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <Logging.h>

#include <FrotzX3.h>

extern "C" {
void frotz_debug_visible_objects(
    const char* visibleText);

void frotz_debug_room_tree(
    const char* visibleText);
}

static constexpr int MAX_GAMES = 16;
static constexpr int MAX_GAME_FILENAME = 128;

static char gGameFilenames
    [MAX_GAMES]
    [MAX_GAME_FILENAME] = {};

static int gGameCount = 0;

static bool gGamePickerActive = false;
static int gGamePickerIndex = 0;

static bool gResumePromptActive = false;
static int gResumePromptIndex = 0;

enum class SessionOrigin {
  None,
  Resume,
  NewGame
};

static SessionOrigin gSessionOrigin =
    SessionOrigin::None;

static bool gResumeSaveExistedAtStart = false;

static bool gExitReplacePromptActive = false;
static int gExitReplacePromptIndex = 0;

static constexpr int MANUAL_SAVE_SLOTS = 3;

static bool gManualSaveOverwritePromptActive = false;
static int gManualSaveOverwritePromptIndex = 0;
static int gPendingManualSaveSlot = -1;

static bool gManualSaveStatusVisible = false;
static bool gManualSaveStatusSucceeded = false;
static int gManualSaveStatusSlot = -1;

static bool gManualLoadPromptActive = false;
static int gManualLoadPromptIndex = 0;
static int gPendingManualLoadSlot = -1;

static bool gManualLoadStatusVisible = false;
static bool gManualLoadStatusSucceeded = false;
static int gManualLoadStatusSlot = -1;

static int gPendingManualSaveExecuteSlot = -1;
static int gPendingManualLoadExecuteSlot = -1;

/*
 * Crash recovery.
 *
 * We do NOT write an additional Quetzal save every turn.
 * Instead, a tiny marker file records which existing rewind
 * checkpoint is the newest safe crash-recovery state.
 */
static bool gRecoveryPromptActive = false;
static int gRecoveryPromptIndex = 0;
static int gRecoveryCheckpointSlot = -1;

/*
 * True only when the Load Game screen was opened from the
 * pre-game startup choices rather than from the in-game menu.
 * This lets Back return to the startup choices correctly.
 */
static bool gStartupManualLoadActive = false;

/*
 * Deferred game-picker launch.
 *
 * The top Select button is also the X3 sleep/wake button. Starting
 * or restoring Frotz can block long enough that the same physical
 * press is interpreted as a sleep hold. The picker therefore only
 * ARMS a launch on press; the actual interpreter work begins after
 * Select is physically released.
 */
enum class DeferredPickerAction {
  None,
  StartFresh,
  StartResume,
  Recover
};

static DeferredPickerAction gDeferredPickerAction =
    DeferredPickerAction::None;

static int gDeferredPickerRecoverySlot = -1;
static bool gDeferredPickerUsesPowerButton = false;

/*
 * Adventure Log + rewind history.
 *
 * Keep the visible log deliberately small and fixed-size in RAM.
 * Quetzal rewind checkpoints themselves live on the SD card.
 *
 * The newest five log entries can retain rewind checkpoints.
 * Up to ten recent command/result summaries remain visible.
 */
static constexpr int MAX_ADVENTURE_LOG_ENTRIES = 10;
static constexpr int MAX_REWIND_CHECKPOINTS = 5;
static constexpr int ADVENTURE_LOG_OUTPUT_LENGTH = 160;
static constexpr int ADVENTURE_LOG_VISIBLE_ROWS = 4;

struct AdventureLogEntry {
  unsigned long turnNumber = 0;
  char command[64] = {};
  char output[ADVENTURE_LOG_OUTPUT_LENGTH] = {};
  int checkpointSlot = -1;
  bool hasRewind = false;
};

static AdventureLogEntry
    gAdventureLog[MAX_ADVENTURE_LOG_ENTRIES] = {};

static int gAdventureLogCount = 0;
static int gAdventureLogIndex = 0;
static unsigned long gAdventureTurnCounter = 0;

static int gNextRewindSlot = 0;

static bool gPendingRewindExecute = false;
static int gPendingRewindLogIndex = -1;

static bool gCommandSubmitArmed = false;
static int gPreparedRewindSlot = -1;

static bool gPendingAdventureLogEntry = false;
static char gPendingAdventureLogCommand[64] = {};
static int gPendingAdventureLogCheckpointSlot = -1;

static char gSelectedStoryPath[256] =
    "/adventures/lostpig.z8";

static bool gFrotzStoryLoaded = false;
static const char* gFrotzLastError = "";
static char gFrotzBootOutput[2048] = "";

static bool gFrotzOutputCaptured = false;
static int gTranscriptPage = 0;

static bool gPageBackLongPressHandled = false;
static bool gPageForwardLongPressHandled = false;

static bool gBackLongPressHandled = false;

constexpr unsigned long TRANSCRIPT_LONG_PRESS_MS = 600;
constexpr unsigned long GAME_MENU_LONG_PRESS_MS = 600;

static constexpr int MAX_CONTEXT_OBJECTS = 16;
static constexpr int MAX_CONTEXT_OBJECT_LENGTH = 32;

static constexpr int MAX_DICTIONARY_CACHE = 128;
static constexpr int MAX_DICTIONARY_WORD_LENGTH = 24;

struct DictionaryCacheEntry {
  char word[MAX_DICTIONARY_WORD_LENGTH] = {};
  bool isWord = false;
};

static DictionaryCacheEntry
    gDictionaryCache[MAX_DICTIONARY_CACHE] = {};

static int gDictionaryCacheCount = 0;

static char gContextObjects
    [MAX_CONTEXT_OBJECTS]
    [MAX_CONTEXT_OBJECT_LENGTH] = {};

static int gContextObjectCount = 0;

static constexpr int MAX_INVENTORY_OBJECTS = 16;

static char gInventoryObjects
    [MAX_INVENTORY_OBJECTS]
    [MAX_CONTEXT_OBJECT_LENGTH] = {};

static int gInventoryObjectCount = 0;

static bool gCaptureNextOutputAsInventory = false;

namespace {

/*
 * Forward declaration.
 *
 * loadManualSlot() uses resetContextObjects() before the full
 * helper definition appears later in this file.
 */
void resetContextObjects();
void resetInventoryObjects();

void resetAdventureLogState() {

  gAdventureLogCount = 0;
  gAdventureLogIndex = 0;
  gAdventureTurnCounter = 0;

  gNextRewindSlot = 0;

  gPendingRewindExecute = false;
  gPendingRewindLogIndex = -1;

  gCommandSubmitArmed = false;
  gPreparedRewindSlot = -1;

  gPendingAdventureLogEntry = false;
  gPendingAdventureLogCommand[0] = '\0';
  gPendingAdventureLogCheckpointSlot = -1;

  for (int i = 0;
       i < MAX_ADVENTURE_LOG_ENTRIES;
       ++i) {

    gAdventureLog[i] = AdventureLogEntry{};
  }
}


bool isSupportedStoryFile(const char* filename) {

  if (filename == nullptr) {
    return false;
  }

  const char* extension =
      strrchr(filename, '.');

  if (extension == nullptr) {
    return false;
  }

  return
      strcasecmp(extension, ".z3") == 0 ||
      strcasecmp(extension, ".z4") == 0 ||
      strcasecmp(extension, ".z5") == 0 ||
      strcasecmp(extension, ".z6") == 0 ||
      strcasecmp(extension, ".z7") == 0 ||
      strcasecmp(extension, ".z8") == 0;
}


bool selectedGameHasResumeSave() {

  if (gGameCount <= 0 ||
      gGamePickerIndex < 0 ||
      gGamePickerIndex >= gGameCount) {

    return false;
  }

  char savePath[256] = {};

  snprintf(
      savePath,
      sizeof(savePath),
      "/adventures/saves/%s.sav",
      gGameFilenames[gGamePickerIndex]);

  return Storage.exists(savePath);
}


bool selectedGameHasManualSave() {

  if (gGameCount <= 0 ||
      gGamePickerIndex < 0 ||
      gGamePickerIndex >= gGameCount) {

    return false;
  }

  for (int slot = 0;
       slot < MANUAL_SAVE_SLOTS;
       ++slot) {

    char path[256] = {};

    snprintf(
        path,
        sizeof(path),
        "/adventures/saves/manual/%s.manual%d.sav",
        gGameFilenames[gGamePickerIndex],
        slot + 1);

    if (Storage.exists(path)) {
      return true;
    }
  }

  return false;
}


void buildRecoveryMarkerPathForFilename(
    const char* filename,
    char* path,
    size_t pathSize) {

  if (filename == nullptr ||
      filename[0] == '\0' ||
      path == nullptr ||
      pathSize == 0) {

    return;
  }

  snprintf(
      path,
      pathSize,
      "/adventures/saves/%s.recovery",
      filename);
}


void buildCurrentRecoveryMarkerPath(
    char* path,
    size_t pathSize) {

  if (path == nullptr ||
      pathSize == 0) {

    return;
  }

  const char* filename =
      strrchr(
          gSelectedStoryPath,
          '/');

  if (filename != nullptr) {
    ++filename;
  } else {
    filename = gSelectedStoryPath;
  }

  buildRecoveryMarkerPathForFilename(
      filename,
      path,
      pathSize);
}


bool readRecoveryMarkerForFilename(
    const char* filename,
    int* checkpointSlot) {

  if (checkpointSlot == nullptr) {
    return false;
  }

  *checkpointSlot = -1;

  char path[256] = {};

  buildRecoveryMarkerPathForFilename(
      filename,
      path,
      sizeof(path));

  if (path[0] == '\0' ||
      !Storage.exists(path)) {

    return false;
  }

  HalFile file =
      Storage.open(path);

  if (!file) {
    return false;
  }

  char buffer[16] = {};
  const int bytesRead =
      file.read(
          buffer,
          sizeof(buffer) - 1);

  file.close();

  if (bytesRead <= 0) {
    return false;
  }

  buffer[bytesRead] = '\0';

  int slot = -1;

  if (sscanf(
          buffer,
          "%d",
          &slot) != 1 ||
      slot < 0 ||
      slot >= MAX_REWIND_CHECKPOINTS) {

    return false;
  }

  *checkpointSlot = slot;
  return true;
}


bool selectedGameHasRecoverySave(
    int* checkpointSlot) {

  if (gGameCount <= 0 ||
      gGamePickerIndex < 0 ||
      gGamePickerIndex >= gGameCount) {

    return false;
  }

  int slot = -1;

  if (!readRecoveryMarkerForFilename(
          gGameFilenames[gGamePickerIndex],
          &slot)) {

    LOG_INF(
        "FROTZREC",
        "no recovery marker for %s",
        gGameFilenames[gGamePickerIndex]);

    return false;
  }

  LOG_INF(
      "FROTZREC",
      "recovery marker for %s points to slot %d",
      gGameFilenames[gGamePickerIndex],
      slot + 1);

  char rewindPath[256] = {};

  snprintf(
      rewindPath,
      sizeof(rewindPath),
      "/adventures/saves/rewind/%s.rewind%d.sav",
      gGameFilenames[gGamePickerIndex],
      slot + 1);

  if (!Storage.exists(rewindPath)) {

    LOG_ERR(
        "FROTZREC",
        "recovery checkpoint missing: %s",
        rewindPath);

    return false;
  }

  LOG_INF(
      "FROTZREC",
      "recovery checkpoint found: %s",
      rewindPath);

  if (checkpointSlot != nullptr) {
    *checkpointSlot = slot;
  }

  return true;
}


bool writeRecoveryMarker(
    int checkpointSlot) {

  if (checkpointSlot < 0 ||
      checkpointSlot >= MAX_REWIND_CHECKPOINTS) {

    return false;
  }

  char path[256] = {};

  buildCurrentRecoveryMarkerPath(
      path,
      sizeof(path));

  if (path[0] == '\0') {
    return false;
  }

  HalFile file;

  if (!Storage.openFileForWrite(
          "FROTZ",
          path,
          file)) {

    return false;
  }

  char marker[16] = {};

  const int markerLength =
      snprintf(
          marker,
          sizeof(marker),
          "%d\n",
          checkpointSlot);

  const bool wrote =
      markerLength > 0 &&
      file.write(
          marker,
          markerLength) ==
              static_cast<size_t>(
                  markerLength);

  const bool synced =
      wrote &&
      file.sync();

  file.close();

  return synced;
}


void clearCurrentRecoveryMarker() {

  char path[256] = {};

  buildCurrentRecoveryMarkerPath(
      path,
      sizeof(path));

  if (path[0] != '\0' &&
      Storage.exists(path)) {

    Storage.remove(path);
  }
}


void migrateSaveLayout() {

  Storage.ensureDirectoryExists(
      "/adventures/saves");

  Storage.ensureDirectoryExists(
      "/adventures/saves/manual");

  Storage.ensureDirectoryExists(
      "/adventures/saves/rewind");

  for (int gameIndex = 0;
       gameIndex < gGameCount;
       ++gameIndex) {

    const char* filename =
        gGameFilenames[gameIndex];

    for (int slot = 0;
         slot < MANUAL_SAVE_SLOTS;
         ++slot) {

      char oldPath[256] = {};
      char newPath[256] = {};

      snprintf(
          oldPath,
          sizeof(oldPath),
          "/adventures/saves/%s.manual%d.sav",
          filename,
          slot + 1);

      snprintf(
          newPath,
          sizeof(newPath),
          "/adventures/saves/manual/%s.manual%d.sav",
          filename,
          slot + 1);

      if (Storage.exists(oldPath) &&
          !Storage.exists(newPath)) {

        Storage.rename(
            oldPath,
            newPath);
      }
    }

    for (int slot = 0;
         slot < MAX_REWIND_CHECKPOINTS;
         ++slot) {

      char oldPath[256] = {};
      char newPath[256] = {};

      snprintf(
          oldPath,
          sizeof(oldPath),
          "/adventures/saves/%s.rewind%d.sav",
          filename,
          slot + 1);

      snprintf(
          newPath,
          sizeof(newPath),
          "/adventures/saves/rewind/%s.rewind%d.sav",
          filename,
          slot + 1);

      if (Storage.exists(oldPath) &&
          !Storage.exists(newPath)) {

        Storage.rename(
            oldPath,
            newPath);
      }
    }
  }
}


void buildManualSavePath(
    int slotIndex,
    char* path,
    size_t pathSize) {

  if (path == nullptr ||
      pathSize == 0 ||
      slotIndex < 0 ||
      slotIndex >= MANUAL_SAVE_SLOTS) {

    return;
  }

  const char* filename =
      strrchr(
          gSelectedStoryPath,
          '/');

  if (filename != nullptr) {
    ++filename;
  } else {
    filename = gSelectedStoryPath;
  }

  snprintf(
      path,
      pathSize,
      "/adventures/saves/manual/%s.manual%d.sav",
      filename,
      slotIndex + 1);
}


bool manualSaveSlotExists(
    int slotIndex) {

  char path[256] = {};

  buildManualSavePath(
      slotIndex,
      path,
      sizeof(path));

  return
      path[0] != '\0' &&
      Storage.exists(path);
}


bool saveManualSlot(
    int slotIndex) {

  char path[256] = {};

  buildManualSavePath(
      slotIndex,
      path,
      sizeof(path));

  if (path[0] == '\0') {
    return false;
  }

  return FrotzX3::saveToPath(path);
}


bool loadManualSlot(
    int slotIndex) {

  char path[256] = {};

  buildManualSavePath(
      slotIndex,
      path,
      sizeof(path));

  if (path[0] == '\0' ||
      !Storage.exists(path)) {

    return false;
  }

  /*
   * Manual LOAD replaces the current running interpreter with
   * the same story restored from the chosen Quetzal slot.
   */
  FrotzX3::stopStory();

  gFrotzBootOutput[0] = '\0';
  gFrotzOutputCaptured = false;
  gTranscriptPage = 0;

  resetContextObjects();
  resetInventoryObjects();
  gCaptureNextOutputAsInventory = false;

  resetAdventureLogState();

  gDictionaryCacheCount = 0;

  for (int i = 0;
       i < MAX_DICTIONARY_CACHE;
       ++i) {

    gDictionaryCache[i].word[0] = '\0';
    gDictionaryCache[i].isWord = false;
  }

  if (!FrotzX3::setRestorePathOnStart(path)) {

    return false;
  }

  gFrotzStoryLoaded =
      FrotzX3::startStory(
          gSelectedStoryPath);

  gFrotzLastError =
      FrotzX3::lastError();

  if (!gFrotzStoryLoaded) {

    FrotzX3::setRestoreOnStart(false);
    return false;
  }

  /*
   * From this point onward the loaded manual state is the active
   * session.  Exiting normally should autosave that state into the
   * ordinary Resume slot, not back into the manual slot.
   */
  gSessionOrigin =
      SessionOrigin::Resume;

  gResumeSaveExistedAtStart = true;

  /*
   * The loaded manual state is now authoritative. A fresh recovery
   * marker will be created on the next submitted command.
   */
  clearCurrentRecoveryMarker();

  return true;
}



void buildRewindSavePath(
    int checkpointSlot,
    char* path,
    size_t pathSize) {

  if (path == nullptr ||
      pathSize == 0 ||
      checkpointSlot < 0 ||
      checkpointSlot >= MAX_REWIND_CHECKPOINTS) {

    return;
  }

  const char* filename =
      strrchr(
          gSelectedStoryPath,
          '/');

  if (filename != nullptr) {
    ++filename;
  } else {
    filename = gSelectedStoryPath;
  }

  snprintf(
      path,
      pathSize,
      "/adventures/saves/rewind/%s.rewind%d.sav",
      filename,
      checkpointSlot + 1);
}


void invalidateRewindSlot(
    int checkpointSlot) {

  for (int i = 0;
       i < gAdventureLogCount;
       ++i) {

    if (gAdventureLog[i].checkpointSlot ==
            checkpointSlot) {

      gAdventureLog[i].hasRewind = false;
      gAdventureLog[i].checkpointSlot = -1;
    }
  }
}


bool saveRewindCheckpoint(
    int checkpointSlot) {

  char path[256] = {};

  buildRewindSavePath(
      checkpointSlot,
      path,
      sizeof(path));

  if (path[0] == '\0') {
    return false;
  }

  LOG_INF(
      "FROTZTIME",
      "rewind checkpoint BEGIN slot=%d",
      checkpointSlot + 1);

  const bool success =
      FrotzX3::saveToPath(path);

  LOG_INF(
      "FROTZTIME",
      "rewind checkpoint END slot=%d success=%d",
      checkpointSlot + 1,
      success ? 1 : 0);

  /*
   * Only retire the older owner of this slot after the replacement
   * checkpoint was actually written successfully.
   */
  if (success) {
    invalidateRewindSlot(
        checkpointSlot);

    /*
     * Crash recovery reuses this exact rewind snapshot.
     * Only a tiny marker is updated; no second Quetzal save is written.
     */
    writeRecoveryMarker(
        checkpointSlot);
  }

  return success;
}


int latestRewindLogIndex() {

  for (int i = gAdventureLogCount - 1;
       i >= 0;
       --i) {

    if (gAdventureLog[i].hasRewind) {
      return i;
    }
  }

  return -1;
}


void makeAdventureLogExcerpt(
    const char* source,
    char* destination,
    size_t destinationSize) {

  if (destination == nullptr ||
      destinationSize == 0) {

    return;
  }

  destination[0] = '\0';

  if (source == nullptr) {
    return;
  }

  size_t sourceIndex = 0;
  size_t destIndex = 0;
  bool previousWasSpace = false;

  while (source[sourceIndex] != '\0' &&
         destIndex < destinationSize - 1) {

    char c =
        source[sourceIndex++];

    if (c == '\n' ||
        c == '\r' ||
        c == '\t') {

      c = ' ';
    }

    if (c == ' ') {

      if (previousWasSpace) {
        continue;
      }

      previousWasSpace = true;

    } else {

      previousWasSpace = false;
    }

    destination[destIndex++] = c;
  }

  while (destIndex > 0 &&
         destination[destIndex - 1] == ' ') {

    --destIndex;
  }

  destination[destIndex] = '\0';
}


void appendAdventureLogEntry(
    const char* command,
    const char* output,
    int checkpointSlot) {

  if (command == nullptr ||
      command[0] == '\0') {

    return;
  }

  /*
   * Keep only the ten most recent visible entries.
   *
   * Dropping an old visible entry does not require deleting its
   * checkpoint file here.  The five physical checkpoint slots are
   * reused in a ring and will overwrite old files naturally.
   */
  if (gAdventureLogCount >=
      MAX_ADVENTURE_LOG_ENTRIES) {

    for (int i = 1;
         i < MAX_ADVENTURE_LOG_ENTRIES;
         ++i) {

      gAdventureLog[i - 1] =
          gAdventureLog[i];
    }

    gAdventureLogCount =
        MAX_ADVENTURE_LOG_ENTRIES - 1;
  }

  AdventureLogEntry& entry =
      gAdventureLog[
          gAdventureLogCount];

  entry = AdventureLogEntry{};

  entry.turnNumber =
      ++gAdventureTurnCounter;

  snprintf(
      entry.command,
      sizeof(entry.command),
      "%s",
      command);

  makeAdventureLogExcerpt(
      output,
      entry.output,
      sizeof(entry.output));

  entry.checkpointSlot =
      checkpointSlot;

  entry.hasRewind =
      checkpointSlot >= 0;

  ++gAdventureLogCount;

  gAdventureLogIndex =
      gAdventureLogCount - 1;
}


bool loadRewindCheckpointForLogIndex(
    int logIndex) {

  if (logIndex < 0 ||
      logIndex >= gAdventureLogCount ||
      !gAdventureLog[logIndex].hasRewind ||
      gAdventureLog[logIndex].checkpointSlot < 0) {

    return false;
  }

  const int checkpointSlot =
      gAdventureLog[logIndex].checkpointSlot;

  char path[256] = {};

  buildRewindSavePath(
      checkpointSlot,
      path,
      sizeof(path));

  if (path[0] == '\0' ||
      !Storage.exists(path)) {

    gAdventureLog[logIndex].hasRewind = false;
    gAdventureLog[logIndex].checkpointSlot = -1;

    return false;
  }

  /*
   * Rewind uses the same startup-restore path as manual LOAD, while
   * preserving the original New Game / Resume session origin.
   */
  FrotzX3::stopStory();

  gFrotzBootOutput[0] = '\0';
  gFrotzOutputCaptured = false;
  gTranscriptPage = 0;

  resetContextObjects();
  resetInventoryObjects();
  gCaptureNextOutputAsInventory = false;

  gDictionaryCacheCount = 0;

  for (int i = 0;
       i < MAX_DICTIONARY_CACHE;
       ++i) {

    gDictionaryCache[i].word[0] = '\0';
    gDictionaryCache[i].isWord = false;
  }

  if (!FrotzX3::setRestorePathOnStart(path)) {
    return false;
  }

  gFrotzStoryLoaded =
      FrotzX3::startStory(
          gSelectedStoryPath);

  gFrotzLastError =
      FrotzX3::lastError();

  if (!gFrotzStoryLoaded) {

    FrotzX3::setRestoreOnStart(false);
    return false;
  }

  /*
   * The selected checkpoint is from immediately BEFORE this command.
   * Everything from this log entry onward belongs to the abandoned
   * future timeline, so remove those entries from the active log.
   */
  gAdventureLogCount =
      logIndex;

  if (gAdventureLogCount > 0) {
    gAdventureLogIndex =
        gAdventureLogCount - 1;
  } else {
    gAdventureLogIndex = 0;
  }

  gPendingAdventureLogEntry = false;
  gPendingAdventureLogCommand[0] = '\0';
  gPendingAdventureLogCheckpointSlot = -1;

  gPreparedRewindSlot = -1;

  /*
   * Quetzal restores the game state, not the CrossInk transcript.
   * Display a local confirmation instead of stale post-command text.
   */
  snprintf(
      gFrotzBootOutput,
      sizeof(gFrotzBootOutput),
      "Rewound to before turn %lu.",
      gAdventureLog[logIndex].turnNumber);

  gFrotzOutputCaptured = true;
  gTranscriptPage = 0;

  /*
   * The active timeline just moved backward. If the device crashes
   * now, recover to this selected checkpoint rather than the abandoned
   * future timeline's newest checkpoint.
   */
  writeRecoveryMarker(
      checkpointSlot);

  return true;
}


bool loadRecoveryCheckpoint(
    int checkpointSlot) {

  if (checkpointSlot < 0 ||
      checkpointSlot >= MAX_REWIND_CHECKPOINTS) {

    return false;
  }

  char path[256] = {};

  buildRewindSavePath(
      checkpointSlot,
      path,
      sizeof(path));

  if (path[0] == '\0' ||
      !Storage.exists(path)) {

    return false;
  }

  FrotzX3::stopStory();

  gFrotzBootOutput[0] = '\0';
  gFrotzOutputCaptured = false;
  gTranscriptPage = 0;

  resetContextObjects();
  resetInventoryObjects();
  gCaptureNextOutputAsInventory = false;

  resetAdventureLogState();

  gDictionaryCacheCount = 0;

  for (int i = 0;
       i < MAX_DICTIONARY_CACHE;
       ++i) {

    gDictionaryCache[i].word[0] = '\0';
    gDictionaryCache[i].isWord = false;
  }

  if (!FrotzX3::setRestorePathOnStart(path)) {
    return false;
  }

  gFrotzStoryLoaded =
      FrotzX3::startStory(
          gSelectedStoryPath);

  gFrotzLastError =
      FrotzX3::lastError();

  if (!gFrotzStoryLoaded) {

    FrotzX3::setRestoreOnStart(false);
    return false;
  }

  /*
   * A recovered session becomes the active timeline.
   * A later clean exit should save normally to the Resume slot.
   */
  gSessionOrigin =
      SessionOrigin::Resume;

  gResumeSaveExistedAtStart = true;

  snprintf(
      gFrotzBootOutput,
      sizeof(gFrotzBootOutput),
      "Crash recovery restored. You are back at the last safe command prompt.");

  gFrotzOutputCaptured = true;
  gTranscriptPage = 0;

  /*
   * Keep the marker alive until a clean exit. If another crash occurs
   * before the next command, this same safe checkpoint remains usable.
   */
  writeRecoveryMarker(
      checkpointSlot);

  return true;
}


void scanGames() {

  gGameCount = 0;

  for (int i = 0;
       i < MAX_GAMES;
       ++i) {

    gGameFilenames[i][0] = '\0';
  }

  LOG_INF(
      "FROTZ",
      "--- FROTZ GAME SCAN ---");

  HalFile directory =
      Storage.open("/adventures");

  if (!directory ||
      !directory.isDirectory()) {

    LOG_ERR(
        "FROTZ",
        "Could not open /adventures");

    directory.close();
    return;
  }

  for (HalFile entry =
           directory.openNextFile();
       entry &&
       gGameCount < MAX_GAMES;
       entry =
           directory.openNextFile()) {

    char filename[MAX_GAME_FILENAME] = {};

    entry.getName(
        filename,
        sizeof(filename));

    const bool isDirectory =
        entry.isDirectory();

    entry.close();

    if (isDirectory) {
      continue;
    }

    if (!isSupportedStoryFile(
            filename)) {

      continue;
    }

    snprintf(
        gGameFilenames[gGameCount],
        MAX_GAME_FILENAME,
        "%s",
        filename);

    LOG_INF(
        "FROTZ",
        "Game %d: %s",
        gGameCount + 1,
        gGameFilenames[gGameCount]);

    ++gGameCount;
  }

  directory.close();

  LOG_INF(
      "FROTZ",
      "Found %d supported game(s)",
      gGameCount);

  LOG_INF(
      "FROTZ",
      "--- END FROTZ GAME SCAN ---");
}

constexpr int HEADER_HEIGHT = 80;
constexpr int LEFT_MARGIN = 24;

constexpr int ROOM_TITLE_Y = 92;
constexpr int BODY_Y = 92;

constexpr int HISTORY_Y = 285;
constexpr int MENU_Y = 440;

constexpr int LINE_HEIGHT = 28;

constexpr int MAX_LINE_CHARS = 38;
constexpr int MAX_RESPONSE_LINES = 4;
constexpr int TRANSCRIPT_LINES_PER_PAGE = 11;

// --------------------------------------------------
// T9 KEYBOARD
// --------------------------------------------------

const char* KEY_GROUPS[] = {
    "ABC",
    "DEF",
    "GHI",
    "JKL",
    "MNO",
    "PQRS",
    "TUV",
    "WXYZ"
};

constexpr int GROUP_COUNT =
    sizeof(KEY_GROUPS) / sizeof(KEY_GROUPS[0]);

/*
 * Z-machine single-key choices.
 *
 * Used when the story calls READ_CHAR / os_read_key(). The normal
 * Actions grid is temporarily replaced by these direct key choices.
 */
static const char* SINGLE_KEY_LABELS[13] = {
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    "Y", "0", "N",
    "MORE"
};

static const unsigned char SINGLE_KEY_VALUES[12] = {
    '1', '2', '3',
    '4', '5', '6',
    '7', '8', '9',
    'Y', '0', 'N'
};

/*
 * Frotz/Z-machine special input codes.
 *
 * These values come directly from Frotz's frotz.h character-code
 * definitions.  FrotzX3::submitKey(char) converts the byte back to
 * unsigned char before handing it to the platform layer, so values
 * above 0x7f remain intact.
 */
static constexpr unsigned char ZKEY_RETURN = 0x0d;
static constexpr unsigned char ZKEY_ESCAPE = 0x1b;

static constexpr unsigned char ZKEY_ARROW_UP = 0x81;
static constexpr unsigned char ZKEY_ARROW_DOWN = 0x82;
static constexpr unsigned char ZKEY_ARROW_LEFT = 0x83;
static constexpr unsigned char ZKEY_ARROW_RIGHT = 0x84;

static constexpr unsigned char ZKEY_F1 = 0x85;

static const char* MORE_KEY_LABELS[5] = {
    "LETTERS",
    "SYMBOLS",
    "NAVIGATION",
    "FUNCTION KEYS",
    "BACK"
};

static const char* NAVIGATION_KEY_LABELS[6] = {
    "UP",
    "DOWN",
    "LEFT",
    "RIGHT",
    "ENTER",
    "ESC"
};

static const unsigned char NAVIGATION_KEY_VALUES[6] = {
    ZKEY_ARROW_UP,
    ZKEY_ARROW_DOWN,
    ZKEY_ARROW_LEFT,
    ZKEY_ARROW_RIGHT,
    ZKEY_RETURN,
    ZKEY_ESCAPE
};

static const char* SYMBOL_KEY_LABELS[24] = {
    "SPACE", "!", "\"",
    "#", "$", "%",
    "&", "'", "(",
    ")", "*", "+",
    ",", "-", ".",
    "/", ":", ";",
    "<", "=", ">",
    "?", "@", "_"
};

static const unsigned char SYMBOL_KEY_VALUES[24] = {
    ' ', '!', '"',
    '#', '$', '%',
    '&', '\'', '(',
    ')', '*', '+',
    ',', '-', '.',
    '/', ':', ';',
    '<', '=', '>',
    '?', '@', '_'
};

// --------------------------------------------------
// WRAPPED TEXT
// --------------------------------------------------

const char* readWrappedLine(
    const char* p,
    char* line,
    int lineCapacity,
    int maxChars) {

  if (p == nullptr ||
      line == nullptr ||
      lineCapacity <= 0) {

    return p;
  }

  line[0] = '\0';

  while (*p == ' ' ||
         *p == '\n') {

    ++p;
  }

  if (*p == '\0') {
    return p;
  }

  int lineLength = 0;

  while (*p != '\0') {

    const char* wordStart = p;
    int wordLength = 0;

    while (p[wordLength] != '\0' &&
           p[wordLength] != ' ' &&
           p[wordLength] != '\n') {

      ++wordLength;
    }

    const int needed =
        wordLength +
        (lineLength > 0 ? 1 : 0);

    if (lineLength > 0 &&
        lineLength + needed > maxChars) {

      break;
    }

    if (lineLength > 0 &&
        lineLength < lineCapacity - 1) {

      line[lineLength++] = ' ';
    }

    int copyLength =
        wordLength;

    if (lineLength + copyLength >
        maxChars) {

      copyLength =
          maxChars - lineLength;
    }

    if (lineLength + copyLength >
        lineCapacity - 1) {

      copyLength =
          lineCapacity -
          lineLength - 1;
    }

    if (copyLength > 0) {

      memcpy(
          line + lineLength,
          wordStart,
          copyLength);

      lineLength +=
          copyLength;

      line[lineLength] =
          '\0';
    }

    p += wordLength;

    if (*p == '\n') {

      ++p;
      break;
    }

    while (*p == ' ') {
      ++p;
    }

    if (lineLength >=
        maxChars) {

      break;
    }
  }

  return p;
}


int countWrappedLines(
    const char* text,
    int maxChars) {

  if (text == nullptr ||
      text[0] == '\0') {

    return 0;
  }

  const char* p = text;

  int lineCount = 0;

  while (*p != '\0') {

    char line[64] = {};

    const char* next =
        readWrappedLine(
            p,
            line,
            sizeof(line),
            maxChars);

    if (line[0] != '\0') {
      ++lineCount;
    }

    if (next == p) {
      break;
    }

    p = next;
  }

  return lineCount;
}


void drawWrappedTextPage(
    GfxRenderer& renderer,
    int x,
    int y,
    const char* text,
    int maxChars,
    int maxLines,
    int pageIndex) {

  if (text == nullptr) {
    return;
  }

  const char* p = text;

  const int firstLine =
      pageIndex * maxLines;

  for (int i = 0;
       i < firstLine &&
       *p != '\0';
       ++i) {

    char skippedLine[64] = {};

    p =
        readWrappedLine(
            p,
            skippedLine,
            sizeof(skippedLine),
            maxChars);
  }

  for (int lineNumber = 0;
       lineNumber < maxLines &&
       *p != '\0';
       ++lineNumber) {

    char line[64] = {};

    p =
        readWrappedLine(
            p,
            line,
            sizeof(line),
            maxChars);

    if (line[0] == '\0') {
      break;
    }

    renderer.drawText(
        UI_12_FONT_ID,
        x,
        y +
            lineNumber *
                LINE_HEIGHT,
        line,
        true,
        EpdFontFamily::REGULAR);
  }
}


void drawWrappedText(
    GfxRenderer& renderer,
    int x,
    int y,
    const char* text,
    int maxChars,
    int maxLines) {

  drawWrappedTextPage(
      renderer,
      x,
      y,
      text,
      maxChars,
      maxLines,
      0);
}

// --------------------------------------------------
// PREFIX MATCH
// --------------------------------------------------

bool beginsWith(
    const char* text,
    const char* prefix) {

  const size_t prefixLength =
      strlen(prefix);

  if (prefixLength == 0) {
    return false;
  }

  return strncmp(
             text,
             prefix,
             prefixLength) == 0;
}
bool isAsciiLetter(char c) {

  return
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z');
}

char toUpperAscii(char c) {

  if (c >= 'a' && c <= 'z') {
    return c - ('a' - 'A');
  }

  return c;
}
bool containsTextIgnoreCase(
    const char* text,
    const char* needle) {

  if (text == nullptr ||
      needle == nullptr ||
      needle[0] == '\0') {

    return false;
  }

  for (const char* start = text;
       *start != '\0';
       ++start) {

    const char* a = start;
    const char* b = needle;

    while (*a != '\0' &&
           *b != '\0') {

      if (toUpperAscii(*a) !=
          toUpperAscii(*b)) {

        break;
      }

      ++a;
      ++b;
    }

    if (*b == '\0') {
      return true;
    }
  }

  return false;
}
bool isContextArticle(const char* word) {

  return
      strcmp(word, "A") == 0 ||
      strcmp(word, "AN") == 0 ||
      strcmp(word, "THE") == 0 ||
      strcmp(word, "SOME") == 0;
}

bool isContextStopWord(const char* word) {

  static const char* stopWords[] = {
      "A",
      "AN",
      "THE",
      "SOME",

      "I",
      "YOU",
      "HE",
      "SHE",
      "IT",
      "WE",
      "THEY",

      "IS",
      "ARE",
      "WAS",
      "WERE",
      "BE",
      "BEEN",
      "BEING",

      "HAVE",
      "HAS",
      "HAD",

      "DO",
      "DOES",
      "DID",

      "IN",
      "ON",
      "AT",
      "OF",
      "FROM",
      "TO",
      "WITH",
      "WITHOUT",
      "FOR",
      "BY",

      "AND",
      "OR",
      "BUT",
      "IF",
      "THAT",
      "THIS",
      "THERE",
      "HERE",

      "ALL",
      "NO",
      "NOT",

      "YOUR",
      "MY",
      "HIS",
      "HER",
      "ITS",
      "OUR",
      "THEIR",

      "NORTH",
      "SOUTH",
      "EAST",
      "WEST",
      "UP",
      "DOWN",

      "LOOK",
      "EXAMINE",
      "TAKE",
      "DROP",
      "OPEN",
      "CLOSE",
      "READ",
      "ENTER",
      "EXIT",

      "S",
      "M"
  };

  constexpr int stopWordCount =
      sizeof(stopWords) /
      sizeof(stopWords[0]);

  for (int i = 0;
       i < stopWordCount;
       ++i) {

    if (strcmp(
            word,
            stopWords[i]) == 0) {

      return true;
    }
  }

  return false;
}

bool isDictionaryWordCached(
    const char* word) {

  if (word == nullptr ||
      word[0] == '\0') {

    return false;
  }

  for (int i = 0;
       i < gDictionaryCacheCount;
       ++i) {

    if (strcmp(
            gDictionaryCache[i].word,
            word) == 0) {

      return
          gDictionaryCache[i].isWord;
    }
  }

  const bool result =
      FrotzX3::isDictionaryWord(word);

  if (gDictionaryCacheCount <
      MAX_DICTIONARY_CACHE) {

    snprintf(
        gDictionaryCache[
            gDictionaryCacheCount].word,
        MAX_DICTIONARY_WORD_LENGTH,
        "%s",
        word);

    gDictionaryCache[
        gDictionaryCacheCount].isWord =
            result;

    ++gDictionaryCacheCount;
  }

  return result;
}

bool contextObjectAlreadyExists(
    const char* object) {

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    if (strcmp(
            gContextObjects[i],
            object) == 0) {

      return true;
    }
  }

  return false;
}

void resetContextObjects() {

  gContextObjectCount = 0;

  for (int i = 0;
       i < MAX_CONTEXT_OBJECTS;
       ++i) {

    gContextObjects[i][0] =
        '\0';
  }
}

bool inventoryObjectAlreadyExists(
    const char* object) {

  for (int i = 0;
       i < gInventoryObjectCount;
       ++i) {

    if (strcmp(
            gInventoryObjects[i],
            object) == 0) {

      return true;
    }
  }

  return false;
}

void resetInventoryObjects() {

  gInventoryObjectCount = 0;

  for (int i = 0;
       i < MAX_INVENTORY_OBJECTS;
       ++i) {

    gInventoryObjects[i][0] =
        '\0';
  }
}

void addInventoryObject(
    const char* object) {

  if (object == nullptr ||
      object[0] == '\0') {

    return;
  }

  if (inventoryObjectAlreadyExists(
          object)) {

    return;
  }

  if (gInventoryObjectCount >=
      MAX_INVENTORY_OBJECTS) {

    for (int i = 1;
         i < MAX_INVENTORY_OBJECTS;
         ++i) {

      snprintf(
          gInventoryObjects[i - 1],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          gInventoryObjects[i]);
    }

    gInventoryObjectCount =
        MAX_INVENTORY_OBJECTS - 1;
  }

  snprintf(
      gInventoryObjects[
          gInventoryObjectCount],
      MAX_CONTEXT_OBJECT_LENGTH,
      "%s",
      object);

  ++gInventoryObjectCount;
}

bool isInventoryNoiseWord(
    const char* word) {

  static const char* noiseWords[] = {
      "CARRY",
      "CARRIED",
      "CARRIES",
      "CARRYING",
      "HOLD",
      "HOLDING",
      "NOTHING",
      "EMPTY",
      "EMPTYHANDED",
      "HANDED"
  };

  constexpr int noiseWordCount =
      sizeof(noiseWords) /
      sizeof(noiseWords[0]);

  for (int i = 0;
       i < noiseWordCount;
       ++i) {

    if (strcmp(
            word,
            noiseWords[i]) == 0) {

      return true;
    }
  }

  return false;
}

void detectInventoryObjects(
    const char* text) {

  if (text == nullptr ||
      text[0] == '\0') {

    return;
  }

  char word[24] = {};
  int wordLength = 0;

  for (size_t i = 0;
       ;
       ++i) {

    const char c = text[i];

    if (isAsciiLetter(c)) {

      if (wordLength <
          static_cast<int>(
              sizeof(word)) - 1) {

        word[wordLength++] =
            toUpperAscii(c);

        word[wordLength] =
            '\0';
      }

    } else {

      if (wordLength > 0) {

        if (wordLength > 1 &&
            !isContextStopWord(word) &&
            !isInventoryNoiseWord(word) &&
            isDictionaryWordCached(word)) {

          addInventoryObject(word);
        }

        word[0] = '\0';
        wordLength = 0;
      }

      if (c == '\0') {
        break;
      }
    }
  }
}

void addContextObject(
    const char* object) {

  if (object == nullptr ||
      object[0] == '\0') {

    return;
  }

  if (contextObjectAlreadyExists(
          object)) {

    return;
  }

  /*
   * If the cache is full, discard the oldest observed word
   * so newer room-description vocabulary can replace startup
   * banner vocabulary.
   */
  if (gContextObjectCount >=
      MAX_CONTEXT_OBJECTS) {

    for (int i = 1;
         i < MAX_CONTEXT_OBJECTS;
         ++i) {

      snprintf(
          gContextObjects[i - 1],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          gContextObjects[i]);
    }

    gContextObjectCount =
        MAX_CONTEXT_OBJECTS - 1;
  }

  snprintf(
      gContextObjects[
          gContextObjectCount],
      MAX_CONTEXT_OBJECT_LENGTH,
      "%s",
      object);

  ++gContextObjectCount;
}

void detectContextObjects(
    const char* text) {

  if (text == nullptr ||
      text[0] == '\0') {

    return;
  }

  /*
   * Don't learn vocabulary from obvious parser errors.
   */
  if (containsTextIgnoreCase(
          text,
          "DON'T UNDERSTAND") ||
      containsTextIgnoreCase(
          text,
          "DO NOT UNDERSTAND") ||
      containsTextIgnoreCase(
          text,
          "YOU USED THE WORD") ||
      containsTextIgnoreCase(
          text,
          "I DON'T KNOW THE WORD") ||
      containsTextIgnoreCase(
          text,
          "I DO NOT KNOW THE WORD") ||
      containsTextIgnoreCase(
          text,
          "I ONLY UNDERSTOOD YOU")) {

    return;
  }

  char word[24] = {};
  int wordLength = 0;

  for (size_t i = 0;
       ;
       ++i) {

    const char c = text[i];

    if (isAsciiLetter(c)) {

      if (wordLength <
          static_cast<int>(
              sizeof(word)) - 1) {

        word[wordLength++] =
            toUpperAscii(c);

        word[wordLength] =
            '\0';
      }

    } else {

      if (wordLength > 0) {

        /*
         * We're deliberately being permissive here.
         *
         * If a visible word:
         *
         *   - is not an obvious stop word
         *   - exists in the game's real dictionary
         *
         * then allow it into the contextual object list.
         *
         * This means adjectives, scenery words and occasional
         * nonsense interaction targets may appear. That's
         * intentional: Frotz itself gets to decide whether
         * commands using them make sense.
         */
        if (wordLength > 1) {

          const bool stopWord =
              isContextStopWord(word);

          const bool dictionaryWord =
              !stopWord &&
              isDictionaryWordCached(word);

          const bool accepted =
              !stopWord &&
              dictionaryWord;

          LOG_INF(
              "FROTZNOUN",
              "candidate=%s len=%d stop=%d dict=%d accepted=%d",
              word,
              wordLength,
              stopWord ? 1 : 0,
              dictionaryWord ? 1 : 0,
              accepted ? 1 : 0);

          if (accepted) {
            addContextObject(word);
          }
        }

        word[0] = '\0';
        wordLength = 0;
      }

      if (c == '\0') {
        break;
      }
    }
  }

  LOG_INF(
      "FROTZNOUN",
      "context cache now has %d item(s)",
      gContextObjectCount);

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    LOG_INF(
        "FROTZNOUN",
        "context[%d]=%s",
        i,
        gContextObjects[i]);
  }
}


}  // namespace

// ==================================================
// START
// ==================================================

void prepareSelectedStoryForLaunch() {

  snprintf(
      gSelectedStoryPath,
      sizeof(gSelectedStoryPath),
      "/adventures/%s",
      gGameFilenames[gGamePickerIndex]);

  gFrotzBootOutput[0] = '\0';
  gFrotzOutputCaptured = false;
  gTranscriptPage = 0;

  resetContextObjects();
  resetInventoryObjects();
  gCaptureNextOutputAsInventory = false;

  resetAdventureLogState();

  gDictionaryCacheCount = 0;

  for (int i = 0;
       i < MAX_DICTIONARY_CACHE;
       ++i) {

    gDictionaryCache[i].word[0] = '\0';
    gDictionaryCache[i].isWord = false;
  }
}


void armDeferredPickerAction(
    DeferredPickerAction action,
    int recoverySlot = -1,
    bool usesPowerButton = false) {

  gDeferredPickerAction = action;
  gDeferredPickerRecoverySlot = recoverySlot;
  gDeferredPickerUsesPowerButton =
      usesPowerButton;
}


void InteractiveFictionActivity::onEnter() {
scanGames();
migrateSaveLayout();
gFrotzBootOutput[0] = '\0';

gFrotzOutputCaptured = false;

gTranscriptPage = 0;

gBackLongPressHandled = false;

resetContextObjects();
resetInventoryObjects();
gCaptureNextOutputAsInventory = false;

gDictionaryCacheCount = 0;

for (int i = 0;
     i < MAX_DICTIONARY_CACHE;
     ++i) {

  gDictionaryCache[i].word[0] =
      '\0';

  gDictionaryCache[i].isWord =
      false;
}

gGamePickerActive = true;
gGamePickerIndex = 0;

gResumePromptActive = false;
gResumePromptIndex = 0;

gSessionOrigin = SessionOrigin::None;

gResumeSaveExistedAtStart = false;

gExitReplacePromptActive = false;
gExitReplacePromptIndex = 0;

gManualSaveOverwritePromptActive = false;
gManualSaveOverwritePromptIndex = 0;
gPendingManualSaveSlot = -1;

gManualSaveStatusVisible = false;
gManualSaveStatusSucceeded = false;
gManualSaveStatusSlot = -1;

gManualLoadPromptActive = false;
gManualLoadPromptIndex = 0;
gPendingManualLoadSlot = -1;

gManualLoadStatusVisible = false;
gManualLoadStatusSucceeded = false;
gManualLoadStatusSlot = -1;

gPendingManualSaveExecuteSlot = -1;
gPendingManualLoadExecuteSlot = -1;

gRecoveryPromptActive = false;
gRecoveryPromptIndex = 0;
gRecoveryCheckpointSlot = -1;
gStartupManualLoadActive = false;

gDeferredPickerAction =
    DeferredPickerAction::None;

gDeferredPickerRecoverySlot = -1;
gDeferredPickerUsesPowerButton = false;

resetAdventureLogState();

gFrotzStoryLoaded = false;
gFrotzLastError = "";

  Activity::onEnter();

  currentMenu = Menu::Main;
  menuBeforeKeyboard = Menu::Main;

  keyboardMode = KeyboardMode::Groups;

  singleKeyScreen = SingleKeyScreen::Common;
  singleKeyIndex = 0;

  selectedIndex = 0;
  keyboardIndex = 0;
  selectedGroup = 0;
  suggestionIndex = 0;

  mailboxOpen = false;
  leafletTaken = false;

  lastCommand = nullptr;
  message = nullptr;

   requestUpdate();
}

// ==================================================
// INPUT
// ==================================================

void InteractiveFictionActivity::loop() {

  /*
   * GAME PICKER / STARTUP LAUNCH EXECUTION
   *
   * Never perform Frotz startup/restore while the top Select button
   * is still physically held. This is the same sleep-button safeguard
   * already used by manual saves, loads, rewind, and command submit.
   */
  if (gDeferredPickerAction !=
          DeferredPickerAction::None &&
      !mappedInput.isPressed(
          MappedInputManager::Button::Confirm) &&
      !mappedInput.isPressed(
          MappedInputManager::Button::Power)) {

    const DeferredPickerAction action =
        gDeferredPickerAction;

    const int recoverySlot =
        gDeferredPickerRecoverySlot;

    gDeferredPickerAction =
        DeferredPickerAction::None;

    gDeferredPickerRecoverySlot = -1;
    gDeferredPickerUsesPowerButton = false;

    if (action ==
        DeferredPickerAction::Recover) {

      snprintf(
          gSelectedStoryPath,
          sizeof(gSelectedStoryPath),
          "/adventures/%s",
          gGameFilenames[gGamePickerIndex]);

      gRecoveryPromptActive = false;
      gGamePickerActive = false;

      const bool success =
          loadRecoveryCheckpoint(
              recoverySlot);

      if (success) {

        currentMenu = Menu::Main;
        selectedIndex = 0;

      } else {

        gGamePickerActive = true;
        gRecoveryCheckpointSlot = -1;
      }

      requestUpdate();
      return;
    }

    prepareSelectedStoryForLaunch();

    clearCurrentRecoveryMarker();

    if (action ==
        DeferredPickerAction::StartResume) {

      gSessionOrigin =
          SessionOrigin::Resume;

      gResumeSaveExistedAtStart = true;

      FrotzX3::setRestoreOnStart(true);

    } else {

      gSessionOrigin =
          SessionOrigin::NewGame;

      gResumeSaveExistedAtStart =
          selectedGameHasResumeSave();

      FrotzX3::setRestoreOnStart(false);
    }

    gFrotzStoryLoaded =
        FrotzX3::startStory(
            gSelectedStoryPath);

    gFrotzLastError =
        FrotzX3::lastError();

    gResumePromptActive = false;
    gRecoveryPromptActive = false;
    gGamePickerActive = false;

    currentMenu = Menu::Main;
    selectedIndex = 0;

    requestUpdate();
    return;
  }

  auto armPickerAction =
      [this](DeferredPickerAction action,
             int recoverySlot = -1) {

        const bool usesPowerButton =
            mappedInput.isPressed(
                MappedInputManager::Button::Power);

        if (usesPowerButton) {
          mappedInput.suppressNextPowerRelease();
          mappedInput.suppressNextPowerConfirmRelease();
        }

        armDeferredPickerAction(
            action,
            recoverySlot,
            usesPowerButton);
      };

  if (gGamePickerActive) {

    /*
     * Crash recovery prompt comes before the normal Resume/New Game
     * prompt. RECOVER restores the newest safe rewind checkpoint.
     * IGNORE discards the stale crash marker and continues normally.
     */
    if (gRecoveryPromptActive) {

      if (mappedInput.wasPressed(
              MappedInputManager::Button::Back)) {

        gRecoveryPromptActive = false;
        gRecoveryCheckpointSlot = -1;

        requestUpdate();
        return;
      }

      if (mappedInput.wasPressed(
              MappedInputManager::Button::PageBack) ||
          mappedInput.wasPressed(
              MappedInputManager::Button::Left)) {

        --gRecoveryPromptIndex;

        if (gRecoveryPromptIndex < 0) {
          gRecoveryPromptIndex = 1;
        }

        requestUpdate();
        return;
      }

      if (mappedInput.wasPressed(
              MappedInputManager::Button::PageForward) ||
          mappedInput.wasPressed(
              MappedInputManager::Button::Right)) {

        ++gRecoveryPromptIndex;

        if (gRecoveryPromptIndex > 1) {
          gRecoveryPromptIndex = 0;
        }

        requestUpdate();
        return;
      }

      if (mappedInput.wasPressed(
              MappedInputManager::Button::Confirm)) {

        snprintf(
            gSelectedStoryPath,
            sizeof(gSelectedStoryPath),
            "/adventures/%s",
            gGameFilenames[gGamePickerIndex]);

        if (gRecoveryPromptIndex == 0) {

          armPickerAction(
              DeferredPickerAction::Recover,
              gRecoveryCheckpointSlot);

          return;
        }

        /*
         * IGNORE:
         * The user intentionally declined crash recovery.
         * Remove the stale marker, then continue through the existing
         * Resume/New Game flow.
         */
        clearCurrentRecoveryMarker();

        gRecoveryPromptActive = false;
        gRecoveryCheckpointSlot = -1;

        if (selectedGameHasResumeSave() ||
            selectedGameHasManualSave()) {

          gResumePromptActive = true;
          gResumePromptIndex = 0;

          requestUpdate();
          return;
        }

        /*
         * No normal resume exists: start fresh, but only after
         * the Select button has physically been released.
         */
        armPickerAction(
            DeferredPickerAction::StartFresh);

        return;
      }

      return;
    }

    /*
     * A selected game with an existing resume file gets this
     * intermediate Resume / New Game prompt.
     */
    if (gResumePromptActive) {

      const bool hasResume =
          selectedGameHasResumeSave();

      const bool hasManual =
          selectedGameHasManualSave();

      const int startupChoiceCount =
          (hasResume ? 1 : 0) +
          (hasManual ? 1 : 0) +
          1;  // NEW GAME is always available.

      if (mappedInput.wasPressed(
              MappedInputManager::Button::Back)) {

        gResumePromptActive = false;

        requestUpdate();
        return;
      }

      if (mappedInput.wasPressed(
              MappedInputManager::Button::PageBack) ||
          mappedInput.wasPressed(
              MappedInputManager::Button::Left)) {

        --gResumePromptIndex;

        if (gResumePromptIndex < 0) {
          gResumePromptIndex =
              startupChoiceCount - 1;
        }

        requestUpdate();
        return;
      }

      if (mappedInput.wasPressed(
              MappedInputManager::Button::PageForward) ||
          mappedInput.wasPressed(
              MappedInputManager::Button::Right)) {

        ++gResumePromptIndex;

        if (gResumePromptIndex >=
            startupChoiceCount) {

          gResumePromptIndex = 0;
        }

        requestUpdate();
        return;
      }

      if (mappedInput.wasPressed(
              MappedInputManager::Button::Confirm)) {

        snprintf(
            gSelectedStoryPath,
            sizeof(gSelectedStoryPath),
            "/adventures/%s",
            gGameFilenames[gGamePickerIndex]);

        int choiceIndex = 0;

        const int resumeChoice =
            hasResume
                ? choiceIndex++
                : -1;

        const int manualChoice =
            hasManual
                ? choiceIndex++
                : -1;

        const int newGameChoice =
            choiceIndex;

        /*
         * LOAD MANUAL can now be entered before starting a game.
         * No interpreter is running yet; loadManualSlot() already
         * safely handles that case and starts the story from the
         * selected Quetzal file.
         */
        if (gResumePromptIndex ==
            manualChoice) {

          clearCurrentRecoveryMarker();

          gResumePromptActive = false;
          gGamePickerActive = false;
          gStartupManualLoadActive = true;

          currentMenu = Menu::Load;
          selectedIndex = 0;

          gManualLoadStatusVisible = false;

          requestUpdate();
          return;
        }

        if (gResumePromptIndex ==
            resumeChoice) {

          armPickerAction(
              DeferredPickerAction::StartResume);

          return;
        }

        if (gResumePromptIndex ==
            newGameChoice) {

          armPickerAction(
              DeferredPickerAction::StartFresh);

          return;
        }

        /*
         * Defensive fallback. This should never happen because
         * the visible startup choices are built from the same
         * hasResume/hasManual flags.
         */
        return;
      }

      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Back)) {

      gDeferredPickerAction =
          DeferredPickerAction::None;

      gDeferredPickerRecoverySlot = -1;
      gDeferredPickerUsesPowerButton = false;

      finish();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageBack) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Left)) {

      if (gGameCount > 0) {

        --gGamePickerIndex;

        if (gGamePickerIndex < 0) {
          gGamePickerIndex =
              gGameCount - 1;
        }

        requestUpdate();
      }

      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Right)) {

      if (gGameCount > 0) {

        ++gGamePickerIndex;

        if (gGamePickerIndex >=
            gGameCount) {

          gGamePickerIndex = 0;
        }

        requestUpdate();
      }

      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Confirm)) {

      if (gGameCount <= 0) {
        return;
      }

      /*
       * If the previous session ended without a clean exit and its
       * newest rewind checkpoint still exists, offer crash recovery
       * before the ordinary Resume/New Game decision.
       */
      int recoverySlot = -1;

      if (selectedGameHasRecoverySave(
              &recoverySlot)) {

        gRecoveryPromptActive = true;
        gRecoveryPromptIndex = 0;
        gRecoveryCheckpointSlot =
            recoverySlot;

        requestUpdate();
        return;
      }

      /*
       * If this exact story already has a resume save, ask whether
       * to Resume or start a New Game.
       */
      if (selectedGameHasResumeSave() ||
          selectedGameHasManualSave()) {

        gResumePromptActive = true;
        gResumePromptIndex = 0;

        requestUpdate();
        return;
      }

      /*
       * No resume exists for this story, so launch it fresh.
       */
      /*
       * No resume/manual save exists for this story. Arm a fresh
       * launch now and let Select release perform the actual work.
       */
      armPickerAction(
          DeferredPickerAction::StartFresh);

      return;
    }

    return;
  }

if (gExitReplacePromptActive) {

  if (mappedInput.wasPressed(
          MappedInputManager::Button::Back)) {

    gExitReplacePromptActive = false;

    /*
     * This Back press already dismissed the modal prompt.
     * Swallow its later release so the normal gameplay Back
     * handler does not immediately reopen the same prompt.
     */
    gBackLongPressHandled = true;

    requestUpdate();
    return;
  }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageBack) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Left)) {

      --gExitReplacePromptIndex;

      if (gExitReplacePromptIndex < 0) {
        gExitReplacePromptIndex = 1;
      }

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Right)) {

      ++gExitReplacePromptIndex;

      if (gExitReplacePromptIndex > 1) {
        gExitReplacePromptIndex = 0;
      }

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Confirm)) {

      if (gExitReplacePromptIndex == 0) {

        /*
         * YES:
         * Replace the previous resume save with this
         * New Game session, then quit.
         */
        if (FrotzX3::saveResume()) {

          clearCurrentRecoveryMarker();

          FrotzX3::stopStory();
          finish();

        } else {

          /*
           * Save failed. Stay in the game so progress is
           * not silently lost.
           */
          gExitReplacePromptActive = false;
          requestUpdate();
        }

      } else {

        /*
         * NO:
         * Keep the existing resume save untouched,
         * discard this New Game session, and quit.
         */
        clearCurrentRecoveryMarker();

        FrotzX3::stopStory();
        finish();
      }

      return;
    }

    /*
     * While this modal prompt is active, swallow all
     * other input so the game underneath cannot react.
     */
    return;
  }

  if (gManualSaveOverwritePromptActive) {

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Back)) {

      gManualSaveOverwritePromptActive = false;
      gPendingManualSaveSlot = -1;

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageBack) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Left)) {

      --gManualSaveOverwritePromptIndex;

      if (gManualSaveOverwritePromptIndex < 0) {
        gManualSaveOverwritePromptIndex = 1;
      }

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Right)) {

      ++gManualSaveOverwritePromptIndex;

      if (gManualSaveOverwritePromptIndex > 1) {
        gManualSaveOverwritePromptIndex = 0;
      }

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Confirm)) {

      if (gManualSaveOverwritePromptIndex == 0 &&
          gPendingManualSaveSlot >= 0) {

        gPendingManualSaveExecuteSlot =
            gPendingManualSaveSlot;
      }

      gManualSaveOverwritePromptActive = false;
      gPendingManualSaveSlot = -1;

      requestUpdate();
      return;
    }

    return;
  }

  if (gManualLoadPromptActive) {

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Back)) {

      gManualLoadPromptActive = false;
      gPendingManualLoadSlot = -1;

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageBack) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Left)) {

      --gManualLoadPromptIndex;

      if (gManualLoadPromptIndex < 0) {
        gManualLoadPromptIndex = 1;
      }

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(
            MappedInputManager::Button::Right)) {

      ++gManualLoadPromptIndex;

      if (gManualLoadPromptIndex > 1) {
        gManualLoadPromptIndex = 0;
      }

      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(
            MappedInputManager::Button::Confirm)) {

      if (gManualLoadPromptIndex == 0 &&
          gPendingManualLoadSlot >= 0) {

        gPendingManualLoadExecuteSlot =
            gPendingManualLoadSlot;
      }

      gManualLoadPromptActive = false;
      gPendingManualLoadSlot = -1;

      requestUpdate();
      return;
    }

    return;
  }

  /*
   * MANUAL SAVE / LOAD EXECUTION
   *
   * The top Select button is also the X3 sleep/wake button.
   * Manual save/load can block while Frotz and the SD card work.
   * Waiting until Select is physically released prevents that same
   * press from being interpreted as a sleep hold.
   */
  if (mappedInput.wasReleased(
          MappedInputManager::Button::Confirm)) {

    if (gPendingManualSaveExecuteSlot >= 0) {

      const int slot =
          gPendingManualSaveExecuteSlot;

      gPendingManualSaveExecuteSlot = -1;

      const bool success =
          saveManualSlot(slot);

      gManualSaveStatusVisible = true;
      gManualSaveStatusSucceeded = success;
      gManualSaveStatusSlot = slot;

      requestUpdate();
      return;
    }

    if (gPendingManualLoadExecuteSlot >= 0) {

      const int slot =
          gPendingManualLoadExecuteSlot;

      gPendingManualLoadExecuteSlot = -1;

      const bool success =
          loadManualSlot(slot);

      gManualLoadStatusVisible = true;
      gManualLoadStatusSucceeded = success;
      gManualLoadStatusSlot = slot;

      if (success) {
        gStartupManualLoadActive = false;

        currentMenu = Menu::Main;
        selectedIndex = 0;
      }

      requestUpdate();
      return;
    }

    if (gPendingRewindExecute) {

      const int logIndex =
          gPendingRewindLogIndex;

      gPendingRewindExecute = false;
      gPendingRewindLogIndex = -1;

      const bool success =
          loadRewindCheckpointForLogIndex(
              logIndex);

      if (success) {
        currentMenu = Menu::Main;
        selectedIndex = 0;
      }

      requestUpdate();
      return;
    }

    if (gCommandSubmitArmed) {

      /*
       * The Select button is now physically released, so it is safe
       * to perform the blocking Quetzal checkpoint without risking
       * the X3 interpreting the same press as a sleep hold.
       *
       * Five SD checkpoint files are reused in a ring.  The log can
       * retain older readable entries even after their checkpoint
       * slot has been reused.
       */
      const int checkpointSlot =
          gNextRewindSlot;

      if (saveRewindCheckpoint(
              checkpointSlot)) {

        gPreparedRewindSlot =
            checkpointSlot;

        gNextRewindSlot =
            (gNextRewindSlot + 1) %
            MAX_REWIND_CHECKPOINTS;

      } else {

        gPreparedRewindSlot = -1;
      }

      submitTypedCommand();
      return;
    }
  }

  /*
   * Whenever Frotz reaches an input prompt, the output for that turn
   * is complete and stable. Copy it for the e-ink renderer.
   */
  if (gFrotzStoryLoaded &&
      !gFrotzOutputCaptured &&
      FrotzX3::waitingForInput()) {

    LOG_INF(
        "FROTZTIME",
        "command response COMPLETE");

    const char* output =
        FrotzX3::output();

    gFrotzBootOutput[0] = '\0';

    if (output != nullptr) {

      size_t sourceIndex = 0;
      size_t destIndex = 0;

      bool previousWasSpace = false;

      while (output[sourceIndex] != '\0' &&
             destIndex <
                 sizeof(gFrotzBootOutput) - 1) {

        char c =
            output[sourceIndex++];

        if (c == '\r' ||
            c == '\t') {

          c = ' ';
        }

        if (c == ' ') {

          if (previousWasSpace) {
            continue;
          }

          previousWasSpace = true;

        } else {

          previousWasSpace = false;
        }

        gFrotzBootOutput[
            destIndex++] = c;
      }

      gFrotzBootOutput[
          destIndex] = '\0';
    }
detectContextObjects(
    gFrotzBootOutput);

/*
 * Diagnostic only:
 * identify the likely current room from the first visible line,
 * then dump that real Z-machine object's current child tree.
 */
frotz_debug_room_tree(
    gFrotzBootOutput);

if (gCaptureNextOutputAsInventory) {

  resetInventoryObjects();

  detectInventoryObjects(
      gFrotzBootOutput);

  gCaptureNextOutputAsInventory = false;
}

gTranscriptPage = 0;

gFrotzOutputCaptured = true;

/*
 * A READ_CHAR prompt temporarily turns the lower Actions area into
 * a one-key choice pad. Start that pad at "1" each time it appears.
 */
if (FrotzX3::waitingForKeyInput() &&
    currentMenu == Menu::Main) {

  singleKeyScreen =
      SingleKeyScreen::Common;

  singleKeyIndex = 0;
}

if (gPendingAdventureLogEntry) {

  appendAdventureLogEntry(
      gPendingAdventureLogCommand,
      gFrotzBootOutput,
      gPendingAdventureLogCheckpointSlot);

  gPendingAdventureLogEntry = false;
  gPendingAdventureLogCommand[0] = '\0';
  gPendingAdventureLogCheckpointSlot = -1;
}

    requestUpdate();
  }

  /*
   * BACK BUTTON
   *
   * Tap:
   *   Preserve the existing normal Back behavior.
   *
   * Hold:
   *   Open the out-of-game Game Menu.
   *
   * Like the page-button transcript controls, the tap action
   * happens on release so a long press cannot also trigger Back.
   */
  if (mappedInput.wasPressed(
          MappedInputManager::Button::Back)) {

    gBackLongPressHandled = false;
  }

  if (mappedInput.isPressed(
          MappedInputManager::Button::Back) &&
      !gBackLongPressHandled &&
      currentMenu != Menu::Keyboard &&
      currentMenu != Menu::GameMenu &&
      currentMenu != Menu::AdventureLog &&
      currentMenu != Menu::Save &&
      currentMenu != Menu::Load &&
      !(FrotzX3::waitingForKeyInput() &&
        singleKeyScreen != SingleKeyScreen::Common) &&
      mappedInput.getHeldTime() >=
          GAME_MENU_LONG_PRESS_MS) {

    currentMenu = Menu::GameMenu;
    selectedIndex = 0;

    gManualSaveStatusVisible = false;
    gManualLoadStatusVisible = false;

    gBackLongPressHandled = true;

    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(
          MappedInputManager::Button::Back)) {

    if (!gBackLongPressHandled) {
      goBack();
    }

    gBackLongPressHandled = false;
    return;
  }

  /*
   * REAL FROTZ KEYBOARD ENTRY
   *
   * On the temporary real-Frotz screen, Confirm opens the existing
   * T9-style keyboard.  The keyboard's ENTER key will submit the typed
   * command to the live interpreter.
   */
 if (gFrotzStoryLoaded &&
    currentMenu != Menu::Keyboard &&
    FrotzX3::waitingForInput() &&
    mappedInput.wasPressed(
        MappedInputManager::Button::Confirm)) {

  /*
   * Single-key prompts bypass normal command handling entirely.
   * Send the selected character directly to os_read_key().
   */
  if (FrotzX3::waitingForKeyInput() &&
      currentMenu == Menu::Main) {

    activateSingleKeySelection();
    return;
  }

  activateSelection();
  return;
}

 const int transcriptLineCount =
    countWrappedLines(
        gFrotzBootOutput,
        MAX_LINE_CHARS);

const int transcriptPageCount =
    transcriptLineCount > 0
        ? (transcriptLineCount +
           TRANSCRIPT_LINES_PER_PAGE - 1) /
              TRANSCRIPT_LINES_PER_PAGE
        : 1;


/*
 * PAGE BUTTONS
 *
 * Tap:
 *   Preserve original menu / keyboard navigation.
 *
 * Hold:
 *   Change transcript page when multiple pages exist.
 *
 * The normal navigation action happens on release so a
 * long press does not also move the menu highlight.
 */


/*
 * PAGE BACK
 */
if (mappedInput.wasPressed(
        MappedInputManager::Button::PageBack)) {

  gPageBackLongPressHandled = false;
}


if (mappedInput.isPressed(
        MappedInputManager::Button::PageBack) &&
    !gPageBackLongPressHandled &&
    currentMenu != Menu::Keyboard &&
    transcriptPageCount > 1 &&
    mappedInput.getHeldTime() >=
        TRANSCRIPT_LONG_PRESS_MS) {

  if (gTranscriptPage > 0) {

    --gTranscriptPage;
    requestUpdate();
  }

  gPageBackLongPressHandled = true;

  return;
}


if (mappedInput.wasReleased(
        MappedInputManager::Button::PageBack)) {

  if (!gPageBackLongPressHandled) {

    if (currentMenu == Menu::Keyboard) {
      moveKeyboard(-1);
    } else {
      moveSelection(-1);
    }
  }

  gPageBackLongPressHandled = false;

  return;
}


/*
 * PAGE FORWARD
 */
if (mappedInput.wasPressed(
        MappedInputManager::Button::PageForward)) {

  gPageForwardLongPressHandled = false;
}


if (mappedInput.isPressed(
        MappedInputManager::Button::PageForward) &&
    !gPageForwardLongPressHandled &&
    currentMenu != Menu::Keyboard &&
    transcriptPageCount > 1 &&
    mappedInput.getHeldTime() >=
        TRANSCRIPT_LONG_PRESS_MS) {

  if (gTranscriptPage <
      transcriptPageCount - 1) {

    ++gTranscriptPage;
    requestUpdate();
  }

  gPageForwardLongPressHandled = true;

  return;
}


if (mappedInput.wasReleased(
        MappedInputManager::Button::PageForward)) {

  if (!gPageForwardLongPressHandled) {

    if (currentMenu == Menu::Keyboard) {
      moveKeyboard(1);
    } else {
      moveSelection(1);
    }
  }

  gPageForwardLongPressHandled = false;

  return;
}


/*
 * LEFT / RIGHT
 *
 * Restore their original behavior completely.
 */
if (mappedInput.wasPressed(
        MappedInputManager::Button::Left)) {

  if (currentMenu == Menu::Keyboard) {
    moveKeyboard(-1);
  } else {
    moveSelection(-1);
  }

  return;
}


if (mappedInput.wasPressed(
        MappedInputManager::Button::Right)) {

  if (currentMenu == Menu::Keyboard) {
    moveKeyboard(1);
  } else {
    moveSelection(1);
  }

  return;
}

  if (mappedInput.wasPressed(
          MappedInputManager::Button::Confirm)) {

    if (currentMenu == Menu::Keyboard) {
      activateKeyboardKey();
    } else {
      activateSelection();
    }

    return;
  }
}

// ==================================================
// NORMAL MENU NAVIGATION
// ==================================================

void InteractiveFictionActivity::moveSelection(int delta) {

  if (currentMenu == Menu::Main &&
      FrotzX3::waitingForKeyInput()) {

    moveSingleKeySelection(delta);
    return;
  }

  int itemCount = 0;

  switch (currentMenu) {

    case Menu::Main:
  itemCount = 12;
  break;

    case Menu::Go:
      itemCount = 4;
      break;

case Menu::Take:
case Menu::Examine:
case Menu::Open:
  /*
   * Every contextual-object submenu has one extra item:
   * TYPE...
   */
  itemCount =
      gContextObjectCount + 1;
  break;

    case Menu::GameMenu:
      itemCount = 5;
      break;

    case Menu::AdventureLog:
      if (gAdventureLogCount <= 0) {
        return;
      }

      itemCount =
          gAdventureLogCount;
      break;

    case Menu::Save:
    case Menu::Load:
      itemCount = MANUAL_SAVE_SLOTS;
      break;

    case Menu::Mailbox:
      if (!mailboxOpen) {
        itemCount = 2;
      } else if (!leafletTaken) {
        itemCount = 3;
      } else {
        itemCount = 2;
      }
      break;

    case Menu::Keyboard:
      return;
  }

  selectedIndex += delta;

  if (selectedIndex < 0) {
    selectedIndex = itemCount - 1;
  }

  if (selectedIndex >= itemCount) {
    selectedIndex = 0;
  }

  requestUpdate();
}

// ==================================================
// SINGLE-KEY INPUT
// ==================================================

int InteractiveFictionActivity::getSingleKeyItemCount() const {

  switch (singleKeyScreen) {

    case SingleKeyScreen::Common:
      return 13;

    case SingleKeyScreen::More:
      return 5;

    case SingleKeyScreen::Letters:
      return 26;

    case SingleKeyScreen::Symbols:
      return 24;

    case SingleKeyScreen::Navigation:
      return 6;

    case SingleKeyScreen::FunctionKeys:
      return 12;
  }

  return 0;
}


void InteractiveFictionActivity::moveSingleKeySelection(
    int delta) {

  const int itemCount =
      getSingleKeyItemCount();

  if (itemCount <= 0) {
    return;
  }

  singleKeyIndex += delta;

  if (singleKeyIndex < 0) {
    singleKeyIndex = itemCount - 1;
  }

  if (singleKeyIndex >= itemCount) {
    singleKeyIndex = 0;
  }

  requestUpdate();
}


void InteractiveFictionActivity::submitSingleKeyValue(
    int key) {

  const unsigned char keyByte =
      static_cast<unsigned char>(
          key & 0xff);

  LOG_INF(
      "FROTZKEY",
      "submit single key: 0x%02X",
      static_cast<unsigned int>(
          keyByte));

  if (!FrotzX3::submitKey(
          static_cast<char>(
              keyByte))) {

    LOG_ERR(
        "FROTZKEY",
        "single key submit failed: 0x%02X",
        static_cast<unsigned int>(
            keyByte));

    return;
  }

  gFrotzOutputCaptured = false;
  gFrotzBootOutput[0] = '\0';

  singleKeyScreen =
      SingleKeyScreen::Common;

  singleKeyIndex = 0;

  requestUpdate();
}


void InteractiveFictionActivity::activateSingleKeySelection() {

  switch (singleKeyScreen) {

    case SingleKeyScreen::Common:

      if (singleKeyIndex >= 0 &&
          singleKeyIndex < 12) {

        submitSingleKeyValue(
            SINGLE_KEY_VALUES[
                singleKeyIndex]);

        return;
      }

      if (singleKeyIndex == 12) {

        singleKeyScreen =
            SingleKeyScreen::More;

        singleKeyIndex = 0;

        requestUpdate();
      }

      return;


    case SingleKeyScreen::More:

      switch (singleKeyIndex) {

        case 0:
          singleKeyScreen =
              SingleKeyScreen::Letters;
          break;

        case 1:
          singleKeyScreen =
              SingleKeyScreen::Symbols;
          break;

        case 2:
          singleKeyScreen =
              SingleKeyScreen::Navigation;
          break;

        case 3:
          singleKeyScreen =
              SingleKeyScreen::FunctionKeys;
          break;

        case 4:
          singleKeyScreen =
              SingleKeyScreen::Common;
          break;

        default:
          return;
      }

      singleKeyIndex = 0;
      requestUpdate();
      return;


    case SingleKeyScreen::Letters:

      if (singleKeyIndex >= 0 &&
          singleKeyIndex < 26) {

        submitSingleKeyValue(
            'A' + singleKeyIndex);
      }

      return;


    case SingleKeyScreen::Symbols:

      if (singleKeyIndex >= 0 &&
          singleKeyIndex < 24) {

        submitSingleKeyValue(
            SYMBOL_KEY_VALUES[
                singleKeyIndex]);
      }

      return;


    case SingleKeyScreen::Navigation:

      if (singleKeyIndex >= 0 &&
          singleKeyIndex < 6) {

        submitSingleKeyValue(
            NAVIGATION_KEY_VALUES[
                singleKeyIndex]);
      }

      return;


    case SingleKeyScreen::FunctionKeys:

      if (singleKeyIndex >= 0 &&
          singleKeyIndex < 12) {

        submitSingleKeyValue(
            ZKEY_F1 +
            singleKeyIndex);
      }

      return;
  }
}


// ==================================================
// NORMAL COMMANDS
// ==================================================

void InteractiveFictionActivity::activateSelection() {

  switch (currentMenu) {

    case Menu::Main:

  switch (selectedIndex) {

    case 0:

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "LOOK");

      submitTypedCommand();
      return;

    case 1:

      currentMenu = Menu::Go;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 2:

      currentMenu = Menu::Take;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 3:

      openKeyboard();

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "DROP ");

      requestUpdate();
      return;

    case 4:

      currentMenu = Menu::Examine;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 5:

      currentMenu = Menu::Open;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 6:

      openKeyboard();

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "CLOSE ");

      requestUpdate();
      return;

    case 7:

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "INVENTORY");

      submitTypedCommand();
      return;

    case 8:

      openKeyboard();

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "READ ");

      requestUpdate();
      return;

    case 9:

      openKeyboard();

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "SEARCH ");

      requestUpdate();
      return;

    case 10:

      openKeyboard();

      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "CLIMB ");

      requestUpdate();
      return;

    case 11:

      typedCommand[0] = '\0';

      openKeyboard();
      return;
  }

  break;

    case Menu::Go:

  switch (selectedIndex) {

    case 0:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "NORTH");
      break;

    case 1:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "SOUTH");
      break;

    case 2:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "EAST");
      break;

    case 3:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "WEST");
      break;
  }

  submitTypedCommand();
  return;

  case Menu::Take:

  /*
   * Last item is always TYPE...
   */
  if (selectedIndex >= gContextObjectCount) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s",
        "TAKE ");

    openKeyboard();
    return;
  }

  snprintf(
      typedCommand,
      sizeof(typedCommand),
      "TAKE %s",
      gContextObjects[selectedIndex]);

  submitTypedCommand();
  return;


case Menu::Examine:

  /*
   * Last item is always TYPE...
   */
  if (selectedIndex >= gContextObjectCount) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s",
        "EXAMINE ");

    openKeyboard();
    return;
  }

  snprintf(
      typedCommand,
      sizeof(typedCommand),
      "EXAMINE %s",
      gContextObjects[selectedIndex]);

  submitTypedCommand();
  return;


case Menu::Open:

  /*
   * Last item is always TYPE...
   */
  if (selectedIndex >= gContextObjectCount) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s",
        "OPEN ");

    openKeyboard();
    return;
  }

  snprintf(
      typedCommand,
      sizeof(typedCommand),
      "OPEN %s",
      gContextObjects[selectedIndex]);

  submitTypedCommand();
  return;

    case Menu::GameMenu:

      if (selectedIndex == 0) {

        gManualSaveStatusVisible = false;

        currentMenu = Menu::Save;
        selectedIndex = 0;

        requestUpdate();
        return;
      }

      if (selectedIndex == 1) {

        gManualLoadStatusVisible = false;
        gStartupManualLoadActive = false;

        currentMenu = Menu::Load;
        selectedIndex = 0;

        requestUpdate();
        return;
      }

      if (selectedIndex == 2) {

        /*
         * Quick one-step REWIND.
         *
         * This simply targets the newest still-rewindable Adventure
         * Log entry.  Execution waits for Select release.
         */
        const int rewindIndex =
            latestRewindLogIndex();

        if (rewindIndex >= 0) {

          gPendingRewindLogIndex =
              rewindIndex;

          gPendingRewindExecute = true;
        }

        requestUpdate();
        return;
      }

      if (selectedIndex == 3) {

        currentMenu =
            Menu::AdventureLog;

        if (gAdventureLogCount > 0) {
          gAdventureLogIndex =
              gAdventureLogCount - 1;
          selectedIndex =
              gAdventureLogIndex;
        } else {
          gAdventureLogIndex = 0;
          selectedIndex = 0;
        }

        requestUpdate();
        return;
      }

      if (selectedIndex == 4) {

        /*
         * EXIT GAME
         *
         * Use the same safe exit behavior that previously lived on
         * a short Back press from the main gameplay screen.
         */
        if (gSessionOrigin == SessionOrigin::NewGame &&
            gResumeSaveExistedAtStart) {

          gExitReplacePromptActive = true;
          gExitReplacePromptIndex = 1;  // Default to NO.

          requestUpdate();
          return;
        }

        if (FrotzX3::saveResume()) {

          clearCurrentRecoveryMarker();

          FrotzX3::stopStory();
          finish();
        }

        /*
         * If autosave fails, stay in the game instead of silently
         * discarding progress.
         */
        return;
      }

      return;

    case Menu::AdventureLog:

      if (gAdventureLogCount <= 0 ||
          selectedIndex < 0 ||
          selectedIndex >=
              gAdventureLogCount) {

        return;
      }

      gAdventureLogIndex =
          selectedIndex;

      if (gAdventureLog[
              gAdventureLogIndex].hasRewind) {

        gPendingRewindLogIndex =
            gAdventureLogIndex;

        gPendingRewindExecute =
            true;
      }

      requestUpdate();
      return;

    case Menu::Save: {

      const int slot =
          selectedIndex;

      if (slot < 0 ||
          slot >= MANUAL_SAVE_SLOTS) {

        return;
      }

      gManualSaveStatusVisible = false;

      if (manualSaveSlotExists(slot)) {

        gPendingManualSaveSlot = slot;
        gManualSaveOverwritePromptActive = true;
        gManualSaveOverwritePromptIndex = 1;

        requestUpdate();
        return;
      }

      gPendingManualSaveExecuteSlot =
          slot;

      requestUpdate();
      return;
    }

    case Menu::Load: {

      const int slot =
          selectedIndex;

      if (slot < 0 ||
          slot >= MANUAL_SAVE_SLOTS) {

        return;
      }

      gManualLoadStatusVisible = false;

      /*
       * EMPTY slots are visible but unavailable.
       */
      if (!manualSaveSlotExists(slot)) {

        gManualLoadStatusVisible = true;
        gManualLoadStatusSucceeded = false;
        gManualLoadStatusSlot = slot;

        requestUpdate();
        return;
      }

      gPendingManualLoadSlot = slot;
      gManualLoadPromptActive = true;
      gManualLoadPromptIndex = 1;  // Default to NO.

      requestUpdate();
      return;
    }

    case Menu::Mailbox:

      if (!mailboxOpen) {

        if (selectedIndex == 0) {

          lastCommand = "OPEN MAILBOX";

          mailboxOpen = true;

          message =
              "You open the mailbox, revealing a small leaflet.";

          selectedIndex = 0;

        } else {

          currentMenu = Menu::Examine;
          selectedIndex = 0;
        }

      } else if (!leafletTaken) {

        switch (selectedIndex) {

          case 0:

            lastCommand = "TAKE LEAFLET";

            leafletTaken = true;

            message = "Taken.";

            selectedIndex = 0;

            break;

          case 1:

            lastCommand = "EXAMINE LEAFLET";

            message =
                "The leaflet welcomes you to the "
                "Great Underground Empire.";

            break;

          case 2:

            currentMenu = Menu::Examine;
            selectedIndex = 0;

            break;
        }

      } else {

        if (selectedIndex == 0) {

          lastCommand = "EXAMINE MAILBOX";

          message =
              "The mailbox is open and empty.";

        } else {

          currentMenu = Menu::Examine;
          selectedIndex = 0;
        }
      }

      break;

    case Menu::Keyboard:
      break;
  }

  requestUpdate();
}

// ==================================================
// BACK
// ==================================================

void InteractiveFictionActivity::goBack() {

  if (currentMenu == Menu::Main &&
      FrotzX3::waitingForKeyInput() &&
      singleKeyScreen !=
          SingleKeyScreen::Common) {

    if (singleKeyScreen ==
        SingleKeyScreen::More) {

      singleKeyScreen =
          SingleKeyScreen::Common;

    } else {

      singleKeyScreen =
          SingleKeyScreen::More;
    }

    singleKeyIndex = 0;

    requestUpdate();
    return;
  }

  if (currentMenu == Menu::Keyboard) {

    if (keyboardMode == KeyboardMode::Letters ||
        keyboardMode == KeyboardMode::Suggestions) {

      keyboardMode = KeyboardMode::Groups;
      keyboardIndex = 0;

      requestUpdate();
      return;
    }

    currentMenu = menuBeforeKeyboard;

    selectedIndex = 0;

    typedCommand[0] = '\0';

    requestUpdate();
    return;
  }

if (currentMenu == Menu::Save ||
    currentMenu == Menu::Load) {

  const bool leavingSaveMenu =
      currentMenu == Menu::Save;

  /*
   * LOAD MANUAL may be opened directly from the pre-game startup
   * choices. In that case Back should return there, not to the
   * in-game Game Menu (because no game is running yet).
   */
  if (currentMenu == Menu::Load &&
      gStartupManualLoadActive) {

    gStartupManualLoadActive = false;

    gManualLoadStatusVisible = false;

    gGamePickerActive = true;
    gResumePromptActive = true;

    requestUpdate();
    return;
  }

  currentMenu = Menu::GameMenu;
  selectedIndex = 0;

  if (leavingSaveMenu) {
    gManualSaveStatusVisible = false;
  } else {
    gManualLoadStatusVisible = false;
  }

  requestUpdate();
  return;
}

if (currentMenu == Menu::AdventureLog) {

  currentMenu = Menu::GameMenu;
  selectedIndex = 3;

  requestUpdate();
  return;
}

if (currentMenu == Menu::GameMenu) {

  currentMenu = Menu::Main;
  selectedIndex = 0;

  requestUpdate();
  return;
}

if (currentMenu == Menu::Main) {

  /*
   * A short Back press no longer exits the story.
   *
   * Exit now lives behind:
   *
   *   Hold Back -> Game Menu -> EXIT GAME
   *
   * Keeping short Back unused on the main gameplay screen also
   * leaves it available for a future gameplay shortcut.
   */
  return;
}

  if (currentMenu == Menu::Mailbox) {
    currentMenu = Menu::Examine;
  } else {
    currentMenu = Menu::Main;
  }

  selectedIndex = 0;

  requestUpdate();
}

// ==================================================
// OPEN KEYBOARD
// ==================================================

void InteractiveFictionActivity::openKeyboard() {

  /*
   * Remember where the keyboard was opened from before
   * switching currentMenu to Keyboard.
   */
  menuBeforeKeyboard = currentMenu;

  /*
   * Contextual submenus pre-fill their verb.
   * Main -> TYPE COMMAND starts blank.
   */
  switch (menuBeforeKeyboard) {

    case Menu::Take:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "TAKE ");
      break;

    case Menu::Examine:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "EXAMINE ");
      break;

    case Menu::Open:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          "OPEN ");
      break;

    default:
      typedCommand[0] = '\0';
      break;
  }

  currentMenu = Menu::Keyboard;

  keyboardMode = KeyboardMode::Groups;

  /*
   * If the prefilled command already has useful completions,
   * make SUGGEST the default action.  Otherwise start at ABC.
   */
  keyboardIndex =
      getSuggestionCount() > 0
          ? 9
          : 0;

  selectedGroup = 0;
  suggestionIndex = 0;

  requestUpdate();
}

// ==================================================
// KEYBOARD NAVIGATION
// ==================================================

void InteractiveFictionActivity::moveKeyboard(int delta) {

  int itemCount = 0;

  if (keyboardMode == KeyboardMode::Groups) {

    itemCount = 12;

  } else if (keyboardMode == KeyboardMode::Letters) {

    itemCount =
        strlen(KEY_GROUPS[selectedGroup]);

  } else {

    itemCount =
        getSuggestionCount();

    if (itemCount <= 0) {
      keyboardMode = KeyboardMode::Groups;
      keyboardIndex = 0;

      requestUpdate();
      return;
    }
  }

  keyboardIndex += delta;

  if (keyboardIndex < 0) {
    keyboardIndex = itemCount - 1;
  }

  if (keyboardIndex >= itemCount) {
    keyboardIndex = 0;
  }

  requestUpdate();
}

// ==================================================
// KEYBOARD SELECT
// ==================================================

void InteractiveFictionActivity::activateKeyboardKey() {

  if (keyboardMode == KeyboardMode::Groups) {

    if (keyboardIndex < GROUP_COUNT) {

      selectedGroup = keyboardIndex;

      keyboardMode = KeyboardMode::Letters;

      keyboardIndex = 0;

      requestUpdate();
      return;
    }

    if (keyboardIndex == 8) {

      const size_t length =
          strlen(typedCommand);

      if (length > 0 &&
          length < sizeof(typedCommand) - 1 &&
          typedCommand[length - 1] != ' ') {

        typedCommand[length] = ' ';
        typedCommand[length + 1] = '\0';
      }

      suggestionIndex = 0;

      /*
       * After editing the command, prefer SUGGEST whenever
       * completions are available.
       */
      keyboardIndex =
          getSuggestionCount() > 0
              ? 9
              : 8;

      requestUpdate();
      return;
    }

    if (keyboardIndex == 9) {

      if (getSuggestionCount() > 0) {

        keyboardMode =
            KeyboardMode::Suggestions;

        keyboardIndex = 0;
      }

      requestUpdate();
      return;
    }

    if (keyboardIndex == 10) {

      const size_t length =
          strlen(typedCommand);

      if (length > 0) {
        typedCommand[length - 1] = '\0';
      }

      suggestionIndex = 0;

      keyboardIndex =
          getSuggestionCount() > 0
              ? 9
              : 10;

      requestUpdate();
      return;
    }

    if (keyboardIndex == 11) {

      submitTypedCommand();
      return;
    }
  }

  if (keyboardMode == KeyboardMode::Letters) {

    const char* group =
        KEY_GROUPS[selectedGroup];

    const size_t length =
        strlen(typedCommand);

    if (length < sizeof(typedCommand) - 1) {

      typedCommand[length] =
          group[keyboardIndex];

      typedCommand[length + 1] =
          '\0';
    }

    keyboardMode =
        KeyboardMode::Groups;

    /*
     * Once a letter creates matching completions, put SUGGEST
     * under Select automatically.  If there are no completions,
     * return to ABC as before.
     */
    keyboardIndex =
        getSuggestionCount() > 0
            ? 9
            : 0;

    suggestionIndex = 0;

    requestUpdate();
    return;
  }

  if (keyboardMode == KeyboardMode::Suggestions) {

    suggestionIndex =
        keyboardIndex;

    acceptSuggestion();

    return;
  }
}

// ==================================================
// AUTOCOMPLETE
// ==================================================

int InteractiveFictionActivity::getSuggestionCount() const {

  int count = 0;

  for (int i = 0;
       i < 3;
       ++i) {

    if (getSuggestion(i) == nullptr) {
      break;
    }

    ++count;
  }

  return count;
}

const char*
InteractiveFictionActivity::getSuggestion(
    int index) const {

  if (typedCommand[0] == '\0') {
    return nullptr;
  }

  static const char* objectVerbs[] = {
      "TAKE ",
      "EXAMINE ",
      "OPEN ",
      "CLOSE ",
      "READ ",
      "SEARCH ",
      "CLIMB ",
      "DROP "
  };

  constexpr int objectVerbCount =
      sizeof(objectVerbs) /
      sizeof(objectVerbs[0]);

  /*
   * A static buffer is safe here because the renderer consumes
   * each returned suggestion before requesting the next one.
   */
  static char contextualSuggestion[64];

  for (int verbIndex = 0;
       verbIndex < objectVerbCount;
       ++verbIndex) {

    const char* verb =
        objectVerbs[verbIndex];

    const size_t verbLength =
        strlen(verb);

    if (strncmp(
            typedCommand,
            verb,
            verbLength) == 0) {

      int found = 0;

      const bool inventoryFirst =
          strcmp(verb, "DROP ") == 0;

      for (int pass = 0;
           pass < 2;
           ++pass) {

        const bool useInventory =
            inventoryFirst
                ? (pass == 0)
                : (pass == 1);

        const int objectCount =
            useInventory
                ? gInventoryObjectCount
                : gContextObjectCount;

        for (int i = objectCount - 1;
             i >= 0;
             --i) {

          const char* object =
              useInventory
                  ? gInventoryObjects[i]
                  : gContextObjects[i];

          bool duplicateInPreferredCache = false;

          if (pass == 1) {

            if (inventoryFirst) {

              duplicateInPreferredCache =
                  inventoryObjectAlreadyExists(
                      object);

            } else {

              duplicateInPreferredCache =
                  contextObjectAlreadyExists(
                      object);
            }
          }

          if (duplicateInPreferredCache) {
            continue;
          }

          snprintf(
              contextualSuggestion,
              sizeof(contextualSuggestion),
              "%s%s",
              verb,
              object);

          if (beginsWith(
                  contextualSuggestion,
                  typedCommand)) {

            if (found == index) {
              return contextualSuggestion;
            }

            ++found;

            if (found >= 3) {
              return nullptr;
            }
          }
        }
      }

      return nullptr;
    }
  }

  static const char* commands[] = {
      "LOOK",
      "INVENTORY",
      "NORTH",
      "SOUTH",
      "EAST",
      "WEST",
      "UP",
      "DOWN",
      "ENTER",
      "EXIT",
      "OPEN ",
      "CLOSE ",
      "TAKE ",
      "DROP ",
      "EXAMINE ",
      "READ ",
      "SEARCH ",
      "CLIMB "
  };

  constexpr int commandCount =
      sizeof(commands) /
      sizeof(commands[0]);

  int found = 0;

  for (int i = 0;
       i < commandCount;
       ++i) {

    if (beginsWith(
            commands[i],
            typedCommand)) {

      if (found == index) {
        return commands[i];
      }

      ++found;

      if (found >= 3) {
        break;
      }
    }
  }

  return nullptr;
}

void InteractiveFictionActivity::acceptSuggestion() {

  const char* suggestion =
      getSuggestion(suggestionIndex);

  if (suggestion == nullptr) {
    return;
  }

  snprintf(
      typedCommand,
      sizeof(typedCommand),
      "%s",
      suggestion);

  keyboardMode =
      KeyboardMode::Groups;

  keyboardIndex = 11;

  requestUpdate();
}

// ==================================================
// SUBMIT TYPED COMMAND
// ==================================================

void InteractiveFictionActivity::submitTypedCommand() {

  if (typedCommand[0] == '\0') {
    return;
  }

  /*
   * Every current command submission originates from the top Select
   * button.  The rewind checkpoint is a blocking SD/Quetzal write, so
   * do not perform it while that button is still physically held.
   *
   * First call (Select press):
   *   arm the command and return.
   *
   * Second call (Select release, from loop()):
   *   execute the command after the rewind checkpoint has completed.
   */
  if (!gCommandSubmitArmed) {

    gCommandSubmitArmed = true;
    return;
  }

  gCommandSubmitArmed = false;

  /*
   * Frotz only accepts a line while the interpreter is blocked inside
   * os_read_line().  If it somehow is not ready yet, keep the keyboard
   * open rather than throwing the command away.
   */
  if (!gFrotzStoryLoaded ||
      !FrotzX3::waitingForInput()) {

    return;
  }

  static char commandCopy[64];

  snprintf(
      commandCopy,
      sizeof(commandCopy),
      "%s",
      typedCommand);
/*
 * LOOK and movement commands establish a fresh location context.
 *
 * Clear objects from the previous room before Frotz produces the
 * new description. The resulting output will repopulate the cache.
 */
const bool resetsRoomContext =
    strcmp(commandCopy, "LOOK") == 0 ||
    strcmp(commandCopy, "NORTH") == 0 ||
    strcmp(commandCopy, "SOUTH") == 0 ||
    strcmp(commandCopy, "EAST") == 0 ||
    strcmp(commandCopy, "WEST") == 0 ||
    strcmp(commandCopy, "UP") == 0 ||
    strcmp(commandCopy, "DOWN") == 0 ||
    strcmp(commandCopy, "N") == 0 ||
    strcmp(commandCopy, "S") == 0 ||
    strcmp(commandCopy, "E") == 0 ||
    strcmp(commandCopy, "W") == 0 ||
    strcmp(commandCopy, "U") == 0 ||
    strcmp(commandCopy, "D") == 0 ||
    strcmp(commandCopy, "GO NORTH") == 0 ||
    strcmp(commandCopy, "GO SOUTH") == 0 ||
    strcmp(commandCopy, "GO EAST") == 0 ||
    strcmp(commandCopy, "GO WEST") == 0;

if (resetsRoomContext) {
  resetContextObjects();
}

const bool requestsInventory =
    strcmp(commandCopy, "INVENTORY") == 0 ||
    strcmp(commandCopy, "I") == 0;

  LOG_INF(
      "FROTZTIME",
      "command submit BEGIN: %s",
      commandCopy);

  if (!FrotzX3::submitCommand(
          typedCommand)) {

    LOG_ERR(
        "FROTZTIME",
        "command submit FAILED: %s",
        commandCopy);

    gPreparedRewindSlot = -1;
    return;
  }

  LOG_INF(
      "FROTZTIME",
      "command submit ACCEPTED: %s",
      commandCopy);

  snprintf(
      gPendingAdventureLogCommand,
      sizeof(gPendingAdventureLogCommand),
      "%s",
      commandCopy);

  gPendingAdventureLogCheckpointSlot =
      gPreparedRewindSlot;

  gPendingAdventureLogEntry =
      true;

  gPreparedRewindSlot = -1;

  if (requestsInventory) {
    gCaptureNextOutputAsInventory = true;
  }

  /*
   * Keep a copy for later transcript/UI work.
   */
  lastCommand =
      commandCopy;

  /*
   * The Frotz-side handoff clears its capture buffer before releasing
   * os_read_line().  Mark our local copy stale as well.  When Frotz
   * reaches the next input prompt, loop() will copy the real response.
   */
  gFrotzOutputCaptured =
      false;

  gFrotzBootOutput[0] =
      '\0';

  typedCommand[0] =
      '\0';

  currentMenu =
      Menu::Main;

  keyboardMode =
      KeyboardMode::Groups;

  selectedIndex = 0;
  keyboardIndex = 0;
  suggestionIndex = 0;

  requestUpdate();
}

// ==================================================
// RENDER
// ==================================================

void InteractiveFictionActivity::render(
    RenderLock&&) {

  const auto pageWidth =
      renderer.getScreenWidth();

  renderer.clearScreen();

/*
 * Compact three-part gameplay header.
 *
 * Keep the standard CrossInk header for picker / modal screens.
 * During active gameplay, draw three independently positioned fields:
 *
 *   FrotzX3        Story.z5        1/3
 *
 * This keeps the story name truly centered and the page status
 * consistently right-aligned regardless of filename/page-count length.
 */
if (!gGamePickerActive &&
    gFrotzStoryLoaded) {

  GUI.drawHeader(
      renderer,
      Rect{
          0,
          0,
          pageWidth,
          HEADER_HEIGHT
      },
      "");

  const char* storyFilename =
      strrchr(
          gSelectedStoryPath,
          '/');

  if (storyFilename != nullptr) {
    ++storyFilename;
  } else {
    storyFilename =
        gSelectedStoryPath;
  }

  const int headerTranscriptLineCount =
      countWrappedLines(
          gFrotzBootOutput,
          MAX_LINE_CHARS);

  const int headerTranscriptPageCount =
      headerTranscriptLineCount > 0
          ? (headerTranscriptLineCount +
             TRANSCRIPT_LINES_PER_PAGE - 1) /
                TRANSCRIPT_LINES_PER_PAGE
          : 1;

  char pageStatus[16] = "*/*";

  if (headerTranscriptPageCount > 1) {

    snprintf(
        pageStatus,
        sizeof(pageStatus),
        "%d/%d",
        gTranscriptPage + 1,
        headerTranscriptPageCount);
  }

  constexpr int HEADER_TEXT_Y = 48;

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      HEADER_TEXT_Y,
      "FrotzX3",
      true,
      EpdFontFamily::BOLD);

  /*
   * UI_12 is approximately 8 pixels wide per character on the X3.
   * Use that to center the filename without depending on a separate
   * text-measurement API.
   */
  constexpr int APPROX_CHAR_WIDTH = 8;

  int storyX =
      (pageWidth -
       static_cast<int>(
           strlen(storyFilename)) *
           APPROX_CHAR_WIDTH) / 2;

  if (storyX < 120) {
    storyX = 120;
  }

  renderer.drawText(
      UI_12_FONT_ID,
      storyX,
      HEADER_TEXT_Y,
      storyFilename,
      true,
      EpdFontFamily::BOLD);

  int pageX =
      pageWidth -
      LEFT_MARGIN -
      static_cast<int>(
          strlen(pageStatus)) *
          APPROX_CHAR_WIDTH;

  renderer.drawText(
      UI_12_FONT_ID,
      pageX,
      HEADER_TEXT_Y,
      pageStatus,
      true,
      EpdFontFamily::BOLD);

} else {

  GUI.drawHeader(
      renderer,
      Rect{
          0,
          0,
          pageWidth,
          HEADER_HEIGHT
      },
      "FrotzX3");
}

if (gExitReplacePromptActive) {

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      110,
      "Replace existing resume save?",
      true,
      EpdFontFamily::BOLD);

  static const char* options[] = {
      "YES",
      "NO"
  };

  for (int i = 0;
       i < 2;
       ++i) {

    char label[32];

    snprintf(
        label,
        sizeof(label),
        i == gExitReplacePromptIndex
            ? "> %s"
            : "  %s",
        options[i]);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        175 + i * 50,
        label,
        true,
        i == gExitReplacePromptIndex
            ? EpdFontFamily::BOLD
            : EpdFontFamily::REGULAR);
  }

  const auto labels =
      mappedInput.mapLabels(
          "Back",
          "Select",
          "Prev",
          "Next");

  GUI.drawButtonHints(
      renderer,
      labels.btn1,
      labels.btn2,
      labels.btn3,
      labels.btn4);

  renderer.displayBuffer();

  return;
}

  if (gManualSaveOverwritePromptActive) {

    char title[64] = {};

    snprintf(
        title,
        sizeof(title),
        "Replace manual save Slot %d?",
        gPendingManualSaveSlot + 1);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        110,
        title,
        true,
        EpdFontFamily::BOLD);

    static const char* options[] = {
        "YES",
        "NO"
    };

    for (int i = 0;
         i < 2;
         ++i) {

      char label[32];

      snprintf(
          label,
          sizeof(label),
          i == gManualSaveOverwritePromptIndex
              ? "> %s"
              : "  %s",
          options[i]);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          175 + i * 50,
          label,
          true,
          i == gManualSaveOverwritePromptIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  if (gManualLoadPromptActive) {

    char title[64] = {};

    snprintf(
        title,
        sizeof(title),
        "Load manual save Slot %d?",
        gPendingManualLoadSlot + 1);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        110,
        title,
        true,
        EpdFontFamily::BOLD);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        145,
        "Unsaved current progress will be lost.",
        true,
        EpdFontFamily::REGULAR);

    static const char* options[] = {
        "YES",
        "NO"
    };

    for (int i = 0;
         i < 2;
         ++i) {

      char label[32];

      snprintf(
          label,
          sizeof(label),
          i == gManualLoadPromptIndex
              ? "> %s"
              : "  %s",
          options[i]);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          200 + i * 50,
          label,
          true,
          i == gManualLoadPromptIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  if (gGamePickerActive) {

    if (gRecoveryPromptActive) {

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          105,
          gGameFilenames[gGamePickerIndex],
          true,
          EpdFontFamily::BOLD);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          145,
          "Previous session did not exit cleanly.",
          true,
          EpdFontFamily::REGULAR);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          175,
          "Recover the last safe command prompt?",
          true,
          EpdFontFamily::REGULAR);

      static const char* recoveryOptions[] = {
          "RECOVER",
          "IGNORE"
      };

      for (int i = 0;
           i < 2;
           ++i) {

        char label[32] = {};

        snprintf(
            label,
            sizeof(label),
            i == gRecoveryPromptIndex
                ? "[ %s ]"
                : "  %s",
            recoveryOptions[i]);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            235 + i * 55,
            label,
            true,
            i == gRecoveryPromptIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }

      const auto labels =
          mappedInput.mapLabels(
              "Back",
              "Select",
              "Prev",
              "Next");

      GUI.drawButtonHints(
          renderer,
          labels.btn1,
          labels.btn2,
          labels.btn3,
          labels.btn4);

      renderer.displayBuffer();
      return;
    }

    if (gResumePromptActive) {

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          105,
          gGameFilenames[gGamePickerIndex],
          true,
          EpdFontFamily::BOLD);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          140,
          "Start from:",
          true,
          EpdFontFamily::REGULAR);

      const bool hasResume =
          selectedGameHasResumeSave();

      const bool hasManual =
          selectedGameHasManualSave();

      const char* startupOptions[3] = {};
      int startupOptionCount = 0;

      if (hasResume) {
        startupOptions[
            startupOptionCount++] =
                "RESUME";
      }

      if (hasManual) {
        startupOptions[
            startupOptionCount++] =
                "LOAD MANUAL";
      }

      startupOptions[
          startupOptionCount++] =
              "NEW GAME";

      if (gResumePromptIndex >=
          startupOptionCount) {

        gResumePromptIndex = 0;
      }

      for (int i = 0;
           i < startupOptionCount;
           ++i) {

        char label[40] = {};

        snprintf(
            label,
            sizeof(label),
            i == gResumePromptIndex
                ? "[ %s ]"
                : "  %s",
            startupOptions[i]);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            190 + i * 55,
            label,
            true,
            i == gResumePromptIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }

      const auto labels =
          mappedInput.mapLabels(
              "Back",
              "Select",
              "Prev",
              "Next");

      GUI.drawButtonHints(
          renderer,
          labels.btn1,
          labels.btn2,
          labels.btn3,
          labels.btn4);

      renderer.displayBuffer();

      return;
    }

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        110,
        "Choose Game",
        true,
        EpdFontFamily::BOLD);

    if (gGameCount <= 0) {

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          165,
          "No Z-machine games found.",
          true,
          EpdFontFamily::REGULAR);

    } else {

      for (int i = 0;
           i < gGameCount;
           ++i) {

        char label[144];

        snprintf(
            label,
            sizeof(label),
            i == gGamePickerIndex
                ? "> %s"
                : "  %s",
            gGameFilenames[i]);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            165 + i * 42,
            label,
            true,
            i == gGamePickerIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }
    }

    const auto pickerLabels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        pickerLabels.btn1,
        pickerLabels.btn2,
        pickerLabels.btn3,
        pickerLabels.btn4);

    renderer.displayBuffer();

    return;
  }

  /*
   * Expanded READ_CHAR key picker.
   *
   * The normal 1-9/Y/0/N pad remains embedded below the transcript.
   * MORE opens these dedicated screens so unusual keys stay available
   * without turning the normal gameplay UI into a giant keyboard.
   */
  if (gFrotzStoryLoaded &&
      FrotzX3::waitingForKeyInput() &&
      currentMenu == Menu::Main &&
      singleKeyScreen !=
          SingleKeyScreen::Common) {

    const char* title = "More Keys";

    if (singleKeyScreen ==
        SingleKeyScreen::Letters) {

      title = "Letters";

    } else if (singleKeyScreen ==
               SingleKeyScreen::Symbols) {

      title = "Symbols";

    } else if (singleKeyScreen ==
               SingleKeyScreen::Navigation) {

      title = "Navigation";

    } else if (singleKeyScreen ==
               SingleKeyScreen::FunctionKeys) {

      title = "Function Keys";
    }

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        105,
        title,
        true,
        EpdFontFamily::BOLD);

    if (singleKeyScreen ==
        SingleKeyScreen::More) {

      for (int i = 0;
           i < 5;
           ++i) {

        char label[40] = {};

        snprintf(
            label,
            sizeof(label),
            i == singleKeyIndex
                ? "[ %s ]"
                : "  %s",
            MORE_KEY_LABELS[i]);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            165 + i * 58,
            label,
            true,
            i == singleKeyIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }

    } else if (singleKeyScreen ==
               SingleKeyScreen::Letters) {

      constexpr int COL_WIDTH = 145;
      constexpr int ROW_HEIGHT = 45;
      constexpr int GRID_Y = 155;

      for (int i = 0;
           i < 26;
           ++i) {

        const int row = i / 3;
        const int col = i % 3;

        char label[12] = {};

        snprintf(
            label,
            sizeof(label),
            i == singleKeyIndex
                ? "[ %c ]"
                : "  %c",
            static_cast<char>(
                'A' + i));

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN +
                col * COL_WIDTH,
            GRID_Y +
                row * ROW_HEIGHT,
            label,
            true,
            i == singleKeyIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }

    } else if (singleKeyScreen ==
               SingleKeyScreen::Symbols) {

      constexpr int COL_WIDTH = 145;
      constexpr int ROW_HEIGHT = 48;
      constexpr int GRID_Y = 160;

      for (int i = 0;
           i < 24;
           ++i) {

        const int row = i / 3;
        const int col = i % 3;

        char label[18] = {};

        snprintf(
            label,
            sizeof(label),
            i == singleKeyIndex
                ? "[ %s ]"
                : "  %s",
            SYMBOL_KEY_LABELS[i]);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN +
                col * COL_WIDTH,
            GRID_Y +
                row * ROW_HEIGHT,
            label,
            true,
            i == singleKeyIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }

    } else if (singleKeyScreen ==
               SingleKeyScreen::Navigation) {

      constexpr int COL_WIDTH = 220;
      constexpr int ROW_HEIGHT = 65;
      constexpr int GRID_Y = 175;

      for (int i = 0;
           i < 6;
           ++i) {

        const int row = i / 2;
        const int col = i % 2;

        char label[24] = {};

        snprintf(
            label,
            sizeof(label),
            i == singleKeyIndex
                ? "[ %s ]"
                : "  %s",
            NAVIGATION_KEY_LABELS[i]);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN +
                col * COL_WIDTH,
            GRID_Y +
                row * ROW_HEIGHT,
            label,
            true,
            i == singleKeyIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }

    } else if (singleKeyScreen ==
               SingleKeyScreen::FunctionKeys) {

      constexpr int COL_WIDTH = 145;
      constexpr int ROW_HEIGHT = 62;
      constexpr int GRID_Y = 170;

      for (int i = 0;
           i < 12;
           ++i) {

        const int row = i / 3;
        const int col = i % 3;

        char keyName[8] = {};

        snprintf(
            keyName,
            sizeof(keyName),
            "F%d",
            i + 1);

        char label[16] = {};

        snprintf(
            label,
            sizeof(label),
            i == singleKeyIndex
                ? "[ %s ]"
                : "  %s",
            keyName);

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN +
                col * COL_WIDTH,
            GRID_Y +
                row * ROW_HEIGHT,
            label,
            true,
            i == singleKeyIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }
    }

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        555,
        "Back = previous key screen",
        true,
        EpdFontFamily::REGULAR);

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();
    return;
  }

  if (currentMenu == Menu::GameMenu) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        100,
        "Game Menu",
        true,
        EpdFontFamily::BOLD);

    const int latestRewindIndex =
        latestRewindLogIndex();

    const char* menuItems[] = {
        "SAVE GAME",
        "LOAD GAME",
        latestRewindIndex >= 0
            ? "REWIND"
            : "REWIND  [EMPTY]",
        "ADVENTURE LOG",
        "EXIT GAME"
    };

    constexpr int GAME_MENU_START_Y = 155;
    constexpr int GAME_MENU_ROW_HEIGHT = 58;

    for (int i = 0;
         i < 5;
         ++i) {

      char label[48] = {};

      if (i == selectedIndex) {

        snprintf(
            label,
            sizeof(label),
            "[ %s ]",
            menuItems[i]);

      } else {

        snprintf(
            label,
            sizeof(label),
            "  %s",
            menuItems[i]);
      }

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          GAME_MENU_START_Y +
              i * GAME_MENU_ROW_HEIGHT,
          label,
          true,
          i == selectedIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        470,
        "Back = return to game",
        true,
        EpdFontFamily::REGULAR);

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  if (currentMenu == Menu::AdventureLog) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        100,
        "Adventure Log",
        true,
        EpdFontFamily::BOLD);

    if (gAdventureLogCount <= 0) {

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          155,
          "No commands recorded yet.",
          true,
          EpdFontFamily::REGULAR);

    } else {

      if (selectedIndex < 0) {
        selectedIndex = 0;
      }

      if (selectedIndex >=
          gAdventureLogCount) {

        selectedIndex =
            gAdventureLogCount - 1;
      }

      gAdventureLogIndex =
          selectedIndex;

      int firstVisible =
          gAdventureLogIndex -
          (ADVENTURE_LOG_VISIBLE_ROWS - 1);

      if (firstVisible < 0) {
        firstVisible = 0;
      }

      if (firstVisible +
              ADVENTURE_LOG_VISIBLE_ROWS >
          gAdventureLogCount) {

        firstVisible =
            gAdventureLogCount -
            ADVENTURE_LOG_VISIBLE_ROWS;

        if (firstVisible < 0) {
          firstVisible = 0;
        }
      }

      /*
       * Four visible turns, each with:
       *
       *   > 6  WEST [R]
       *       Forest path...
       *       A second wrapped line if needed.
       *
       * This uses the same log data as before; only presentation
       * changes. [R] still means the turn has a live rewind snapshot.
       */
      constexpr int LOG_START_Y = 145;
      constexpr int LOG_ROW_HEIGHT = 96;
      constexpr int LOG_OUTPUT_X = LEFT_MARGIN + 28;
      constexpr int LOG_OUTPUT_MAX_CHARS = 34;

      for (int row = 0;
           row < ADVENTURE_LOG_VISIBLE_ROWS;
           ++row) {

        const int logIndex =
            firstVisible + row;

        if (logIndex >=
            gAdventureLogCount) {

          break;
        }

        const AdventureLogEntry& entry =
            gAdventureLog[logIndex];

        char commandLine[72] = {};

        snprintf(
            commandLine,
            sizeof(commandLine),
            logIndex == gAdventureLogIndex
                ? "> %lu  %.25s%s"
                : "  %lu  %.25s%s",
            entry.turnNumber,
            entry.command,
            entry.hasRewind
                ? " [R]"
                : "");

        const int y =
            LOG_START_Y +
            row * LOG_ROW_HEIGHT;

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            y,
            commandLine,
            true,
            logIndex == gAdventureLogIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);

        const char* outputText =
            entry.output[0] != '\0'
                ? entry.output
                : "(no captured output)";

        drawWrappedText(
            renderer,
            LOG_OUTPUT_X,
            y + 28,
            outputText,
            LOG_OUTPUT_MAX_CHARS,
            2);
      }

      char logPosition[32] = {};

      snprintf(
          logPosition,
          sizeof(logPosition),
          "%d of %d",
          gAdventureLogIndex + 1,
          gAdventureLogCount);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          535,
          "[R] Select = rewind",
          true,
          EpdFontFamily::REGULAR);

      renderer.drawText(
          UI_12_FONT_ID,
          pageWidth - 85,
          535,
          logPosition,
          true,
          EpdFontFamily::REGULAR);
    }

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  if (currentMenu == Menu::Save) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        100,
        "Save Game",
        true,
        EpdFontFamily::BOLD);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        135,
        "Choose a manual save slot",
        true,
        EpdFontFamily::REGULAR);

    constexpr int SAVE_SLOT_START_Y = 190;
    constexpr int SAVE_SLOT_ROW_HEIGHT = 64;

    for (int i = 0;
         i < MANUAL_SAVE_SLOTS;
         ++i) {

      const bool occupied =
          manualSaveSlotExists(i);

      char label[72] = {};

      if (i == selectedIndex) {

        snprintf(
            label,
            sizeof(label),
            "[ SLOT %d ]      %s",
            i + 1,
            occupied
                ? "[SAVED]"
                : "[EMPTY]");

      } else {

        snprintf(
            label,
            sizeof(label),
            "  SLOT %d        %s",
            i + 1,
            occupied
                ? "[SAVED]"
                : "[EMPTY]");
      }

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          SAVE_SLOT_START_Y +
              i * SAVE_SLOT_ROW_HEIGHT,
          label,
          true,
          i == selectedIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

    if (gManualSaveStatusVisible &&
        gManualSaveStatusSlot >= 0) {

      char status[64] = {};

      snprintf(
          status,
          sizeof(status),
          gManualSaveStatusSucceeded
              ? "Slot %d saved."
              : "Slot %d save FAILED.",
          gManualSaveStatusSlot + 1);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          420,
          status,
          true,
          gManualSaveStatusSucceeded
              ? EpdFontFamily::REGULAR
              : EpdFontFamily::BOLD);
    }

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  if (currentMenu == Menu::Load) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        100,
        "Load Game",
        true,
        EpdFontFamily::BOLD);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        135,
        "Choose a manual save slot",
        true,
        EpdFontFamily::REGULAR);

    constexpr int LOAD_SLOT_START_Y = 190;
    constexpr int LOAD_SLOT_ROW_HEIGHT = 64;

    for (int i = 0;
         i < MANUAL_SAVE_SLOTS;
         ++i) {

      const bool occupied =
          manualSaveSlotExists(i);

      char label[72] = {};

      if (i == selectedIndex) {

        snprintf(
            label,
            sizeof(label),
            "[ SLOT %d ]      %s",
            i + 1,
            occupied
                ? "[SAVED]"
                : "[EMPTY]");

      } else {

        snprintf(
            label,
            sizeof(label),
            "  SLOT %d        %s",
            i + 1,
            occupied
                ? "[SAVED]"
                : "[EMPTY]");
      }

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          LOAD_SLOT_START_Y +
              i * LOAD_SLOT_ROW_HEIGHT,
          label,
          true,
          i == selectedIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

    if (gManualLoadStatusVisible &&
        gManualLoadStatusSlot >= 0 &&
        !gManualLoadStatusSucceeded) {

      char status[64] = {};

      if (manualSaveSlotExists(
              gManualLoadStatusSlot)) {

        snprintf(
            status,
            sizeof(status),
            "Slot %d load FAILED.",
            gManualLoadStatusSlot + 1);

      } else {

        snprintf(
            status,
            sizeof(status),
            "Slot %d is empty.",
            gManualLoadStatusSlot + 1);
      }

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          420,
          status,
          true,
          EpdFontFamily::BOLD);
    }

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  if (currentMenu == Menu::Keyboard) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        105,
        "Type Command",
        true,
        EpdFontFamily::BOLD);

    char commandLine[72];

    snprintf(
        commandLine,
        sizeof(commandLine),
        "> %s_",
        typedCommand);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        150,
        commandLine,
        true,
        EpdFontFamily::REGULAR);

    if (keyboardMode ==
        KeyboardMode::Groups) {

      const int suggestionCount =
          getSuggestionCount();

      if (suggestionCount > 0) {

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            195,
            "Suggestions:",
            true,
            EpdFontFamily::BOLD);

        for (int i = 0;
             i < suggestionCount;
             ++i) {

          const char* suggestion =
              getSuggestion(i);

          if (suggestion != nullptr) {

            renderer.drawText(
                UI_12_FONT_ID,
                LEFT_MARGIN,
                225 + i * 25,
                suggestion,
                true,
                EpdFontFamily::REGULAR);
          }
        }
      }

      /*
       * Cleaner keypad layout:
       *
       *   ABC       DEF       GHI
       *   JKL       MNO       PQRS
       *   TUV       WXYZ      SPACE
       *   SUGGEST   DELETE    ENTER
       *
       * SUGGEST is deliberately on the final action row.  When
       * completions exist it becomes the default selected action.
       */
      constexpr int GRID_X = 22;
      constexpr int GRID_Y = 315;

      constexpr int COL_WIDTH = 145;
      constexpr int ROW_HEIGHT = 58;

      const char* items[12] = {
          "ABC",
          "DEF",
          "GHI",

          "JKL",
          "MNO",
          "PQRS",

          "TUV",
          "WXYZ",
          "SPACE",

          "SUGGEST",
          "DELETE",
          "ENTER"
      };

      for (int i = 0;
           i < 12;
           ++i) {

        const int row =
            i / 3;

        const int col =
            i % 3;

        const int x =
            GRID_X +
            col * COL_WIDTH;

        const int y =
            GRID_Y +
            row * ROW_HEIGHT;

        char label[24];

        /*
         * Brackets give the active key a much stronger visual
         * identity than the old floating '>' marker, while staying
         * cheap and crisp on the e-ink display.
         */
        if (i == keyboardIndex) {

          snprintf(
              label,
              sizeof(label),
              "[%s]",
              items[i]);

        } else {

          snprintf(
              label,
              sizeof(label),
              " %s ",
              items[i]);
        }

        renderer.drawText(
            UI_12_FONT_ID,
            x,
            y,
            label,
            true,
            i == keyboardIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }
    }

    else if (keyboardMode ==
             KeyboardMode::Letters) {

      const char* group =
          KEY_GROUPS[selectedGroup];

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          230,
          "Choose Letter",
          true,
          EpdFontFamily::BOLD);

      const int count =
          strlen(group);

      for (int i = 0;
           i < count;
           ++i) {

        char label[8];

        if (i == keyboardIndex) {

          snprintf(
              label,
              sizeof(label),
              "> %c",
              group[i]);

        } else {

          snprintf(
              label,
              sizeof(label),
              "  %c",
              group[i]);
        }

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN + 40,
            290 + i * 55,
            label,
            true,
            i == keyboardIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }
    }

    else {

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          220,
          "Choose Suggestion",
          true,
          EpdFontFamily::BOLD);

      const int suggestionCount =
          getSuggestionCount();

      for (int i = 0;
           i < suggestionCount;
           ++i) {

        const char* suggestion =
            getSuggestion(i);

        if (suggestion == nullptr) {
          continue;
        }

        char line[64];

        if (i == keyboardIndex) {

          snprintf(
              line,
              sizeof(line),
              "> %s",
              suggestion);

        } else {

          snprintf(
              line,
              sizeof(line),
              "  %s",
              suggestion);
        }

        renderer.drawText(
            UI_12_FONT_ID,
            LEFT_MARGIN,
            285 + i * 55,
            line,
            true,
            i == keyboardIndex
                ? EpdFontFamily::BOLD
                : EpdFontFamily::REGULAR);
      }
    }

    const auto labels =
        mappedInput.mapLabels(
            "Back",
            "Select",
            "Prev",
            "Next");

    GUI.drawButtonHints(
        renderer,
        labels.btn1,
        labels.btn2,
        labels.btn3,
        labels.btn4);

    renderer.displayBuffer();

    return;
  }

  /*
   * TEMPORARY REAL-FROTZ SCREEN
   */

  if (gFrotzStoryLoaded) {

    if (gFrotzBootOutput[0] != '\0') {

const int transcriptLineCount =
    countWrappedLines(
        gFrotzBootOutput,
        MAX_LINE_CHARS);

const int transcriptPageCount =
    transcriptLineCount > 0
        ? (transcriptLineCount +
           TRANSCRIPT_LINES_PER_PAGE - 1) /
              TRANSCRIPT_LINES_PER_PAGE
        : 1;

if (gTranscriptPage >=
    transcriptPageCount) {

  gTranscriptPage =
      transcriptPageCount - 1;
}

drawWrappedTextPage(
    renderer,
    LEFT_MARGIN,
    BODY_Y,
    gFrotzBootOutput,
    MAX_LINE_CHARS,
    TRANSCRIPT_LINES_PER_PAGE,
    gTranscriptPage);


    } else {

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN,
          BODY_Y,
          FrotzX3::waitingForInput()
              ? "Frotz is waiting for player input."
              : "Frotz is processing...",
          true,
          EpdFontFamily::REGULAR);
    }

  } else {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        ROOM_TITLE_Y,
        "FROTZ START FAILED",
        true,
        EpdFontFamily::BOLD);

    drawWrappedText(
        renderer,
        LEFT_MARGIN,
        BODY_Y,
        gFrotzLastError,
        MAX_LINE_CHARS,
        8);
  }

// --------------------------------------------------
// LIVE ACTION / OBJECT GRID
// --------------------------------------------------

constexpr int ACTION_TITLE_Y = 425;
constexpr int ACTION_GRID_Y = 460;

constexpr int ACTION_COL_WIDTH = 220;
constexpr int ACTION_ROW_HEIGHT = 32;

if (currentMenu == Menu::Main) {

  if (FrotzX3::waitingForKeyInput()) {

    /*
     * READ_CHAR mode.
     *
     * Replace the ordinary command actions with a direct one-key pad.
     * This handles numbered game menus such as vgame.z8 as well as
     * common Y/N prompts without forcing single keys through the T9
     * line-input keyboard.
     */
    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        ACTION_TITLE_Y,
        "Choose Key",
        true,
        EpdFontFamily::BOLD);

    constexpr int KEY_COL_WIDTH = 145;
    constexpr int KEY_ROW_HEIGHT = 28;

    for (int i = 0;
         i < 13;
         ++i) {

      const int row =
          i / 3;

      const int col =
          i % 3;

      const int x =
          LEFT_MARGIN +
          col * KEY_COL_WIDTH;

      const int y =
          ACTION_GRID_Y +
          row * KEY_ROW_HEIGHT;

      char label[16] = {};

      snprintf(
          label,
          sizeof(label),
          i == singleKeyIndex
              ? "[ %s ]"
              : "  %s",
          SINGLE_KEY_LABELS[i]);

      renderer.drawText(
          UI_12_FONT_ID,
          x,
          y,
          label,
          true,
          i == singleKeyIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

  } else {

    static const char* mainActions[] = {
        "LOOK",
        "GO",
        "TAKE",
        "DROP",
        "EXAMINE",
        "OPEN",
        "CLOSE",
        "INVENTORY",
        "READ",
        "SEARCH",
        "CLIMB",
        "TYPE COMMAND"
    };

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        ACTION_TITLE_Y,
        "Actions",
        true,
        EpdFontFamily::BOLD);

    for (int i = 0;
         i < 12;
         ++i) {

      const int row =
          i / 2;

      const int col =
          i % 2;

      const int x =
          LEFT_MARGIN +
          col * ACTION_COL_WIDTH;

      const int y =
          ACTION_GRID_Y +
          row * ACTION_ROW_HEIGHT;

      char label[32];

      snprintf(
          label,
          sizeof(label),
          i == selectedIndex
              ? "> %s"
              : "  %s",
          mainActions[i]);

      renderer.drawText(
          UI_12_FONT_ID,
          x,
          y,
          label,
          true,
          i == selectedIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }
  }

} else if (currentMenu == Menu::Go) {

  static const char* directions[] = {
      "NORTH",
      "SOUTH",
      "EAST",
      "WEST"
  };

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      ACTION_TITLE_Y,
      "Go",
      true,
      EpdFontFamily::BOLD);

  for (int i = 0;
       i < 4;
       ++i) {

    const int row =
        i / 2;

    const int col =
        i % 2;

    const int x =
        LEFT_MARGIN +
        col * ACTION_COL_WIDTH;

    const int y =
        ACTION_GRID_Y +
        row * ACTION_ROW_HEIGHT;

    char label[24];

    snprintf(
        label,
        sizeof(label),
        i == selectedIndex
            ? "> %s"
            : "  %s",
        directions[i]);

    renderer.drawText(
        UI_12_FONT_ID,
        x,
        y,
        label,
        true,
        i == selectedIndex
            ? EpdFontFamily::BOLD
            : EpdFontFamily::REGULAR);
  }

} else if (
    currentMenu == Menu::Take ||
    currentMenu == Menu::Examine ||
    currentMenu == Menu::Open) {

  const char* verb = "";

  if (currentMenu == Menu::Take) {
    verb = "Take";
  } else if (currentMenu == Menu::Examine) {
    verb = "Examine";
  } else {
    verb = "Open";
  }

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      ACTION_TITLE_Y,
      verb,
      true,
      EpdFontFamily::BOLD);

  /*
   * Objects plus one final TYPE... entry.
   */
  const int itemCount =
      gContextObjectCount + 1;

  for (int i = 0;
       i < itemCount;
       ++i) {

    const int row =
        i / 2;

    const int col =
        i % 2;

    const int x =
        LEFT_MARGIN +
        col * ACTION_COL_WIDTH;

    const int y =
        ACTION_GRID_Y +
        row * ACTION_ROW_HEIGHT;

    const char* item;

    if (i < gContextObjectCount) {
      item =
          gContextObjects[i];
    } else {
      item =
          "TYPE...";
    }

    char label[40];

    snprintf(
        label,
        sizeof(label),
        i == selectedIndex
            ? "> %s"
            : "  %s",
        item);

    renderer.drawText(
        UI_12_FONT_ID,
        x,
        y,
        label,
        true,
        i == selectedIndex
            ? EpdFontFamily::BOLD
            : EpdFontFamily::REGULAR);
  }
}

 const auto bootLabels =
    mappedInput.mapLabels(
        "Back",
        "Select",
        "Prev",
        "Next");

  GUI.drawButtonHints(
      renderer,
      bootLabels.btn1,
      bootLabels.btn2,
      bootLabels.btn3,
      bootLabels.btn4);

  renderer.displayBuffer();

  return;

  /*
   * OLD PROTOTYPE UI BELOW THIS POINT.
   * It remains intentionally unreachable for now.
   */

  if (gFrotzStoryLoaded) {
    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        ROOM_TITLE_Y,
        "FROTZ LOAD SUCCESS",
        true,
        EpdFontFamily::BOLD);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        BODY_Y,
        "Zork I loaded successfully.",
        true);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        BODY_Y + LINE_HEIGHT,
        "Real Frotz init_memory() completed.",
        true);
  } else {
    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        ROOM_TITLE_Y,
        "FROTZ LOAD FAILED",
        true,
        EpdFontFamily::BOLD);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        BODY_Y,
        gFrotzLastError,
        true);
  }

  if (!mailboxOpen) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        BODY_Y + LINE_HEIGHT * 3,
        "There is a small mailbox here.",
        true);

  } else if (!leafletTaken) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        BODY_Y + LINE_HEIGHT * 3,
        "The mailbox is open. A leaflet is inside.",
        true);

  } else {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        BODY_Y + LINE_HEIGHT * 3,
        "The mailbox is open and empty.",
        true);
  }

  if (lastCommand != nullptr) {

    char commandLine[64];

    snprintf(
        commandLine,
        sizeof(commandLine),
        "> %s",
        lastCommand);

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        HISTORY_Y,
        commandLine,
        true,
        EpdFontFamily::BOLD);
  }

  if (message != nullptr) {

    drawWrappedText(
        renderer,
        LEFT_MARGIN,
        HISTORY_Y + LINE_HEIGHT + 8,
        message,
        MAX_LINE_CHARS,
        MAX_RESPONSE_LINES);
  }

  const char* items[5] = {};

  int itemCount = 0;

  switch (currentMenu) {

    case Menu::Main:

      items[0] = "LOOK";
      items[1] = "GO";
      items[2] = "TAKE";
      items[3] = "EXAMINE";
      items[4] = "TYPE COMMAND";

      itemCount = 5;

      break;

    case Menu::Go:

      items[0] = "NORTH";
      items[1] = "SOUTH";
      items[2] = "EAST";
      items[3] = "WEST";

      itemCount = 4;

      break;

    case Menu::Take:

      if (!mailboxOpen) {

        items[0] = "MAILBOX";
        itemCount = 1;

      } else if (!leafletTaken) {

        items[0] = "MAILBOX";
        items[1] = "LEAFLET";

        itemCount = 2;

      } else {

        items[0] = "MAILBOX";
        itemCount = 1;
      }

      break;

    case Menu::Examine:

      items[0] = "MAILBOX";
      items[1] = "HOUSE";
      items[2] = "DOOR";

      itemCount = 3;

      break;

    case Menu::Mailbox:

      if (!mailboxOpen) {

        items[0] = "OPEN";
        items[1] = "BACK";

        itemCount = 2;

      } else if (!leafletTaken) {

        items[0] = "TAKE LEAFLET";
        items[1] = "EXAMINE LEAFLET";
        items[2] = "BACK";

        itemCount = 3;

      } else {

        items[0] = "EXAMINE";
        items[1] = "BACK";

        itemCount = 2;
      }

      break;
case Menu::Open:
  break;
case Menu::GameMenu:
  break;
case Menu::AdventureLog:
  break;
case Menu::Save:
  break;
case Menu::Load:
  break;
    case Menu::Keyboard:
      break;
  }

  int y = MENU_Y;

  for (int i = 0;
       i < itemCount;
       ++i) {

    char line[48];

    if (i == selectedIndex) {

      snprintf(
          line,
          sizeof(line),
          "> %s",
          items[i]);

    } else {

      snprintf(
          line,
          sizeof(line),
          "  %s",
          items[i]);
    }

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        y,
        line,
        true,
        i == selectedIndex
            ? EpdFontFamily::BOLD
            : EpdFontFamily::REGULAR);

    y += LINE_HEIGHT;
  }

  const auto labels =
      mappedInput.mapLabels(
          "Back",
          "Select",
          "Prev",
          "Next");

  GUI.drawButtonHints(
      renderer,
      labels.btn1,
      labels.btn2,
      labels.btn3,
      labels.btn4);

  renderer.displayBuffer();
}
