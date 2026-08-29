#include "frotz.h"

/* Story file name, id number and size */

char *story_name = 0;

enum story story_id = UNKNOWN;
long story_size = 0;


/* Story file header data */

zbyte h_version = 0;
zbyte h_config = 0;
zword h_release = 0;
zword h_resident_size = 0;
zword h_start_pc = 0;
zword h_dictionary = 0;
zword h_objects = 0;
zword h_globals = 0;
zword h_dynamic_size = 0;
zword h_flags = 0;
zbyte h_serial[6] = { 0, 0, 0, 0, 0, 0 };
zword h_abbreviations = 0;
zword h_file_size = 0;
zword h_checksum = 0;
zbyte h_interpreter_number = 0;
zbyte h_interpreter_version = 0;
zbyte h_screen_rows = 0;
zbyte h_screen_cols = 0;
zword h_screen_width = 0;
zword h_screen_height = 0;
zbyte h_font_height = 1;
zbyte h_font_width = 1;
zword h_functions_offset = 0;
zword h_strings_offset = 0;
zbyte h_default_background = 0;
zbyte h_default_foreground = 0;
zword h_terminating_keys = 0;
zword h_line_width = 0;
zbyte h_standard_high = 1;
zbyte h_standard_low = 0;
zword h_alphabet = 0;
zword h_extension_table = 0;
zbyte h_user_name[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

zword hx_table_size = 0;
zword hx_mouse_x = 0;
zword hx_mouse_y = 0;
zword hx_unicode_table = 0;


/* Stack data */

zword stack[STACK_SIZE];
zword *sp = 0;
zword *fp = 0;
zword frame_count = 0;


/* IO streams */

bool ostream_screen = TRUE;
bool ostream_script = FALSE;
bool ostream_memory = FALSE;
bool ostream_record = FALSE;
bool istream_replay = FALSE;
bool message = FALSE;


/* Current window and mouse data */

int cwin = 0;
int mwin = 0;

int mouse_y = 0;
int mouse_x = 0;


/* Window attributes */

bool enable_wrapping = FALSE;
bool enable_scripting = FALSE;
bool enable_scrolling = FALSE;
bool enable_buffering = FALSE;


/* Misc */

int option_sound = 1;
char *option_zcode_path = 0;

long reserve_mem = 0;


/* Frotz runtime configuration */

f_setup_t f_setup;


/*
 * z_piracy
 *
 * This normally lives in Frotz's main.c.
 */
void z_piracy(void)
{
    branch(!f_setup.piracy);
}

void frotz_set_story_file(char *path)
{
    f_setup.story_file = path;
    story_name = path;
}