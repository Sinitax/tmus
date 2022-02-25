#pragma once

#define NCURSES_WIDECHAR 1

#include <ncurses.h>

#include <stdbool.h>

struct pane;

typedef bool (*pane_handler)(wint_t c);
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
void pane_deinit(struct pane *pane);

void pane_resize(struct pane *pane, int sx, int sy, int ex, int ey);
void pane_clearln(struct pane *pane, int y);
void pane_writeln(struct pane *pane, int y, const char *line);

