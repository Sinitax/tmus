#pragma once

#include "list.h"
#include "listnav.h"
#include "pane.h"

#include <stdbool.h>
#include <wchar.h>

void tui_init(void);
void tui_deinit(void);

bool tui_update(void);
void tui_restore(void);

extern struct pane *cmd_pane, *tag_pane, *track_pane;
extern struct pane *pane_sel, *pane_top_sel;

extern struct list *tracks_vis;
extern int track_show_playlist;

extern struct listnav tag_nav;
extern struct listnav track_nav;


