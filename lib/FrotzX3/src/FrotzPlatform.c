#include "frotz.h"

#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"



/*
 * ---------------------------------------------------------
 * Frotz execution trap
 * ---------------------------------------------------------
 *
 * setjmp/longjmp is now used for:
 *
 * 1. Escaping safely from os_fatal().
 * 2. Stopping the persistent interpreter when CrossInk
 *    explicitly requests that the story be closed.
 *
 * Normal player input DOES NOT longjmp out of interpret().
 * Instead os_read_line() remains blocked while waiting for
 * CrossInk to eventually supply a command.
 */

static jmp_buf frotzJump;
static int frotzJumpActive = 0;

#define FROTZ_JUMP_FATAL 1
#define FROTZ_JUMP_INPUT 2
#define FROTZ_JUMP_STOP  3

static char frotzLastError[128] = "";

static volatile int frotzWaitingForInput = 0;
static volatile int frotzWaitingForLineInput = 0;
static volatile int frotzStopRequested = 0;

/*
 * Autosave request/result handoff.
 *
 * The UI task only posts a request.  The actual Quetzal write is
 * performed by the Frotz task while it is blocked in an input routine.
 */
static volatile int frotzAutosaveRequested = 0;
static volatile int frotzAutosaveDone = 0;
static volatile int frotzAutosaveSucceeded = 0;

/*
 * Command handoff from CrossInk to the blocked Frotz input routine.
 *
 * This first version supports line input only.  The command buffer is
 * filled by CrossInk while Frotz is blocked inside os_read_line().
 */
#define FROTZ_COMMAND_SIZE 128

static char frotzPendingCommand[FROTZ_COMMAND_SIZE] = "";
static volatile int frotzCommandReady = 0;

/*
 * Single-key handoff for Z-machine READ_CHAR / os_read_key().
 *
 * Some games use one-key menus (for example numeric startup choices)
 * instead of normal command lines. Keep this separate from the line
 * command buffer so the UI can tell which kind of input Frotz needs.
 */
static volatile int frotzWaitingForKeyInput = 0;
static volatile int frotzPendingKey = 0;
static volatile int frotzKeyReady = 0;


/*
 * ---------------------------------------------------------
 * Temporary captured-output buffer
 * ---------------------------------------------------------
 *
 * For now Frotz output is captured as plain text.
 *
 * Later this can become a proper transcript/history buffer
 * owned by the Interactive Fiction UI.
 */

#define FROTZ_OUTPUT_SIZE 2048

static char frotzOutput[FROTZ_OUTPUT_SIZE];
static unsigned int frotzOutputLength = 0;

extern void frotz_debug_log(const char *message);
extern void frotz_error_log(const char *message);

static void frotz_output_clear(void)
{
    frotzOutputLength = 0;
    frotzOutput[0] = '\0';
}


static void frotz_output_char(zchar c)
{
    char out;

    /*
     * Translate Frotz/Z-machine display codes into
     * simple text suitable for our temporary transcript.
     */
    if (c == ZC_RETURN) {

        out = '\n';

    } else if (c == ZC_INDENT) {

        out = ' ';

    } else if (c == ZC_GAP) {

        out = ' ';

    } else if (c >= 32 && c <= 126) {

        out = (char)c;

    } else {

        /*
         * Ignore style/font control codes for now.
         */
        return;
    }

    if (frotzOutputLength + 1 >= FROTZ_OUTPUT_SIZE)
        return;

    frotzOutput[frotzOutputLength++] = out;
    frotzOutput[frotzOutputLength] = '\0';
}


void frotz_output_newline(void)
{
    /*
     * screen_new_line() normally moves a terminal cursor rather
     * than emitting an actual newline character.
     *
     * CrossInk is capturing a transcript instead of drawing to
     * a traditional terminal, so record one here.
     */
    frotz_output_char(ZC_RETURN);
}


const char *frotz_get_output(void)
{
    return frotzOutput;
}


const char *frotz_get_last_error(void)
{
    return frotzLastError;
}


