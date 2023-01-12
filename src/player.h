#pragma once

#include "list.h"
#include "util.h"

#define PLAYER_STATUS_INFO(...) \
	PLAYER_STATUS(PLAYER_STATUS_MSG_INFO, __VA_ARGS__)

#define PLAYER_STATUS_ERR(...) \
	PLAYER_STATUS(PLAYER_STATUS_MSG_ERR, __VA_ARGS__)

#define PLAYER_STATUS(lvl, ...) do { \
		player.status_lvl = (lvl); \
		if (player.status) free(player.status); \
		player.status = aprintf(__VA_ARGS__); \
	} while (0)

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

struct player {
	/* list of tracks to choose from on prev / next */
	struct list playlist; /* struct track (link_pl) */

	/* played track history */
	struct list history;  /* struct track (link_hs) */

	/* queued tracks */
	struct list queue; /* struct track (link_pq) */

	/* last used track */
	struct track *track;

	/* player track name (not necessarily from player.track) */
	char *track_name;

	/* player has a track is loaded (not necessarily player.track) */
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

int player_play_track(struct track *track, bool new);
int player_clear_track(void);

void player_add_history(struct track *track);

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

