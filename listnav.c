#include "listnav.h"
#include "util.h"

#include <string.h>

void
listnav_init(struct listnav *nav)
{
	memset(nav, 0, sizeof(struct listnav));
}

void
listnav_update_bounds(struct listnav *nav, int min, int max)
{
	nav->min = min;
	nav->max = max;
	nav->wmin = MAX(nav->wmin, nav->min);
	nav->wmax = MIN(nav->wmin + nav->wlen, nav->max);
	nav->sel = MIN(MAX(nav->sel, nav->wmin), nav->wmax - 1);
}

void
listnav_update_wlen(struct listnav *nav, int wlen)
{
	nav->wlen = wlen;
	nav->wmax = MIN(nav->wmin + nav->wlen, nav->max);
	nav->sel = MIN(MAX(nav->sel, nav->wmin), nav->wmax - 1);
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

