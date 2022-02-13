#pragma once

#define NCURSES_WIDECHAR 1

#include <ncurses.h>

#define ATTR_ON(win, attr) wattr_on(win, attr, NULL)
#define ATTR_OFF(win, attr) wattr_off(win, attr, NULL)

enum {
	STYLE_DEFAULT,
	STYLE_TITLE,
	STYLE_PANE_SEP,
	STYLE_ITEM_SEL,
	STYLE_ITEM_HOVER,
	STYLE_ITEM_HOVER_SEL,
	STYLE_PREV,
	STYLE_ERROR,
	STYLE_COUNT
};

void style_init(void);

void style_add(int style, int fg, int bg, int attr);
void style_on(WINDOW *win, int style);
void style_off(WINDOW *win, int style);

