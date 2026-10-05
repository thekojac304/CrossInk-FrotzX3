#include "FrotzX3.h"
#include "FrotzX3Paths.h"

#include <cstdio>
#include <cstring>

#include <HalStorage.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <Logging.h>

static HalFile gStoryFile;

static HalFile gSaveFile;

/*
 * Quetzal emits much of its save stream one byte at a time.
 * Sending every byte directly to the SD-backed HalFile is expensive,
 * so collect those bytes into one SD-sized block first.
 *
 * This changes only the I/O batching; the Quetzal bytes written to
 * disk and all save/restore semantics remain unchanged.
 */
static constexpr size_t FROTZ_SAVE_WRITE_BUFFER_SIZE = 512;
static unsigned char
    gSaveWriteBuffer[FROTZ_SAVE_WRITE_BUFFER_SIZE] = {};
static size_t gSaveWriteBufferLength = 0;

static bool flushSaveWriteBuffer()
{
    if (gSaveWriteBufferLength == 0) {
        return true;
    }

    if (!gSaveFile.isOpen()) {
        gSaveWriteBufferLength = 0;
        return false;
    }

    const size_t bytesToWrite =
        gSaveWriteBufferLength;

    const size_t bytesWritten =
        gSaveFile.write(
            gSaveWriteBuffer,
            bytesToWrite
        );

    if (bytesWritten != bytesToWrite) {
        return false;
    }

    gSaveWriteBufferLength = 0;
    return true;
}

static TaskHandle_t gFrotzTask = nullptr;

static volatile bool gFrotzRunning = false;

static char gFrotzStoryPath[256] = "";
static char gFrotzSavePath[256] = "";
static char gFrotzResumePath[256] = "";
static char gPendingRestorePath[256] = "";

static bool gRestoreOnStart = false;
static bool gCustomRestorePathActive = false;

