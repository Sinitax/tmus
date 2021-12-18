#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include "util.h"
#include "list.h"
#include "history.h"
#include "tag.h"
#include "track.h"
#include "player.h"
#include "listnav.h"
#include "ref.h"

#include "mpd/player.h"
#include "curses.h"

#include <dirent.h>
#include <locale.h>
#include <signal.h>
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
	STYLE_COUNT
};

enum {
	IMODE_EXECUTE,
	IMODE_SEARCH
};

struct pane;

typedef int (*pane_handler)(wint_t c);
typedef void (*pane_updater)(struct pane *pane, int sel);
typedef wchar_t *(*completion_generator)(const wchar_t *text, int fwd, int state);
typedef int (*cmd_handler)(const char *args);

struct pane {
	WINDOW *win;
	int sx, sy, ex, ey;
	int w, h;
	int active;

	pane_handler handle;
	pane_updater update;
};

struct cmd {
	const wchar_t *name;
	cmd_handler func;
};

int style_attrs[STYLE_COUNT];

const char *datadir;
int scrw, scrh;
int quit;

struct pane *pane_sel, *pane_top_sel;
struct pane pane_left, pane_right, pane_bot;
struct pane *const panes[] = {
	&pane_left,
	&pane_right,
	&pane_bot
};

struct inputln completion_query = { 0 };
int completion_reset = 1;
completion_generator completion;

struct pane *cmd_pane;
struct history search_history, command_history;
struct history *history;
int cmd_show, cmd_mode;

const char player_state_chars[] = {
	[PLAYER_STATE_PAUSED] = '|',
	[PLAYER_STATE_PLAYING] = '>',
	[PLAYER_STATE_STOPPED] = '#'
};

struct pane *tag_pane;
struct listnav tag_nav;
struct link tags;
struct link tags_sel;

struct pane *track_pane;
struct listnav track_nav;
struct link playlist;
struct link tracks;

void init(void);
void cleanup(void);

void data_load(void);
void tracks_load(struct tag *tag);

void data_save(void);
void tracks_save(struct tag *tag);

void resize(void);
void pane_resize(struct pane *pane,
	int sx, int sy, int ex, int ey);

void pane_init(struct pane *p, pane_handler handle, pane_updater update);
void pane_title(struct pane *pane, const char *title, int highlight);

void style_init(int style, int fg, int bg, int attr);
void style_on(WINDOW *win, int style);
void style_off(WINDOW *win, int style);

wchar_t *command_name_generator(const wchar_t *text, int fwd, int state);
wchar_t *track_name_generator(const wchar_t *text, int fwd, int state);

int tag_input(wint_t c);
void tag_vis(struct pane *pane, int sel);

int track_input(wint_t c);
void track_vis(struct pane *pane, int sel);

int cmd_input(wint_t c);
void cmd_vis(struct pane *pane, int sel);

void main_input(wint_t c);
void main_vis(void);

int usercmd_save(const char *args);

const struct cmd cmds[] = {
	{ L"save", usercmd_save },
};

void
init(void)
{
	quit = 0;

	setlocale(LC_ALL, "");

	/* TODO handle as character intead of signal */
	signal(SIGINT, exit);
	atexit(cleanup);

	history = &command_history;
	history_init(&search_history);
	history_init(&command_history);

	datadir = getenv("TMUS_DATA");
	ASSERT(datadir != NULL);
	data_load();

	player_init();

	/* ncurses init */
	initscr();
	raw();
	noecho();
	halfdelay(1);
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	start_color();
	curs_set(0);
	ESCDELAY = 0;

	memset(style_attrs, 0, sizeof(style_attrs));
	style_init(STYLE_DEFAULT, COLOR_WHITE, COLOR_BLACK, 0);
	style_init(STYLE_TITLE, COLOR_WHITE, COLOR_BLUE, A_BOLD);
	style_init(STYLE_PANE_SEP, COLOR_BLUE, COLOR_BLACK, 0);
	style_init(STYLE_ITEM_SEL, COLOR_YELLOW, COLOR_BLACK, A_BOLD);
	style_init(STYLE_ITEM_HOVER, COLOR_WHITE, COLOR_BLUE, 0);
	style_init(STYLE_ITEM_HOVER_SEL, COLOR_YELLOW, COLOR_BLUE, A_BOLD);

	pane_init((tag_pane = &pane_left), tag_input, tag_vis);
	pane_init((track_pane = &pane_right), track_input, track_vis);
	pane_init((cmd_pane = &pane_bot), cmd_input, cmd_vis);

	pane_sel = &pane_left;
	pane_top_sel = pane_sel;

	playlist = LIST_HEAD;
	tags_sel = LIST_HEAD;

	listnav_init(&tag_nav);
	listnav_init(&track_nav);
}

