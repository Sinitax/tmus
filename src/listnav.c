#include "listnav.h"
#include "util.h"

#include <string.h>

static void listnav_update_win(struct listnav *nav);

void
listnav_update_win(struct listnav *nav)
{
	nav->wmin = MAX(nav->wmax - nav->wlen, nav->min);
	nav->wmax = MIN(nav->wmin + nav->wlen, nav->max);
	nav->sel = MAX(MIN(nav->sel, nav->wmax - 1), nav->wmin);
}

void
listnav_init(struct listnav *nav)
{
	nav->sel = 0;
	nav->min = 0;
	nav->max = 0;
	nav->wlen = 0;
	nav->wmin = 0;
	nav->wmax = 0;
}

void
listnav_update_bounds(struct listnav *nav, int min, int max)
{
	ASSERT(max >= min);

	nav->min = min;
	nav->max = max;

	listnav_update_win(nav);
}

void
listnav_update_wlen(struct listnav *nav, int wlen)
{
	ASSERT(wlen >= 0);

	nav->wlen = wlen;
	listnav_update_win(nav);
}

void
listnav_update_sel(struct listnav *nav, int sel)
{
	nav->sel = MAX(MIN(sel, nav->max - 1), nav->min);
	if (nav->sel >= nav->wmax) {
		nav->wmax = nav->sel + 1;
		nav->wmin = MAX(nav->min, nav->wmax - nav->wlen);
	} else if (nav->sel < nav->wmin) {
		nav->wmin = nav->sel;
		nav->wmax = MIN(nav->wmin + nav->wlen, nav->max);
	}
}