extern "C" {

void frotz_debug_log(const char *message)
{
#ifdef FROTZX3_DEBUG_LOG
    if (message != nullptr) {
        LOG_INF("FROTZDBG", "%s", message);
    }
#else
    (void)message;
#endif
}

void frotz_error_log(const char *message)
{
    if (message != nullptr) {
        LOG_ERR("FROTZ", "%s", message);
    }
}

void frotz_hal_set_restore_on_start(int enabled)
{
    gRestoreOnStart =
        enabled != 0;
}

int frotz_hal_take_restore_on_start(void)
{
    const int result =
        gRestoreOnStart ? 1 : 0;

    FROTZX3_LOG_DEBUG(
        "FROTZDBG",
        "take_restore_on_start: returning %d",
        result
    );

    gRestoreOnStart = false;

    return result;
}

void frotz_set_story_file(char* path);

void init_buffer(void);
void init_err(void);
void reset_memory(void);

int frotz_try_init_memory(void);
int frotz_try_boot_to_input(void);

const char* frotz_get_last_error(void);
const char* frotz_get_output(void);

int frotz_is_waiting_for_input(void);
int frotz_is_waiting_for_key_input(void);
void frotz_prepare_run(void);
void frotz_request_stop(void);
int frotz_submit_command(const char* command);
int frotz_submit_key(int key);
int frotz_dictionary_contains(const char* word);

/*
 * Fill caller-owned fixed-width slots with short names of objects
 * contained by the currently inferred room object.
 *
 * Implemented in object.c.  The caller must only invoke this while
 * the interpreter is parked waiting for player input.
 */
int frotz_get_room_object_names(
    const char* visible_text,
    char* names,
    int max_objects,
    int name_size);

int frotz_find_room_name(
    const char* visible_text,
    char* room_name,
    int room_name_size);

int frotz_get_v3_status(
    char* room_name,
    int room_name_size,
    int* uses_time,
    int* value1,
    int* value2);

int frotz_request_autosave(void);
int frotz_is_autosave_done(void);
int frotz_autosave_succeeded(void);

const char* frotz_hal_get_save_path(void)
{
    return gFrotzSavePath;
}

/*
 * ---------------------------------------------------------
 * CrossInk / HalFile bridge used by FrotzPlatform.c
 * and fastmem.c
 * ---------------------------------------------------------
 */

int frotz_hal_open_story(const char* path)
{
    if (gStoryFile.isOpen()) {
        gStoryFile.close();
    }

    return Storage.openFileForRead(
        "FROTZ",
        path,
        gStoryFile
    ) ? 1 : 0;
}


int frotz_hal_read_story(
    void* buffer,
    unsigned int count)
{
    if (!gStoryFile.isOpen()) {
        return -1;
    }

    return (int)gStoryFile.read(
        buffer,
        count
    );
}


int frotz_hal_seek_story(
    long offset,
    int whence)
{
    if (!gStoryFile.isOpen()) {
        return -1;
    }

    bool ok = false;

    if (whence == 0) {

        if (offset < 0) {
            return -1;
        }

        ok = gStoryFile.seekSet(
            (size_t)offset
        );

    } else if (whence == 1) {

        ok = gStoryFile.seekCur(
            (int64_t)offset
        );

    } else if (whence == 2) {

        long target =
            (long)gStoryFile.fileSize()
            + offset;

        if (target < 0) {
            return -1;
        }

        ok = gStoryFile.seekSet(
            (size_t)target
        );
    }

    return ok ? 0 : -1;
}


long frotz_hal_tell_story(void)
{
    if (!gStoryFile.isOpen()) {
        return -1;
    }

    return (long)gStoryFile.position();
}


void frotz_hal_close_story(void)
{
    if (gStoryFile.isOpen()) {
        gStoryFile.close();
    }
}
/*
 * ---------------------------------------------------------
 * Quetzal save-file bridge
 * ---------------------------------------------------------
 */

int frotz_hal_open_save_read(const char* path)
{
    if (gSaveFile.isOpen()) {
        flushSaveWriteBuffer();
        gSaveFile.close();
    }

    gSaveWriteBufferLength = 0;

    const bool opened =
        Storage.openFileForRead(
            "FROTZ",
            path,
            gSaveFile
        );

    /*
     * A manual startup restore temporarily points the normal
     * Frotz save-file bridge at a manual slot.
     *
     * Once that file has been opened for RESTORE, immediately
     * switch the bridge back to the normal resume path.  Future
     * autosaves and SAVE operations must never accidentally
     * overwrite the manual slot that was just loaded.
     */
    if (gCustomRestorePathActive) {

        snprintf(
            gFrotzSavePath,
            sizeof(gFrotzSavePath),
            "%s",
            gFrotzResumePath
        );

        gCustomRestorePathActive = false;
    }

    return opened ? 1 : 0;
}


int frotz_hal_open_save_write(const char* path)
{
    if (gSaveFile.isOpen()) {
        flushSaveWriteBuffer();
        gSaveFile.close();
    }

    gSaveWriteBufferLength = 0;

    return Storage.openFileForWrite(
        "FROTZ",
        path,
        gSaveFile
    ) ? 1 : 0;
}


int frotz_hal_read_save(
    void* buffer,
    unsigned int count)
{
    if (!gSaveFile.isOpen()) {
        return -1;
    }

    return (int)gSaveFile.read(
        buffer,
        count
    );
}


int frotz_hal_read_save_byte(void)
{
    if (!gSaveFile.isOpen()) {
        return -1;
    }

    return gSaveFile.read();
}


int frotz_hal_write_save(
    const void* buffer,
    unsigned int count)
{
    if (!gSaveFile.isOpen()) {
        return -1;
    }

    if (!flushSaveWriteBuffer()) {
        return -1;
    }

    return (int)gSaveFile.write(
        buffer,
        count
    );
}


int frotz_hal_write_save_byte(
    unsigned char value)
{
    if (!gSaveFile.isOpen()) {
        return -1;
    }

    gSaveWriteBuffer[
        gSaveWriteBufferLength++] =
            value;

    if (gSaveWriteBufferLength >=
        FROTZ_SAVE_WRITE_BUFFER_SIZE) {

        if (!flushSaveWriteBuffer()) {
            return -1;
        }
    }

    /*
     * Match putc()-style behavior expected by Quetzal:
     * return the written byte value on success.
     */
    return (int)value;
}


int frotz_hal_seek_save(
    long offset,
    int whence)
{
    if (!gSaveFile.isOpen()) {
        return -1;
    }

    if (!flushSaveWriteBuffer()) {
        return -1;
    }

    bool ok = false;

    if (whence == 0) {

        if (offset < 0) {
            return -1;
        }

        ok = gSaveFile.seekSet(
            (size_t)offset
        );

    } else if (whence == 1) {

        ok = gSaveFile.seekCur(
            (int64_t)offset
        );

    } else if (whence == 2) {

        long target =
            (long)gSaveFile.fileSize()
            + offset;

        if (target < 0) {
            return -1;
        }

        ok = gSaveFile.seekSet(
            (size_t)target
        );
    }

    return ok ? 0 : -1;
}


long frotz_hal_tell_save(void)
{
    if (!gSaveFile.isOpen()) {
        return -1;
    }

    if (!flushSaveWriteBuffer()) {
        return -1;
    }

    return (long)gSaveFile.position();
}


int frotz_hal_sync_save(void)
{
    if (!gSaveFile.isOpen()) {
        return 0;
    }

    if (!flushSaveWriteBuffer()) {
        return 0;
    }

    return gSaveFile.sync() ? 1 : 0;
}


void frotz_hal_close_save(void)
{
    if (gSaveFile.isOpen()) {
        flushSaveWriteBuffer();
        gSaveFile.close();
    }

    gSaveWriteBufferLength = 0;
}


/*
 * Quetzal occasionally needs individual bytes from the
 * original story while compressing/restoring dynamic memory.
 */
int frotz_hal_read_story_byte(void)
{
    if (!gStoryFile.isOpen()) {
        return -1;
    }

    return gStoryFile.read();
}
}


