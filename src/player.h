#pragma once

#include "track.h"
#include "list.h"
#include "util.h"

enum {
	PLAYER_STATUS_OK,
	PLAYER_STATUS_ERR
};

enum {
	PLAYER_STATUS_MSG_NONE,
	PLAYER_STATUS_MSG_INFO,
	PLAYER_STATUS_MSG_ERR
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
	/* played track history */
	struct list history;  /* struct ref -> struct track */
	struct link *history_sel; /* position in history */

	/* queued tracks */
	struct list queue; /* struct ref -> struct track */

	/* selected track, not (yet) part of history or queue */
	struct track *track;

	/* list of tracks to choose from on prev / next */
	struct list playlist; /* struct ref -> struct track */
	struct link *playlist_sel; /* position in playlist */

	/* a track is loaded, not necessarily player.track */
	bool loaded;

	/* automatically select new tracks when queue empty */
	bool autoplay;

	/* randomize which track is chosen when queue empty */
	bool shuffle;

	/* stopped, paused or playing */
	int state;

	/* volume adjustment when possible */
	int volume;

	/* track position and duration */
	unsigned int time_pos, time_end;

	/* status messaging */
	char *status;
	int status_lvl;
};

void player_init(void);
void player_deinit(void);

void player_update(void);

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

extern struct player player;

