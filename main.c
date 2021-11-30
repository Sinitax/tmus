#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

#include <unistd.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include "ncurses.h"
#include "readline/readline.h"
#include "readline/history.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define ARRLEN(x) (sizeof(x)/sizeof((x)[0]))

#define OFFSET(parent, attr) ((size_t) &((type *)0)->attr)
#define UPCAST(ptr, type) ({ \
	const typeof( ((type *)0)->link ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - OFFSET(type, link) ); })

#define ATTR_ON(win, attr) wattr_on((win), (attr), NULL)
#define ATTR_OFF(win, attr) wattr_off((win), (attr), NULL)
#define STYLE_ON(win, style) ATTR_ON((win), COLOR_PAIR(STYLE_ ## style))
#define STYLE_OFF(win, style) ATTR_OFF((win), COLOR_PAIR(STYLE_ ## style))

#define KEY_ESC '\x1b'
#define KEY_TAB '\t'

struct pane;

typedef int (*pane_handler)(int c);
typedef void (*pane_updater)(struct pane *pane, int sel);

struct link {
	struct link *prev, *next;
};

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

enum {
	STYLE_DEFAULT,
	STYLE_TITLE,
	STYLE_PANE_SEP
};

enum {
	EXECUTE,
	SEARCH
};

int scrw, scrh;
int quit;

struct pane pane_left, pane_right, pane_bot;
struct pane *pane_sel;
float win_ratio;

struct track *track_sel;
struct track *tracks;
struct track_ref *playlist;
int track_paused;

int cmdshow, cmdcur;
int cmdmode;

int rl_input, rl_input_avail;

struct pane *const panes[] = {
	&pane_left,
	&pane_right,
	&pane_bot
};

void init(void);
void cleanup(void);

void cancel(int sig);

void resize(void);
void pane_resize(struct pane *pane,
	int sx, int sy, int ex, int ey);

void pane_init(struct pane *p, pane_handler handle, pane_updater update);
void pane_title(struct pane *pane, const char *title, int highlight);

char **track_name_completion(const char *text, int start, int end);
char *track_name_generator(const char *text, int state);

int readline_getc(FILE *f);
int readline_input_avail(void);
void readline_consume(int c);
int exit_cmdmode(int a, int b);

int strnwidth(const char *str, int len);

void evalcmd(char *cmd);

int track_input(int c);
void track_vis(struct pane *pane, int sel);

int tag_input(int c);
void tag_vis(struct pane *pane, int sel);

int cmd_input(int c);
void cmd_vis(struct pane *pane, int sel);

void main_input(int c);
void main_vis(void);

void
init(void)
{
	quit = 0;
	win_ratio = 0.3f;
	cmdshow = 0;
	cmdcur = 0;

	/* readline */
	rl_initialize();
	rl_input = 0;
	rl_input_avail = 0;

	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;
	rl_change_environment = 0;

	rl_getc_function = readline_getc;
	rl_input_available_hook = readline_input_avail;
	rl_callback_handler_install("", evalcmd);
	rl_attempted_completion_function = track_name_completion;
	rl_bind_key('\x07', exit_cmdmode);

	/* curses */
	initscr();
	cbreak();
	noecho();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	start_color();
	curs_set(0);
	ESCDELAY = 0;

	signal(SIGINT, cancel);

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
	rl_callback_handler_remove();
	delwin(pane_left.win);
	delwin(pane_right.win);
	delwin(pane_bot.win);
	endwin();
}

void
cancel(int sig)
{
	if (pane_sel == &pane_bot) {
		pane_sel = NULL;
	} else {
		quit = 1;
	}
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

char **
track_name_completion(const char *text, int start, int end)
{
	rl_attempted_completion_over = 1;
	return rl_completion_matches(text, track_name_generator);
}

char *
track_name_generator(const char *text, int state)
{
	static int list_index, len;
	char *name;

	if (cmdmode != SEARCH) return NULL;

	if (!state) {
		list_index = 0;
		len = strlen(text);
	}

	if (list_index++ == 0 && !strncmp("hello", text, len))
		return strdup("hello");
	// TODO: iter over playlist
	// while ((name = track_names[list_index++])) {
	// 	if (strncmp(name, text, len) == 0) {
	// 		return strdup(name);
	// 	}
	// }

	return NULL;
}

int
readline_input_avail(void)
{
	return rl_input_avail;
}

int
readline_getc(FILE *dummy)
{
	rl_input_avail = false;
	return rl_input;
}

void
readline_consume(int c)
{
	rl_input = c;
	rl_input_avail = true;
	rl_callback_read_char();
}

int
exit_cmdmode(int a, int b)
{
	pane_sel = NULL;
	move(5, 5);
	wprintw(stdscr, "HIT %i %i", a, b);
	return 0;
}

int
strnwidth(const char *s, int n)
{
	mbstate_t shift_state;
	wchar_t wc;
	size_t wc_len;
	size_t width = 0;

	memset(&shift_state, '\0', sizeof shift_state);

	for (size_t i = 0; i < n; i += wc_len) {
		wc_len = mbrtowc(&wc, s + i, MB_CUR_MAX, &shift_state);
		if (!wc_len) {
			break;
		} else if (wc_len >= (size_t)-2) {
			width += MIN(n - 1, strlen(s + i));
			break;
		} else {
			width += iswcntrl(wc) ? 2 : MAX(0, wcwidth(wc));
		}
	}

done:
	return width;
}

void
evalcmd(char *line)
{
	if (!line || !*line) {
		pane_sel = NULL;
		return;
	}

	if (*line) add_history(line);
}

int
tag_input(int c)
{
	return 0;
}

void
tag_vis(struct pane *pane, int sel)
{
	werase(pane->win);
	pane_title(pane, "Tags", sel);
}

int
track_input(int c)
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
cmd_input(int c)
{
	readline_consume(c);
	return 1;
}

void
cmd_vis(struct pane *pane, int sel)
{
	int in_range;

	werase(pane->win);

	if (track_sel)
		pane_title(pane, track_sel->title, track_paused);
	else
		pane_title(pane, "", 0);

	if (sel || cmdshow) {
		wmove(pane->win, 2, 0);
		waddch(pane->win, cmdmode == SEARCH ? '/' : ':');
		wprintw(pane->win, "%-*.*s", pane->w - 1, pane->w - 1, rl_line_buffer);
		if (sel) {
			cmdcur = strnwidth(rl_line_buffer, rl_point);
			ATTR_ON(pane->win, A_REVERSE);
			wmove(pane->win, 2, 1 + cmdcur);
			in_range = cmdcur < strlen(rl_line_buffer);
			waddch(pane->win, in_range ? rl_line_buffer[cmdcur] : ' ');
			ATTR_OFF(pane->win, A_REVERSE);
		}
	}
}

void
main_input(int c)
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
	case CTRL('c'):
		quit = 1;
		break;
	case ':':
		cmdmode = EXECUTE;
		pane_sel = &pane_bot;
		break;
	case '?':
	case '/':
		cmdmode = SEARCH;
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
	int i, c, handled;

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

		keypad(stdscr, pane_sel != &pane_bot);
		c = getch();
	} while (!quit);

	cleanup();
}
