#pragma once

#include "util.h"

#include "sndfile.h"

#include <signal.h>

enum {
	PLAYER_NONE,
	PLAYER_PAUSE,
	PLAYER_PLAY,
	PLAYER_SKIP,
	PLAYER_PREV,
	PLAYER_STOP,
	PLAYER_LOAD,
	PLAYER_EXIT
};

enum {
	PLAYER_NOTSET,
	PLAYER_OK,
	PLAYER_FAIL
};

struct player {
	int action, resp;

	int reload;
	char *filepath;
	SNDFILE *file;
	SF_INFO info;

	int sample_index;

	int alive;
	pid_t pid;
};

struct player *player_thread(void);

void player_main(void);

int player_alive(void);

void player_loadfile(const char *path);
void player_action(int action);

int player_pause(void);
int player_play(void);
int player_prev(void);
int player_skip(void);

extern struct player *player;
