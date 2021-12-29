#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include "util.h"
#include "list.h"
#include "history.h"
#include "tag.h"
#include "pane.h"
#include "player.h"
#include "listnav.h"
#include "ref.h"

#include "mpd/player.h"
#include "curses.h"

#include <dirent.h>
#include <locale.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#undef KEY_ENTER
#define KEY_ENTER '\n'
#define KEY_SPACE ' '
#define KEY_ESC '\x1b'
#define KEY_TAB '\t'
#define KEY_CTRL(c) ((c) & ~0x60)

#define ATTR_ON(win, attr) wattr_on(win, attr, NULL)
#define ATTR_OFF(win, attr) wattr_off(win, attr, NULL)

enum {
	STYLE_DEFAULT,
	STYLE_TITLE,
	STYLE_PANE_SEP,
	STYLE_ITEM_SEL,
	STYLE_ITEM_HOVER,
	STYLE_ITEM_HOVER_SEL,
	STYLE_PREV,
	STYLE_COUNT
};

enum {
	IMODE_EXECUTE,
	IMODE_SEARCH
};

typedef wchar_t *(*completion_gen)(const wchar_t *text, int fwd, int state);
typedef int (*cmd_handler)(const wchar_t *args);

struct cmd {
	const wchar_t *name;
	cmd_handler func;
};

static const char player_state_chars[] = {
	[PLAYER_STATE_PAUSED] = '|',
	[PLAYER_STATE_PLAYING] = '>',
	[PLAYER_STATE_STOPPED] = '#'
};

static int style_attrs[STYLE_COUNT];

static const char *datadir;
static int scrw, scrh;
static int quit;

static struct pane *pane_sel, *pane_top_sel;
static struct pane pane_left, pane_right, pane_bot;
static struct pane *const panes[] = {
	&pane_left,
	&pane_right,
	&pane_bot
};

/* 'tracks' holds all *unique* tracks */
static struct link tracks;
static struct link tags;
static struct link tags_sel;

/* bottom 'cmd' pane for search / exec */
static struct pane *cmd_pane;
static struct history search_history, command_history;
static struct history *history;
static int cmd_show, cmd_mode;
static struct inputln completion_query = { 0 };
static int completion_reset = 1;
static completion_gen completion;

/* left pane for tags */
static struct pane *tag_pane;
static struct listnav tag_nav;

/* right pane for tracks */
static struct pane *track_pane;
static struct listnav track_nav;

static void init(void);
static void cleanup(int code, void *arg);

static void tui_init(void);
static void tui_resize(void);
static void tui_end(void);

static void ncurses_init(void);

static void data_load(void);
static void data_save(void);
static void data_free(void);

static void tracks_load(struct tag *tag);
static void tracks_save(struct tag *tag);

static void pane_title(struct pane *pane, const char *title, int highlight);

static void style_init(int style, int fg, int bg, int attr);
static void style_on(WINDOW *win, int style);
static void style_off(WINDOW *win, int style);

static wchar_t *command_name_gen(const wchar_t *text, int fwd, int state);
static wchar_t *track_name_gen(const wchar_t *text, int fwd, int state);

static void toggle_current_tag(void);
static int tag_pane_input(wint_t c);
static void tag_pane_vis(struct pane *pane, int sel);

static int track_pane_input(wint_t c);
static void track_pane_vis(struct pane *pane, int sel);

static int cmd_pane_input(wint_t c);
static void cmd_pane_vis(struct pane *pane, int sel);

static void queue_hover(void);
static void main_input(wint_t c);
static void main_vis(void);

static int usercmd_save(const wchar_t *args);

const struct cmd cmds[] = {
	{ L"save", usercmd_save },
};

void
init(void)
{
	setlocale(LC_ALL, "");
	quit = 0;

	signal(SIGINT, exit);
	on_exit(cleanup, NULL);

	history_init(&search_history);
	history_init(&command_history);
	history = &command_history;

	data_load();

	player_init();

	tui_init();

	listnav_init(&tag_nav);
	listnav_init(&track_nav);
}

void
cleanup(int code, void* arg)
{
	tui_end();

	if (code == EXIT_SUCCESS)
		data_save();

	player_free();

	history_free(&search_history);
	history_free(&command_history);
}

