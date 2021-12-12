#pragma once

#include "track.h"
#include "util.h"

#include "mpd/client.h"

#include <signal.h>

enum {
	PLAYER_OK,
	PLAYER_ERR
};

enum {
	PLAYER_MSG_NONE,
	PLAYER_MSG_INFO,
	PLAYER_MSG_ERR
};

enum {
	PLAYER_STATE_PAUSED,
	PLAYER_STATE_PLAYING
};

struct player {
	struct mpd_connection *conn;

	struct link queue;
	struct track *track;
	int state;

	int volume;
	unsigned int time_pos, time_end;

	char *msg;
	int msglvl;
};

void player_init(void);
void player_free(void);
void player_update(void);

void player_queue_clear(void);
void player_queue_append(struct track *track);
void player_queue_insert(struct track *track, size_t pos);

int player_play_track(struct track *track);

int player_toggle_pause(void);
int player_pause(void);
int player_resume(void);
int player_prev(void);
int player_next(void);
int player_seek(int sec);

int player_set_volume(unsigned int vol);

void player_clear_msg(void);

extern struct player *player;

