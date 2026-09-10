#pragma once

namespace FrotzX3 {

const char* version();

bool testLoadStory(const char* path);

bool testBootStory(const char* path);

/*
 * Persistent interpreter API.
 */
bool startStory(const char* path);

void setRestoreOnStart(bool enabled);

/*
 * Restore from a specific Quetzal file the next time startStory()
 * launches a story.  After startup restore opens that file, the
 * normal resume path becomes active again automatically.
 */
bool setRestorePathOnStart(const char* path);

void stopStory();

bool isRunning();

bool waitingForInput();

/*
 * True when the story is blocked on Z-machine READ_CHAR / os_read_key()
 * rather than a normal line-input command prompt.
 */
bool waitingForKeyInput();

/*
 * Submit exactly one character to a story waiting for single-key input.
 */
bool submitKey(char key);

bool submitCommand(const char* command);

/*
 * Save the current game state to a specific Quetzal file.
 *
 * The snapshot is executed by the Frotz task while it is blocked
 * at a player-input prompt.
 */
bool saveToPath(const char* path);

/*
 * Save the current game state to this story's normal resume slot.
 */
bool saveResume();

bool isDictionaryWord(const char* word);

/*
 * Return real Z-machine short names for objects in the currently
 * inferred room.
 *
 * `names` points to maxObjects consecutive slots, each exactly
 * nameSize bytes wide. The function returns the number of filled
 * slots. It returns 0 unless Frotz is safely waiting for input.
 *
 * This is intentionally caller-owned fixed storage: no vector,
 * string, or per-object heap allocation is used.
 */
/*
 * Standardized V1-V3 status information.
 *
 * For score-based games:
 *   value1 = score
 *   value2 = moves
 *
 * For time-based games:
 *   value1 = hour (24-hour value stored by the story)
 *   value2 = minute
 *
 * V4+ stories do not have this standardized status-line model, so
 * getStatusInfo() returns false for them.
 */
struct StatusInfo {
    bool available = false;
    bool usesTime = false;
    char room[64] = {};
    int value1 = 0;
    int value2 = 0;
};

bool getStatusInfo(StatusInfo* status);

/*
 * Find a real Z-machine object short name that appears as an entire
 * visible output line. Used by the UI as a conservative room-heading
 * detector. Returns false unless Frotz is safely waiting for input.
 */
bool getCurrentRoomName(
    const char* visibleText,
    char* roomName,
    int roomNameSize);

int getCurrentRoomObjects(
    const char* visibleText,
    char* names,
    int maxObjects,
    int nameSize);

const char* output();

const char* lastError();

}
