#define NCURSES_WIDECHAR 1

#include "tui.h"

#include "cmd.h"
#include "data.h"
#include "history.h"
#include "pane.h"
#include "player.h"
#include "list.h"
#include "listnav.h"
#include "ref.h"
#include "style.h"
#include "util.h"

#include <ncurses.h>

#include <unistd.h>

#include <wchar.h>
#include <wctype.h>

#undef KEY_ENTER
#define KEY_ENTER '\n'
#define KEY_SPACE ' '
#define KEY_ESC '\x1b'
#define KEY_TAB '\t'
#define KEY_CTRL(c) ((c) & ~0x60)

enum {
	IMODE_EXECUTE,
	IMODE_TRACK_SEARCH,
	IMODE_TAG_SEARCH,
	IMODE_COUNT
};

typedef wchar_t *(*completion_gen)(const wchar_t *text, int fwd, int state);

static void pane_title(struct pane *pane, const char *title, int highlight);

static wchar_t *command_name_gen(const wchar_t *text, int fwd, int state);
static wchar_t *track_name_gen(const wchar_t *text, int fwd, int state);
static wchar_t *tag_name_gen(const wchar_t *text, int fwd, int state);

static void toggle_current_tag(void);
static int tag_pane_input(wint_t c);
static void tag_pane_vis(struct pane *pane, int sel);

static int track_pane_input(wint_t c);
static void track_pane_vis(struct pane *pane, int sel);

static int play_track(const wchar_t *name);
static int select_tag(const wchar_t *name);

static int cmd_pane_input(wint_t c);
static void cmd_pane_vis(struct pane *pane, int sel);

static void queue_hover(void);
static void update_track_playlist(void);
static void main_input(wint_t c);
static void main_vis(void);

static void tui_curses_init(void);
static void tui_resize(void);

static int scrw, scrh;
static int quit;

static struct pane pane_left, pane_right, pane_bot;
static struct pane *const panes[] = {
	&pane_left,
	&pane_right,
	&pane_bot
};

static struct history command_history;
static struct history track_search_history;
static struct history tag_search_history;
static struct history *history;

static int cmd_input_mode;
static int cmd_show;

static struct inputln completion_query;
static int completion_reset;
static completion_gen completion;

struct pane *cmd_pane, *tag_pane, *track_pane;
struct pane *pane_sel, *pane_top_sel;

struct list *tracks_vis;
int track_show_playlist;
struct listnav tag_nav;
struct listnav track_nav;

const char imode_prefix[IMODE_COUNT] = {
	[IMODE_EXECUTE] = ':',
	[IMODE_TRACK_SEARCH] = '/',
	[IMODE_TAG_SEARCH] = '?',
};

static const char player_state_chars[] = {
	[PLAYER_STATE_PAUSED] = '|',
	[PLAYER_STATE_PLAYING] = '>',
	[PLAYER_STATE_STOPPED] = '#'
};

void
pane_title(struct pane *pane, const char *title, int highlight)
{
	wmove(pane->win, 0, 0);

	style_on(pane->win, STYLE_TITLE);
	if (highlight) ATTR_ON(pane->win, A_STANDOUT);

	wprintw(pane->win, " %-*.*s", pane->w - 1, pane->w - 1, title);

	if (highlight) ATTR_OFF(pane->win, A_STANDOUT);
	style_off(pane->win, STYLE_TITLE);
}

wchar_t *
command_name_gen(const wchar_t *text, int fwd, int reset)
{
	static int index, len;
	int dir;

	dir = fwd ? 1 : -1;

	if (reset) {
		index = 0;
		len = wcslen(text);
	} else if (index >= -1 && index <= command_count) {
		index += dir;
	}

	while (index >= 0 && index < command_count) {
		if (!wcsncmp(commands[index].name, text, len))
			return wcsdup(commands[index].name);
		index += dir;
	}

	return NULL;
}