void
cleanup(void)
{
	delwin(pane_left.win);
	delwin(pane_right.win);
	delwin(pane_bot.win);
	endwin();

	data_save();

	player_free();

	history_free(&search_history);
	history_free(&command_history);
}

void
data_load(void)
{
	struct dirent *ent;
	struct tag *tag;
	DIR *dir;

	tags = LIST_HEAD;

	dir = opendir(datadir);
	ASSERT(dir != NULL);
	while ((ent = readdir(dir))) {
		if (ent->d_type != DT_DIR)
			continue;
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		tag = malloc(sizeof(struct tag));
		ASSERT(tag != NULL);
		tag->fname = strdup(ent->d_name);
		ASSERT(tag->fname != NULL);
		tag->fpath = aprintf("%s/%s", datadir, ent->d_name);
		ASSERT(tag->fpath != NULL);
		tag->name = sanitized(tag->fname);
		ASSERT(tag->name != NULL);
		tag->link = LINK_EMPTY;
		list_push_back(&tags, LINK(tag));

		tracks_load(tag);
	}
	closedir(dir);
}

void
tracks_load(struct tag *tag)
{
	struct dirent *ent;
	struct track *track;
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
		ref = ref_init(tag);
		list_push_back(&track->tags, LINK(ref));
		list_push_back(&tracks, LINK(track));
	}
	closedir(dir);
}

void
data_save(void)
{
	
}

void
tracks_save(struct tag *tag)
{
	
}

void
resize(void)
{
	int i, leftw;

	getmaxyx(stdscr, scrh, scrw);

	if (scrw < 10 || scrh < 4) {
		clear();
		printw("Term too small..");
		refresh();
		usleep(10000);
	}

	leftw = MIN(40, 0.3f * scrw);
	pane_resize(&pane_left, 0, 0, leftw, scrh - 3);
	pane_resize(&pane_right, pane_left.ex + 1, 0, scrw, scrh - 3);
	pane_resize(&pane_bot, 0, scrh - 3, scrw, scrh);
}

void
pane_resize(struct pane *pane, int sx, int sy, int ex, int ey)
{
	pane->sx = sx;
	pane->sy = sy;
	pane->ex = ex;
	pane->ey = ey;
	pane->w = pane->ex - pane->sx;
	pane->h = pane->ey - pane->sy;

	pane->active = (pane->w > 0 && pane->h > 0);
	if (pane->active) {
		wresize(pane->win, pane->h, pane->w);
		mvwin(pane->win, pane->sy, pane->sx);
		redrawwin(pane->win);
	}
}