/*
 * ---------------------------------------------------------
 * Persistent Frotz FreeRTOS task
 * ---------------------------------------------------------
 */

static void frotzTaskMain(void* parameter)
{
    (void)parameter;

    LOG_INF(
        "FROTZSTART",
        "task ENTER"
    );

    gFrotzRunning = true;

    /*
     * IMPORTANT:
     *
     * Story setup and init_memory() are deliberately performed in
     * startStory() BEFORE xTaskCreate().
     *
     * Lost Pig needs one contiguous ~42.5 KB dynamic-memory block.
     * Creating the 16 KB FreeRTOS task stack first can fragment the
     * heap enough that the later dynamic-memory malloc fails even
     * though total free RAM is still sufficient.
     *
     * By the time this task starts, Z-machine memory is already
     * initialized and owned by Frotz.
     */

    LOG_INF(
        "FROTZSTART",
        "task: boot_to_input BEGIN"
    );

    frotz_try_boot_to_input();

    LOG_INF(
        "FROTZSTART",
        "task: boot_to_input RETURNED"
    );

    /*
     * We only reach here after:
     *
     * - CrossInk requests the story to stop,
     * - Frotz hits a fatal error, or
     * - interpret() unexpectedly returns.
     */
    LOG_INF(
        "FROTZSTART",
        "task: reset_memory BEGIN"
    );

    reset_memory();

    LOG_INF(
        "FROTZSTART",
        "task: reset_memory COMPLETE"
    );

    gFrotzRunning = false;
    gFrotzTask = nullptr;

    LOG_INF(
        "FROTZSTART",
        "task EXIT"
    );

    vTaskDelete(nullptr);
}


