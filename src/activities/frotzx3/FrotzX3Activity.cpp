#include "FrotzX3Activity.h"

#include <cstdio>
#include <cstring>

#include <GfxRenderer.h>

#include <HalStorage.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <Logging.h>

#include <FrotzX3.h>
#include <FrotzX3Paths.h>

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
    FROTZX3_STORIES_DIR "/lostpig.z8";

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

/*
 * Context-menu candidate storage.
 *
 * Phase 2 raises the transcript/dictionary candidate cap from 16 to 32
 * so the paging code can actually be exercised before the future live
 * object API is merged into these menus.
 *
 * Extra RAM versus the old 16-entry cache:
 *   16 additional entries x 32 bytes = 512 bytes.
 */
static constexpr int MAX_CONTEXT_OBJECTS = 32;
static constexpr int MAX_CONTEXT_OBJECT_LENGTH = 32;

/*
 * The e-ink action grid has 9 rows x 2 columns available for contextual
 * menu entries. Keep paging tied to that physical layout rather than to
 * the candidate-cache size.
 */
static constexpr int CONTEXT_MENU_ITEMS_PER_PAGE = 18;

/*
 * Raw real-Z-machine room tree cache.
 *
 * The object API can identify a room when the visible output begins with
 * a room title (LOOK, movement, startup descriptions, etc.). We retain
 * that bounded result across ordinary turns so later text such as
 * "Opening the mailbox reveals a leaflet" can safely expose an object
 * that was already present in the tree without requiring the response
 * itself to begin with a room title.
 *
 * 24 x 32 bytes = 768 bytes.
 */
static constexpr int MAX_LIVE_ROOM_OBJECTS = 24;
static constexpr int MAX_LIVE_ROOM_OBJECT_LENGTH = 32;

static char gLiveRoomObjects
    [MAX_LIVE_ROOM_OBJECTS]
    [MAX_LIVE_ROOM_OBJECT_LENGTH] = {};

static int gLiveRoomObjectCount = 0;

static char gCurrentRoomName[96] = {};

/*
 * Parser disambiguation UI.
 *
 * Keep this deliberately tiny and fixed-size. The parser's own prompt text
 * remains visible in the transcript; these are only convenience choices
 * extracted from prompts such as:
 *
 *   Do you mean the red key or the blue key?
 */
static constexpr int MAX_PARSER_CHOICES = 4;
static constexpr int MAX_PARSER_CHOICE_LENGTH = 32;

static bool gParserChoiceActive = false;
static int gParserChoiceCount = 0;
static int gParserChoiceIndex = 0;

static char gParserChoices
    [MAX_PARSER_CHOICES]
    [MAX_PARSER_CHOICE_LENGTH] = {};


enum class ContextAction {
  Take,
  Drop,
  Examine,
  Open,
  Read
};

static ContextAction gContextAction =
    ContextAction::Examine;

static constexpr int MAX_CONTEXT_DISPLAY_CHARS = 16;

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

/*
 * Command phrase paired with each visible label.
 *
 * For transcript-only candidates this is normally identical to the
 * display label. Real Z-machine short names may use a different parser
 * noun (for example display "Ensign First Class", command "BLATHER").
 */
static char gContextObjectCommands
    [MAX_CONTEXT_OBJECTS]
    [MAX_CONTEXT_OBJECT_LENGTH] = {};

static int gContextObjectCount = 0;

/*
 * The first entries in the context list are confirmed/exposed real
 * Z-machine objects. Transcript/dictionary-only candidates follow.
 *
 * This affects ordering only. It does NOT decide what verbs are valid.
 */
