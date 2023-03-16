#include <stdbool.h>
#define NCURSES_WIDECHAR 1
#define _GNU_SOURCE

#include "tui.h"

#include "cmd.h"
#include "data.h"
#include "history.h"
#include "pane.h"
#include "player.h"
#include "list.h"
#include "listnav.h"
#include "log.h"
#include "style.h"
#include "strbuf.h"
#include "util.h"

#include <ncurses.h>

#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#undef KEY_ENTER
#define KEY_ENTER '\n'
#define KEY_SPACE ' '
#define KEY_ESC '\x1b'
#define KEY_TAB '\t'
#define KEY_CTRL(c) ((c) & ~0x60)

enum {
	IMODE_EXECUTE,
	IMODE_TRACK_PLAY,
	IMODE_TRACK_VIS_SELECT,
	IMODE_TRACK_SELECT,
	IMODE_TAG_SELECT,
	IMODE_COUNT
};

typedef char *(*completion_gen)(const char *text, int fwd, int state);

static void pane_title(struct pane *pane, bool highlight, const char *fmtstr, ...);
static bool confirm_popup(const char *prompt);

static char *command_name_gen(const char *text, int fwd, int state);
static char *track_vis_name_gen(const char *text, int fwd, int state);
static char *track_name_gen(const char *text, int fwd, int state);
static char *tag_name_gen(const char *text, int fwd, int state);

static bool rename_current_tag(void);
static bool toggle_current_tag(void);
static void select_only_current_tag(void);
static void seek_next_selected_tag(void);
static void delete_current_tag(void);
static bool tag_name_cmp(struct link *l1, struct link *l2);
static void sort_visible_tags(void);
static bool tag_pane_input(wint_t c);
static void tag_pane_vis(struct pane *pane, int sel);

static bool play_current_track(void);
static bool nav_to_track_tag(struct track *target);
static bool nav_to_track(struct track *target);
static bool rename_current_track(void);
static bool delete_current_track(void);
static void queue_current_track(void);
static void unqueue_last_track(void);
static bool track_vis_name_cmp(struct link *l1, struct link *l2);
static void sort_visible_tracks(void);
static bool track_pane_input(wint_t c);
static void track_pane_vis(struct pane *pane, int sel);

static void select_cmd_pane(int mode);
static bool run_cmd(const char *name);
static bool play_track(const char *name);
static bool nav_to_track_by_name(const char *name);
static bool nav_to_track_vis_by_name(const char *name);
static bool seek_tag(const char *name);
static bool cmd_pane_input(wint_t c);
static void cmd_pane_vis(struct pane *pane, int sel);

static void update_tracks_vis(void);
static void reindex_selected_tags(void);
static void main_input(wint_t c);
static void main_vis(void);

static void tui_curses_init(void);
static void tui_resize(void);

static const char imode_prefix[IMODE_COUNT] = {
	[IMODE_EXECUTE] = ':',
	[IMODE_TRACK_PLAY] = '!',
	[IMODE_TRACK_SELECT] = '/',
	[IMODE_TRACK_VIS_SELECT] = '~',
	[IMODE_TAG_SELECT] = '?',
};

static const char player_state_chars[] = {
	[PLAYER_STATE_PAUSED] = '|',
	[PLAYER_STATE_PLAYING] = '>',
	[PLAYER_STATE_STOPPED] = '#'
};

static int scrw, scrh;
static int quit;

static struct pane pane_left, pane_right, pane_bot;
static struct pane *const panes[] = {
	&pane_left,
	&pane_right,
	&pane_bot
};

static struct history command_history;
static struct history track_play_history;
static struct history track_select_history;
static struct history track_vis_select_history;
static struct history tag_select_history;
static struct history *history;

static int cmd_input_mode;
static int cmd_show;

static struct inputln completion_query;
static int completion_reset;
static completion_gen completion;

struct pane *cmd_pane, *tag_pane, *track_pane;
struct pane *pane_sel, *pane_after_cmd;

struct list *tracks_vis;
int track_show_playlist;
struct listnav tag_nav;
struct listnav track_nav;

char *user_status;
int user_status_uptime;

void
pane_title(struct pane *pane, bool highlight, const char *fmtstr, ...)
{
	va_list ap;

	style_on(pane->win, STYLE_TITLE);
	if (highlight) ATTR_ON(pane->win, A_STANDOUT);

	pane_clearln(pane, 0);
	wmove(pane->win, 0, 0);
	va_start(ap, fmtstr);
	vw_printw(pane->win, fmtstr, ap);
	va_end(ap);

	if (highlight) ATTR_OFF(pane->win, A_STANDOUT);
	style_off(pane->win, STYLE_TITLE);
}

