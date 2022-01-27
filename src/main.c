#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include "util.h"
#include "list.h"
#include "log.h"
#include "mpris.h"
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
	IMODE_TRACK_SEARCH,
	IMODE_TAG_SEARCH,
	IMODE_COUNT
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
static struct list tracks;
static struct list tags;
static struct list tags_sel;
static struct list *tracks_vis;
static int track_show_playlist;

/* bottom 'cmd' pane for search / exec */
static struct pane *cmd_pane;
static struct history command_history;
static struct history track_search_history;
static struct history tag_search_history;
static struct history *history;
static int cmd_show, cmd_mode;
static struct inputln completion_query;
static int completion_reset;
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
static void tui_ncurses_init(void);
static void tui_resize(void);
static void tui_end(void);

static void data_load(void);
static void data_save(void);
static void data_free(void);

static void index_update(struct tag *tag);
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

static int play_track(const wchar_t *name);
static int select_tag(const wchar_t *name);
static int cmd_pane_input(wint_t c);
static void cmd_pane_vis(struct pane *pane, int sel);

static void queue_hover(void);
static void update_track_playlist(void);
static void main_input(wint_t c);
static void main_vis(void);

static int usercmd_save(const wchar_t *args);

const char imode_prefix[IMODE_COUNT] = {
	[IMODE_EXECUTE] = ':',
	[IMODE_TRACK_SEARCH] = '/',
	[IMODE_TAG_SEARCH] = '?',
};

const struct cmd cmds[] = {
	{ L"save", usercmd_save },
};

void
init(void)
{
	setlocale(LC_ALL, "");
	srand(time(NULL));
	quit = 0;

	inputln_init(&completion_query);
	completion_reset = 1;

	history_init(&track_search_history);
	history_init(&tag_search_history);
	history_init(&command_history);
	history = &command_history;

	log_init();

	data_load();

	player_init();

	tui_init();

	dbus_init();

	listnav_init(&tag_nav);
	listnav_init(&track_nav);

	track_show_playlist = 0;
	update_track_playlist();

	on_exit(cleanup, NULL);
	signal(SIGINT, exit);
}

void
cleanup(int exitcode, void* arg)
{
	if (!exitcode) tui_end();

	if (!exitcode) data_save();
	data_free();

	player_free();

	dbus_end();

	log_end();

	history_free(&track_search_history);
	history_free(&tag_search_history);
	history_free(&command_history);
}

void
tui_init(void)
{
	tui_ncurses_init();

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
tui_ncurses_init(void)
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
tui_end(void)
{
	pane_free(&pane_left);
	pane_free(&pane_right);
	pane_free(&pane_bot);

	if (!isendwin()) endwin();
}

void
data_load(void)
{
	struct dirent *ent;
	struct tag *tag;
	struct stat st;
	char *path;
	DIR *dir;

	list_init(&tracks);
	list_init(&tags);
	list_init(&tags_sel);

	datadir = getenv("TMUS_DATA");
	ASSERT(datadir != NULL);

	dir = opendir(datadir);
	ASSERT(dir != NULL);
	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		path = aprintf("%s/%s", datadir, ent->d_name);
		ASSERT(path != NULL);

		if (!stat(path, &st) && S_ISDIR(st.st_mode)) {
			tag = tag_init(datadir, ent->d_name);
			tracks_load(tag);
			list_push_back(&tags, LINK(tag));
		}

		free(path);
	}
	closedir(dir);

	ASSERT(!list_empty(&tags));
}

void
data_save(void)
{
	
}

void
data_free(void)
{
	struct link *track_link;
	struct link *tag_link;
	struct tag *tag;

	log_info("MAIN: data_free()\n");

	refs_free(&tracks);
	refs_free(&tags_sel);

	while (!list_empty(&tags)) {
		tag_link = list_pop_front(&tags);

		tag = UPCAST(tag_link, struct tag);
		while (!list_empty(&tag->tracks)) {
			track_link = list_pop_front(&tag->tracks);
			track_free(UPCAST(track_link, struct ref)->data);
			ref_free(UPCAST(track_link, struct ref));
		}
		tag_free(tag);
	}
}

void
index_update(struct tag *tag)
{
	struct track *track, *track_iter;
	struct dirent *ent;
	struct link *iter;
	struct ref *ref;
	struct stat st;
	char *path;
	FILE *file;
	DIR *dir;
	int fid;

	path = aprintf("%s/index", tag->fpath);
	ASSERT(path != NULL);

	file = fopen(path, "w+");
	ASSERT(file != NULL);
	free(path);

	dir = opendir(tag->fpath);
	ASSERT(dir != NULL);

	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;
		if (!strcmp(ent->d_name, "index"))
			continue;

		/* skip files without extension */
		if (!strchr(ent->d_name, '.'))
			continue;

		path = aprintf("%s/%s", tag->fpath, ent->d_name);
		ASSERT(path != NULL);
		fid = stat(path, &st) ? -1 : st.st_ino;
		free(path);

		fprintf(file, "%i:%s\n", fid, ent->d_name);
	}

	closedir(dir);
	fclose(file);
}

