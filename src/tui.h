#pragma once

#include "list.h"
#include "listnav.h"
#include "pane.h"

#include <stdbool.h>

#define CMD_SET_STATUS(...) do { \
		free(cmd_status); \
		cmd_status_uptime = 10; \
		cmd_status = aprintf(__VA_ARGS__); \
	} while (0)

void tui_init(void);
void tui_deinit(void);

bool tui_update(void);
void tui_restore(void);

extern struct pane *cmd_pane, *tag_pane, *track_pane;
extern struct pane *pane_sel, *pane_after_cmd;

extern struct list *tracks_vis;
extern int track_show_playlist;

extern struct listnav tag_nav;
extern struct listnav track_nav;

extern char *cmd_status;
extern int cmd_status_uptime;

