// RetroAchievements for Banjo: Recompiled.
//
// The game-side half of the mod. All the RetroAchievements work (login, reading game memory,
// talking to the server) happens in the mod's native library, bk_retroachievements.so; this file
// only starts it, ticks it once per game frame, and shows its messages as on-screen toasts.

#include "modding.h"
#include "recompui.h"

// Base recomp API.
RECOMP_IMPORT("*", char* recomp_get_config_string(const char* key));
RECOMP_IMPORT("*", void recomp_free_config_string(char* str));
RECOMP_IMPORT("*", u32 recomp_get_config_u32(const char* key));
RECOMP_IMPORT("*", char* recomp_get_mod_folder_path(void));

// This mod's native library.
RECOMP_IMPORT(".", s32 ra_init(const char* username, const char* password, u32 hardcore, const char* mods_folder));
RECOMP_IMPORT(".", void ra_frame(void));
RECOMP_IMPORT(".", s32 ra_toast_tick(char* title, u32 title_size, char* body, u32 body_size));

// Return values of ra_toast_tick.
#define RA_TOAST_NONE 0
#define RA_TOAST_SHOW 1
#define RA_TOAST_HIDE 2

#define TOAST_TITLE_SIZE 128
#define TOAST_BODY_SIZE 384

static char toast_title[TOAST_TITLE_SIZE];
static char toast_body[TOAST_BODY_SIZE];

static int started = 0;
static int ui_ready = 0;
static RecompuiContext toast_context;
static RecompuiResource toast_box;
static RecompuiResource toast_title_label;
static RecompuiResource toast_body_label;

static void create_toast_ui(void) {
    static const RecompuiColor background = { 18, 18, 28, 225 };
    static const RecompuiColor border = { 232, 184, 64, 255 };
    static const RecompuiColor title_color = { 245, 205, 96, 255 };
    static const RecompuiColor body_color = { 240, 240, 240, 255 };

    toast_context = recompui_create_context();
    recompui_open_context(toast_context);

    // Overlay only: the toast never takes input away from the game.
    recompui_set_context_captures_input(toast_context, 0);
    recompui_set_context_captures_mouse(toast_context, 0);

    RecompuiResource root = recompui_context_root(toast_context);

    toast_box = recompui_create_element(toast_context, root);
    recompui_set_position(toast_box, POSITION_ABSOLUTE);
    recompui_set_top(toast_box, 24.0f, UNIT_DP);
    recompui_set_right(toast_box, 24.0f, UNIT_DP);
    recompui_set_max_width(toast_box, 520.0f, UNIT_DP);
    recompui_set_display(toast_box, DISPLAY_NONE);
    recompui_set_flex_direction(toast_box, FLEX_DIRECTION_COLUMN);
    recompui_set_gap(toast_box, 6.0f, UNIT_DP);
    recompui_set_padding(toast_box, 16.0f, UNIT_DP);
    recompui_set_border_width(toast_box, 2.0f, UNIT_DP);
    recompui_set_border_radius(toast_box, 12.0f, UNIT_DP);
    recompui_set_background_color(toast_box, &background);
    recompui_set_border_color(toast_box, &border);

    toast_title_label = recompui_create_label(toast_context, toast_box, "", LABELSTYLE_NORMAL);
    recompui_set_color(toast_title_label, &title_color);

    toast_body_label = recompui_create_label(toast_context, toast_box, "", LABELSTYLE_SMALL);
    recompui_set_color(toast_body_label, &body_color);

    recompui_close_context(toast_context);
    recompui_show_context(toast_context);

    ui_ready = 1;
}

static void start(void) {
    char* username = recomp_get_config_string("username");
    char* password = recomp_get_config_string("password");
    char* mods_folder = recomp_get_mod_folder_path();
    u32 hardcore = recomp_get_config_u32("hardcore");

    ra_init(username != NULL ? username : "", password != NULL ? password : "", hardcore, mods_folder != NULL ? mods_folder : "");

    if (username != NULL) recomp_free_config_string(username);
    if (password != NULL) recomp_free_config_string(password);
    if (mods_folder != NULL) recomp_free_config_string(mods_folder);

    if (recomp_get_config_u32("notifications")) {
        create_toast_ui();
    }
}

static void update_toast(void) {
    s32 action = ra_toast_tick(toast_title, sizeof(toast_title), toast_body, sizeof(toast_body));
    if (action == RA_TOAST_NONE || !ui_ready) {
        return;
    }

    recompui_open_context(toast_context);
    if (action == RA_TOAST_SHOW) {
        recompui_set_text(toast_title_label, toast_title);
        recompui_set_text(toast_body_label, toast_body);
        recompui_set_display(toast_body_label, toast_body[0] != '\0' ? DISPLAY_BLOCK : DISPLAY_NONE);
        recompui_set_display(toast_box, DISPLAY_FLEX);
    }
    else {
        recompui_set_display(toast_box, DISPLAY_NONE);
    }
    recompui_close_context(toast_context);
}

// Called once per game frame by mainLoop (Banjo: Recompiled also runs its UI callbacks here).
RECOMP_HOOK("baMotor_80250C08") void ra_on_frame(void) {
    if (!started) {
        started = 1;
        start();
    }

    ra_frame();
    update_toast();
}