namespace FrotzX3 {

void setRestoreOnStart(bool enabled)
{
    gRestoreOnStart = enabled;

    FROTZX3_LOG_DEBUG(
        "FROTZDBG",
        "setRestoreOnStart(%d)",
        enabled ? 1 : 0
    );

    /*
     * The ordinary Resume/New Game path never uses a custom
     * restore file.  Clear any stale staged path here.
     */
    gPendingRestorePath[0] = '\0';

    if (!enabled) {
        gCustomRestorePathActive = false;
    }
}


bool setRestorePathOnStart(const char* path)
{
    if (path == nullptr ||
        path[0] == '\0') {

        return false;
    }

    snprintf(
        gPendingRestorePath,
        sizeof(gPendingRestorePath),
        "%s",
        path
    );

    gRestoreOnStart = true;

    return true;
}

const char* version()
{
    return "FrotzX3 0.9.0-beta.1";
}


bool testLoadStory(const char* path)
{
    if (path == nullptr ||
        path[0] == '\0') {

        return false;
    }

    frotz_set_story_file(
        const_cast<char*>(path)
    );

    init_buffer();
    init_err();

    const bool success =
        frotz_try_init_memory() != 0;

    if (success) {
        reset_memory();
    }

    return success;
}


/*
 * Legacy test helper.
 *
 * NOTE:
 * The interpreter is now persistent and no longer exits
 * merely because it reaches player input.
 *
 * The Interactive Fiction activity no longer uses this.
 */
bool testBootStory(const char* path)
{
    if (path == nullptr ||
        path[0] == '\0') {

        return false;
    }

    frotz_set_story_file(
        const_cast<char*>(path)
    );

    init_buffer();
    init_err();

    if (frotz_try_init_memory() == 0) {

        reset_memory();

        return false;
    }

    const int bootResult =
        frotz_try_boot_to_input();

    const bool success =
        bootResult == 1;

    reset_memory();

    return success;
}


/*
 * ---------------------------------------------------------
 * Persistent interpreter API
 * ---------------------------------------------------------
 */

bool startStory(const char* path)
{
    LOG_INF(
        "FROTZSTART",
        "%s",
        version()
    );

    LOG_INF(
        "FROTZSTART",
        "startStory ENTER path=%s taskActive=%d running=%d",
        path != nullptr ? path : "(null)",
        gFrotzTask != nullptr ? 1 : 0,
        gFrotzRunning ? 1 : 0
    );

    if (path == nullptr ||
        path[0] == '\0') {

        LOG_ERR(
            "FROTZSTART",
            "startStory ABORT: invalid path"
        );

        return false;
    }

    /*
     * Do not allow two Frotz interpreters to run at once.
     */
    if (gFrotzTask != nullptr) {

        LOG_ERR(
            "FROTZSTART",
            "startStory ABORT: existing task running=%d",
            gFrotzRunning ? 1 : 0
        );

        return false;
    }

    snprintf(
        gFrotzStoryPath,
        sizeof(gFrotzStoryPath),
        "%s",
        path
    );

    LOG_INF(
        "FROTZSTART",
        "story path copied: %s",
        gFrotzStoryPath
    );

/*
 * Build one resume-save path for this exact story file.
 *
 * Example:
 *   /adventures/Zork1.z5
 * becomes:
 *   /adventures/saves/Zork1.z5.sav
 */
LOG_INF(
    "FROTZSTART",
    "ensuring saves directory"
);

Storage.ensureDirectoryExists(
    FROTZX3_SAVES_DIR
);

LOG_INF(
    "FROTZSTART",
    "saves directory ready"
);

const char* filename =
    strrchr(gFrotzStoryPath, '/');

if (filename != nullptr) {
    ++filename;
} else {
    filename = gFrotzStoryPath;
}

snprintf(
    gFrotzResumePath,
    sizeof(gFrotzResumePath),
    FROTZX3_SAVES_DIR "/%s.sav",
    filename
);

LOG_INF(
    "FROTZSTART",
    "resume path: %s",
    gFrotzResumePath
);

if (gPendingRestorePath[0] != '\0') {

    snprintf(
        gFrotzSavePath,
        sizeof(gFrotzSavePath),
        "%s",
        gPendingRestorePath
    );

    gPendingRestorePath[0] = '\0';
    gCustomRestorePathActive = true;

    LOG_INF(
        "FROTZSTART",
        "using custom restore path: %s",
        gFrotzSavePath
    );

} else {

    snprintf(
        gFrotzSavePath,
        sizeof(gFrotzSavePath),
        "%s",
        gFrotzResumePath
    );

    gCustomRestorePathActive = false;

    LOG_INF(
        "FROTZSTART",
        "using normal save path: %s",
        gFrotzSavePath
    );
}

    /*
     * Initialize the story BEFORE creating the persistent FreeRTOS
     * task.  This allocation order is intentional.
     *
     * Large Z8 games such as Lost Pig need a contiguous dynamic-memory
     * allocation (~42.5 KB for Lost Pig).  If the 16 KB task stack is
     * allocated first, the remaining heap can become fragmented enough
     * that init_memory() fails despite having plenty of total free RAM.
     */
    LOG_INF(
        "FROTZSTART",
        "pre-task: set story file (%s)",
        gFrotzStoryPath
    );

    frotz_set_story_file(
        gFrotzStoryPath
    );

    LOG_INF(
        "FROTZSTART",
        "pre-task: init_buffer BEGIN"
    );

    init_buffer();

    LOG_INF(
        "FROTZSTART",
        "pre-task: init_buffer COMPLETE"
    );

    LOG_INF(
        "FROTZSTART",
        "pre-task: init_err BEGIN"
    );

    init_err();

    LOG_INF(
        "FROTZSTART",
        "pre-task: init_err COMPLETE"
    );

    LOG_INF(
        "FROTZSTART",
        "pre-task: frotz_prepare_run BEGIN"
    );

    frotz_prepare_run();

    LOG_INF(
        "FROTZSTART",
        "pre-task: frotz_prepare_run COMPLETE"
    );

    LOG_INF(
        "FROTZSTART",
        "pre-task: init_memory BEGIN"
    );

    if (frotz_try_init_memory() == 0) {

        LOG_ERR(
            "FROTZSTART",
            "pre-task: init_memory FAILED"
        );

        reset_memory();

        snprintf(
            gFrotzSavePath,
            sizeof(gFrotzSavePath),
            "%s",
            gFrotzResumePath
        );

        gCustomRestorePathActive = false;
        gFrotzRunning = false;

        return false;
    }

    LOG_INF(
        "FROTZSTART",
        "pre-task: init_memory COMPLETE"
    );

    gFrotzRunning = true;

    LOG_INF(
        "FROTZSTART",
        "creating Frotz task AFTER init_memory"
    );

    /*
     * On ESP32/ESP-IDF, this stack size is measured in bytes.
     *
     * The original 16 KB stack was intentionally generous during
     * early bring-up.  With large Z8 stories, keeping that much stack
     * on the heap prevents the task from being created after the
     * story's dynamic-memory block has been reserved.
     *
     * 8 KB is still a substantial task stack for this text-only port,
     * while leaving enough contiguous heap for Lost Pig's ~42.5 KB
     * dynamic-memory allocation plus the persistent Frotz task.
     */
    BaseType_t result =
        xTaskCreate(
            frotzTaskMain,
            "Frotz",
            8192,
            nullptr,
            1,
            &gFrotzTask
        );

    LOG_INF(
        "FROTZSTART",
        "xTaskCreate result=%ld taskActive=%d",
        (long)result,
        gFrotzTask != nullptr ? 1 : 0
    );

    if (result != pdPASS) {

        LOG_ERR(
            "FROTZSTART",
            "startStory FAILED: xTaskCreate"
        );

        /*
         * init_memory() already succeeded before task creation, so
         * release that interpreter state if FreeRTOS cannot create
         * the persistent task.
         */
        reset_memory();

        gFrotzRunning = false;
        gFrotzTask = nullptr;

        snprintf(
            gFrotzSavePath,
            sizeof(gFrotzSavePath),
            "%s",
            gFrotzResumePath
        );

        gCustomRestorePathActive = false;

        return false;
    }

    LOG_INF(
        "FROTZSTART",
        "startStory SUCCESS"
    );

    return true;
}


void stopStory()
{
    if (gFrotzTask == nullptr) {
        return;
    }

    /*
     * Ask os_read_line()/os_read_key() to release the
     * persistent interpreter.
     */
    frotz_request_stop();

    /*
     * Do not let CrossInk leave the activity while the
     * Frotz task is still unwinding and freeing its global
     * interpreter state.
     *
     * The Frotz task sets gFrotzTask to nullptr only after
     * reset_memory() has completed.
     *
     * 2 seconds is enormously longer than this cleanup
     * should normally require, but prevents an infinite
     * hang if something ever goes wrong.
     */
    const int maxWaitMs = 2000;
    int waitedMs = 0;

    while (gFrotzTask != nullptr &&
           waitedMs < maxWaitMs) {

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );

        waitedMs += 10;
    }
}