void
tui_init(void)
{
	ncurses_init();

	memset(style_attrs, 0, sizeof(style_attrs));
	style_init(STYLE_DEFAULT, COLOR_WHITE, COLOR_BLACK, 0);
	style_init(STYLE_TITLE, COLOR_WHITE, COLOR_BLUE, A_BOLD);
	style_init(STYLE_PANE_SEP, COLOR_BLUE, COLOR_BLACK, 0);
	style_init(STYLE_ITEM_SEL, COLOR_YELLOW, COLOR_BLACK, A_BOLD);
	style_init(STYLE_ITEM_HOVER, COLOR_WHITE, COLOR_BLUE, 0);
	style_init(STYLE_ITEM_HOVER_SEL, COLOR_YELLOW, COLOR_BLUE, A_BOLD);
	style_init(STYLE_PREV, COLOR_WHITE, COLOR_BLACK, A_DIM);

	pane_init((tag_pane = &pane_left), tag_pane_input, tag_pane_vis);
	pane_init((track_pane = &pane_right), track_pane_input, track_pane_vis);
	pane_init((cmd_pane = &pane_bot), cmd_pane_input, cmd_pane_vis);

	pane_sel = &pane_left;
	pane_top_sel = pane_sel;
}

void
tui_resize(void)
{
	struct link *iter;
	int i, leftw;

	getmaxyx(stdscr, scrh, scrw);

	/* guarantee a minimum terminal size */
	if (scrw < 10 || scrh < 4) {
		clear();
		printw("...");
		refresh();
		usleep(10000);
	}

	/* adjust tag pane width to name lengths */
	leftw = 0;
	for (iter = tags.next; iter; iter = iter->next)
		leftw = MAX(leftw, strlen(UPCAST(iter, struct tag)->name));
	leftw = MAX(leftw + 1, 0.2f * scrw);

	pane_resize(&pane_left, 0, 0, leftw, scrh - 3);
	pane_resize(&pane_right, pane_left.ex + 1, 0, scrw, scrh - 3);
	pane_resize(&pane_bot, 0, scrh - 3, scrw, scrh);
}

void
tui_end(void)
{
	pane_free(&pane_left);
	pane_free(&pane_right);
	pane_free(&pane_bot);

	if (!isendwin()) endwin();
}

void
ncurses_init(void)
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
data_load(void)
{
	struct dirent *ent;
	struct tag *tag;
	DIR *dir;

	tracks = LIST_HEAD;
	tags = LIST_HEAD;
	tags_sel = LIST_HEAD;

	datadir = getenv("TMUS_DATA");
	ASSERT(datadir != NULL);

	dir = opendir(datadir);
	ASSERT(dir != NULL);
	while ((ent = readdir(dir))) {
		if (ent->d_type != DT_DIR)
			continue;
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		tag = tag_init(datadir, ent->d_name);
		list_push_back(&tags, LINK(tag));
		tracks_load(tag);
	}
	closedir(dir);
}

void
data_save(void)
{
	
}

void
data_free(void)
{
	/* TODO free track info */
}

void
tracks_load(struct tag *tag)
{
	struct track *track, *track_iter;
	struct dirent *ent;
	struct link *iter;
	struct ref *ref;
	DIR *dir;

	dir = opendir(tag->fpath);
	ASSERT(dir != NULL);
	while ((ent = readdir(dir))) {
		if (ent->d_type != DT_REG)
			continue;
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		track = track_init(tag->fpath, ent->d_name);

		track->tags = LIST_HEAD;
		ref = ref_init(tag);
		ASSERT(ref != NULL);
		list_push_back(&track->tags, LINK(ref));

		ref = ref_init(track);
		ASSERT(ref != NULL);
		list_push_back(&tag->tracks, LINK(ref));

		for (iter = tracks.next; iter; iter = iter->next) {
			track_iter = UPCAST(iter, struct ref)->data;
			if (track->fid == track_iter->fid)
				break;
		}

		if (!iter) {
			ref = ref_init(track);
			ASSERT(ref != NULL);
			list_push_back(&tracks, LINK(ref));
		}

	}
	closedir(dir);
}

void
tracks_save(struct tag *tag)
{
	
}

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

void
style_init(int style, int fg, int bg, int attr)
{
	style_attrs[style] = attr;
	init_pair(style, fg, bg);
}