void
pane_init(struct pane *pane, pane_handler handle, pane_updater update)
{
	pane->win = newwin(1, 1, 0, 0);
	ASSERT(pane->win != NULL);
	pane->handle = handle;
	pane->update = update;
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
command_name_generator(const wchar_t *text, int fwd, int reset)
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
track_name_generator(const wchar_t *text, int fwd, int reset)
{
	static struct link *cur;
	struct track *track;

	if (reset) {
		cur = tracks.next;
	} else if (cur) {
		cur = fwd ? cur->next : cur->prev;
	}

	while (cur != &tracks && cur) {
		track = UPCAST(cur, struct track);
		if (wcsstr(track->name, text))
			return wcsdup(track->name);
		cur = fwd ? cur->next : cur->prev;
	}

	return NULL;
}

void
select_current_tag(void)
{
	struct link *link, *iter;
	struct track *track;
	struct tag *tag;
	struct ref *ref;

	link = link_iter(tags.next, tag_nav.sel);
	ASSERT(link != NULL);
	tag = UPCAST(link, struct tag);
	if (refs_incl(&tags_sel, tag)) {
		refs_rm(&tags_sel, tag);
	} else {
		ref = ref_init(tag);
		list_push_back(&tags_sel, LINK(ref));
	}
	refs_free(&playlist);
	for (link = tags_sel.next; link; link = link->next) {
		tag = UPCAST(link, struct ref)->data;
		for (iter = tracks.next; iter; iter = iter->next) {
			track = UPCAST(iter, struct track);
			if (refs_incl(&track->tags, tag) && !refs_incl(&playlist, track)) {
				ref = ref_init(track);
				list_push_back(&playlist, LINK(ref));
			}
		}
	}
}

int
tag_input(wint_t c)
{
	switch (c) {
	case KEY_UP:
		listnav_update_sel(&tag_nav, tag_nav.sel - 1);
		return 1;
	case KEY_DOWN:
		listnav_update_sel(&tag_nav, tag_nav.sel + 1);
		return 1;
	case KEY_SPACE:
		select_current_tag();
		return 1;
	case KEY_NPAGE:
		listnav_update_sel(&track_nav, track_nav.sel - track_nav.wlen / 2);
		return 1;
	case KEY_PPAGE:
		listnav_update_sel(&track_nav, track_nav.sel + track_nav.wlen / 2);
		return 1;
	}

	return 0;
}

void
tag_vis(struct pane *pane, int sel)
{
	struct tag *tag, *tag2;
	struct link *iter, *iter2;
	int index, tsel;

	werase(pane->win);
	pane_title(pane, "Tags", sel);

	listnav_update_bounds(&tag_nav, 0, list_len(&tags));
	listnav_update_wlen(&tag_nav, pane->h - 1);

	index = 0;
	for (iter = tags.next; iter; iter = iter->next) {
		tag = UPCAST(iter, struct tag);
		tsel = refs_incl(&tags_sel, tag);

		if (sel && index == tag_nav.sel && tsel)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == tag_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (tsel)
			style_on(pane->win, STYLE_ITEM_SEL);

		wmove(pane->win, 1 + index, 0);
		wprintw(pane->win, "%*.*s", pane->w, pane->w, tag->name);

		if (index == tag_nav.sel && tsel)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (index == tag_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (tsel)
			style_off(pane->win, STYLE_ITEM_SEL);
		index++;
	}
}

int
track_input(wint_t c)
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
		link = link_iter(tracks.next, track_nav.sel);
		ASSERT(link != NULL);
		track = UPCAST(link, struct track);
		player->track = track;
		player_play_track(track);
		return 1;
	case KEY_PPAGE:
		listnav_update_sel(&track_nav, track_nav.sel - track_nav.wlen / 2);
		return 1;
	case KEY_NPAGE:
		listnav_update_sel(&track_nav, track_nav.sel + track_nav.wlen / 2);
		return 1;
	}

	return 0;
}

void
track_vis(struct pane *pane, int sel)
{
	struct track *track;
	struct link *iter;
	int index;

	werase(pane->win);
	pane_title(pane, "Tracks", sel);

	listnav_update_bounds(&track_nav, 0, list_len(&playlist));
	listnav_update_wlen(&track_nav, pane->h - 1);

	wmove(pane->win, 0, 50);
	wprintw(pane->win, "%i", list_len(&playlist));

	index = 0;
	for (iter = playlist.next; iter; iter = iter->next, index++) {
		track = UPCAST(iter, struct ref)->data;

		if (index < track_nav.wmin) continue;
		if (index >= track_nav.wmax) break;

		if (sel && index == track_nav.sel && track == player->track)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (track == player->track)
			style_on(pane->win, STYLE_ITEM_SEL);

		wmove(pane->win, 1 + index - track_nav.wmin, 0);
		wprintw(pane->win, "%-*.*ls", pane->w, pane->w, track->name);

		if (sel && index == track_nav.sel && track == player->track)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_nav.sel)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (track == player->track)
			style_off(pane->win, STYLE_ITEM_SEL);
	}
}

void
run_cmd(const wchar_t *query)
{

}

void
play_track(const wchar_t *query)
{
	struct track *track;
	struct link *iter;

	for (iter = tracks.next; iter; iter = iter->next) {
		track = UPCAST(iter, struct track);
		if (wcsstr(track->name, history->cmd->buf)) {
			player_play_track(track);
			break;
		}
	}
}