bool
confirm_popup(const char *prompt)
{
	WINDOW *win;
	int maxx, maxy;
	int sx, sy, w, h;
	int tx, ty;
	unsigned int c;

	getmaxyx(stdscr, maxy, maxx);

	w = 30;
	h = 5;
	sx = (maxx - w) / 2;
	sy = (maxy - h) / 2;
	win = newwin(h, w, sy, sx);

	tx = (w - strlen(prompt)) / 2;
	ty = h / 2;
	wborder(win, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwprintw(win, ty, tx, "%s", prompt);
	wrefresh(win);

	while ((c = wgetch(win)) && c >= 128);

	delwin(win);

	return c == 'y';
}

char *
command_name_gen(const char *text, int fwd, int reset)
{
	static int index, len;
	char *dup;
	int dir;

	dir = fwd ? 1 : -1;

	if (reset) {
		index = 0;
		len = strlen(text);
	} else if (index >= -1 && index <= command_count) {
		index += dir;
	}

	while (index >= 0 && index < command_count) {
		if (!strncmp(commands[index].name, text, len)) {
			dup = astrdup(commands[index].name);
			return dup;
		}
		index += dir;
	}

	return NULL;
}

char *
track_vis_name_gen(const char *text, int fwd, int reset)
{
	static struct link *cur;
	struct link *link;
	struct track *track;
	const char *prevname;
	char *dup;

	if (reset) {
		prevname = NULL;
		cur = tracks_vis->head.next;
		link = cur;
	} else {
		link = fwd ? cur->next : cur->prev;
		prevname = NULL;
		if (LIST_INNER(cur)) {
			track = tracks_vis_track(cur);
			prevname = track->name;
		}
	}
	while (LIST_INNER(link)) {
		track = tracks_vis_track(link);
		if (prevname && !strcmp(prevname, track->name))
			goto next;

		if (strcasestr(track->name, text)) {
			cur = link;
			dup = astrdup(track->name);
			return dup;
		}

next:
		prevname = track->name;
		link = fwd ? link->next : link->prev;
	}

	return NULL;

}

char *
track_name_gen(const char *text, int fwd, int reset)
{
	static struct link *cur;
	struct link *link;
	struct track *track;
	const char *prevname;
	char *dup;

	if (reset) {
		prevname = NULL;
		cur = tracks.head.next;
		link = cur;
	} else {
		link = fwd ? cur->next : cur->prev;
		prevname = NULL;
		if (LIST_INNER(cur)) {
			track = UPCAST(cur, struct track, link);
			prevname = track->name;
		}
	}

	while (LIST_INNER(link)) {
		track = UPCAST(link, struct track, link);
		if (prevname && !strcmp(prevname, track->name))
			goto next;

		if (strcasestr(track->name, text)) {
			cur = link;
			dup = aprintf("%s/%s", track->tag->name, track->name);
			return dup;
		}

next:
		prevname = track->name;
		link = fwd ? link->next : link->prev;
	}

	return NULL;
}

char *
tag_name_gen(const char *text, int fwd, int reset)
{
	static struct link *cur;
	struct link *link;
	struct tag *tag;
	char *dup;

	if (reset) {
		cur = tags.head.next;
		link = cur;
	} else {
		link = fwd ? cur->next : cur->prev;
	}

	while (LIST_INNER(link)) {
		tag = UPCAST(link, struct tag, link);
		if (strcasestr(tag->name, text)) {
			cur = link;
			dup = astrdup(tag->name);
			return dup;
		}
		link = fwd ? link->next : link->prev;
	}

	return NULL;
}

bool
rename_current_tag(void)
{
	struct link *link;
	struct tag *tag;
	char *cmd;

	link = list_at(&tags, tag_nav.sel);
	if (!link) return false;
	tag = UPCAST(link, struct tag, link);

	cmd = aprintf("rename %s", tag->name);
	select_cmd_pane(IMODE_EXECUTE);
	inputln_replace(history->input, cmd);
	free(cmd);

	return true;
}

bool
toggle_current_tag(void)
{
	struct link *link;
	struct tag *tag;

	if (list_empty(&tags)) return false;

	link = list_at(&tags, tag_nav.sel);
	if (!link) return false;
	tag = UPCAST(link, struct tag, link);

	/* toggle tag in tags_sel */
	if (link_inuse(&tag->link_sel)) {
		link_pop(&tag->link_sel);
	} else {
		list_push_back(&tags_sel, &tag->link_sel);
	}

	playlist_outdated = true;

	return true;
}

void
select_only_current_tag(void)
{
	list_clear(&tags_sel);
	toggle_current_tag();
}

void
seek_next_selected_tag(void)
{
	struct link *link;
	struct tag *tag;
	int index;

	if (list_empty(&tags_sel))
		return;

	if (list_empty(&tags))
		return;

	link = list_at(&tags, tag_nav.sel);
	if (!link) return;

	index = tag_nav.sel;
	tag = UPCAST(link, struct tag, link);
	do {
		index += 1;
		link = tag->link.next;
		if (!LIST_INNER(link)) {
			link = list_at(&tags, 0);
			index = 0;
		}
		tag = UPCAST(link, struct tag, link);
	} while (!link_inuse(&tag->link_sel));

	listnav_update_sel(&tag_nav, index);
}

void
delete_current_tag(void)
{
	struct link *link;
	struct tag *tag;

	if (!confirm_popup("Delete tag?"))
		return;

	link = list_at(&tags, tag_nav.sel);
	if (!link) return;
	tag = UPCAST(link, struct tag, link);
	if (link_inuse(&tag->link_sel))
		playlist_outdated = true;
	tag_rm(tag, true);
}

bool
tag_name_cmp(struct link *l1, struct link *l2)
{
	struct tag *t1, *t2;

	t1 = LINK_UPCAST(l1, struct tag, link);
	t2 = LINK_UPCAST(l2, struct tag, link);

	return strcmp(t1->name, t2->name) <= 0;
}

void
sort_visible_tags(void)
{
	list_sort(&tags, false, tag_name_cmp);
}

bool
tag_pane_input(wint_t c)
{
	switch (c) {
	case KEY_UP: /* nav up */
		listnav_update_sel(&tag_nav, tag_nav.sel - 1);
		break;
	case KEY_DOWN: /* nav down */
		listnav_update_sel(&tag_nav, tag_nav.sel + 1);
		break;
	case KEY_SPACE: /* toggle tag */
		toggle_current_tag();
		break;
	case KEY_ENTER: /* select only current tag */
		select_only_current_tag();
		break;
	case KEY_PPAGE: /* seek half a page up */
		listnav_update_sel(&tag_nav, tag_nav.sel - tag_nav.wlen / 2);
		break;
	case KEY_NPAGE: /* seek half a page down */
		listnav_update_sel(&tag_nav, tag_nav.sel + tag_nav.wlen / 2);
		break;
	case L'r': /* rename tag */
		rename_current_tag();
		break;
	case L'g': /* seek start of list */
		listnav_update_sel(&tag_nav, 0);
		break;
	case L'G': /* seek end of list */
		listnav_update_sel(&tag_nav, tag_nav.max - 1);
		break;
	case L'n': /* nav through selected tags */
		seek_next_selected_tag();
		break;
	case L'D': /* delete tag */
		delete_current_tag();
		break;
	case KEY_CTRL(L's'):
		sort_visible_tags();
		break;
	default:
		return false;
	}

	return true;
}

void
tag_pane_vis(struct pane *pane, int sel)
{
	struct tag *tag;
	struct link *link;
	int index, tagsel;

	werase(pane->win);
	pane_title(pane, sel, "Tags");

	listnav_update_bounds(&tag_nav, 0, list_len(&tags));
	listnav_update_wlen(&tag_nav, pane->h - 1);

	index = -1;
	for (LIST_ITER(&tags, link)) {
		tag = UPCAST(link, struct tag, link);
		tagsel = link_inuse(&tag->link_sel);

		index += 1;
		if (index < tag_nav.wmin) continue;
		if (index >= tag_nav.wmax) break;

		if (sel && tagsel && index == tag_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == tag_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (tagsel)
			style_on(pane->win, STYLE_ITEM_SEL);
		else if (index == tag_nav.sel)
			style_on(pane->win, STYLE_PREV);

		pane_writeln(pane, 1 + index - tag_nav.wmin, tag->name);

		if (sel && tagsel && index == tag_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == tag_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (tagsel)
			style_off(pane->win, STYLE_ITEM_SEL);
		else if (index == tag_nav.sel)
			style_off(pane->win, STYLE_PREV);
	}
}

bool
play_current_track(void)
{
	struct link *link;
	struct track *track;

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) return false;
	track = tracks_vis_track(link);
	player_play_track(track, true);

	return true;
}

bool
nav_to_track_tag(struct track *target)
{
	int index;

	if (!target) return false;

	index = list_index(&tags, &target->tag->link);
	if (index < 0) return false;

	listnav_update_sel(&tag_nav, index);
	update_tracks_vis();

	return true;
}

bool
nav_to_track(struct track *target)
{
	struct link *link;
	struct track *track;
	int index;

	if (!target) return false;

	index = 0;
	for (LIST_ITER(tracks_vis, link)) {
		track = tracks_vis_track(link);
		if (track == target) {
			listnav_update_sel(&track_nav, index);
			break;
		}
		index += 1;
	}

	return true;
}

bool
rename_current_track(void)
{
	struct link *link;
	struct track *track;
	char *cmd;

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) return false;
	track = tracks_vis_track(link);

	cmd = aprintf("rename %s", track->name);
	select_cmd_pane(IMODE_EXECUTE);
	inputln_replace(history->input, cmd);
	free(cmd);

	return true;
}

bool
delete_current_track(void)
{
	struct link *link;
	struct track *track;

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) return false;

	track = tracks_vis_track(link);

	if (!trash_tag || !strcmp(track->tag->name, "trash")) {
		if (!track_rm(track, true))
			USER_STATUS("Failed to remove track");
	} else {
		if (!track_move(track, trash_tag) && errno != EEXIST)
			USER_STATUS("Failed to trash track: %s", strerror(errno));
		if (errno == EEXIST && !track_rm(track, true))
			USER_STATUS("Failed to remove track");
	}

	return true;
}

