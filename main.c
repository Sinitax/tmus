#define _XOPEN_SOURCE 600

#include "util.h"
#include "link.h"
#include "history.h"

#include "curses.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>


#define ATTR_ON(win, attr) wattr_on((win), (attr), NULL)
#define ATTR_OFF(win, attr) wattr_off((win), (attr), NULL)
#define STYLE_ON(win, style) ATTR_ON((win), COLOR_PAIR(STYLE_ ## style))
#define STYLE_OFF(win, style) ATTR_OFF((win), COLOR_PAIR(STYLE_ ## style))

#define KEY_ESC '\x1b'
#define KEY_TAB '\t'
#define KEY_CTRL(c) ((c) & ~0x60)


enum {
	STYLE_DEFAULT,
	STYLE_TITLE,
	STYLE_PANE_SEP
};

enum {
	EXECUTE,
	SEARCH
};

struct pane;

typedef int (*pane_handler)(wint_t c);
typedef void (*pane_updater)(struct pane *pane, int sel);
typedef char *(*completion_generator)(const char *text, int state);
typedef int (*cmd_handler)(const char *args);

struct pane {
	WINDOW *win;
	int sx, sy, ex, ey;
	int w, h;
	int active;

	pane_handler handle;
	pane_updater update;
};

struct track {
	char *title;
	char *artist;
	float duration;
	struct link *tags;

	struct link link;
};

struct track_ref {
	struct track *track;

	struct link link;
};

struct tag {
	const char *name;

	struct link link;
};

struct cmd {
	const char *name;
	cmd_handler func;
};


int scrw, scrh;
int quit;

float win_ratio;
struct pane *pane_sel;
struct pane pane_left, pane_right, pane_bot;
struct pane *const panes[] = {
	&pane_left,
	&pane_right,
	&pane_bot
};

struct track *track_sel;
struct track *tracks;
struct link playlist;
int track_paused;

completion_generator completion;
struct history search_history, command_history;
struct history *history;
int cmd_show, cmd_mode;


void init(void);
void cleanup(void);

void resize(void);
void pane_resize(struct pane *pane,
	int sx, int sy, int ex, int ey);

void pane_init(struct pane *p, pane_handler handle, pane_updater update);
void pane_title(struct pane *pane, const char *title, int highlight);

char *command_name_generator(const char *text, int state);
char *track_name_generator(const char *text, int state);

int track_input(wint_t c);
void track_vis(struct pane *pane, int sel);

int tag_input(wint_t c);
void tag_vis(struct pane *pane, int sel);

int cmd_input(wint_t c);
void cmd_vis(struct pane *pane, int sel);

void main_input(wint_t c);
void main_vis(void);


void
init(void)
{
	quit = 0;
	win_ratio = 0.3f;

	initscr();
	cbreak();
	noecho();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	start_color();
	curs_set(0);
	ESCDELAY = 0;

	history = &command_history;
	history_init(&search_history);
	history_init(&command_history);

	init_pair(STYLE_TITLE, COLOR_WHITE, COLOR_BLUE);
	init_pair(STYLE_PANE_SEP, COLOR_BLUE, COLOR_BLACK);

	pane_init(&pane_left, tag_input, tag_vis);
	pane_init(&pane_right, track_input, track_vis);
	pane_init(&pane_bot, cmd_input, cmd_vis);

	pane_sel = &pane_left;

	atexit(cleanup);
}

void
cleanup(void)
{
	history_free(&search_history);
	history_free(&command_history);

	delwin(pane_left.win);
	delwin(pane_right.win);
	delwin(pane_bot.win);
	endwin();
}

void
resize(void)
{
	int i;

	getmaxyx(stdscr, scrh, scrw);

	if (scrw < 10 || scrh < 4) {
		clear();
		printw("Term too small..");
		refresh();
		usleep(10000);
	}

	pane_resize(&pane_left, 0, 0, win_ratio * scrw, scrh - 3);
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

	STYLE_ON(pane->win, TITLE);
	ATTR_ON(pane->win, A_BOLD);
	if (highlight) ATTR_ON(pane->win, A_STANDOUT);

	wprintw(pane->win, " %-*.*s", pane->w - 1, pane->w - 1, title);

	if (highlight) ATTR_OFF(pane->win, A_STANDOUT);
	ATTR_OFF(pane->win, A_BOLD);
	STYLE_OFF(pane->win, TITLE);
}