int frotz_is_waiting_for_input(void)
{
    return frotzWaitingForInput;
}


int frotz_is_waiting_for_key_input(void)
{
    return
        frotzWaitingForInput &&
        frotzWaitingForKeyInput;
}


void frotz_prepare_run(void)
{
    frotzWaitingForInput = 0;
    frotzWaitingForLineInput = 0;
    frotzWaitingForKeyInput = 0;
    frotzStopRequested = 0;

    frotzCommandReady = 0;
    frotzPendingCommand[0] = '\0';

    frotzKeyReady = 0;
    frotzPendingKey = 0;

    frotzAutosaveRequested = 0;
    frotzAutosaveDone = 0;
    frotzAutosaveSucceeded = 0;
}


/*
 * Ask the Frotz task to write the current resume snapshot.
 *
 * Returns:
 *   1 = request accepted
 *   0 = interpreter is not safely waiting for player input
 */
int frotz_request_autosave(void)
{
    if (!frotzWaitingForInput ||
        frotzAutosaveRequested ||
        frotzCommandReady ||
        frotzKeyReady) {

        return 0;
    }

    frotzAutosaveDone = 0;
    frotzAutosaveSucceeded = 0;
    frotzAutosaveRequested = 1;

    return 1;
}


int frotz_is_autosave_done(void)
{
    return frotzAutosaveDone;
}


int frotz_autosave_succeeded(void)
{
    return frotzAutosaveSucceeded;
}


void frotz_request_stop(void)
{
    frotzStopRequested = 1;
}


/*
 * Submit one complete line of player input.
 *
 * Returns:
 *   1 = command accepted
 *   0 = Frotz is not currently waiting for line input
 */
int frotz_submit_command(const char *command)
{
    if (command == NULL || command[0] == '\0')
        return 0;

   if (!frotzWaitingForInput ||
    !frotzWaitingForLineInput ||
    frotzCommandReady)
    return 0;

    /*
     * The previous output is stable while Frotz is blocked at input.
     * Clear it now so the next captured transcript contains only the
     * response produced by this command.
     */
    frotz_output_clear();

    strncpy(
        frotzPendingCommand,
        command,
        sizeof(frotzPendingCommand) - 1
    );

    frotzPendingCommand[
        sizeof(frotzPendingCommand) - 1
    ] = '\0';

    /*
     * Drop the waiting flag before publishing the command.  CrossInk
     * will therefore wait for Frotz to reach the NEXT input prompt
     * before treating the new output as complete.
     */
    frotzWaitingForInput = 0;
    frotzCommandReady = 1;

    return 1;
}


/*
 * Submit one Z-machine single-key input.
 *
 * Returns:
 *   1 = key accepted
 *   0 = Frotz is not currently blocked inside os_read_key()
 */
int frotz_submit_key(int key)
{
    if (key <= 0 ||
        key > 255) {

        return 0;
    }

    if (!frotzWaitingForInput ||
        !frotzWaitingForKeyInput ||
        frotzKeyReady) {

        return 0;
    }

    frotz_output_clear();

    frotzPendingKey = key;

    /*
     * Publish the key only after dropping the waiting flag so CrossInk
     * waits for the NEXT input prompt before capturing the response.
     */
    frotzWaitingForInput = 0;
    frotzKeyReady = 1;

    return 1;
}


/*
 * Functions supplied by the Frotz core.
 */
extern void init_memory(void);
extern void init_process(void);
extern void z_restart(void);
extern void interpret(void);

extern int frotz_save_resume_snapshot(void);

extern const char* frotz_hal_get_save_path(void);

extern int frotz_hal_take_restore_on_start(void);

/*
 * ---------------------------------------------------------
 * Helpers called by FrotzX3.cpp
 * ---------------------------------------------------------
 */