void
queue_current_track(void)
{
	struct link *link;
	struct track *track;

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) return;

	track = tracks_vis_track(link);
	list_push_back(&player.queue, &track->link_pq);
}

void
unqueue_last_track(void)
{
	struct link *link;

	link = list_back(&player.queue);
	if (!link) return;
	link_pop(link);
}

bool
track_vis_name_cmp(struct link *l1, struct link *l2)
{
	struct track *t1, *t2;

	t1 = tracks_vis_track(l1);
	t2 = tracks_vis_track(l2);

	return strcmp(t1->name, t2->name) <= 0;
}

void
sort_visible_tracks(void)
{
	struct link *link;
	struct tag *tag;

	list_sort(tracks_vis, false, track_vis_name_cmp);

	if (!track_show_playlist) {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return;
		tag = LINK_UPCAST(link, struct tag, link);
		tag->reordered = true;
	}
}

bool
track_pane_input(wint_t c)
{
	switch (c) {
	case KEY_UP: /* nav up */
		listnav_update_sel(&track_nav, track_nav.sel - 1);
		break;
	case KEY_DOWN: /* nav down */
		listnav_update_sel(&track_nav, track_nav.sel + 1);
		break;
	case KEY_ENTER: /* play track */
		play_current_track();
		break;
	case KEY_PPAGE: /* seek half page up */
		listnav_update_sel(&track_nav,
			track_nav.sel - track_nav.wlen / 2);
		break;
	case KEY_NPAGE: /* seek half page down */
		listnav_update_sel(&track_nav,
			track_nav.sel + track_nav.wlen / 2);
		break;
	case L'r': /* rename track */
		rename_current_track();
		break;
	case L'g': /* seek start of list */
		listnav_update_sel(&track_nav, 0);
		break;
	case L'G': /* seek end of list */
		listnav_update_sel(&track_nav, track_nav.max - 1);
		break;
	case L'y': /* push queue track */
		queue_current_track();
		break;
	case L'Y': /* pop queue track */
		unqueue_last_track();
		break;
	case L'n': /* seek playing */
		nav_to_track(player.track);
		break;
	case L'D': /* delete track */
		delete_current_track();
		break;
	case KEY_CTRL(L's'): /* sort track in view */
		sort_visible_tracks();
		break;
	default:
		return false;
	}

	return true;
}