void
style_on(WINDOW *win, int style)
{
	ATTR_ON(win, COLOR_PAIR(style) | style_attrs[style]);
}

void
style_off(WINDOW *win, int style)
{
	ATTR_OFF(win, COLOR_PAIR(style) | style_attrs[style]);
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
	} else if (index >= -1 && index <= ARRLEN(cmds)) {
		index += dir;
	}

	while (index >= 0 && index < ARRLEN(cmds)) {
		if (!wcsncmp(cmds[index].name, text, len))
			return wcsdup(cmds[index].name);
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
		cur = tracks.next;
		iter = cur;
	} else {
		iter = fwd ? cur->next : cur->prev;
	}

	while (iter && iter != &tracks) {
		track = UPCAST(iter, struct ref)->data;
		if (wcsstr(track->name, text)) {
			cur = iter;
			return wcsdup(track->name);
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

	link = link_iter(tags.next, tag_nav.sel);
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
	for (link = tags_sel.next; link; link = link->next) {
		tag = UPCAST(link, struct ref)->data;
		for (iter = tag->tracks.next; iter; iter = iter->next) {
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
	int index, tsel;

	werase(pane->win);
	pane_title(pane, "Tags", sel);

	listnav_update_bounds(&tag_nav, 0, list_len(&tags));
	listnav_update_wlen(&tag_nav, pane->h - 1);

	index = 0;
	for (iter = tags.next; iter; iter = iter->next, index++) {
		tag = UPCAST(iter, struct tag);
		tsel = refs_incl(&tags_sel, tag);

		if (index < tag_nav.wmin) continue;
		if (index >= tag_nav.wmax) break;

		if (sel && index == tag_nav.sel && tsel)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == tag_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (index == tag_nav.sel)
			style_on(pane->win, STYLE_PREV);
		else if (tsel)
			style_on(pane->win, STYLE_ITEM_SEL);

		wmove(pane->win, 1 + index - tag_nav.wmin, 0);
		wprintw(pane->win, "%*.*s", pane->w, pane->w, tag->name);

		if (sel && index == tag_nav.sel && tsel)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == tag_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (index == tag_nav.sel)
			style_off(pane->win, STYLE_PREV);
		else if (tsel)
			style_off(pane->win, STYLE_ITEM_SEL);
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
		link = link_iter(player->playlist.next, track_nav.sel);
		ASSERT(link != NULL);
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
	int index;

	werase(pane->win);
	pane_title(pane, "Tracks", sel);

	listnav_update_bounds(&track_nav, 0, list_len(&player->playlist));
	listnav_update_wlen(&track_nav, pane->h - 1);

	index = 0;
	for (iter = player->playlist.next; iter; iter = iter->next, index++) {
		track = UPCAST(iter, struct ref)->data;

		if (index < track_nav.wmin) continue;
		if (index >= track_nav.wmax) break;

		if (sel && index == track_nav.sel && track == player->track)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (index == track_nav.sel)
			style_on(pane->win, STYLE_PREV);
		else if (track == player->track)
			style_on(pane->win, STYLE_ITEM_SEL);

		wmove(pane->win, 1 + index - track_nav.wmin, 0);
		wprintw(pane->win, "%-*.*ls", pane->w, pane->w, track->name);

		if (sel && index == track_nav.sel && track == player->track)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (index == track_nav.sel)
			style_off(pane->win, STYLE_PREV);
		else if (track == player->track)
			style_off(pane->win, STYLE_ITEM_SEL);
	}
}

int
run_cmd(const wchar_t *query)
{
	const wchar_t *sep;
	int i, cmdlen;

	sep = wcschr(query, L' ');
	cmdlen = sep ? sep - query : wcslen(query);
	for (i = 0; i < ARRLEN(cmds); i++) {
		if (!wcsncmp(cmds[i].name, query, cmdlen)) {
			cmds[i].func(sep ? sep + 1 : NULL);
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

	for (iter = tracks.next; iter; iter = iter->next) {
		track = UPCAST(iter, struct ref)->data;
		if (wcsstr(track->name, query)) {
			player_play_track(track);
			return 1;
		}
	}

	return 0;
}

int
cmd_pane_input(wint_t c)
{
	wchar_t *res;

	if (cmd_mode == IMODE_EXECUTE) {
		history = &command_history;
		completion = command_name_gen;
	} else if (cmd_mode == IMODE_SEARCH) {
		history = &search_history;
		completion = track_name_gen;
	}

	switch (c) {
	case KEY_ESC:
		if (history->cmd == history->query)
			pane_sel = pane_top_sel;
		else
			history->cmd = history->query;
		break;
	case KEY_LEFT:
		inputln_left(history->cmd);
		break;
	case KEY_RIGHT:
		inputln_right(history->cmd);
		break;
	case KEY_CTRL('w'):
		inputln_del(history->cmd, history->cmd->cur);
		break;
	case KEY_UP:
		history_next(history);
		break;
	case KEY_DOWN:
		history_prev(history);
		break;
	case KEY_ENTER:
		if (!*history->cmd->buf) {
			pane_sel = pane_top_sel;
			break;
		}

		if (cmd_mode == IMODE_EXECUTE) {
			run_cmd(history->cmd->buf);
		} else if (cmd_mode == IMODE_SEARCH) {
			play_track(history->cmd->buf);
		}

		history_submit(history);
		pane_sel = pane_top_sel;
		break;
	case KEY_TAB:
	case KEY_BTAB:
		if (history->cmd != history->query) {
			inputln_copy(history->query, history->cmd);
			history->cmd = history->query;
		}

		if (completion_reset)
			inputln_copy(&completion_query, history->query);

		res = completion(completion_query.buf,
			c == KEY_TAB, completion_reset);
		if (res) inputln_replace(history->query, res);
		free(res);

		completion_reset = 0;
		break;
	case KEY_BACKSPACE:
		if (history->cmd->cur == 0) {
			pane_sel = pane_top_sel;
			break;
		}
		inputln_del(history->cmd, 1);
		completion_reset = 1;
		break;
	default:
		if (!iswprint(c)) return 0;
		inputln_addch(history->cmd, c);
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

	/* TODO add clear_line func to fill with spaces instead of
	 * filling buffer */

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

		if (player->autoplay) {
			line += swprintf(line, end - line, L" | AUTOPLAY");
		}

		if (player->shuffle) {
			line += swprintf(line, end - line, L" | SHUFFLE");
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

	if (sel || cmd_show) {
		/* cmd and search input */
		line = linebuf;

		cmd = history->cmd;
		if (cmd != history->query) {
			index = 0;
			iter = history->list.next;
			for (; iter; iter = iter->next, index++)
				if (UPCAST(iter, struct inputln) == cmd)
					break;
			line += swprintf(line, end - line, L"[%i] ",
				iter ? index : -1);
		} else {
			line += swprintf(line, end - line, L"%c",
				cmd_mode == IMODE_SEARCH ? '/' : ':');
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
	}
}

void
queue_hover(void)
{
	struct link *link;

	link = link_iter(player->playlist.next, track_nav.sel);
	ASSERT(link != NULL);
	player_queue_append(UPCAST(link, struct ref)->data);
}

void
main_input(wint_t c)
{
	switch (c) {
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
	case L'w':
		player->autoplay ^= 1;
		break;
	case L'g':
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
		cmd_mode = IMODE_EXECUTE;
		pane_sel = &pane_bot;
		completion_reset = 1;
		break;
	case L'/':
		cmd_mode = IMODE_SEARCH;
		pane_sel = &pane_bot;
		completion_reset = 1;
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

int
usercmd_save(const wchar_t *args)
{
	data_save();
	return 0;
}

int
main(int argc, const char **argv)
{
	int i, handled;
	wint_t c;

	init();

	c = KEY_RESIZE;
	do {
		player_update();

		if (c == KEY_RESIZE) {
			tui_resize();
		} else if (c != ERR) {
			handled = 0;
			if (pane_sel && pane_sel->active)
				handled = pane_sel->handle(c);

			/* fallback if char not handled by pane */
			if (!handled) main_input(c);
		}

		refresh();
		for (i = 0; i < ARRLEN(panes); i++) {
			/* only update ui for panes that are visible */
			if (!panes[i]->active) continue;

			panes[i]->update(panes[i], pane_sel == panes[i]);
			wnoutrefresh(panes[i]->win);
		}

		main_vis();
		doupdate();

		get_wch(&c);
	} while (!quit);
}