static int gRealContextObjectCount = 0;

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
      FROTZX3_SAVES_DIR "/%s.sav",
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
        FROTZX3_MANUAL_SAVES_DIR "/%s.manual%d.sav",
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
      FROTZX3_SAVES_DIR "/%s.recovery",
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
      FROTZX3_REWIND_SAVES_DIR "/%s.rewind%d.sav",
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
      FROTZX3_SAVES_DIR);

  Storage.ensureDirectoryExists(
      FROTZX3_MANUAL_SAVES_DIR);

  Storage.ensureDirectoryExists(
      FROTZX3_REWIND_SAVES_DIR);

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
          FROTZX3_SAVES_DIR "/%s.manual%d.sav",
          filename,
          slot + 1);

      snprintf(
          newPath,
          sizeof(newPath),
          FROTZX3_MANUAL_SAVES_DIR "/%s.manual%d.sav",
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
          FROTZX3_SAVES_DIR "/%s.rewind%d.sav",
          filename,
          slot + 1);

      snprintf(
          newPath,
          sizeof(newPath),
          FROTZX3_REWIND_SAVES_DIR "/%s.rewind%d.sav",
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
      FROTZX3_MANUAL_SAVES_DIR "/%s.manual%d.sav",
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
      FROTZX3_REWIND_SAVES_DIR "/%s.rewind%d.sav",
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
      Storage.open(FROTZX3_STORIES_DIR);

  if (!directory ||
      !directory.isDirectory()) {

    LOG_ERR(
        "FROTZ",
        "Could not open " FROTZX3_STORIES_DIR);

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


enum class ParserFeedbackType {
  None,
  Disambiguation,
  UnknownWord,
  IncompleteCommand,
  NotVisible,
  PartialUnderstanding
};

/*
 * Forward declaration.
 *
 * Parser-choice helpers are defined before the main string-helper section,
 * so declare this here for addParserChoice().
 */
bool stringsEqualIgnoreCase(
    const char* a,
    const char* b);


const char* parserFeedbackTypeName(
    ParserFeedbackType type) {

  switch (type) {

    case ParserFeedbackType::Disambiguation:
      return "DISAMBIGUATION";

    case ParserFeedbackType::UnknownWord:
      return "UNKNOWN_WORD";

    case ParserFeedbackType::IncompleteCommand:
      return "INCOMPLETE_COMMAND";

    case ParserFeedbackType::NotVisible:
      return "NOT_VISIBLE";

    case ParserFeedbackType::PartialUnderstanding:
      return "PARTIAL_UNDERSTANDING";

    case ParserFeedbackType::None:
    default:
      return "NONE";
  }
}


ParserFeedbackType classifyParserFeedback(
    const char* text) {

  if (text == nullptr ||
      text[0] == '\0') {

    return ParserFeedbackType::None;
  }

  /*
   * Keep this intentionally conservative.
   *
   * These signatures are parser-feedback phrases observed in our
   * dedicated FrotzX3 Test Lab and common classic parser families.
   * Ordinary in-world failures such as "The door is locked." must
   * remain normal game output, not parser errors.
   */

  if (containsTextIgnoreCase(
          text,
          "WHICH DO YOU MEAN") ||
      containsTextIgnoreCase(
          text,
          "DID YOU MEAN") ||
      (containsTextIgnoreCase(
           text,
           "DO YOU MEAN") &&
       containsTextIgnoreCase(
           text,
           " OR "))) {

    return ParserFeedbackType::Disambiguation;
  }

  if (containsTextIgnoreCase(
          text,
          "I DIDN'T UNDERSTAND") ||
      containsTextIgnoreCase(
          text,
          "I DON'T UNDERSTAND") ||
      containsTextIgnoreCase(
          text,
          "I DO NOT UNDERSTAND") ||
      containsTextIgnoreCase(
          text,
          "I DON'T KNOW THE WORD") ||
      containsTextIgnoreCase(
          text,
          "I DO NOT KNOW THE WORD") ||
      containsTextIgnoreCase(
          text,
          "YOU USED THE WORD")) {

    return ParserFeedbackType::UnknownWord;
  }

  if (containsTextIgnoreCase(
          text,
          "I THINK YOU WANTED TO SAY") ||
      containsTextIgnoreCase(
          text,
          "WHAT DO YOU WANT TO") ||
      containsTextIgnoreCase(
          text,
          "WHAT DO YOU WANT TO DO WITH")) {

    return ParserFeedbackType::IncompleteCommand;
  }

  if (containsTextIgnoreCase(
          text,
          "CAN'T SEE ANY SUCH THING") ||
      containsTextIgnoreCase(
          text,
          "CANNOT SEE ANY SUCH THING") ||
      containsTextIgnoreCase(
          text,
          "YOU CAN'T SEE") ||
      containsTextIgnoreCase(
          text,
          "YOU CANNOT SEE")) {

    return ParserFeedbackType::NotVisible;
  }

  if (containsTextIgnoreCase(
          text,
          "I ONLY UNDERSTOOD YOU AS FAR AS") ||
      containsTextIgnoreCase(
          text,
          "I ONLY UNDERSTOOD")) {

    return ParserFeedbackType::PartialUnderstanding;
  }

  return ParserFeedbackType::None;
}


void resetParserChoices() {

  gParserChoiceActive = false;
  gParserChoiceCount = 0;
  gParserChoiceIndex = 0;

  for (int i = 0;
       i < MAX_PARSER_CHOICES;
       ++i) {

    gParserChoices[i][0] = '\0';
  }
}


bool addParserChoice(
    const char* choice) {

  if (choice == nullptr ||
      choice[0] == '\0' ||
      gParserChoiceCount >=
          MAX_PARSER_CHOICES) {

    return false;
  }

  for (int i = 0;
       i < gParserChoiceCount;
       ++i) {

    if (stringsEqualIgnoreCase(
            gParserChoices[i],
            choice)) {

      return false;
    }
  }

  snprintf(
      gParserChoices[
          gParserChoiceCount],
      MAX_PARSER_CHOICE_LENGTH,
      "%s",
      choice);

  ++gParserChoiceCount;
  return true;
}


void trimParserChoice(
    char* text) {

  if (text == nullptr) {
    return;
  }

  size_t len = strlen(text);

  while (len > 0 &&
         (text[len - 1] == ' ' ||
          text[len - 1] == '?' ||
          text[len - 1] == '.' ||
          text[len - 1] == ',')) {

    text[--len] = '\0';
  }

  size_t start = 0;

  while (text[start] == ' ') {
    ++start;
  }

  if (start > 0) {
    memmove(
        text,
        text + start,
        strlen(text + start) + 1);
  }

  /*
   * Strip a leading English article from each displayed choice.
   * "the red key" becomes "red key", which is a safer compact reply.
   */
  static const char* articles[] = {
      "THE ",
      "A ",
      "AN "
  };

  for (int i = 0;
       i < 3;
       ++i) {

    const size_t articleLen =
        strlen(articles[i]);

    if (strlen(text) > articleLen &&
        strncasecmp(
            text,
            articles[i],
            articleLen) == 0) {

      memmove(
          text,
          text + articleLen,
          strlen(text + articleLen) + 1);

      break;
    }
  }
}


void extractDisambiguationChoices(
    const char* text) {

  resetParserChoices();

  if (text == nullptr ||
      text[0] == '\0') {

    return;
  }

  /*
   * First-pass grammar for common classic parser prompts:
   *
   *   Do you mean the red key or the blue key?
   *
   * This deliberately does not attempt full natural-language parsing.
   * We only split a short prompt around " OR " and use the two noun
   * phrases flanking it.
   */
  const char* orPos = nullptr;

  for (const char* p = text;
       *p != '\0';
       ++p) {

    if (toUpperAscii(p[0]) == ' ' &&
        toUpperAscii(p[1]) == 'O' &&
        toUpperAscii(p[2]) == 'R' &&
        toUpperAscii(p[3]) == ' ') {

      orPos = p;
      break;
    }
  }

  if (orPos == nullptr) {
    return;
  }

  const char* leftStart = text;

  /*
   * Prefer text after "MEAN " so the first choice does not include
   * "Do you mean".
   */
  const char* meanPos = nullptr;

  for (const char* p = text;
       *p != '\0';
       ++p) {

    if (strncasecmp(
            p,
            "MEAN ",
            5) == 0) {

      meanPos = p;
    }
  }

  if (meanPos != nullptr &&
      meanPos < orPos) {

    leftStart = meanPos + 5;
  }

  char left[MAX_PARSER_CHOICE_LENGTH] = {};
  char right[MAX_PARSER_CHOICE_LENGTH] = {};

  size_t leftLen =
      static_cast<size_t>(
          orPos - leftStart);

  if (leftLen >= sizeof(left)) {
    leftLen = sizeof(left) - 1;
  }

  memcpy(
      left,
      leftStart,
      leftLen);

  left[leftLen] = '\0';

  const char* rightStart =
      orPos + 4;

  size_t rightLen = 0;

  while (rightStart[rightLen] != '\0' &&
         rightStart[rightLen] != '\n' &&
         rightStart[rightLen] != '\r' &&
         rightLen <
             sizeof(right) - 1) {

    right[rightLen] =
        rightStart[rightLen];

    ++rightLen;
  }

  right[rightLen] = '\0';

  trimParserChoice(left);
  trimParserChoice(right);

  if (left[0] != '\0') {
    addParserChoice(left);
  }

  if (right[0] != '\0') {
    addParserChoice(right);
  }

  if (gParserChoiceCount >= 2) {

    gParserChoiceActive = true;
    gParserChoiceIndex = 0;

    LOG_INF(
        "FROTZPARSER",
        "disambiguation choices: \"%s\" / \"%s\"",
        gParserChoices[0],
        gParserChoices[1]);
  }
}
bool stringsEqualIgnoreCase(
    const char* a,
    const char* b) {

  if (a == nullptr ||
      b == nullptr) {

    return false;
  }

  while (*a != '\0' &&
         *b != '\0') {

    if (toUpperAscii(*a) !=
        toUpperAscii(*b)) {

      return false;
    }

    ++a;
    ++b;
  }

  return
      *a == '\0' &&
      *b == '\0';
}


bool isSingleContextWord(
    const char* text) {

  if (text == nullptr ||
      text[0] == '\0') {

    return false;
  }

  for (const char* p = text;
       *p != '\0';
       ++p) {

    if (*p == ' ' ||
        *p == '-' ||
        *p == '\t') {

      return false;
    }
  }

  return true;
}


bool containsWholeWordIgnoreCase(
    const char* text,
    const char* word) {

  if (text == nullptr ||
      word == nullptr ||
      word[0] == '\0') {

    return false;
  }

  const size_t wordLength =
      strlen(word);

  for (const char* p = text;
       *p != '\0';
       ++p) {

    if (p != text &&
        isAsciiLetter(p[-1])) {

      continue;
    }

    size_t i = 0;

    while (i < wordLength &&
           p[i] != '\0' &&
           toUpperAscii(p[i]) ==
               toUpperAscii(word[i])) {

      ++i;
    }

    if (i == wordLength &&
        !isAsciiLetter(p[i])) {

      return true;
    }
  }

  return false;
}


void formatContextDisplayName(
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

  char upper[64] = {};
  size_t length = 0;

  while (source[length] != '\0' &&
         length < sizeof(upper) - 1) {

    upper[length] =
        toUpperAscii(source[length]);

    ++length;
  }

  upper[length] = '\0';

  if (length <=
      MAX_CONTEXT_DISPLAY_CHARS) {

    snprintf(
        destination,
        destinationSize,
        "%s",
        upper);

    return;
  }

  const int prefixLength =
      MAX_CONTEXT_DISPLAY_CHARS - 3;

  snprintf(
      destination,
      destinationSize,
      "%.*s...",
      prefixLength,
      upper);
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

  if (object == nullptr ||
      object[0] == '\0') {

    return true;
  }

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    /*
     * Exact duplicate, ignoring display capitalization.
     */
    if (stringsEqualIgnoreCase(
            gContextObjects[i],
            object)) {

      return true;
    }

    /*
     * If the real object API already supplied a useful multi-word
     * short name such as "small mailbox", suppress transcript-only
     * fragments such as SMALL and MAILBOX.
     *
     * Do not perform the reverse replacement here. Real object names
     * are intentionally added before transcript dictionary tokens.
     */
    if (isSingleContextWord(object) &&
        !isSingleContextWord(
            gContextObjects[i]) &&
        containsWholeWordIgnoreCase(
            gContextObjects[i],
            object)) {

      return true;
    }
  }

  return false;
}

void clearContextCandidates() {

  gContextObjectCount = 0;
  gRealContextObjectCount = 0;
  gLiveRoomObjectCount = 0;

  for (int i = 0;
       i < MAX_CONTEXT_OBJECTS;
       ++i) {

    gContextObjects[i][0] =
        '\0';

    gContextObjectCommands[i][0] =
        '\0';
  }

  for (int i = 0;
       i < MAX_LIVE_ROOM_OBJECTS;
       ++i) {

    gLiveRoomObjects[i][0] =
        '\0';
  }
}


void resetContextObjects() {

  clearContextCandidates();

  gCurrentRoomName[0] =
      '\0';
}

bool contextDisplayMatchesInventory(
    const char* contextDisplay) {

  if (contextDisplay == nullptr ||
      contextDisplay[0] == '\0') {

    return false;
  }

  for (int i = 0;
       i < gInventoryObjectCount;
       ++i) {

    if (stringsEqualIgnoreCase(
            contextDisplay,
            gInventoryObjects[i])) {

      return true;
    }
  }

  return false;
}


int dropContextExtraCount() {

  int count = 0;

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    if (!contextDisplayMatchesInventory(
            gContextObjects[i])) {

      ++count;
    }
  }

  return count;
}