void
track_pane_vis(struct pane *pane, int sel)
{
	struct track *track;
	struct link *link;
	struct tag *tag;
	int index;

	werase(pane->win);
	if (tracks_vis == &player.playlist) {
		pane_title(pane, sel, "Tracks (playlist)");
	} else {
		link = list_at(&tags, tag_nav.sel);
		if (!link) {
			pane_title(pane, sel, "Tracks");
		} else {
			tag = UPCAST(link, struct tag, link);
			pane_title(pane, sel, "Tracks (%s)", tag->name);
		}
	}

	listnav_update_wlen(&track_nav, pane->h - 1);

	index = -1;
	for (LIST_ITER(tracks_vis, link)) {
		track = tracks_vis_track(link);

		index += 1;
		if (index < track_nav.wmin) continue;
		if (index >= track_nav.wmax) break;

		if (sel && index == track_nav.sel && track == player.track)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (track == player.track)
			style_on(pane->win, STYLE_ITEM_SEL);
		else if (index == track_nav.sel)
			style_on(pane->win, STYLE_PREV);

		pane_writeln(pane, 1 + index - track_nav.wmin, track->name);

		if (sel && index == track_nav.sel && track == player.track)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (track == player.track)
			style_off(pane->win, STYLE_ITEM_SEL);
		else if (index == track_nav.sel)
			style_off(pane->win, STYLE_PREV);
	}
}