wchar_t *
track_name_gen(const wchar_t *text, int fwd, int reset)
{
	static struct link *cur;
	struct link *iter;
	struct track *track;

	if (reset) {
		cur = tracks.head.next;
		iter = cur;
	} else {
		iter = fwd ? cur->next : cur->prev;
	}

	while (LIST_INNER(iter)) {
		track = UPCAST(iter, struct ref)->data;
		if (wcscasestr(track->name, text)) {
			cur = iter;
			return wcsdup(track->name);
		}
		iter = fwd ? iter->next : iter->prev;
	}

	return NULL;
}

wchar_t *
tag_name_gen(const wchar_t *text, int fwd, int reset)
{
	static struct link *cur;
	struct link *iter;
	struct tag *tag;

	if (reset) {
		cur = tags.head.next;
		iter = cur;
	} else {
		iter = fwd ? cur->next : cur->prev;
	}

	while (LIST_INNER(iter)) {
		tag = UPCAST(iter, struct tag);
		if (wcscasestr(tag->name, text)) {
			cur = iter;
			return wcsdup(tag->name);
		}
		iter = fwd ? iter->next : iter->prev;
	}

	return NULL;
}

void
toggle_current_tag(void)
{
	struct link *link, *iter;
	struct track *track;
	struct tag *tag;
	struct ref *ref;
	int in_tags, in_playlist;

	if (list_empty(&tags)) return;

	link = list_at(&tags, tag_nav.sel);
	ASSERT(link != NULL);
	tag = UPCAST(link, struct tag);

	/* toggle tag in tags_sel */
	if (refs_incl(&tags_sel, tag)) {
		refs_rm(&tags_sel, tag);
	} else {
		ref = ref_init(tag);
		list_push_back(&tags_sel, LINK(ref));
	}

	/* rebuild the full playlist */
	refs_free(&player->playlist);
	for (LIST_ITER(&tags_sel, link)) {
		tag = UPCAST(link, struct ref)->data;
		for (LIST_ITER(&tag->tracks, iter)) {
			ref = ref_init(UPCAST(iter, struct ref)->data);
			ASSERT(ref != NULL);
			list_push_back(&player->playlist, LINK(ref));
		}
	}
}

int
tag_pane_input(wint_t c)
{
	switch (c) {
	case KEY_UP:
		listnav_update_sel(&tag_nav, tag_nav.sel - 1);
		return 1;
	case KEY_DOWN:
		listnav_update_sel(&tag_nav, tag_nav.sel + 1);
		return 1;
	case KEY_SPACE:
		toggle_current_tag();
		return 1;
	case KEY_ENTER:
		refs_free(&tags_sel);
		toggle_current_tag();
		return 1;
	case KEY_PPAGE:
		listnav_update_sel(&tag_nav, tag_nav.sel - tag_nav.wlen / 2);
		return 1;
	case KEY_NPAGE:
		listnav_update_sel(&tag_nav, tag_nav.sel + tag_nav.wlen / 2);
		return 1;
	}

	return 0;
}

