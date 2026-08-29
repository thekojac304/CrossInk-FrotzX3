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

const char* output();

const char* lastError();

}