int frotz_try_init_memory(void)
{
    int jumpResult;

    frotzLastError[0] = '\0';
    frotzJumpActive = 1;

    jumpResult = setjmp(frotzJump);

    if (jumpResult != 0) {

        if (frotzLastError[0] != '\0') {

            frotz_debug_log(
                frotzLastError
            );

        } else {

            frotz_error_log(
                "init_memory failed with no fatal message"
            );
        }

        frotzJumpActive = 0;
        return 0;
    }

    init_memory();

    frotzJumpActive = 0;
    return 1;
}


/*
 * Run the story interpreter.
 *
 * The important difference from the old boot test is that
 * reaching player input no longer exits interpret().
 *
 * os_read_line() remains alive and waits for CrossInk.
 *
 * Return values:
 *
 *   1 = stopped cleanly by CrossInk
 *   0 = fatal Frotz error
 *  -1 = interpret() returned unexpectedly
 */
int frotz_try_boot_to_input(void)
{
    int jumpResult;

    frotzLastError[0] = '\0';
    frotz_output_clear();

    frotzWaitingForInput = 0;
    frotzStopRequested = 0;

    frotzJumpActive = 1;

    jumpResult = setjmp(frotzJump);

    if (jumpResult == FROTZ_JUMP_FATAL) {

        frotzWaitingForInput = 0;
        frotzJumpActive = 0;

        return 0;
    }

    if (jumpResult == FROTZ_JUMP_STOP) {

        frotzWaitingForInput = 0;
        frotzJumpActive = 0;

        return 1;
    }

    init_process();

    /*
     * Tell Frotz about our virtual screen BEFORE z_restart()
     * initializes its windows.
     */
    os_init_screen();

    /*
     * Initialize stack/program counter and begin actual
     * Z-machine execution.
     */
    z_restart();

  f_setup.restore_mode =
    frotz_hal_take_restore_on_start();

if (f_setup.restore_mode) {
    frotz_debug_log(
        "boot: restore_mode = 1"
    );
} else {
    frotz_debug_log(
        "boot: restore_mode = 0"
    );
}

    /*
     * This now remains active for the life of the game.
     *
     * os_read_line() or os_read_key() will wait rather than
     * jumping out when the game requests input.
     */

   

    interpret();

    frotzWaitingForInput = 0;
    frotzJumpActive = 0;

    return -1;
}


/*
 * ---------------------------------------------------------
 * CrossInk / HalFile bridge
 * ---------------------------------------------------------
 */

extern int frotz_hal_open_story(const char *path);
extern int frotz_hal_read_story(void *buffer, unsigned int count);
extern int frotz_hal_seek_story(long offset, int whence);
extern long frotz_hal_tell_story(void);
extern void frotz_hal_close_story(void);


FILE *os_load_story(void)
{
    if (f_setup.story_file == NULL)
        return NULL;

    if (!frotz_hal_open_story(f_setup.story_file))
        return NULL;

    /*
     * Frotz expects a non-NULL FILE pointer as a handle.
     *
     * Actual story access is performed by our HalFile bridge.
     */
    return (FILE *)1;
}


int os_storyfile_seek(FILE *fp, long offset, int whence)
{
    (void)fp;

    return frotz_hal_seek_story(
        offset,
        whence
    );
}


int os_storyfile_tell(FILE *fp)
{
    (void)fp;

    return (int)frotz_hal_tell_story();
}


/*
 * ---------------------------------------------------------
 * ERROR / STATUS HANDLING
 * ---------------------------------------------------------
 */

void os_fatal(const char *s, ...)
{
    if (s != NULL) {

        strncpy(
            frotzLastError,
            s,
            sizeof(frotzLastError) - 1
        );

        frotzLastError[
            sizeof(frotzLastError) - 1
        ] = '\0';

    } else {

        strcpy(
            frotzLastError,
            "Unknown Frotz fatal error"
        );
    }

    frotz_error_log(frotzLastError);

    if (frotzJumpActive)
        longjmp(
            frotzJump,
            FROTZ_JUMP_FATAL
        );

    while (1) {
    }
}


void os_warn(const char *s, ...)
{
    (void)s;
}


void os_quit(void)
{
}