bool isRunning()
{
    return gFrotzRunning;
}


bool waitingForInput()
{
    return
        frotz_is_waiting_for_input() != 0;
}


bool waitingForKeyInput()
{
    return
        frotz_is_waiting_for_key_input() != 0;
}


bool submitKey(char key)
{
    if (gFrotzTask == nullptr ||
        !gFrotzRunning) {

        return false;
    }

    return
        frotz_submit_key(
            (unsigned char)key) != 0;
}


bool submitCommand(const char* command)
{
    if (command == nullptr ||
        command[0] == '\0') {

        return false;
    }

    if (gFrotzTask == nullptr ||
        !gFrotzRunning) {

        return false;
    }

    return
        frotz_submit_command(command) != 0;
}

bool saveToPath(const char* path)
{
    if (path == nullptr ||
        path[0] == '\0') {

        return false;
    }

    /*
     * Saving must happen from inside the Frotz task while the
     * interpreter is safely blocked at an input prompt.
     */
    if (gFrotzTask == nullptr ||
        !gFrotzRunning ||
        frotz_is_waiting_for_input() == 0) {

        return false;
    }

    /*
     * Temporarily point the existing Quetzal/HalFile bridge at
     * the requested manual-save filename.
     *
     * The safe pre-instruction snapshot machinery in fastmem.c
     * remains completely unchanged.
     */
    snprintf(
        gFrotzSavePath,
        sizeof(gFrotzSavePath),
        "%s",
        path
    );

    if (frotz_request_autosave() == 0) {

        snprintf(
            gFrotzSavePath,
            sizeof(gFrotzSavePath),
            "%s",
            gFrotzResumePath
        );

        return false;
    }

    const int maxWaitMs = 10000;
    int waitedMs = 0;

    while (frotz_is_autosave_done() == 0 &&
           gFrotzTask != nullptr &&
           gFrotzRunning &&
           waitedMs < maxWaitMs) {

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );

        waitedMs += 10;
    }

    const bool completed =
        frotz_is_autosave_done() != 0;

    const bool succeeded =
        completed &&
        frotz_autosave_succeeded() != 0;

    /*
     * Always restore the normal resume destination afterward.
     * This is critical: exiting the activity later must still
     * autosave to <story>.sav, not to the last manual slot.
     */
    snprintf(
        gFrotzSavePath,
        sizeof(gFrotzSavePath),
        "%s",
        gFrotzResumePath
    );

    return succeeded;
}


