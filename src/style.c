#include "style.h"

#include <string.h>

static int style_attrs[STYLE_COUNT];

void
style_init(void)
{
	memset(style_attrs, 0, sizeof(style_attrs));
	style_add(STYLE_DEFAULT, COLOR_WHITE, COLOR_BLACK, 0);
	style_add(STYLE_TITLE, COLOR_WHITE, COLOR_BLUE, A_BOLD);
	style_add(STYLE_PANE_SEP, COLOR_BLUE, COLOR_BLACK, 0);
	style_add(STYLE_ITEM_SEL, COLOR_YELLOW, COLOR_BLACK, A_BOLD);
	style_add(STYLE_ITEM_HOVER, COLOR_WHITE, COLOR_BLUE, 0);
	style_add(STYLE_ITEM_HOVER_SEL, COLOR_YELLOW, COLOR_BLUE, A_BOLD);
	style_add(STYLE_ERROR, COLOR_RED, COLOR_BLACK, 0);
	style_add(STYLE_PREV, COLOR_WHITE, COLOR_BLACK, A_DIM);
}

void
style_add(int style, int fg, int bg, int attr)
{
	style_attrs[style] = attr;
	init_pair(style, fg, bg);
}

void
style_on(WINDOW *win, int style)
{
	ATTR_ON(win, COLOR_PAIR(style) | style_attrs[style]);
}

void
style_off(WINDOW *win, int style)
{
	ATTR_OFF(win, COLOR_PAIR(style) | style_attrs[style]);
}

