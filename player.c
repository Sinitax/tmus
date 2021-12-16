#include "player.h"

#include "portaudio.h"
#include "sndfile.h"
#include "util.h"

#include <mpd/song.h>
#include <mpd/status.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PLAYER_STATUS(lvl, ...) do { \
		player->msglvl = lvl; \
		if (player->msg) free(player->msg); \
		player->msg = aprintf(__VA_ARGS__); \
	} while (0)

static struct player player_static;
struct player *player;

void
player_init(void)
{
	player = malloc(sizeof(struct player));
	ASSERT(player != NULL);

	player->conn = mpd_connection_new(NULL, 0, 0);
	ASSERT(player->conn != NULL);

	player->queue = LIST_HEAD;
	player->track = NULL;
	player->state = PLAYER_STATE_PAUSED;

	player->volume = 0;
	player->time_pos = 0;
	player->time_end = 0;

	player->msg = NULL;
	player->msglvl = PLAYER_MSG_INFO;

	mpd_run_stop(player->conn);
	mpd_run_clear(player->conn);
}

void
player_free(void)
{
	if (!player->conn) return;
	mpd_run_stop(player->conn);
	mpd_run_clear(player->conn);
	mpd_connection_free(player->conn);
}

void
player_update(void)
{
	struct mpd_status *status;
	struct mpd_song *song;
	const char *tmp;

	status = mpd_run_status(player->conn);
	ASSERT(status != NULL);

	player->state = mpd_status_get_state(status) == MPD_STATE_PLAY
		? PLAYER_STATE_PLAYING : PLAYER_STATE_PAUSED;
	player->volume = mpd_status_get_volume(status);

	song = mpd_run_current_song(player->conn);
	if (song) {
		player->time_pos = mpd_status_get_elapsed_time(status);
		player->time_end = mpd_song_get_duration(song);
		mpd_song_free(song);
	} else {
		player->time_pos = 0;
		player->time_end = 0;
	}

	mpd_status_free(status);
}

void
player_queue_clear(void)
{
	struct link *iter, *next;;

	for (iter = &player->queue; iter; ) {
		next = iter->next;
		free(UPCAST(iter, struct track_ref));
		iter = next;
	}

	player->queue = LIST_HEAD;
}

void
player_queue_append(struct track *track)
{
	player_queue_insert(track, list_len(&player->queue));
}

void
player_queue_insert(struct track *track, size_t pos)
{
	struct track_ref *new;
	struct link *iter;
	int i;

	new = malloc(sizeof(struct track_ref));
	new->track = track;
	new->link = LINK_EMPTY;

	iter = &player->queue;
	for (i = 0; i < pos && iter->next; i++)
		iter = iter->next;

	link_append(iter, &new->link);
}

int
player_play_track(struct track *track)
{
	player_clear_msg();
	player->track = track;
	mpd_run_stop(player->conn);
	mpd_run_clear(player->conn);

	if (!mpd_run_add(player->conn, player->track->fpath)
			|| !mpd_run_play(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Playback failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_toggle_pause(void)
{
	if (!mpd_run_toggle_pause(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Pause toggle failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_pause(void)
{
	if (!mpd_run_pause(player->conn, true)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Pausing track failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_resume(void)
{
	if (!mpd_run_pause(player->conn, false)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Resuming track failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_next(void)
{
	if (!mpd_run_next(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Playing next track failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_prev(void)
{
	if (!mpd_run_previous(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Playing prev track failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_seek(int sec)
{
	if (!mpd_run_seek_current(player->conn, sec, false)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Track seek failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_set_volume(unsigned int vol)
{
	if (player->volume == -1) {
		PLAYER_STATUS(PLAYER_MSG_INFO, "Setting volume not supported");
		return PLAYER_ERR;
	}

	if (!mpd_run_set_volume(player->conn, vol)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Setting volume failed");
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

void
player_clear_msg(void)
{
	free(player->msg);
	player->msg = NULL;
	player->msglvl = PLAYER_MSG_NONE;
}
