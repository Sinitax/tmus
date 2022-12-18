#include "player.h"

#include "data.h"
#include "list.h"
#include "util.h"
#include "log.h"

#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER_STATUS(lvl, ...) do { \
		player.status_lvl = PLAYER_STATUS_MSG_ ## lvl; \
		if (player.status) free(player.status); \
		player.status = aprintf(__VA_ARGS__); \
	} while (0)

struct mplay_player {
	FILE *stdin;
	FILE *stdout;
	pid_t pid;
};

struct player player;
struct mplay_player mplay;

static bool mplay_alive(void);
static void mplay_kill(void);
static bool mplay_run(struct track *track);
static char *mplay_readline(void);
static void player_clear_status(void);

bool
mplay_alive(void)
{
	return player.loaded && !kill(mplay.pid, 0);
}

bool
mplay_run(struct track *track)
{
	int output[2];
	int input[2];
	char *path;
	char *line;

	ASSERT(!player.loaded);

	if (pipe(input) == -1)
		err(1, "pipe");

	if (pipe(output) == -1)
		err(1, "pipe");

	mplay.pid = fork();
	if (mplay.pid < 0) err(1, "fork");

	if (mplay.pid != 0) {
		close(output[1]);
		mplay.stdout = fdopen(output[0], "r");
		if (!mplay.stdout) err(1, "fdopen");
		setvbuf(mplay.stdout, NULL, _IONBF, 0);

		close(input[0]);
		mplay.stdin = fdopen(input[1], "w");
		if (!mplay.stdin) err(1, "fdopen");
		setvbuf(mplay.stdin, NULL, _IONBF, 0);
	} else {
		close(0);
		close(1);
		dup2(input[0], 0);
		dup2(output[1], 1);
		close(output[0]);
		close(output[1]);
		path = aprintf("%s/%s/%s", datadir,
			track->tag->name, track->name);
		execl("/usr/bin/mplay", "mplay", path, NULL);
		abort();
	}

	player.loaded = true;

	line = mplay_readline();
	if (!line || strcmp(line, "+READY")) {
		mplay_kill();
		PLAYER_STATUS(ERR, "mplay failed to start");
		return false;
	}

	return true;
}

void
mplay_kill(void)
{
	if (!player.loaded)
		return;

	kill(mplay.pid, SIGKILL);
	waitpid(mplay.pid, NULL, 0);
	player.loaded = false;
}

char *
mplay_readline(void)
{
	static char linebuf[256];
	char *tok;

	/* TODO: add timeout */
	if (!fgets(linebuf, sizeof(linebuf), mplay.stdout))
		return NULL;

	tok = strchr(linebuf, '\n');
	if (tok) *tok = '\0';

	return linebuf;
}

void
player_clear_status(void)
{
	free(player.status);
	player.status = NULL;
	player.status_lvl = PLAYER_STATUS_MSG_NONE;
}

void
player_init(void)
{
	list_init(&player.playlist);
	list_init(&player.history);
	list_init(&player.queue);

	player.track = NULL;
	player.track_name = NULL;

	player.loaded = false;
	player.autoplay = true;
	player.shuffle = true;

	player.state = PLAYER_STATE_PAUSED;
	player.volume = 50;

	player.time_pos = 0;
	player.time_end = 0;

	player.status = NULL;
	player.status_lvl = PLAYER_STATUS_MSG_INFO;
}

void
player_deinit(void)
{
	list_clear(&player.playlist);
	list_clear(&player.queue);
	list_clear(&player.history);

	free(player.status);
	free(player.track_name);

	mplay_kill();
}

void
player_update(void)
{
	char *tok, *line;

	if (!player.loaded) return;

	fprintf(mplay.stdin, "status\n");
	line = mplay_readline();
	if (!line || strncmp(line, "+STATUS:", 8)) {
		PLAYER_STATUS(ERR, "PLAYER: invalid response");
		return;
	}

	tok = line;
	while ((tok = strchr(tok, ' '))) {
		if (!strncmp(tok + 1, "vol:", 4)) {
			player.volume = atoi(tok + 5);
		} else if (!strncmp(tok + 1, "pause:", 6)) {
			player.state = atoi(tok + 7)
				? PLAYER_STATE_PAUSED : PLAYER_STATE_PLAYING;
		} else if (!strncmp(tok + 1, "pos:", 4)) {
			player.time_pos = atoi(tok + 5);
			player.time_end = MAX(player.time_pos, player.time_end);
		}
		tok += 1;
	}
}

int
player_play_track(struct track *track, bool new)
{
	ASSERT(track != NULL);
	player_clear_track();

	if (!mplay_run(track))
		return PLAYER_STATUS_ERR;

	/* add last track to history */
	if (player.track && !link_inuse(&player.track->link_hs))
		player_add_history(player.track);

	/* new invocations result in updated history pos */
	if (new) link_pop(&track->link_hs);

	player.track = track;
	player.time_pos = 0;
	player.time_end = 0;

	return PLAYER_STATUS_OK;
}

int
player_clear_track(void)
{
	player.track = NULL;
	mplay_kill();

	return PLAYER_STATUS_OK;
}

int
player_toggle_pause(void)
{
	char *line;

	fprintf(mplay.stdin, "pause\n");
	line = mplay_readline();
	if (!line || strncmp(line, "+PAUSE:", 7)) {
		PLAYER_STATUS(ERR, "PLAYER: invalid response");
		return PLAYER_STATUS_ERR;
	}

	return PLAYER_STATUS_OK;
}

int
player_pause(void)
{
	if (player.state != PLAYER_STATE_PAUSED)
		player_toggle_pause();

	return PLAYER_STATUS_OK;
}

int
player_resume(void)
{
	if (player.state != PLAYER_STATE_PLAYING)
		player_toggle_pause();

	return PLAYER_STATUS_OK;
}

int
player_play(void)
{
	return PLAYER_STATUS_OK;
}

int
player_stop(void)
{
	player_clear_track();

	return PLAYER_STATUS_OK;
}

int
player_seek(int sec)
{
	char *line;

	player_clear_status();

	fprintf(mplay.stdin, "seek %i\n", sec);
	line = mplay_readline();
	if (!line || strncmp(line, "+SEEK:", 6)) {
		PLAYER_STATUS(ERR, "PLAYER: Bad response");
		return PLAYER_STATUS_ERR;
	}

	player.time_pos = atoi(line + 7);
	player.time_end = MAX(player.time_pos, player.time_end);

	return PLAYER_STATUS_OK;
}

int
player_set_volume(unsigned int vol)
{
	char *line;

	player_clear_status();

	if (player.volume == -1) {
		PLAYER_STATUS(ERR, "Volume control not supported");
		return PLAYER_STATUS_ERR;
	}

	fprintf(mplay.stdin, "vol %i\n", vol);
	line = mplay_readline();
	if (!line || strncmp(line, "+VOLUME:", 8)) {
		PLAYER_STATUS(ERR, "PLAYER: Bad response");
		return PLAYER_STATUS_ERR;
	}

	player.volume = atoi(line + 9);

	return PLAYER_STATUS_OK;
}

