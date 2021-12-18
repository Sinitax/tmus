#include "player.h"
#include "ref.h"

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

	player->seek_delay = 0;

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
	struct ref *track;
	const char *tmp;

	status = mpd_run_status(player->conn);
	ASSERT(status != NULL);

	switch (mpd_status_get_state(status)) {
	case MPD_STATE_PAUSE:
		player->state = PLAYER_STATE_PAUSED;
		break;
	case MPD_STATE_PLAY:
		player->state = PLAYER_STATE_PLAYING;
		break;
	case MPD_STATE_STOP:
		player->state = PLAYER_STATE_STOPPED;
		break;
	default:
		ASSERT(0);
	}
	player->volume = mpd_status_get_volume(status);

	if (player->seek_delay) {
		player->seek_delay -= 1;
		if (!player->seek_delay)
			player_play();
	}

	if (!mpd_run_current_song(player->conn)
			&& !list_empty(&player->queue)) {
		track = UPCAST(link_pop(player->queue.next),
			struct ref);
		player_play_track(track->data);
		ref_free(track);
	}

	song = mpd_run_current_song(player->conn);
	if (song) {
		player->loaded = true;
		player->time_pos = mpd_status_get_elapsed_time(status);
		player->time_end = mpd_song_get_duration(song);
		mpd_song_free(song);
	} else {
		player->track = NULL;
		player->loaded = false;
		player->time_pos = 0;
		player->time_end = 0;
	}

	mpd_status_free(status);
}

void
player_queue_clear(void)
{
	struct ref *ref;

	while (player->queue.next) {
		ref = UPCAST(link_pop(player->queue.next), struct ref);
		ref_free(ref);
	}
}

void
player_queue_append(struct track *track)
{
	struct ref *ref;
	struct link *link;

	ref = ref_init(track);
	link = link_back(&player->queue);
	link_append(link, LINK(ref));
}

void
player_queue_insert(struct track *track, size_t pos)
{
	struct ref *ref;
	struct link *link;

	ref = ref_init(track);
	link = link_iter(player->queue.next, pos);
	link_prepend(link, LINK(ref));
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
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_toggle_pause(void)
{
	if (!mpd_run_toggle_pause(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Pause toggle failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_pause(void)
{
	if (!mpd_run_pause(player->conn, true)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Pausing track failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_resume(void)
{
	if (!mpd_run_pause(player->conn, false)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Resuming track failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_next(void)
{
	if (!player->loaded) return PLAYER_ERR;

	if (!mpd_run_next(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Playing next track failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_prev(void)
{
	/* TODO prevent mpd from dying on error, how to use properly */
	if (!player->loaded) return PLAYER_ERR;

	if (!mpd_run_previous(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Playing prev track failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_play(void)
{
	if (!mpd_run_play(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Playing track failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_stop(void)
{
	if (!mpd_run_stop(player->conn)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Stopping track failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	return PLAYER_OK;
}

int
player_seek(int sec)
{
	if (!player->loaded || player->state == PLAYER_STATE_STOPPED) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "No track loaded");
		return PLAYER_ERR;
	}

	if (!mpd_run_seek_current(player->conn, sec, false)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Track seek failed");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	player->seek_delay = 8;
	player_pause();

	return PLAYER_OK;
}

int
player_set_volume(unsigned int vol)
{
	if (player->volume == -1) {
		PLAYER_STATUS(PLAYER_MSG_INFO, "Setting volume not supported");
		mpd_run_clearerror(player->conn);
		return PLAYER_ERR;
	}

	if (!mpd_run_set_volume(player->conn, vol)) {
		PLAYER_STATUS(PLAYER_MSG_ERR, "Setting volume failed");
		mpd_run_clearerror(player->conn);
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
