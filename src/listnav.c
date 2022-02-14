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
	ASSERT(max >= min);

	nav->min = min;
	nav->max = max;
	listnav_update_wlen(nav, MIN(nav->wlen, nav->max - nav->min));
}

void
listnav_update_wlen(struct listnav *nav, int wlen)
{
	ASSERT(wlen >= 0);

	nav->wlen = MIN(wlen, nav->max - nav->min);
	if (nav->wmin < nav->min)
		nav->wmin = nav->min;
	if (nav->wmin + nav->wlen > nav->max)
		nav->wmin = nav->max - nav->wlen;
	nav->wmax = nav->wmin + nav->wlen;
	nav->sel = MAX(MIN(nav->sel, nav->wmax - 1), nav->wmin);
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