int contextMenuObjectCount() {

  if (gContextAction ==
      ContextAction::Drop) {

    return
        gInventoryObjectCount +
        dropContextExtraCount();
  }

  return gContextObjectCount;
}


const char* contextMenuDisplayAt(
    int index) {

  if (index < 0) {
    return nullptr;
  }

  if (gContextAction !=
      ContextAction::Drop) {

    if (index >=
        gContextObjectCount) {

      return nullptr;
    }

    return
        gContextObjects[index];
  }

  /*
   * DROP: carried items first.
   */
  if (index <
      gInventoryObjectCount) {

    return
        gInventoryObjects[index];
  }

  int remaining =
      index -
      gInventoryObjectCount;

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    if (contextDisplayMatchesInventory(
            gContextObjects[i])) {

      continue;
    }

    if (remaining == 0) {
      return gContextObjects[i];
    }

    --remaining;
  }

  return nullptr;
}


const char* contextMenuCommandAt(
    int index) {

  if (index < 0) {
    return nullptr;
  }

  if (gContextAction !=
      ContextAction::Drop) {

    if (index >=
        gContextObjectCount) {

      return nullptr;
    }

    return
        gContextObjectCommands[index];
  }

  if (index <
      gInventoryObjectCount) {

    return
        gInventoryObjects[index];
  }

  int remaining =
      index -
      gInventoryObjectCount;

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    if (contextDisplayMatchesInventory(
            gContextObjects[i])) {

      continue;
    }

    if (remaining == 0) {
      return gContextObjectCommands[i];
    }

    --remaining;
  }

  return nullptr;
}


