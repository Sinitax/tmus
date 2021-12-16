#pragma once

struct listnav {
	/* current window */
	int wmin, wmax, wlen;

	/* selected item moving inside of window */
	int sel;

	/* bounds of actual list */
	int min, max;
};

void listnav_init(struct listnav *nav);
void listnav_update_bounds(struct listnav *nav, int min, int max);
void listnav_update_wlen(struct listnav *nav, int wlen);
void listnav_update_sel(struct listnav *nav, int sel);