void
tag_pane_vis(struct pane *pane, int sel)
{
	struct tag *tag;
	struct link *iter;
	int index, tagsel;

	werase(pane->win);
	pane_title(pane, "Tags", sel);

	listnav_update_bounds(&tag_nav, 0, list_len(&tags));
	listnav_update_wlen(&tag_nav, pane->h - 1);

	index = -1;
	for (LIST_ITER(&tags, iter)) {
		tag = UPCAST(iter, struct tag);
		tagsel = refs_incl(&tags_sel, tag);

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

		wmove(pane->win, 1 + index - tag_nav.wmin, 0);
		wprintw(pane->win, "%-*.*ls", pane->w, pane->w, tag->name);

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

int
track_pane_input(wint_t c)
{
	struct link *link;
	struct track *track;

	switch (c) {
	case KEY_UP:
		listnav_update_sel(&track_nav, track_nav.sel - 1);
		return 1;
	case KEY_DOWN:
		listnav_update_sel(&track_nav, track_nav.sel + 1);
		return 1;
	case KEY_ENTER:
		link = list_at(tracks_vis, track_nav.sel);
		if (!link) return 1;
		track = UPCAST(link, struct ref)->data;
		player_play_track(track);
		return 1;
	case KEY_PPAGE:
		listnav_update_sel(&track_nav,
			track_nav.sel - track_nav.wlen / 2);
		return 1;
	case KEY_NPAGE:
		listnav_update_sel(&track_nav,
			track_nav.sel + track_nav.wlen / 2);
		return 1;
	}

	return 0;
}

void
track_pane_vis(struct pane *pane, int sel)
{
	struct track *track;
	struct link *iter;
	struct tag *tag;
	int index;

	werase(pane->win);
	pane_title(pane, "Tracks", sel);

	listnav_update_bounds(&track_nav, 0, list_len(tracks_vis));
	listnav_update_wlen(&track_nav, pane->h - 1);

	index = -1;
	for (LIST_ITER(tracks_vis, iter)) {
		track = UPCAST(iter, struct ref)->data;

		index += 1;
		if (index < track_nav.wmin) continue;
		if (index >= track_nav.wmax) break;

		if (sel && index == track_nav.sel && track == player->track)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (track == player->track)
			style_on(pane->win, STYLE_ITEM_SEL);
		else if (index == track_nav.sel)
			style_on(pane->win, STYLE_PREV);

		wmove(pane->win, 1 + index - track_nav.wmin, 0);
		wprintw(pane->win, "%-*.*ls", pane->w, pane->w, track->name);

		if (sel && index == track_nav.sel && track == player->track)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (track == player->track)
			style_off(pane->win, STYLE_ITEM_SEL);
		else if (index == track_nav.sel)
			style_off(pane->win, STYLE_PREV);
	}
}

int
run_cmd(const wchar_t *query)
{
	const wchar_t *sep;
	int i, cmdlen;
	bool success;

	sep = wcschr(query, L' ');
	cmdlen = sep ? sep - query : wcslen(query);
	for (i = 0; i < command_count; i++) {
		if (!wcsncmp(commands[i].name, query, cmdlen)) {
			success = commands[i].func(sep ? sep + 1 : NULL);
			if (!success && !cmd_status) {
				free(cmd_status);
				cmd_status = wcsdup(L"Command Failed!\n");
				ASSERT(cmd_status != NULL);
			}
			return 1;
		}
	}

	return 0;
}

int
play_track(const wchar_t *query)
{
	struct track *track;
	struct link *iter;

	for (LIST_ITER(&tracks, iter)) {
		track = UPCAST(iter, struct ref)->data;
		if (!wcscmp(track->name, query)) {
			player_play_track(track);
			return 1;
		}
	}

	return 0;
}

int
select_tag(const wchar_t *query)
{
	struct tag *tag;
	struct link *iter;
	int index;

	index = -1;
	for (LIST_ITER(&tags, iter)) {
		index += 1;
		tag = UPCAST(iter, struct tag);
		if (wcscasestr(tag->name, query)) {
			listnav_update_sel(&tag_nav, index);
			return 1;
		}
	}

	return 0;
}

int
cmd_pane_input(wint_t c)
{
	wchar_t *res;
	int match;

	switch (c) {
	case KEY_ESC:
		match = wcscmp(completion_query.buf, history->input->buf);
		if (!completion_reset && match)
			inputln_copy(history->input, &completion_query);
		else if (history->sel == history->input)
			pane_sel = pane_top_sel;
		else
			history->sel = history->input;
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
			pane_sel = pane_top_sel;
			break;
		}

		if (cmd_input_mode == IMODE_EXECUTE) {
			run_cmd(history->sel->buf);
		} else if (cmd_input_mode == IMODE_TRACK_SEARCH) {
			play_track(history->sel->buf);
		} else if (cmd_input_mode == IMODE_TAG_SEARCH) {
			select_tag(history->sel->buf);
		}

		history_submit(history);
		pane_sel = pane_top_sel;
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
			pane_sel = pane_top_sel;
			break;
		}
		inputln_del(history->sel, 1);
		completion_reset = 1;
		break;
	default:
		if (!iswprint(c)) return 0;
		inputln_addch(history->sel, c);
		completion_reset = 1;
		break;
	}

	return 1; /* grab everything */
}

