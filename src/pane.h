#pragma once

#define NCURSES_WIDECHAR 1

#include <ncurses.h>

#include <wchar.h>

struct pane;

typedef int (*pane_handler)(wint_t c);
typedef void (*pane_updater)(struct pane *pane, int sel);

struct pane {
	WINDOW *win;
	int sx, sy, ex, ey;
	int w, h;
	int active;

	pane_handler handle;
	pane_updater update;
};

void pane_init(struct pane *pane, pane_handler handle, pane_updater update);
void pane_resize(struct pane *pane, int sx, int sy, int ex, int ey);
void pane_clearln(struct pane *pane, int y);
void pane_free(struct pane *p);

