#pragma once

#include "list.h"
#include "listnav.h"
#include "pane.h"

#include <stdbool.h>

#define USER_STATUS(...) do { \
		free(user_status); \
		user_status = aprintf(__VA_ARGS__); \
		user_status_uptime = 10; \
	} while (0)

void tui_init(void);
void tui_deinit(void);
bool tui_update(void);

extern struct pane *cmd_pane, *tag_pane, *track_pane;
extern struct pane *pane_sel, *pane_after_cmd;

extern struct list *tracks_vis;
extern int track_show_playlist;

extern struct listnav tag_nav;
extern struct listnav track_nav;

extern char *user_status;
extern int user_status_uptime;

static inline bool
tui_enabled(void)
{
	return !isendwin();
}

static inline void
tui_restore(void)
{
	if (!isendwin())
		endwin();
}