void
cmd_pane_vis(struct pane *pane, int sel)
{
	static wchar_t *linebuf = NULL;
	static int linecap = 0;
	struct inputln *cmd;
	struct link *iter;
	wchar_t *line, *end;
	int index, offset;

	werase(pane->win);

	/* static line buffer for perf */
	if (pane->w > linecap) {
		linecap = MAX(linecap, pane->w);
		linebuf = realloc(linebuf, linecap * sizeof(wchar_t));
	}
	end = linebuf + linecap;

	/* track name */
	style_on(pane->win, STYLE_TITLE);
	pane_clearln(pane, 0);
	if (player->track) {
		swprintf(linebuf, linecap, L"%ls", player->track->name);
		mvwaddwstr(pane->win, 0, 1, linebuf);
	}
	style_off(pane->win, STYLE_TITLE);

	if (player->loaded) {
		/* status line */
		line = linebuf;
		line += swprintf(line, end - line, L"%c ",
			player_state_chars[player->state]);
		line += swprintf(line, end - line, L"%s / ",
			timestr(player->time_pos));
		line += swprintf(line, end - line, L"%s",
			timestr(player->time_end));

		if (player->volume >= 0) {
			line += swprintf(line, end - line, L" - vol: %u%%",
				player->volume);
		}

		if (player->msg) {
			line += swprintf(line, end - line, L" | [PLAYER] %s",
				player->msg);
		}

		if (!list_empty(&player->queue)) {
			line += swprintf(line, end - line,
				L" | [QUEUE] %i tracks",
				list_len(&player->queue));
		}

		ATTR_ON(pane->win, A_REVERSE);
		pane_clearln(pane, 1);
		mvwaddwstr(pane->win, 1, 0, linebuf);
		ATTR_OFF(pane->win, A_REVERSE);
	} else if (player->msg) {
		/* player message */
		line = linebuf;
		line += swprintf(line, linecap, L"[PLAYER] %s", player->msg);
		line += swprintf(line, end - line, L"%*.*s",
				pane->w, pane->w, L" ");

		pane_clearln(pane, 1);
		mvwaddwstr(pane->win, 1, 0, linebuf);
	}

	/* status bits on right of status line */
	if (player->loaded) ATTR_ON(pane->win, A_REVERSE);
	mvwaddstr(pane->win, 1, pane->w - 5, "[   ]");
	if (track_show_playlist) mvwaddstr(pane->win, 1, pane->w - 4, "P");
	if (player->autoplay) mvwaddstr(pane->win, 1, pane->w - 3, "A");
	if (player->shuffle) mvwaddstr(pane->win, 1, pane->w - 2, "S");
	if (player->loaded) ATTR_OFF(pane->win, A_REVERSE);

	if (sel || cmd_show) {
		/* cmd and search input */
		line = linebuf;

		free(cmd_status);
		cmd_status = NULL;

		cmd = history->sel;
		if (cmd != history->input) {
			index = 0;
			for (LIST_ITER(&history->list, iter)) {
				if (UPCAST(iter, struct inputln) == cmd)
					break;
				index += 1;
			}
			line += swprintf(line, end - line, L"[%i] ",
				iter ? index : -1);
		} else {
			line += swprintf(line, end - line, L"%c",
				imode_prefix[cmd_input_mode]);
		}
		offset = wcslen(linebuf);

		line += swprintf(line, end - line, L"%ls", cmd->buf);

		pane_clearln(pane, 2);
		mvwaddwstr(pane->win, 2, 0, linebuf);

		if (sel) { /* show cursor in text */
			ATTR_ON(pane->win, A_REVERSE);
			wmove(pane->win, 2, offset + cmd->cur);
			waddch(pane->win, cmd->cur < cmd->len
				? cmd->buf[cmd->cur] : L' ');
			ATTR_OFF(pane->win, A_REVERSE);
		}
	} else if (cmd_status) {
		pane_clearln(pane, 2);
		style_on(pane->win, STYLE_ERROR);
		mvwaddwstr(pane->win, 2, 1, cmd_status);
		style_off(pane->win, STYLE_ERROR);
	}
}

