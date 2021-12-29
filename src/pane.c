#include "pane.h"
#include "util.h"

void
pane_init(struct pane *pane, pane_handler handle, pane_updater update)
{
	pane->win = newwin(1, 1, 0, 0);
	ASSERT(pane->win != NULL);
	pane->handle = handle;
	pane->update = update;
}

void
pane_resize(struct pane *pane, int sx, int sy, int ex, int ey)
{
	pane->sx = sx;
	pane->sy = sy;
	pane->ex = ex;
	pane->ey = ey;
	pane->w = pane->ex - pane->sx;
	pane->h = pane->ey - pane->sy;

	pane->active = (pane->w > 0 && pane->h > 0);
	if (pane->active) {
		wresize(pane->win, pane->h, pane->w);
		mvwin(pane->win, pane->sy, pane->sx);
		redrawwin(pane->win);
	}
}

void
pane_clearln(struct pane *pane, int y)
{
	int i;

	wmove(pane->win, y, 0);
	for (i = 0; i < pane->w; i++)
		waddch(pane->win, ' ');
}

void
pane_free(struct pane *pane)
{
	delwin(pane->win);
}