void
select_cmd_pane(int mode)
{
	switch (mode) {
	case IMODE_EXECUTE:
		cmd_input_mode = IMODE_EXECUTE;
		pane_after_cmd = pane_sel;
		history = &command_history;
		completion = command_name_gen;
		break;
	case IMODE_TAG_SELECT:
		cmd_input_mode = IMODE_TAG_SELECT;
		pane_after_cmd = pane_sel;
		history = &tag_select_history;
		completion = tag_name_gen;
		break;
	case IMODE_TRACK_PLAY:
		cmd_input_mode = IMODE_TRACK_PLAY;
		pane_after_cmd = pane_sel;
		history = &track_play_history;
		completion = track_name_gen;
		break;
	case IMODE_TRACK_SELECT:
		cmd_input_mode = IMODE_TRACK_SELECT;
		pane_after_cmd = pane_sel;
		history = &track_select_history;
		completion = track_name_gen;
		break;
	case IMODE_TRACK_VIS_SELECT:
		cmd_input_mode = IMODE_TRACK_VIS_SELECT;
		pane_after_cmd = pane_sel;
		history = &track_vis_select_history;
		completion = track_vis_name_gen;
		break;
	default:
		ASSERT(0);
	}

	pane_sel = cmd_pane;
	completion_reset = 1;
}

bool
run_cmd(const char *query)
{
	bool success, found;

	success = cmd_run(query, &found);
	if (!success && !user_status)
		USER_STATUS("FAIL");
	else if (success && !user_status)
		USER_STATUS("OK");

	return found;
}

bool
play_track(const char *query)
{
	struct track *track;
	struct link *link;

	for (LIST_ITER(&tracks, link)) {
		track = UPCAST(link, struct track, link);
		if (!strcmp(track->name, query)) {
			player_play_track(track, true);
			return true;
		}
	}

	return false;
}

bool
nav_to_track_by_name(const char *query)
{
	struct track *track;
	struct tag *tag;
	struct link *link;
	const char *qtrack;
	const char *qtag;

	qtag = query;
	qtrack = strchr(query, '/');
	if (!qtrack) return false;
	qtrack += 1;

	for (LIST_ITER(&tags, link)) {
		tag = UPCAST(link, struct tag, link);
		if (!strncmp(tag->name, qtag, qtrack - qtag - 1))
			break;
	}
	if (!LIST_INNER(link))
		return false;

	for (LIST_ITER(&tag->tracks, link)) {
		track = UPCAST(link, struct track, link_tt);
		if (!strcmp(track->name, qtrack)) {
			nav_to_track_tag(track);
			nav_to_track(track);
			pane_after_cmd = track_pane;
			return true;
		}
	}

	return false;
}

bool
nav_to_track_vis_by_name(const char *query)
{
	struct track *track;
	struct link *link;

	for (LIST_ITER(tracks_vis, link)) {
		track = tracks_vis_track(link);
		if (!strcmp(track->name, query)) {
			nav_to_track(track);
			pane_after_cmd = track_pane;
			return true;
		}
	}

	return false;
}

bool
seek_tag(const char *query)
{
	struct tag *tag;
	struct link *link;
	int index;

	index = -1;
	for (LIST_ITER(&tags, link)) {
		index += 1;
		tag = UPCAST(link, struct tag, link);
		if (!strcmp(tag->name, query)) {
			listnav_update_sel(&tag_nav, index);
			pane_after_cmd = tag_pane;
			return true;
		}
	}

	return false;
}