void
queue_hover(void)
{
	struct link *link;

	link = list_at(&player->playlist, track_nav.sel);
	if (!link) return;
	player_queue_append(UPCAST(link, struct ref)->data);
}

void
update_track_playlist(void)
{
	struct link *link;
	struct tag *tag;

	if (track_show_playlist) {
		tracks_vis = &player->playlist;
	} else {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return;
		tag = UPCAST(link, struct tag);
		tracks_vis = &tag->tracks;
	}
}

void
main_input(wint_t c)
{
	switch (c) {
	case KEY_CTRL('r'):
		redrawwin(stdscr);
		break;
	case KEY_TAB:
		pane_sel = pane_sel == &pane_left
			? &pane_right : &pane_left;
		pane_top_sel = pane_sel;
		break;
	case KEY_ESC:
		pane_sel = pane_top_sel;
		break;
	case KEY_LEFT:
		if (!player->loaded) break;
		player_seek(MAX(player->time_pos - 10, 0));
		break;
	case KEY_RIGHT:
		if (!player->loaded) break;
		player_seek(MIN(player->time_pos + 10, player->time_end));
		break;
	case L'y':
		queue_hover();
		break;
	case L'o':
		player_queue_clear();
		break;
	case L'c':
		player_toggle_pause();
		break;
	case L'n':
	case L'>':
		player_next();
		break;
	case L'p':
	case L'<':
		player_prev();
		break;
	case L'P':
		if (track_show_playlist) {
			pane_sel = tag_pane;
			track_show_playlist = 0;
		} else {
			pane_sel = track_pane;
			track_show_playlist = 1;
		}
		break;
	case L'A':
		player->autoplay ^= 1;
		break;
	case L'S':
		player->shuffle ^= 1;
		break;
	case L'b':
		player_seek(0);
		break;
	case L'x':
		if (player->state == PLAYER_STATE_PLAYING) {
			player_stop();
		} else {
			player_play();
		}
		break;
	case L':':
		cmd_input_mode = IMODE_EXECUTE;
		pane_sel = &pane_bot;
		completion_reset = 1;
		history = &command_history;
		completion = command_name_gen;
		break;
	case L'/':
		cmd_input_mode = IMODE_TRACK_SEARCH;
		pane_sel = &pane_bot;
		completion_reset = 1;
		history = &track_search_history;
		completion = track_name_gen;
		break;
	case L'?':
		cmd_input_mode = IMODE_TAG_SEARCH;
		pane_sel = &pane_bot;
		completion_reset = 1;
		history = &tag_search_history;
		completion = tag_name_gen;
		break;
	case L'+':
		player_set_volume(MIN(100, player->volume + 5));
		break;
	case L'-':
		player_set_volume(MAX(0, player->volume - 5));
		break;
	case L'q':
		quit = 1;
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
	struct link *iter;
	struct tag *tag;
	int i, leftw;

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
	for (LIST_ITER(&tags, iter)) {
		tag = UPCAST(iter, struct tag);
		leftw = MAX(leftw, wcslen(tag->name));
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
	cmd_input_mode = IMODE_TRACK_SEARCH;

	inputln_init(&completion_query);
	completion_reset = 1;

	history_init(&track_search_history);
	history_init(&tag_search_history);
	history_init(&command_history);
	history = &command_history;

	tui_curses_init();

	style_init();

	pane_init((tag_pane = &pane_left), tag_pane_input, tag_pane_vis);
	pane_init((track_pane = &pane_right), track_pane_input, track_pane_vis);
	pane_init((cmd_pane = &pane_bot), cmd_pane_input, cmd_pane_vis);

	pane_sel = &pane_left;
	pane_top_sel = pane_sel;

	listnav_init(&tag_nav);
	listnav_init(&track_nav);

	track_show_playlist = 0;
	update_track_playlist();

	tui_resize();
}

void
tui_deinit(void)
{
	pane_free(&pane_left);
	pane_free(&pane_right);
	pane_free(&pane_bot);

	history_free(&track_search_history);
	history_free(&tag_search_history);
	history_free(&command_history);

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

	update_track_playlist();

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

void
tui_restore(void)
{
	if (!isendwin())
		endwin();
}