/*
 * ---------------------------------------------------------
 * BASIC SCREEN / TEXT OUTPUT
 * ---------------------------------------------------------
 */

void os_init_screen(void)
{
    /*
     * Present the X3 to the Z-machine as a simple
     * 38-column text display.
     *
     * Frotz internally uses 1x1 character units in our
     * current text-only port, so pixel dimensions and
     * character dimensions can be the same for now.
     */

    h_screen_cols = 38;
    h_screen_rows = 20;

    h_screen_width = 38;
    h_screen_height = 20;

    h_font_width = 1;
    h_font_height = 1;
}


void os_reset_screen(void)
{
}


void os_restart_game(int stage)
{
    (void)stage;
}


void os_display_char(zchar c)
{
    frotz_output_char(c);
}


void os_display_string(const zchar *s)
{
    if (s == NULL)
        return;

    while (*s)
        frotz_output_char(*s++);
}


int os_char_width(zchar c)
{
    (void)c;

    return 1;
}


int os_string_width(const zchar *s)
{
    int width = 0;

    if (s == NULL)
        return 0;

    while (*s++) {
        width++;
    }

    return width;
}


void os_set_cursor(int row, int col)
{
    (void)row;
    (void)col;
}


void os_set_text_style(int style)
{
    (void)style;
}


void os_set_colour(int foreground, int background)
{
    (void)foreground;
    (void)background;
}


void os_set_font(int font)
{
    (void)font;
}


int os_font_data(
    int font,
    int *height,
    int *width)
{
    (void)font;

    if (height != NULL)
        *height = 1;

    if (width != NULL)
        *width = 1;

    return 1;
}


void os_erase_area(
    int top,
    int left,
    int bottom,
    int right,
    int win)
{
    (void)top;
    (void)left;
    (void)bottom;
    (void)right;
    (void)win;
}


void os_scroll_area(
    int top,
    int left,
    int bottom,
    int right,
    int units)
{
    (void)top;
    (void)left;
    (void)bottom;
    (void)right;
    (void)units;
}


void os_more_prompt(void)
{
}


int os_peek_colour(void)
{
    return DEFAULT_COLOUR;
}


/*
 * ---------------------------------------------------------
 * INPUT
 * ---------------------------------------------------------
 */

zchar os_read_line(
    int max,
    zchar *buf,
    int timeout,
    int width,
    int continued)
{
    frotz_debug_log(
    "entered os_read_line"
);
    (void)timeout;
    (void)width;
    (void)continued;

    /*
     * The Z-machine has reached a player command prompt.
     *
     * Keep interpret() alive here until CrossInk supplies a complete
     * command or asks the story to stop.
     */
    frotzWaitingForInput = 1;
    frotzWaitingForLineInput = 1;
    frotzWaitingForKeyInput = 0;

frotz_debug_log(
    "os_read_line: waiting flags set"
);

    while (!frotzStopRequested &&
           !frotzCommandReady) {

        if (frotzAutosaveRequested) {

            frotzAutosaveSucceeded =
                frotz_save_resume_snapshot();

            frotzAutosaveRequested = 0;
            frotzAutosaveDone = 1;

            /*
             * Stay inside os_read_line().  The player command prompt
             * is still active and the UI may now stop the story after
             * observing autosave completion.
             */
            continue;
        }

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );
    }

    if (frotzStopRequested) {

    frotzWaitingForInput = 0;
    frotzWaitingForLineInput = 0;

        if (frotzJumpActive) {

            longjmp(
                frotzJump,
                FROTZ_JUMP_STOP
            );
        }

        return ZC_RETURN;
    }

    /*
     * Copy the pending CrossInk command into Frotz's input buffer.
     *
     * input.c expects a normal null-terminated zchar string here.
     */
    if (buf != NULL && max > 0) {

        int i = 0;

        while (frotzPendingCommand[i] != '\0' &&
               i < max) {

            buf[i] =
                (zchar)frotzPendingCommand[i];

            ++i;
        }

        buf[i] = 0;
    }

    frotzPendingCommand[0] = '\0';
    frotzCommandReady = 0;
   frotzWaitingForInput = 0;