int
cmd_input(wint_t c)
{
	wchar_t *res;

	if (cmd_mode == IMODE_EXECUTE) {
		history = &command_history;
		completion = command_name_generator;
	} else if (cmd_mode == IMODE_SEARCH) {
		history = &search_history;
		completion = track_name_generator;
	}

	switch (c) {
	case KEY_ESC:
		if (history->cmd == history->query) {
			pane_sel = pane_top_sel;
		} else {
			history->cmd = history->query;
		}
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

		if (completion_reset) {
			inputln_copy(&completion_query, history->query);
		}

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
	return 1;
}

void
cmd_vis(struct pane *pane, int sel)
{
	struct inputln *cmd;
	struct link *iter;
	int index, offset;
	char *line;

	werase(pane->win);

	wmove(pane->win, 0, 0);
	style_on(pane->win, STYLE_TITLE);
	wprintw(pane->win, " %-*.*ls\n", pane->w - 1, pane->w - 1,
			player->track ? player->track->name : L"");
	style_off(pane->win, STYLE_TITLE);

	if (player->loaded) {
		line = appendstrf(NULL, "%c ", player_state_chars[player->state]);
		line = appendstrf(line, "%s / ", timestr(player->time_pos));
		line = appendstrf(line, "%s", timestr(player->time_end));
		if (player->volume >= 0)
			line = appendstrf(line, " - vol: %u%%", player->volume);
		if (player->msg)
			line = appendstrf(line, " | [PLAYER] %s", player->msg);
		if (!list_empty(&player->queue))
			line = appendstrf(line, " | [QUEUE] %i tracks",
					list_len(&player->queue));

		wmove(pane->win, 1, 0);
		ATTR_ON(pane->win, A_REVERSE);
		wprintw(pane->win, "%-*.*s\n", pane->w, pane->w, line);
		ATTR_OFF(pane->win, A_REVERSE);

		free(line);
	} else {
		if (player->msg) {
			wmove(pane->win, 1, 0);
			line = aprintf("[PLAYER] %s", player->msg);
			wprintw(pane->win, "%-*.*s\n", pane->w, pane->w, line);
			free(line);
		}
	}

	if (sel || cmd_show) {
		cmd = history->cmd;
		if (cmd != history->query) {
			index = 0;
			for (iter = history->list.next; iter; iter = iter->next, index++)
				if (UPCAST(iter, struct inputln) == cmd)
					break;
			line = appendstrf(NULL, "[%i] ", iter ? index : -1);
		} else {
			line = appendstrf(NULL, "%c", cmd_mode == IMODE_SEARCH ? '/' : ':');
		}
		offset = strlen(line);
		line = appendstrf(line, "%ls", cmd->buf);
		wprintw(pane->win, "%-*.*s", pane->w, pane->w, line);
		free(line);
		if (sel) { /* cursor */
			ATTR_ON(pane->win, A_REVERSE);
			wmove(pane->win, 2, offset + cmd->cur);
			waddch(pane->win, cmd->cur < cmd->len ? cmd->buf[cmd->cur] : ' ');
			ATTR_OFF(pane->win, A_REVERSE);
		}
	}
}

void
queue_hover(void)
{
	struct link *link;

	link = link_iter(playlist.next, track_nav.sel);
	ASSERT(link != NULL);
	player_queue_append(UPCAST(link, struct ref)->data);
}

void
main_input(wint_t c)
{
	switch (c) {
	case KEY_TAB:
		if (pane_sel == &pane_left)
			pane_sel = &pane_right;
		else
			pane_sel = &pane_left;
		pane_top_sel = pane_sel;
		break;
	case KEY_ESC:
		pane_sel = pane_top_sel;
		break;
	case KEY_LEFT:
		if (player->track)
			player_seek(MAX(player->time_pos - 10, 0));
		break;
	case KEY_RIGHT:
		if (player->track) {
			if (player->time_end > player->time_pos + 10) {
				player_seek(player->time_pos + 10);
			} else {
				player_next();
			}
		}
		break;
	case L'y':
		queue_hover();
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
	case L'b':
		player_seek(0);
		break;
	case L's':
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

	style_on(stdscr, STYLE_TITLE);
	move(0, pane_left.ex);
	addch(' ');
	style_off(stdscr, STYLE_TITLE);

	style_on(stdscr, STYLE_PANE_SEP);
	for (i = pane_left.sy + 1; i < pane_left.ey; i++) {
		move(i, pane_left.ex);
		addch(ACS_VLINE);
	}
	style_off(stdscr, STYLE_PANE_SEP);
}

int
usercmd_save(const char *args)
{
	data_save();
	return 1;
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
			resize();
		} else if (c != ERR) {
			handled = 0;
			if (pane_sel && pane_sel->active)
				handled = pane_sel->handle(c);

			if (!handled) main_input(c);
		}

		refresh();
		for (i = 0; i < ARRLEN(panes); i++) {
			if (!panes[i]->active) continue;
			panes[i]->update(panes[i], pane_sel == panes[i]);
			wnoutrefresh(panes[i]->win);
		}
		main_vis();
		doupdate();

		get_wch(&c);
	} while (!quit);
}