void
tracks_load(struct tag *tag)
{
	char linebuf[1024];
	struct link *link;
	struct track *track;
	struct track *track2;
	struct ref *ref;
	char *index_path;
	char *track_name, *sep;
	int track_fid;
	FILE *file;

	index_path = aprintf("%s/index", tag->fpath);
	ASSERT(index_path != NULL);

	file = fopen(index_path, "r");
	if (file == NULL) {
		index_update(tag);
		file = fopen(index_path, "r");
		ASSERT(file != NULL);
	}

	while (fgets(linebuf, sizeof(linebuf), file)) {
		sep = strchr(linebuf, '\n');
		if (sep) *sep = '\0';

		sep = strchr(linebuf, ':');
		ASSERT(sep != NULL);
		*sep = '\0';

		track_fid = atoi(linebuf);
		track_name = sep + 1;
		track = track_init(tag->fpath, track_name, track_fid);

		ref = ref_init(tag);
		ASSERT(ref != NULL);
		list_push_back(&track->tags, LINK(ref));

		ref = ref_init(track);
		ASSERT(ref != NULL);
		list_push_back(&tag->tracks, LINK(ref));

		for (LIST_ITER(&tracks, link)) {
			track2 = UPCAST(link, struct ref)->data;
			if (track->fid == track2->fid)
				break;
		}

		if (!link) {
			ref = ref_init(track);
			ASSERT(ref != NULL);
			list_push_back(&tracks, LINK(ref));
		}
	}

	fclose(file);
	free(index_path);
}

void
tracks_save(struct tag *tag)
{
	struct track *track;
	struct link *link;
	char *index_path;
	FILE *file;

	/* write playlist back to index file */

	index_path = aprintf("%s/index", tag->fpath);
	ASSERT(index != NULL);

	file = fopen(index_path, "w+");
	ASSERT(file != NULL);

	for (LIST_ITER(&tracks, link)) {
		track = UPCAST(link, struct ref)->data;
		fprintf(file, "%i:%s\n", track->fid, track->fname);
	}

	fclose(file);
	free(index_path);
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
		cur = tracks.head.next;
		iter = cur;
	} else {
		iter = fwd ? cur->next : cur->prev;
	}

	while (iter && LIST_INNER(&tracks, iter)) {
		track = UPCAST(iter, struct ref)->data;
		if (wcsstr(track->name, text)) {
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

	while (iter && LIST_INNER(&tags, iter)) {
		tag = UPCAST(iter, struct tag);
		if (wcsstr(tag->name, text)) {
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
		if (list_empty(tracks_vis)) return 1;
		link = list_at(tracks_vis, track_nav.sel);
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

	for (LIST_ITER(&tracks, iter)) {
		track = UPCAST(iter, struct ref)->data;
		if (wcsstr(track->name, query)) {
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

	index = 0;
	for (LIST_ITER(&tags, iter)) {
		tag = UPCAST(iter, struct tag);
		if (wcsstr(tag->name, query)) {
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

		if (cmd_mode == IMODE_EXECUTE) {
			run_cmd(history->sel->buf);
		} else if (cmd_mode == IMODE_TRACK_SEARCH) {
			play_track(history->sel->buf);
		} else if (cmd_mode == IMODE_TAG_SEARCH) {
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

		cmd = history->sel;
		if (cmd != history->input) {
			index = 0;
			for (LIST_ITER(&history->list, iter)) {
				if (UPCAST(iter, struct inputln) == cmd)
					break;
			}
			line += swprintf(line, end - line, L"[%i] ",
				iter ? index : -1);
		} else {
			line += swprintf(line, end - line, L"%c",
				imode_prefix[cmd_mode]);
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

	link = list_at(&player->playlist, track_nav.sel);
	ASSERT(link != NULL);
	player_queue_append(UPCAST(link, struct ref)->data);
}

void
update_track_playlist(void)
{
	struct link *iter;
	struct tag *tag;

	if (track_show_playlist) {
		tracks_vis = &player->playlist;
	} else {
		iter = list_at(&tags, tag_nav.sel);
		ASSERT(iter != NULL);
		tag = UPCAST(iter, struct tag);
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
		cmd_mode = IMODE_EXECUTE;
		pane_sel = &pane_bot;
		completion_reset = 1;
		history = &command_history;
		completion = command_name_gen;
		break;
	case L'/':
		cmd_mode = IMODE_TRACK_SEARCH;
		pane_sel = &pane_bot;
		completion_reset = 1;
		history = &track_search_history;
		completion = track_name_gen;
		break;
	case L'?':
		cmd_mode = IMODE_TAG_SEARCH;
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
		dbus_update();
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

		get_wch(&c);
	} while (!quit);
}