frotzWaitingForLineInput = 0;

return ZC_RETURN;
}


zchar os_read_key(
    int timeout,
    int cursor)
{
    (void)timeout;
    (void)cursor;

    frotz_debug_log(
        "entered os_read_key"
    );

    /*
     * READ_CHAR / single-key input.
     *
     * Stay blocked inside the Frotz task until CrossInk supplies one
     * character, asks for an autosave, or stops the story.
     */
    frotzWaitingForInput = 1;
    frotzWaitingForLineInput = 0;
    frotzWaitingForKeyInput = 1;

    frotzKeyReady = 0;
    frotzPendingKey = 0;

    frotz_debug_log(
        "os_read_key: waiting for single key"
    );

    while (!frotzStopRequested &&
           !frotzKeyReady) {

        if (frotzAutosaveRequested) {

            frotzAutosaveSucceeded =
                frotz_save_resume_snapshot();

            frotzAutosaveRequested = 0;
            frotzAutosaveDone = 1;

            continue;
        }

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );
    }

    if (frotzStopRequested) {

        frotzWaitingForInput = 0;
        frotzWaitingForLineInput = 0;
        frotzWaitingForKeyInput = 0;

        if (frotzJumpActive) {

            longjmp(
                frotzJump,
                FROTZ_JUMP_STOP
            );
        }

        return ZC_RETURN;
    }

    const zchar result =
        (zchar)frotzPendingKey;

    frotzPendingKey = 0;
    frotzKeyReady = 0;

    frotzWaitingForInput = 0;
    frotzWaitingForLineInput = 0;
    frotzWaitingForKeyInput = 0;

    return result;
}


/*
 * ---------------------------------------------------------
 * FILE SELECTION / SAVE / RESTORE
 * ---------------------------------------------------------
 */

int os_read_file_name(
    char *file_name,
    const char *default_name,
    int flag)
{
    (void)default_name;
    (void)flag;

    const char *save_path =
        frotz_hal_get_save_path();

    if (save_path == NULL ||
        save_path[0] == '\0') {

        return 0;
    }

    strcpy(
        file_name,
        save_path
    );

    return 1;
}


/*
 * ---------------------------------------------------------
 * RANDOM NUMBER SEED
 * ---------------------------------------------------------
 */

int os_random_seed(void)
{
    return 12345;
}


/*
 * ---------------------------------------------------------
 * GRAPHICS
 * ---------------------------------------------------------
 */

void os_draw_picture(
    int picture,
    int y,
    int x)
{
    (void)picture;
    (void)y;
    (void)x;
}


int os_picture_data(
    int picture,
    int *height,
    int *width)
{
    (void)picture;

    if (height != NULL)
        *height = 0;

    if (width != NULL)
        *width = 0;

    return 0;
}


/*
 * ---------------------------------------------------------
 * SOUND
 * ---------------------------------------------------------
 */

void os_init_sound(void)
{
}


void os_beep(int number)
{
    (void)number;
}


void os_prepare_sample(int number)
{
    (void)number;
}


void os_start_sample(
    int number,
    int volume,
    int repeats,
    zword eos)
{
    (void)number;
    (void)volume;
    (void)repeats;
    (void)eos;
}


void os_stop_sample(void)
{
}


void os_finish_with_sample(void)
{
}


/*
 * ---------------------------------------------------------
 * MISC
 * ---------------------------------------------------------
 */

void os_tick(void)
{
}


void os_init_setup(void)
{
}


void os_process_arguments(
    int argc,
    char *argv[])
{
    (void)argc;
    (void)argv;
}


int os_repaint_window(
    int win,
    int ypos_old,
    int ypos_new,
    int xpos,
    int ysize,
    int xsize)
{
    (void)win;
    (void)ypos_old;
    (void)ypos_new;
    (void)xpos;
    (void)ysize;
    (void)xsize;

    return FALSE;
}