bool
cmd_pane_input(wint_t c)
{
	char *res;
	int match;

	switch (c) {
	case KEY_ESC:
		match = strcmp(completion_query.buf, history->input->buf);
		if (!completion_reset && match) {
			inputln_copy(history->input, &completion_query);
		} else if (history->sel == history->input) {
			inputln_replace(history->input, "");
			pane_sel = pane_after_cmd;
		} else {
			history->sel = history->input;
		}
		break;
	case KEY_LEFT:
		inputln_left(history->sel);
		break;
	case KEY_RIGHT:
		inputln_right(history->sel);
		break;
	case KEY_CTRL('w'):
		inputln_del(history->sel, history->sel->cur);
		break;
	case KEY_UP:
		history_next(history);
		break;
	case KEY_DOWN:
		history_prev(history);
		break;
	case KEY_ENTER:
		if (!*history->sel->buf) {
			pane_sel = pane_after_cmd;
			break;
		}

		if (cmd_input_mode == IMODE_EXECUTE) {
			if (!run_cmd(history->sel->buf))
				USER_STATUS("No such command");
		} else if (cmd_input_mode == IMODE_TRACK_PLAY) {
			if (!play_track(history->sel->buf))
				USER_STATUS("Failed to find track");
		} else if (cmd_input_mode == IMODE_TRACK_SELECT) {
			if (!nav_to_track_by_name(history->sel->buf))
				USER_STATUS("Failed to find track");
		} else if (cmd_input_mode == IMODE_TRACK_VIS_SELECT) {
			if (!nav_to_track_vis_by_name(history->sel->buf))
				USER_STATUS("Failed to find track in view");
		} else if (cmd_input_mode == IMODE_TAG_SELECT) {
			if (!seek_tag(history->sel->buf))
				USER_STATUS("Failed to find tag");
		}

		history_submit(history);
		pane_sel = pane_after_cmd;
		break;
	case KEY_TAB:
	case KEY_BTAB:
		if (history->sel != history->input) {
			inputln_copy(history->input, history->sel);
			history->sel = history->input;
		}

		if (completion_reset)
			inputln_copy(&completion_query, history->input);

		res = completion(completion_query.buf,
			c == KEY_TAB, completion_reset);
		if (res) inputln_replace(history->input, res);
		free(res);

		completion_reset = 0;
		break;
	case KEY_BACKSPACE:
		if (history->sel->cur == 0) {
			pane_sel = pane_after_cmd;
			break;
		}
		inputln_del(history->sel, 1);
		completion_reset = 1;
		break;
	default:
		/* TODO: wide char input support */
		if (!isprint(c)) return 0;
		inputln_addch(history->sel, c);
		completion_reset = 1;
		break;
	}

	return true; /* grab everything */
}

void
cmd_pane_vis(struct pane *pane, int sel)
{
	static struct strbuf line = { 0 };
	struct inputln *cmd;
	struct link *link;
	int index, offset;

	werase(pane->win);

	/* track name */
	style_on(pane->win, STYLE_TITLE);
	pane_clearln(pane, 0);
	if (player.loaded) {
		strbuf_clear(&line);
		if (player.track)
			strbuf_append(&line, " %s", player.track->name);
		else if (player.track_name)
			strbuf_append(&line, " (*) %s", player.track_name);
		else
			strbuf_append(&line, " <UNKNOWN>");
		pane_writeln(pane, 0, line.buf);
	}
	style_off(pane->win, STYLE_TITLE);

	if (player.loaded) {
		/* status line */
		strbuf_clear(&line);
		strbuf_append(&line, "%c ", player_state_chars[player.state]);
		strbuf_append(&line, "%s / ", timestr(player.time_pos));
		strbuf_append(&line, "%s", timestr(player.time_end));

		if (player.volume >= 0)
			strbuf_append(&line, " - vol: %u%%", player.volume);

		if (player.status)
			strbuf_append(&line, " | [PLAYER] %s", player.status);

		if (list_len(&player.queue) > 0)
			strbuf_append(&line, " | [QUEUE] %i tracks",
				list_len(&player.queue));

		ATTR_ON(pane->win, A_REVERSE);
		pane_writeln(pane, 1, line.buf);
		ATTR_OFF(pane->win, A_REVERSE);
	} else if (player.status) {
		/* player message */
		strbuf_clear(&line);
		strbuf_append(&line, "[PLAYER] %s", player.status);
		pane_writeln(pane, 1, line.buf);
	}

	/* status bits on right of status line */
	if (player.loaded)
		ATTR_ON(pane->win, A_REVERSE);

	mvwaddstr(pane->win, 1, pane->w - 6, "[    ]");
	if (list_empty(&player.history))
		mvwaddstr(pane->win, 1, pane->w - 5, "H");
	if (track_show_playlist)
		mvwaddstr(pane->win, 1, pane->w - 4, "P");
	if (player.autoplay)
		mvwaddstr(pane->win, 1, pane->w - 3, "A");
	if (player.shuffle)
		mvwaddstr(pane->win, 1, pane->w - 2, "S");

	if (player.loaded)
		ATTR_OFF(pane->win, A_REVERSE);

	if (sel || cmd_show) {
		/* cmd and search input */
		strbuf_clear(&line);

		free(user_status);
		user_status = NULL;

		cmd = history->sel;
		if (cmd != history->input) {
			index = 0;
			for (LIST_ITER(&history->list, link)) {
				if (UPCAST(link, struct inputln, link) == cmd)
					break;
				index += 1;
			}
			strbuf_append(&line, "[%i] ", link ? index : -1);
		} else {
			strbuf_append(&line, "%c", imode_prefix[cmd_input_mode]);
		}
		offset = strlen(line.buf);

		strbuf_append(&line, "%s", cmd->buf);

		pane_writeln(pane, 2, line.buf);

		if (sel) { /* show cursor in text */
			ATTR_ON(pane->win, A_REVERSE);
			wmove(pane->win, 2, offset + cmd->cur);
			waddch(pane->win, cmd->cur < cmd->len
				? cmd->buf[cmd->cur] : L' ');
			ATTR_OFF(pane->win, A_REVERSE);
		}
	} else if (user_status && user_status_uptime) {
		user_status_uptime--;
		strbuf_clear(&line);
		strbuf_append(&line, " %s", user_status);
		pane_writeln(pane, 2, line.buf);
	} else {
		free(user_status);
		user_status = NULL;
	}
}

