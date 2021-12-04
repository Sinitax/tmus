#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include "util.h"
#include "link.h"
#include "history.h"
#include "tag.h"
#include "track.h"
#include "player.h"

#include "sndfile.h"
#include "portaudio.h"
#include "curses.h"

#include <dirent.h>
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

struct cmd {
	const char *name;
	cmd_handler func;
};


int style_attrs[STYLE_COUNT] = { 0 };

const char *datadir;
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

struct link tags;

struct track *track_sel;
struct link tracks;
struct link playlist;
int track_paused;

completion_generator completion;
struct history search_history, command_history;
struct history *history;
int cmd_show, cmd_mode;

int tag_index;
struct link tags_sel;

int track_index;


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

char *command_name_generator(const char *text, int state);
char *track_name_generator(const char *text, int state);

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
	{ "save", usercmd_save },
};


void
init(void)
{
	quit = 0;
	win_ratio = 0.3f;

	initscr();
	cbreak();
	noecho();
	halfdelay(1);
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	start_color();
	curs_set(0);
	ESCDELAY = 0;

	history = &command_history;
	history_init(&search_history);
	history_init(&command_history);

	style_init(STYLE_DEFAULT, COLOR_WHITE, COLOR_BLACK, 0);
	style_init(STYLE_TITLE, COLOR_WHITE, COLOR_BLUE, A_BOLD);
	style_init(STYLE_PANE_SEP, COLOR_BLUE, COLOR_BLACK, 0);
	style_init(STYLE_ITEM_SEL, COLOR_YELLOW, COLOR_BLACK, A_BOLD);
	style_init(STYLE_ITEM_HOVER, COLOR_WHITE, COLOR_BLUE, 0);
	style_init(STYLE_ITEM_HOVER_SEL, COLOR_YELLOW, COLOR_BLUE, A_BOLD);

	pane_init(&pane_left, tag_input, tag_vis);
	pane_init(&pane_right, track_input, track_vis);
	pane_init(&pane_bot, cmd_input, cmd_vis);

	pane_sel = &pane_left;

	datadir = getenv("TMUS_DATA");
	ASSERT(datadir != NULL);

	track_index = 0;
	tag_index = 0;
	tags_sel = LIST_HEAD;

	// player = player_thread();

	data_load();

	atexit(cleanup);
}

void
cleanup(void)
{
	int status;

	// TODO stop player

	data_save();

	kill(player->pid, SIGTERM);
	waitpid(player->pid, &status, 0);

	// TODO free player

	history_free(&search_history);
	history_free(&command_history);

	delwin(pane_left.win);
	delwin(pane_right.win);
	delwin(pane_bot.win);
	endwin();
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
		link_push_back(&tags, &tag->link);

		tracks_load(tag);
	}
	closedir(dir);
}

void
tracks_load(struct tag *tag)
{
	struct dirent *ent;
	struct track *track;
	struct tag_ref *tagref;
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
		tagref = malloc(sizeof(struct tag_ref));
		ASSERT(tagref != NULL);
		tagref->tag = tag;
		tagref->link = LINK_EMPTY;
		link_push_back(&track->tags, &tagref->link);

		link_push_back(&tracks, &track->link);
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

char *
command_name_generator(const char *text, int reset)
{
	static int index, len;

	if (reset) {
		index = 0;
		len = strlen(text);
	}

	for (; index < ARRLEN(cmds); index++) {
		if (!strncmp(cmds[index].name, text, len))
			return strdup(cmds[index].name);
	}

	return NULL;
}

char *
track_name_generator(const char *text, int reset)
{
	static struct link *cur;
	struct track *track;
	static int len;

	if (reset) {
		cur = tracks.next;
		len = strlen(text);
	}

	for (; cur; cur = cur->next) {
		track = UPCAST(cur, struct track);
		if (!strncmp(track->name, text, len))
			return strdup(track->name);
	}

	return NULL;
}

int
tag_input(wint_t c)
{
	struct link *cur;
	struct tag *tag;

	switch (c) {
	case KEY_UP:
		tag_index = MAX(0, tag_index - 1);
		return 1;
	case KEY_DOWN:
		tag_index = MIN(list_len(&tags) - 1,
				tag_index + 1);
		return 1;
	case KEY_SPACE:
		cur = link_iter(tags.next, tag_index);
		ASSERT(cur != NULL);
		tag = UPCAST(cur, struct tag);
		if (tagrefs_incl(&tags_sel, tag)) {
			tagrefs_rm(&tags_sel, tag);
		} else {
			tagrefs_add(&tags_sel, tag);
		}
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

	index = 0;
	for (iter = tags.next; iter; iter = iter->next) {
		tag = UPCAST(iter, struct tag);
		tsel = tagrefs_incl(&tags_sel, tag);

		if (sel && index == tag_index && tsel)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == tag_index)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (tsel)
			style_on(pane->win, STYLE_ITEM_SEL);

		wmove(pane->win, 1 + index, 0);
		wprintw(pane->win, "%*.*s", pane->w, pane->w, tag->name);

		if (index == tag_index && tsel)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (index == tag_index)
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
		track_index = MAX(0, track_index - 1);
		return 1;
	case KEY_DOWN:
		track_index = MIN(list_len(&tracks) - 1,
				track_index + 1);
		return 1;
	case KEY_ENTER:
		link = link_iter(tracks.next, track_index);
		ASSERT(link != NULL);
		track = UPCAST(link, struct track);
		if (track != track_sel) {
			track_sel = track;
			track_paused = 0;
		} else {
			track_paused ^= 1;
		}
		// if (!track_paused)
		// 	track_play();
		// else
		// 	track_pause();
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

	index = 0;
	for (iter = tracks.next; iter; iter = iter->next) {
		track = UPCAST(iter, struct track);

		if (sel && index == track_index && track == track_sel)
			style_on(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_index)
			style_on(pane->win, STYLE_ITEM_HOVER);
		else if (track == track_sel)
			style_on(pane->win, STYLE_ITEM_SEL);

		wmove(pane->win, 1 + index, 0);
		wprintw(pane->win, "%-*.*s", pane->w, pane->w, track->name);

		if (sel && index == track_index && track == track_sel)
			style_off(pane->win, STYLE_ITEM_HOVER_SEL);
		else if (sel && index == track_index)
			style_off(pane->win, STYLE_ITEM_HOVER);
		else if (track == track_sel)
			style_off(pane->win, STYLE_ITEM_SEL);

		index++;
	}
}

int
cmd_input(wint_t c)
{
	if (cmd_mode == IMODE_EXECUTE) {
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
	case KEY_ENTER:
		history_submit(history);
		if (!*history->cmd->buf)
			pane_sel = NULL;
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
		pane_title(pane, track_sel->name, sel);
	else
		pane_title(pane, "", sel);

	static int i = 0;
	wmove(pane->win, 1, 0);
	wprintw(pane->win, "%i", i++);

	if (sel || cmd_show) {
		wmove(pane->win, 2, 0);
		waddch(pane->win, cmd_mode == IMODE_SEARCH ? '/' : ':');
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
		cmd_mode = IMODE_EXECUTE;
		pane_sel = &pane_bot;
		break;
	case '/':
		cmd_mode = IMODE_SEARCH;
		pane_sel = &pane_bot;
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

	cleanup();
}