char *
command_name_generator(const char *text, int reset)
{
	return NULL;
}

char *
track_name_generator(const char *text, int reset)
{
	static int index, len;
	char *name;

	if (cmd_mode != SEARCH) return NULL;

	if (reset) {
		index = 0;
		len = strlen(text);
	}

	if (index++ == 0 && !strncmp("hello", text, len))
		return strdup("hello");

	// TODO: iter over playlist
	// while ((name = track_names[list_index++])) {
	// 	if (strncmp(name, text, len) == 0) {
	// 		return strdup(name);
	// 	}
	// }

	return NULL;
}

void
tag_init(void)
{
	/* TODO: load tags as directory names (excluding unsorted) */
}

int
tag_input(wint_t c)
{
	return 0;
}

void
tag_vis(struct pane *pane, int sel)
{
	werase(pane->win);
	pane_title(pane, "Tags", sel);
}

void
track_init(void)
{
	/* TODO: for each tag director load track names */
}

int
track_input(wint_t c)
{
	return 0;
}

void
track_vis(struct pane *pane, int sel)
{
	werase(pane->win);
	pane_title(pane, "Tracks", sel);
}

int
cmd_input(wint_t c)
{
	if (cmd_mode == EXECUTE) {
		history = &command_history;
		completion = command_name_generator;
	} else {
		history = &search_history;
		completion = track_name_generator;
	}

	switch (c) {
	case KEY_ESC:
		if (history->cmd == history->query)
			return 0;
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
		// TODO: show visually that no more matches
		history_next(history);
		break;
	case KEY_DOWN:
		history_prev(history);
		break;
	case '\n':
	case KEY_ENTER:
		history_submit(history);
		break;
	case KEY_TAB:
		break;
	case KEY_BACKSPACE:
		inputln_del(history->cmd, 1);
		break;
	default:
		if (!iswprint(c)) return 0;
		inputln_addch(history->cmd, c);
		break;
	}
	return 1;
}

void
cmd_vis(struct pane *pane, int sel)
{
	struct inputln *cmd;

	werase(pane->win);

	if (track_sel)
		pane_title(pane, track_sel->title, track_paused);
	else
		pane_title(pane, "", 0);

	if (sel || cmd_show) {
		wmove(pane->win, 2, 0);
		waddch(pane->win, cmd_mode == SEARCH ? '/' : ':');
		cmd = history->cmd;
		wprintw(pane->win, "%-*.*ls", pane->w - 1, pane->w - 1, cmd->buf);
		// TODO: if query != cmd, highlight query substr
		if (sel) {
			ATTR_ON(pane->win, A_REVERSE);
			wmove(pane->win, 2, 1 + cmd->cur);
			waddch(pane->win, cmd->cur < cmd->len ? cmd->buf[cmd->cur] : ' ');
			ATTR_OFF(pane->win, A_REVERSE);
		}
	}
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
		break;
	case KEY_ESC:
		pane_sel = NULL;
		break;
	case KEY_LEFT:
		pane_sel = &pane_left;
		break;
	case KEY_RIGHT:
		pane_sel = &pane_right;
		break;
	case ':':
		cmd_mode = EXECUTE;
		pane_sel = &pane_bot;
		break;
	case '/':
		cmd_mode = SEARCH;
		pane_sel = &pane_bot;
		break;
	}
}

void
main_vis(void)
{
	int i;

	STYLE_ON(stdscr, TITLE);
	move(0, pane_left.ex);
	addch(' ');
	STYLE_OFF(stdscr, TITLE);

	STYLE_ON(stdscr, PANE_SEP);
	for (i = pane_left.sy + 1; i < pane_left.ey; i++) {
		move(i, pane_left.ex);
		addch(ACS_VLINE);
	}
	STYLE_OFF(stdscr, PANE_SEP);
}

int
main(int argc, const char **argv)
{
	int i, handled;
	wint_t c;

	init();

	c = KEY_RESIZE;
	do {
		if (c == KEY_RESIZE) {
			resize();
		} else {
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

	cleanup();
}

