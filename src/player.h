#pragma once

#include "track.h"
#include "list.h"
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
	PLAYER_STATE_NONE,
	PLAYER_STATE_PAUSED,
	PLAYER_STATE_PLAYING,
	PLAYER_STATE_STOPPED
};

enum {
	PLAYER_ACTION_NONE,
	PLAYER_ACTION_PLAY_PREV,
	PLAYER_ACTION_PLAY_NEXT
};

struct player {
	/* TODO move implementation details to source file */
	struct mpd_connection *conn;

	/* TODO combine with index */
	/* for navigating forward and backwards in time */
	struct list queue;
	struct list history;

	/* list of track refs to choose from on prev / next */
	struct list playlist;

	/* last player track */
	struct track *track;

	/* player has a track loaded,
	 * not necessarily player->track */
	int loaded;

	/* stopped, paused or playing */
	int state;

	/* automatically select new tracks when queue empty */
	int autoplay;

	int shuffle;

	int action;

	/* number of frames to wait before unpausing after
	 * seek to prevent pause-play cycle noises */
	int seek_delay;

	int volume;

	unsigned int time_pos, time_end;

	/* status messaging */
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
int player_play(void);
int player_stop(void);

int player_set_volume(unsigned int vol);

extern struct player *player;