bool saveResume()
{
    return saveToPath(
        gFrotzResumePath
    );
}


bool isDictionaryWord(const char* word)
{
    if (word == nullptr ||
        word[0] == '\0') {

        return false;
    }

    /*
     * Only inspect the story dictionary while Frotz is safely
     * blocked waiting for player input.
     *
     * Before this point the story header/dictionary may still
     * be initializing, and the Frotz task may still be using
     * the SD-backed story file.
     */
    if (gFrotzTask == nullptr ||
        !gFrotzRunning ||
        frotz_is_waiting_for_input() == 0) {

        return false;
    }

    return
        frotz_dictionary_contains(word) != 0;
}


bool getStatusInfo(StatusInfo* status)
{
    if (status == nullptr) {
        return false;
    }

    *status = StatusInfo{};

    /*
     * The status accessor reads live Z-machine globals/object data.
     * Match the safety rule used by the dictionary and object APIs:
     * only inspect that state while the persistent interpreter task is
     * parked at a player-input prompt.
     */
    if (gFrotzTask == nullptr ||
        !gFrotzRunning ||
        frotz_is_waiting_for_input() == 0) {

        return false;
    }

    int usesTime = 0;
    int value1 = 0;
    int value2 = 0;

    if (frotz_get_v3_status(
            status->room,
            sizeof(status->room),
            &usesTime,
            &value1,
            &value2) == 0) {

        return false;
    }

    status->available = true;
    status->usesTime =
        usesTime != 0;
    status->value1 = value1;
    status->value2 = value2;

    return true;
}