int contextMenuItemCount() {

  /*
   * Every contextual-object submenu has one final TYPE... entry.
   */
  return contextMenuObjectCount() + 1;
}


int contextMenuPageCount() {

  const int itemCount =
      contextMenuItemCount();

  return
      (itemCount +
       CONTEXT_MENU_ITEMS_PER_PAGE - 1) /
          CONTEXT_MENU_ITEMS_PER_PAGE;
}


int contextMenuPageForSelection(
    int selectionIndex) {

  if (selectionIndex < 0) {
    return 0;
  }

  return
      selectionIndex /
      CONTEXT_MENU_ITEMS_PER_PAGE;
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

bool isTranscriptContextNoiseWord(
    const char* word) {

  /*
   * Deliberately tiny and conservative.
   *
   * These are banner/control words that can exist in a story dictionary
   * but are poor current-scene object candidates. Do not turn this into
   * broad semantic filtering; odd scenery nouns remain fair game.
   */
  static const char* noiseWords[] = {
      "RELEASE",
      "VERSION",
      "COPYRIGHT"
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
    const char* object,
    const char* command = nullptr);

void addRealContextObject(
    const char* object,
    const char* command);

void refreshLiveRoomObjectCache(
    const char* roomName) {

  if (roomName == nullptr ||
      roomName[0] == '\0') {

    return;
  }

  char refreshedObjects
      [MAX_LIVE_ROOM_OBJECTS]
      [MAX_LIVE_ROOM_OBJECT_LENGTH] = {};

  const int refreshedCount =
      FrotzX3::getCurrentRoomObjects(
          roomName,
          &refreshedObjects[0][0],
          MAX_LIVE_ROOM_OBJECTS,
          MAX_LIVE_ROOM_OBJECT_LENGTH);

  gLiveRoomObjectCount =
      refreshedCount;

  for (int i = 0;
       i < MAX_LIVE_ROOM_OBJECTS;
       ++i) {

    gLiveRoomObjects[i][0] =
        '\0';
  }

  for (int i = 0;
       i < refreshedCount;
       ++i) {

    snprintf(
        gLiveRoomObjects[i],
        MAX_LIVE_ROOM_OBJECT_LENGTH,
        "%s",
        refreshedObjects[i]);
  }

  LOG_INF(
      "FROTZLIVE",
      "room=\"%s\" cached %d raw room-tree object(s)",
      roomName,
      gLiveRoomObjectCount);
}


void copyLastWordUpper(
    const char* text,
    char* destination,
    size_t destinationSize) {

  if (destination == nullptr ||
      destinationSize == 0) {

    return;
  }

  destination[0] = '\0';

  if (text == nullptr) {
    return;
  }

  const char* lastWord = nullptr;

  for (const char* p = text;
       *p != '\0';
       ++p) {

    if (isAsciiLetter(*p) &&
        (p == text ||
         !isAsciiLetter(p[-1]))) {

      lastWord = p;
    }
  }

  if (lastWord == nullptr) {
    return;
  }

  size_t out = 0;

  while (isAsciiLetter(*lastWord) &&
         out < destinationSize - 1) {

    destination[out++] =
        toUpperAscii(*lastWord++);
  }

  destination[out] = '\0';
}


bool findFollowingCapitalizedWord(
    const char* visibleText,
    const char* objectName,
    char* destination,
    size_t destinationSize) {

  if (visibleText == nullptr ||
      objectName == nullptr ||
      destination == nullptr ||
      destinationSize == 0) {

    return false;
  }

  destination[0] = '\0';

  const size_t objectLength =
      strlen(objectName);

  for (const char* p = visibleText;
       *p != '\0';
       ++p) {

    size_t i = 0;

    while (i < objectLength &&
           p[i] != '\0' &&
           toUpperAscii(p[i]) ==
               toUpperAscii(objectName[i])) {

      ++i;
    }

    if (i != objectLength) {
      continue;
    }

    const char* q =
        p + objectLength;

    while (*q == ' ' ||
           *q == '\t') {

      ++q;
    }

    /*
     * A capitalized word immediately following a real short name is
     * often the parser-facing proper noun omitted from that short name.
     *
     * Planetfall example:
     *   "Ensign First Class Blather ..."
     *                       ^^^^^^^
     */
    if (!(*q >= 'A' && *q <= 'Z')) {
      continue;
    }

    size_t out = 0;

    while (isAsciiLetter(*q) &&
           out < destinationSize - 1) {

      destination[out++] =
          toUpperAscii(*q++);
    }

    destination[out] = '\0';

    return out > 0;
  }

  return false;
}


void chooseRealObjectCommandName(
    const char* visibleText,
    const char* objectName,
    char* destination,
    size_t destinationSize) {

  if (findFollowingCapitalizedWord(
          visibleText,
          objectName,
          destination,
          destinationSize)) {

    return;
  }

  /*
   * Generic fallback: use the final word of the object's short name.
   * This turns "small mailbox" into "MAILBOX" and "Patrol uniform"
   * into "UNIFORM", which is generally much safer for parser input
   * than submitting the full display phrase.
   */
  copyLastWordUpper(
      objectName,
      destination,
      destinationSize);
}


void exposeVisibleLiveObjects(
    const char* visibleText) {

  if (visibleText == nullptr ||
      visibleText[0] == '\0') {

    return;
  }

  for (int i = 0;
       i < gLiveRoomObjectCount;
       ++i) {

    const char* object =
        gLiveRoomObjects[i];

    if (object[0] == '\0') {
      continue;
    }

    /*
     * Conservative visibility rule:
     *
     * A real object-tree entry becomes a UI candidate only after the
     * game has actually printed its short name to the player.
     *
     * This keeps hidden descendants such as Zork's leaflet out of the
     * menu while the mailbox is closed, yet promotes the leaflet as
     * soon as OPEN MAILBOX reveals it in visible text.
     *
     * It also naturally filters internal/player objects such as
     * "player" or Zork's "cretin" unless the story explicitly exposes
     * that name.
     */
    if (!containsTextIgnoreCase(
            visibleText,
            object)) {

      continue;
    }

    char commandName[
        MAX_CONTEXT_OBJECT_LENGTH] = {};

    chooseRealObjectCommandName(
        visibleText,
        object,
        commandName,
        sizeof(commandName));

    LOG_INF(
        "FROTZLIVE",
        "exposed real object: display=\"%s\" command=\"%s\"",
        object,
        commandName);

    addRealContextObject(
        object,
        commandName);
  }
}


int findContextObjectExactIndex(
    const char* object) {

  if (object == nullptr ||
      object[0] == '\0') {

    return -1;
  }

  for (int i = 0;
       i < gContextObjectCount;
       ++i) {

    if (stringsEqualIgnoreCase(
            gContextObjects[i],
            object)) {

      return i;
    }
  }

  return -1;
}


void addRealContextObject(
    const char* object,
    const char* command) {

  if (object == nullptr ||
      object[0] == '\0') {

    return;
  }

  if (command == nullptr ||
      command[0] == '\0') {

    command = object;
  }

  const int existingIndex =
      findContextObjectExactIndex(
          object);

  if (existingIndex >= 0) {

    /*
     * Already in the confirmed-real prefix: just refresh the parser
     * command pairing.
     */
    if (existingIndex <
        gRealContextObjectCount) {

      snprintf(
          gContextObjectCommands[
              existingIndex],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          command);

      return;
    }

    /*
     * It was previously learned only from transcript text.
     * Promote that exact entry into the real-object prefix.
     */
    char savedDisplay[
        MAX_CONTEXT_OBJECT_LENGTH] = {};

    char savedCommand[
        MAX_CONTEXT_OBJECT_LENGTH] = {};

    snprintf(
        savedDisplay,
        sizeof(savedDisplay),
        "%s",
        gContextObjects[
            existingIndex]);

    snprintf(
        savedCommand,
        sizeof(savedCommand),
        "%s",
        command);

    for (int i = existingIndex;
         i > gRealContextObjectCount;
         --i) {

      snprintf(
          gContextObjects[i],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          gContextObjects[i - 1]);

      snprintf(
          gContextObjectCommands[i],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          gContextObjectCommands[i - 1]);
    }

    snprintf(
        gContextObjects[
            gRealContextObjectCount],
        MAX_CONTEXT_OBJECT_LENGTH,
        "%s",
        savedDisplay);

    snprintf(
        gContextObjectCommands[
            gRealContextObjectCount],
        MAX_CONTEXT_OBJECT_LENGTH,
        "%s",
        savedCommand);

    ++gRealContextObjectCount;
    return;
  }

  /*
   * Preserve a bounded list. If full, drop one transcript-only entry;
   * never grow the buffer and never evict a confirmed real object merely
   * to insert another dictionary token.
   */
  if (gContextObjectCount >=
      MAX_CONTEXT_OBJECTS) {

    if (gRealContextObjectCount >=
        gContextObjectCount) {

      return;
    }

    --gContextObjectCount;
  }

  /*
   * Insert directly after the existing real-object prefix, shifting
   * transcript-only candidates right.
   */
  for (int i = gContextObjectCount;
       i > gRealContextObjectCount;
       --i) {

    snprintf(
        gContextObjects[i],
        MAX_CONTEXT_OBJECT_LENGTH,
        "%s",
        gContextObjects[i - 1]);

    snprintf(
        gContextObjectCommands[i],
        MAX_CONTEXT_OBJECT_LENGTH,
        "%s",
        gContextObjectCommands[i - 1]);
  }

  snprintf(
      gContextObjects[
          gRealContextObjectCount],
      MAX_CONTEXT_OBJECT_LENGTH,
      "%s",
      object);

  snprintf(
      gContextObjectCommands[
          gRealContextObjectCount],
      MAX_CONTEXT_OBJECT_LENGTH,
      "%s",
      command);

  ++gRealContextObjectCount;
  ++gContextObjectCount;
}


void addContextObject(
    const char* object,
    const char* command) {

  if (object == nullptr ||
      object[0] == '\0') {

    return;
  }

  if (command == nullptr ||
      command[0] == '\0') {

    command = object;
  }

  if (contextObjectAlreadyExists(
          object)) {

    return;
  }

  /*
   * If full, discard the oldest transcript-only candidate while keeping
   * the confirmed real-object prefix intact.
   */
  if (gContextObjectCount >=
      MAX_CONTEXT_OBJECTS) {

    if (gRealContextObjectCount >=
        gContextObjectCount) {

      return;
    }

    for (int i = gRealContextObjectCount + 1;
         i < MAX_CONTEXT_OBJECTS;
         ++i) {

      snprintf(
          gContextObjects[i - 1],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          gContextObjects[i]);

      snprintf(
          gContextObjectCommands[i - 1],
          MAX_CONTEXT_OBJECT_LENGTH,
          "%s",
          gContextObjectCommands[i]);
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

  snprintf(
      gContextObjectCommands[
          gContextObjectCount],
      MAX_CONTEXT_OBJECT_LENGTH,
      "%s",
      command);

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

          const bool noiseWord =
              !stopWord &&
              isTranscriptContextNoiseWord(
                  word);

          const bool dictionaryWord =
              !stopWord &&
              !noiseWord &&
              isDictionaryWordCached(word);

          const bool accepted =
              !stopWord &&
              !noiseWord &&
              dictionaryWord;

          LOG_INF(
              "FROTZNOUN",
              "candidate=%s len=%d stop=%d noise=%d dict=%d accepted=%d",
              word,
              wordLength,
              stopWord ? 1 : 0,
              noiseWord ? 1 : 0,
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
        "context[%d]=%s source=%s",
        i,
        gContextObjects[i],
        i < gRealContextObjectCount
            ? "REAL"
            : "TEXT");
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
      FROTZX3_STORIES_DIR "/%s",
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


void FrotzX3Activity::onEnter() {
scanGames();
migrateSaveLayout();
gFrotzBootOutput[0] = '\0';

gFrotzOutputCaptured = false;

gTranscriptPage = 0;

gBackLongPressHandled = false;

resetContextObjects();
resetInventoryObjects();
resetParserChoices();
gCaptureNextOutputAsInventory = false;

gContextAction =
    ContextAction::Examine;

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

void FrotzX3Activity::loop() {

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
          FROTZX3_STORIES_DIR "/%s",
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
            FROTZX3_STORIES_DIR "/%s",
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
            FROTZX3_STORIES_DIR "/%s",
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
/*
 * Classify parser feedback BEFORE room/context analysis.
 *
 * Parser prompts such as "You can't see any such thing" are not room
 * descriptions and must not contaminate room/object heuristics.
 */
const ParserFeedbackType parserFeedback =
    classifyParserFeedback(
        gFrotzBootOutput);

if (parserFeedback !=
    ParserFeedbackType::None) {

  LOG_INF(
      "FROTZPARSER",
      "type=%s output=\"%.120s\"",
      parserFeedbackTypeName(
          parserFeedback),
      gFrotzBootOutput);
}

if (parserFeedback ==
    ParserFeedbackType::Disambiguation) {

  extractDisambiguationChoices(
      gFrotzBootOutput);

} else {

  resetParserChoices();
}

/*
 * Only ordinary game output is allowed to update room/context state.
 *
 * DISAMBIGUATION has its own bounded choice extractor above.
 * UNKNOWN_WORD / INCOMPLETE_COMMAND / NOT_VISIBLE / PARTIAL_UNDERSTANDING
 * deliberately leave the existing room/context cache untouched.
 */
if (parserFeedback ==
    ParserFeedbackType::None) {

  /*
   * Phase 3 room tracking.
   *
   * Search the entire completed output for a line that EXACTLY matches a
   * real Z-machine object short name. This recognizes room headings even
   * after narrative text without mistaking ordinary prose for a room heading.
   */
  char detectedRoomName[96] = {};

  if (FrotzX3::getCurrentRoomName(
          gFrotzBootOutput,
          detectedRoomName,
          sizeof(detectedRoomName))) {

    const bool roomChanged =
        !stringsEqualIgnoreCase(
            gCurrentRoomName,
            detectedRoomName);

    if (roomChanged) {

      LOG_INF(
          "FROTZLIVE",
          "room change: \"%s\" -> \"%s\"",
          gCurrentRoomName[0] != '\0'
              ? gCurrentRoomName
              : "(unknown)",
          detectedRoomName);

      clearContextCandidates();

      snprintf(
          gCurrentRoomName,
          sizeof(gCurrentRoomName),
          "%s",
          detectedRoomName);
    }

    refreshLiveRoomObjectCache(
        gCurrentRoomName);
  }

  exposeVisibleLiveObjects(
      gFrotzBootOutput);

  detectContextObjects(
      gFrotzBootOutput);

  /*
   * Diagnostic only for ordinary output.
   */
  frotz_debug_room_tree(
      gFrotzBootOutput);
}

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

void FrotzX3Activity::moveSelection(int delta) {

  if (currentMenu == Menu::Main &&
      gParserChoiceActive) {

    gParserChoiceIndex += delta;

    if (gParserChoiceIndex < 0) {
      gParserChoiceIndex =
          gParserChoiceCount - 1;
    }

    if (gParserChoiceIndex >=
        gParserChoiceCount) {

      gParserChoiceIndex = 0;
    }

    requestUpdate();
    return;
  }

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
      itemCount = 12;
      break;

case Menu::Take:
case Menu::Examine:
case Menu::Open:
  /*
   * Navigation still moves through one logical flat list.
   * Rendering below chooses the visible 18-item page from
   * selectedIndex, so crossing a page boundary automatically
   * reveals the next/previous page without changing controls.
   */
  itemCount =
      contextMenuItemCount();
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

int FrotzX3Activity::getSingleKeyItemCount() const {

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


void FrotzX3Activity::moveSingleKeySelection(
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


void FrotzX3Activity::submitSingleKeyValue(
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


void FrotzX3Activity::activateSingleKeySelection() {

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

void FrotzX3Activity::activateSelection() {

  if (currentMenu == Menu::Main &&
      gParserChoiceActive &&
      gParserChoiceCount > 0 &&
      gParserChoiceIndex >= 0 &&
      gParserChoiceIndex <
          gParserChoiceCount) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s",
        gParserChoices[
            gParserChoiceIndex]);

    resetParserChoices();

    submitTypedCommand();
    return;
  }

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

      gContextAction =
          ContextAction::Take;

      currentMenu = Menu::Take;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 3:

      gContextAction =
          ContextAction::Drop;

      /*
       * Reuse the Take contextual screen, but its data source becomes
       * inventory-first when ContextAction::Drop is active.
       */
      currentMenu = Menu::Take;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 4:

      gContextAction =
          ContextAction::Examine;

      currentMenu = Menu::Examine;
      selectedIndex = 0;

      requestUpdate();
      return;

    case 5:

      gContextAction =
          ContextAction::Open;

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

      gContextAction =
          ContextAction::Read;

      /*
       * Reuse the existing contextual submenu machinery. The action
       * selector below changes its title/submission verb to READ.
       */
      currentMenu = Menu::Examine;
      selectedIndex = 0;

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

  /*
   * Logical movement order follows the compass clockwise:
   *
   *   N -> NE -> E -> SE -> S -> SW -> W -> NW
   *
   * Then the non-compass movement choices:
   *
   *   UP -> DOWN -> IN -> OUT
   *
   * Prev/Next therefore feels like rotating around a compass rather
   * than stepping through an arbitrary vertical list.
   */
  static const char* movementCommands[] = {
      "NORTH",
      "NORTHEAST",
      "EAST",
      "SOUTHEAST",
      "SOUTH",
      "SOUTHWEST",
      "WEST",
      "NORTHWEST",
      "UP",
      "DOWN",
      "IN",
      "OUT"
  };

  if (selectedIndex >= 0 &&
      selectedIndex < 12) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s",
        movementCommands[selectedIndex]);

    submitTypedCommand();
  }

  return;

  case Menu::Take: {

  const char* commandVerb =
      gContextAction ==
              ContextAction::Drop
          ? "DROP"
          : "TAKE";

  const int objectCount =
      contextMenuObjectCount();

  /*
   * Last item is always TYPE...
   */
  if (selectedIndex >=
      objectCount) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s ",
        commandVerb);

    openKeyboard();
    return;
  }

  const char* commandObject =
      contextMenuCommandAt(
          selectedIndex);

  if (commandObject == nullptr ||
      commandObject[0] == '\0') {

    return;
  }

  snprintf(
      typedCommand,
      sizeof(typedCommand),
      "%s %s",
      commandVerb,
      commandObject);

  submitTypedCommand();
  return;
}


case Menu::Examine: {

  const char* commandVerb =
      gContextAction ==
              ContextAction::Read
          ? "READ"
          : "EXAMINE";

  /*
   * Last item is always TYPE...
   */
  if (selectedIndex >=
      gContextObjectCount) {

    snprintf(
        typedCommand,
        sizeof(typedCommand),
        "%s ",
        commandVerb);

    openKeyboard();
    return;
  }

  snprintf(
      typedCommand,
      sizeof(typedCommand),
      "%s %s",
      commandVerb,
      gContextObjectCommands[
          selectedIndex]);

  submitTypedCommand();
  return;
}


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
      gContextObjectCommands[selectedIndex]);

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

void FrotzX3Activity::goBack() {

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

void FrotzX3Activity::openKeyboard() {

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
          gContextAction ==
                  ContextAction::Drop
              ? "DROP "
              : "TAKE ");
      break;

    case Menu::Examine:
      snprintf(
          typedCommand,
          sizeof(typedCommand),
          "%s",
          gContextAction ==
                  ContextAction::Read
              ? "READ "
              : "EXAMINE ");
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

void FrotzX3Activity::moveKeyboard(int delta) {

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

void FrotzX3Activity::activateKeyboardKey() {

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

int FrotzX3Activity::getSuggestionCount() const {

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
FrotzX3Activity::getSuggestion(
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

        for (int iteration = 0;
             iteration < objectCount;
             ++iteration) {

          /*
           * DROP keeps the old newest-inventory-first behavior.
           * Other object verbs prefer the front of the context list,
           * where Phase-3 real object short names are inserted before
           * transcript dictionary fragments.
           */
          const int i =
              useInventory
                  ? (objectCount - 1 - iteration)
                  : iteration;

          const char* object =
              useInventory
                  ? gInventoryObjects[i]
                  : gContextObjectCommands[i];

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
      "NORTHEAST",
      "NORTHWEST",
      "SOUTH",
      "SOUTHEAST",
      "SOUTHWEST",
      "EAST",
      "WEST",
      "UP",
      "DOWN",
      "IN",
      "OUT",
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

void FrotzX3Activity::acceptSuggestion() {

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

void FrotzX3Activity::submitTypedCommand() {

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

/*
 * Room context is no longer cleared merely because a command LOOKS like
 * movement. The completed output is authoritative: if a real room heading
 * is detected there, the old room cache is replaced at that point.
 */
(void)resetsRoomContext;

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

void FrotzX3Activity::render(
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
  constexpr int APPROX_CHAR_WIDTH = 8;

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      HEADER_TEXT_Y,
      "FrotzX3",
      true,
      EpdFontFamily::BOLD);

  /*
   * Z1-Z3 have a real interpreter-managed status line.
   *
   * Use that structured status data when available:
   *
   *   FrotzX3        West of House        0 / 2
   *
   * or, for time-based stories:
   *
   *   FrotzX3        Station              13:42
   *
   * Z4-Z8 do not have the same standardized status model, so they
   * retain the existing story-filename + transcript-page header.
   */
  FrotzX3::StatusInfo statusInfo = {};

  const bool hasNativeStatus =
      FrotzX3::getStatusInfo(
          &statusInfo);

  if (hasNativeStatus) {

    char statusRight[24] = {};

    if (statusInfo.usesTime) {

      snprintf(
          statusRight,
          sizeof(statusRight),
          "%d:%02d",
          statusInfo.value1,
          statusInfo.value2);

    } else {

      snprintf(
          statusRight,
          sizeof(statusRight),
          "%d / %d",
          statusInfo.value1,
          statusInfo.value2);
    }

    const int rightX =
        pageWidth -
        LEFT_MARGIN -
        static_cast<int>(
            strlen(statusRight)) *
            APPROX_CHAR_WIDTH;

    renderer.drawText(
        UI_12_FONT_ID,
        rightX,
        HEADER_TEXT_Y,
        statusRight,
        true,
        EpdFontFamily::BOLD);

    /*
     * Center the room name in the remaining middle area.
     * Cap its visible length so it cannot collide with FrotzX3
     * on the left or score/time on the right.
     */
    char roomDisplay[24] = {};

    snprintf(
        roomDisplay,
        sizeof(roomDisplay),
        "%.21s",
        statusInfo.room);

    int roomX =
        (pageWidth -
         static_cast<int>(
             strlen(roomDisplay)) *
             APPROX_CHAR_WIDTH) / 2;

    if (roomX < 100) {
      roomX = 100;
    }

    const int roomRight =
        roomX +
        static_cast<int>(
            strlen(roomDisplay)) *
            APPROX_CHAR_WIDTH;

    if (roomRight >
        rightX - 12) {

      roomX =
          rightX -
          12 -
          static_cast<int>(
              strlen(roomDisplay)) *
              APPROX_CHAR_WIDTH;
    }

    if (roomX < 100) {
      roomX = 100;
    }

    renderer.drawText(
        UI_12_FONT_ID,
        roomX,
        HEADER_TEXT_Y,
        roomDisplay,
        true,
        EpdFontFamily::BOLD);

  } else {

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
  }

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

  if (gParserChoiceActive &&
      gParserChoiceCount > 0) {

    renderer.drawText(
        UI_12_FONT_ID,
        LEFT_MARGIN,
        ACTION_TITLE_Y,
        "Choose Meaning",
        true,
        EpdFontFamily::BOLD);

    constexpr int PARSER_CHOICE_COL_WIDTH = 220;
    constexpr int PARSER_CHOICE_ROW_HEIGHT = 40;

    for (int i = 0;
         i < gParserChoiceCount;
         ++i) {

      const int row = i / 2;
      const int col = i % 2;

      char displayChoice[32] = {};

      formatContextDisplayName(
          gParserChoices[i],
          displayChoice,
          sizeof(displayChoice));

      char label[40] = {};

      snprintf(
          label,
          sizeof(label),
          i == gParserChoiceIndex
              ? "[ %s ]"
              : "  %s",
          displayChoice);

      renderer.drawText(
          UI_12_FONT_ID,
          LEFT_MARGIN +
              col * PARSER_CHOICE_COL_WIDTH,
          ACTION_GRID_Y +
              row * PARSER_CHOICE_ROW_HEIGHT,
          label,
          true,
          i == gParserChoiceIndex
              ? EpdFontFamily::BOLD
              : EpdFontFamily::REGULAR);
    }

  } else if (FrotzX3::waitingForKeyInput()) {

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

  /*
   * Purpose-built movement screen.
   *
   * The first eight logical items are arranged as a compass:
   *
   *        NW     N      NE
   *        W             E
   *        SW     S      SE
   *
   * Prev/Next follows the logical clockwise order defined in
   * activateSelection():
   *
   *   N -> NE -> E -> SE -> S -> SW -> W -> NW
   *
   * UP/DOWN and IN/OUT sit underneath as secondary movement choices.
   */
static const char* movementLabels[12] = {
    "N",
    "NE",
    "E",
    "SE",
    "S",
    "SW",
    "W",
    "NW",
    "UP",
    "DOWN",
    "IN",
    "OUT"
};

  /*
   * X positions use the same roughly 3-column spacing already proven
   * elsewhere in the UI, but are tuned here to center the compass.
   */
  static const int movementX[12] = {
      170,  // NORTH
      306,  // NORTHEAST
      306,  // EAST
      306,  // SOUTHEAST
      170,  // SOUTH
       24,  // SOUTHWEST
       24,  // WEST
       24,  // NORTHWEST
       92,  // UP
      246,  // DOWN
       92,  // IN
      246   // OUT
  };

  static const int movementY[12] = {
      456,  // NORTH
      484,  // NORTHEAST
      516,  // EAST
      548,  // SOUTHEAST
      576,  // SOUTH
      548,  // SOUTHWEST
      516,  // WEST
      484,  // NORTHWEST
      620,  // UP
      620,  // DOWN
      652,  // IN
      652   // OUT
  };

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      ACTION_TITLE_Y,
      "Go",
      true,
      EpdFontFamily::BOLD);

  for (int i = 0;
       i < 12;
       ++i) {

    char label[24] = {};

    snprintf(
        label,
        sizeof(label),
        i == selectedIndex
            ? "[%s]"
            : " %s ",
        movementLabels[i]);

    renderer.drawText(
        UI_12_FONT_ID,
        movementX[i],
        movementY[i],
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
    verb =
        gContextAction ==
                ContextAction::Drop
            ? "Drop"
            : "Take";
  } else if (currentMenu == Menu::Examine) {
    verb =
        gContextAction ==
                ContextAction::Read
            ? "Read"
            : "Examine";
  } else {
    verb = "Open";
  }

  const int itemCount =
      contextMenuItemCount();

  const int pageCount =
      contextMenuPageCount();

  int pageIndex =
      contextMenuPageForSelection(
          selectedIndex);

  if (pageIndex >= pageCount) {
    pageIndex =
        pageCount - 1;
  }

  if (pageIndex < 0) {
    pageIndex = 0;
  }

  const int firstItem =
      pageIndex *
      CONTEXT_MENU_ITEMS_PER_PAGE;

  int visibleItemCount =
      itemCount - firstItem;

  if (visibleItemCount >
      CONTEXT_MENU_ITEMS_PER_PAGE) {

    visibleItemCount =
        CONTEXT_MENU_ITEMS_PER_PAGE;
  }

  char contextTitle[32] = {};

  if (pageCount > 1) {

    snprintf(
        contextTitle,
        sizeof(contextTitle),
        "%s  %d/%d",
        verb,
        pageIndex + 1,
        pageCount);

  } else {

    snprintf(
        contextTitle,
        sizeof(contextTitle),
        "%s",
        verb);
  }

  renderer.drawText(
      UI_12_FONT_ID,
      LEFT_MARGIN,
      ACTION_TITLE_Y,
      contextTitle,
      true,
      EpdFontFamily::BOLD);

  /*
   * Render at most one physical 9-row x 2-column page.
   *
   * selectedIndex remains an absolute index into the whole logical
   * menu. As Prev/Next crosses index 17 -> 18 (or vice versa), the
   * visible page changes automatically.
   */
  const int objectCount =
      contextMenuObjectCount();

  for (int visibleIndex = 0;
       visibleIndex < visibleItemCount;
       ++visibleIndex) {

    const int itemIndex =
        firstItem +
        visibleIndex;

    const int row =
        visibleIndex / 2;

    const int col =
        visibleIndex % 2;

    const int x =
        LEFT_MARGIN +
        col * ACTION_COL_WIDTH;

    const int y =
        ACTION_GRID_Y +
        row * ACTION_ROW_HEIGHT;

    char displayItem[32] = {};

    if (itemIndex <
        objectCount) {

      const char* sourceDisplay =
          contextMenuDisplayAt(
              itemIndex);

      formatContextDisplayName(
          sourceDisplay,
          displayItem,
          sizeof(displayItem));

    } else {

      snprintf(
          displayItem,
          sizeof(displayItem),
          "%s",
          "TYPE...");
    }

    char label[40];

    snprintf(
        label,
        sizeof(label),
        itemIndex == selectedIndex
            ? "> %s"
            : "  %s",
        displayItem);

    renderer.drawText(
        UI_12_FONT_ID,
        x,
        y,
        label,
        true,
        itemIndex == selectedIndex
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
