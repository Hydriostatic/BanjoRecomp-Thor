#ifndef __RECOMPUI_H__
#define __RECOMPUI_H__

// The subset of the recomp UI API this mod uses. Signatures match RecompFrontend's ui_api.cpp.

#include "modding.h"

typedef u32 RecompuiContext;
typedef u32 RecompuiResource;

typedef struct {
    u8 r, g, b, a;
} RecompuiColor;

typedef enum { POSITION_ABSOLUTE, POSITION_RELATIVE } RecompuiPosition;
typedef enum { UNIT_PX, UNIT_DP, UNIT_PERCENT } RecompuiUnit;
typedef enum { DISPLAY_NONE, DISPLAY_BLOCK, DISPLAY_INLINE, DISPLAY_INLINE_BLOCK, DISPLAY_FLOW_ROOT, DISPLAY_FLEX } RecompuiDisplay;
typedef enum { FLEX_DIRECTION_ROW, FLEX_DIRECTION_COLUMN } RecompuiFlexDirection;
typedef enum { LABELSTYLE_ANNOTATION, LABELSTYLE_SMALL, LABELSTYLE_NORMAL, LABELSTYLE_LARGE } RecompuiLabelStyle;

RECOMP_IMPORT("*", RecompuiContext recompui_create_context(void));
RECOMP_IMPORT("*", void recompui_open_context(RecompuiContext context));
RECOMP_IMPORT("*", void recompui_close_context(RecompuiContext context));
RECOMP_IMPORT("*", void recompui_show_context(RecompuiContext context));
RECOMP_IMPORT("*", void recompui_hide_context(RecompuiContext context));
RECOMP_IMPORT("*", RecompuiResource recompui_context_root(RecompuiContext context));
RECOMP_IMPORT("*", void recompui_set_context_captures_input(RecompuiContext context, int captures_input));
RECOMP_IMPORT("*", void recompui_set_context_captures_mouse(RecompuiContext context, int captures_mouse));

RECOMP_IMPORT("*", RecompuiResource recompui_create_element(RecompuiContext context, RecompuiResource parent));
RECOMP_IMPORT("*", RecompuiResource recompui_create_label(RecompuiContext context, RecompuiResource parent, const char* text, RecompuiLabelStyle style));
RECOMP_IMPORT("*", void recompui_set_text(RecompuiResource element, const char* text));

RECOMP_IMPORT("*", void recompui_set_position(RecompuiResource id, RecompuiPosition position));
RECOMP_IMPORT("*", void recompui_set_top(RecompuiResource id, float top, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_right(RecompuiResource id, float right, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_width(RecompuiResource id, float width, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_max_width(RecompuiResource id, float width, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_padding(RecompuiResource id, float padding, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_gap(RecompuiResource id, float gap, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_border_width(RecompuiResource id, float width, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_border_radius(RecompuiResource id, float radius, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_font_size(RecompuiResource id, float size, RecompuiUnit unit));
RECOMP_IMPORT("*", void recompui_set_display(RecompuiResource id, RecompuiDisplay display));
RECOMP_IMPORT("*", void recompui_set_flex_direction(RecompuiResource id, RecompuiFlexDirection direction));
RECOMP_IMPORT("*", void recompui_set_background_color(RecompuiResource id, const RecompuiColor* color));
RECOMP_IMPORT("*", void recompui_set_border_color(RecompuiResource id, const RecompuiColor* color));
RECOMP_IMPORT("*", void recompui_set_color(RecompuiResource id, const RecompuiColor* color));

#endif