void
update_tracks_vis(void)
{
	struct link *link;
	struct tag *tag;

	if (track_show_playlist) {
		tracks_vis = &player.playlist;
	} else {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return;
		tag = UPCAST(link, struct tag, link);
		tracks_vis = &tag->tracks;
	}

	listnav_update_bounds(&track_nav, 0, list_len(tracks_vis));
}

void
reindex_selected_tags(void)
{
	struct link *link;
	struct tag *tag;
	struct track *track;
	struct tag *playing_tag;
	char *playing_name;

	playing_tag = NULL;
	playing_name = NULL;

	if (player.track) {
		playing_tag = player.track->tag;
		playing_name = astrdup(player.track->name);
	}

	if (track_show_playlist) {
		for (LIST_ITER(&tags_sel, link)) {
			tag = UPCAST(link, struct tag, link_sel);
			tag_reindex_tracks(tag);
		}
	} else {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return;
		tag = UPCAST(link, struct tag, link);
		tag_reindex_tracks(tag);
	}

	if (playing_tag) {
		for (LIST_ITER(&playing_tag->tracks, link)) {
			track = UPCAST(link, struct track, link_tt);
			if (!strcmp(track->name, playing_name)) {
				player.track = track;
				break;
			}
		}
	}
}

void
main_input(wint_t c)
{
	switch (c) {
	case KEY_TAB:
		pane_sel = pane_sel == &pane_left
			? &pane_right : &pane_left;
		break;
	case KEY_ESC:
		if (pane_sel == cmd_pane)
			pane_sel = pane_after_cmd;
		break;
	case KEY_LEFT:
		if (!player.loaded) break;
		player_seek(player.time_pos - 10);
		break;
	case KEY_RIGHT:
		if (!player.loaded) break;
		player_seek(player.time_pos + 10);
		break;
	case L'o':
		list_clear(&player.queue);
		break;
	case L'h':
		list_clear(&player.history);
		break;
	case L'c':
		player_toggle_pause();
		break;
	case L'>':
		player_next();
		break;
	case L'<':
		player_prev();
		break;
	case L'P':
		track_show_playlist ^= 1;
		break;
	case L'A':
		player.autoplay ^= 1;
		break;
	case L'S':
		player.shuffle ^= 1;
		break;
	case L'b':
		player_seek(0);
		break;
	case L'x':
		if (player.state == PLAYER_STATE_PLAYING)
			player_stop();
		else
			player_play();
		break;
	case L'.':
		cmd_rerun();
		break;
	case L':':
		select_cmd_pane(IMODE_EXECUTE);
		break;
	case L'/':
		select_cmd_pane(IMODE_TRACK_SELECT);
		break;
	case L'~':
		select_cmd_pane(IMODE_TRACK_VIS_SELECT);
		break;
	case L'!':
		select_cmd_pane(IMODE_TRACK_PLAY);
		break;
	case L'?':
		select_cmd_pane(IMODE_TAG_SELECT);
		break;
	case L'+':
		player_set_volume(MIN(100, player.volume + 5));
		break;
	case L'-':
		player_set_volume(MAX(0, player.volume - 5));
		break;
	case KEY_CTRL('l'):
		clear();
		refresh();
		break;
	case KEY_CTRL(L'r'):
		reindex_selected_tags();
		break;
	case L'q':
		quit = 1;
		break;
	case L'N':
		if (!nav_to_track_tag(player.track))
			return;
		track_show_playlist = false;
		update_tracks_vis();
		nav_to_track(player.track);
		pane_sel = track_pane;
		break;
	}
}