bool getCurrentRoomName(
    const char* visibleText,
    char* roomName,
    int roomNameSize)
{
    if (visibleText == nullptr ||
        visibleText[0] == '\0' ||
        roomName == nullptr ||
        roomNameSize <= 1) {

        return false;
    }

    roomName[0] = '\0';

    if (gFrotzTask == nullptr ||
        !gFrotzRunning ||
        frotz_is_waiting_for_input() == 0) {

        return false;
    }

    return
        frotz_find_room_name(
            visibleText,
            roomName,
            roomNameSize) != 0;
}


int getCurrentRoomObjects(
    const char* visibleText,
    char* names,
    int maxObjects,
    int nameSize)
{
    if (visibleText == nullptr ||
        visibleText[0] == '\0' ||
        names == nullptr ||
        maxObjects <= 0 ||
        nameSize <= 1) {

        return 0;
    }

    /*
     * object.c touches live Z-machine object tables and uses the
     * normal Frotz text decoder/story reader. Those globals are not
     * thread-safe while the interpreter is executing.
     *
     * Match isDictionaryWord(): the UI may inspect them only while
     * the persistent Frotz task is parked at an input prompt.
     */
    if (gFrotzTask == nullptr ||
        !gFrotzRunning ||
        frotz_is_waiting_for_input() == 0) {

        return 0;
    }

    return frotz_get_room_object_names(
        visibleText,
        names,
        maxObjects,
        nameSize);
}


const char* output()
{
#ifdef FROTZX3_DEBUG_LOG
    /*
     * Status-line diagnostic.
     *
     * The activity asks for output only after Frotz has reached the next
     * input prompt, which is exactly when the structured V1-V3 status API
     * is safe to inspect.
     */
    StatusInfo status;

    if (getStatusInfo(&status)) {

        if (status.usesTime) {
            LOG_INF(
                "FROTZSTATUS",
                "room=\"%s\" time=%02d:%02d",
                status.room,
                status.value1,
                status.value2
            );
        } else {
            LOG_INF(
                "FROTZSTATUS",
                "room=\"%s\" score=%d moves=%d",
                status.room,
                status.value1,
                status.value2
            );
        }
    }
#endif

    return frotz_get_output();
}


const char* lastError()
{
    return frotz_get_last_error();
}


}