void
main_vis(void)
{
	int i;

	/* add missing title tile at the top */
	style_on(stdscr, STYLE_TITLE);
	move(0, pane_left.ex);
	addch(' ');
	style_off(stdscr, STYLE_TITLE);

	/* draw left-right separator line */
	style_on(stdscr, STYLE_PANE_SEP);
	for (i = pane_left.sy + 1; i < pane_left.ey; i++) {
		move(i, pane_left.ex);
		addch(ACS_VLINE);
	}
	style_off(stdscr, STYLE_PANE_SEP);
}

void
tui_curses_init(void)
{
	initscr();

	/* do most of the handling ourselves,
	 * enable special keys */
	raw();
	noecho();
	keypad(stdscr, TRUE);

	/* update screen occasionally for things like
	 * time even when no input was received */
	halfdelay(1);

	/* inits COLOR and COLOR_PAIRS used by styles */
	start_color();

	/* dont show cursor */
	curs_set(0);

	/* we use ESC deselecting the current pane
	 * and not for escape sequences, so dont wait */
	ESCDELAY = 0;
}

void
tui_resize(void)
{
	struct link *link;
	struct tag *tag;
	int leftw;

	getmaxyx(stdscr, scrh, scrw);

	/* guarantee a minimum terminal size */
	while (scrw < 10 || scrh < 4) {
		clear();
		refresh();
		usleep(10000);
		getmaxyx(stdscr, scrh, scrw);
	}

	/* adjust tag pane width to name lengths */
	leftw = 0;
	for (LIST_ITER(&tags, link)) {
		tag = UPCAST(link, struct tag, link);
		leftw = MAX(leftw, strlen(tag->name));
	}
	leftw = MAX(leftw + 1, 0.2f * scrw);

	pane_resize(&pane_left, 0, 0, leftw, scrh - 3);
	pane_resize(&pane_right, pane_left.ex + 1, 0, scrw, scrh - 3);
	pane_resize(&pane_bot, 0, scrh - 3, scrw, scrh);
}

void
tui_init(void)
{
	quit = 0;
	cmd_input_mode = IMODE_TRACK_SELECT;

	user_status = NULL;
	user_status_uptime = 0;

	inputln_init(&completion_query);
	completion_reset = 1;

	history_init(&track_play_history);
	history_init(&track_select_history);
	history_init(&track_vis_select_history);
	history_init(&tag_select_history);
	history_init(&command_history);
	history = &command_history;

	tui_curses_init();

	style_init();

	pane_init((tag_pane = &pane_left), tag_pane_input, tag_pane_vis);
	pane_init((track_pane = &pane_right), track_pane_input, track_pane_vis);
	pane_init((cmd_pane = &pane_bot), cmd_pane_input, cmd_pane_vis);

	pane_sel = &pane_left;
	pane_after_cmd = pane_sel;

	listnav_init(&tag_nav);
	listnav_init(&track_nav);

	track_show_playlist = 0;
	update_tracks_vis();

	tui_resize();
}

void
tui_deinit(void)
{
	free(user_status);

	inputln_deinit(&completion_query);

	pane_deinit(&pane_left);
	pane_deinit(&pane_right);
	pane_deinit(&pane_bot);

	history_deinit(&track_play_history);
	history_deinit(&track_select_history);
	history_deinit(&track_vis_select_history);
	history_deinit(&tag_select_history);
	history_deinit(&command_history);

	if (!isendwin()) endwin();
}

bool
tui_update(void)
{
	bool handled;
	wint_t c;
	int i;

	get_wch(&c);

	if (c == KEY_RESIZE) {
		tui_resize();
	} else if (c != ERR) {
		handled = 0;
		if (pane_sel && pane_sel->active)
			handled = pane_sel->handle(c);

		/* fallback if char not handled by pane */
		if (!handled) main_input(c);
	}

	playlist_update();
	update_tracks_vis();

	refresh();
	for (i = 0; i < ARRLEN(panes); i++) {
		/* only update ui for panes that are visible */
		if (!panes[i]->active) continue;

		panes[i]->update(panes[i], pane_sel == panes[i]);
		wnoutrefresh(panes[i]->win);
	}

	main_vis();
	doupdate();

	return !quit;
}
