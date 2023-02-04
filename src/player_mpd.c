#include "player.h"

#include "data.h"
#include "list.h"
#include "util.h"
#include "log.h"

#include <mpd/client.h>
#include <mpd/song.h>
#include <mpd/status.h>

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct mpd_player {
	struct mpd_connection *conn;

	/* number of frames to wait before unpausing after
	 * seek to prevent pause-play cycle noises */
	int seek_delay;

	/* action to perform on next update */
	int action;
};

struct player player;
struct mpd_player mpd;

static bool mpd_handle_status(int status);
static char *mpd_loaded_track_name(struct mpd_song *song);

bool
mpd_handle_status(int status)
{
	const char *errstr;

	switch (status) {
	case MPD_ERROR_SERVER:
	case MPD_ERROR_ARGUMENT:
		if (!mpd_connection_clear_error(mpd.conn))
			ERRORX(SYSTEM, "Player failed to recover");
	case MPD_ERROR_SYSTEM:
		errstr = mpd_connection_get_error_message(mpd.conn);
		PLAYER_STATUS("MPD ERR - %s", errstr);
		return false;
	case MPD_ERROR_CLOSED:
		ERRORX(SYSTEM, "Player connection abruptly closed");
	}

	return true;
}

char *
mpd_loaded_track_name(struct mpd_song *song)
{
	const char *path, *sep;

	path = mpd_song_get_uri(song);

	sep = strrchr(path, '/');
	if (!sep) return astrdup(path);

	return astrdup(sep + 1);
}

void
player_init(void)
{
	mpd.conn = NULL;
	mpd.seek_delay = 0;

	list_init(&player.playlist);
	list_init(&player.history);
	list_init(&player.queue);

	player.track = NULL;
	player.track_name = NULL;

	player.loaded = 0;
	player.autoplay = true;
	player.shuffle = true;

	player.state = PLAYER_STATE_PAUSED;
	player.volume = 50;

	player.time_pos = 0;
	player.time_end = 0;
}

void
player_deinit(void)
{
	list_clear(&player.playlist);
	list_clear(&player.queue);
	list_clear(&player.history);

	free(player.status);
	free(player.track_name);

	if (mpd.conn) mpd_connection_free(mpd.conn);
}

void
player_update(void)
{
	struct mpd_status *status;
	struct mpd_song *current_song;
	bool queue_empty;

	if (!mpd.conn) {
		mpd.conn = mpd_connection_new(NULL, 0, 0);
		if (!mpd.conn) ERRX("MPD connection failed");
	}

	status = mpd_run_status(mpd.conn);
	if (!status) {
		PLAYER_STATUS("MPD connection reset: %s",
			mpd_connection_get_error_message(mpd.conn));
		mpd_connection_free(mpd.conn);
		mpd.conn = NULL;
		return;
	}

	current_song = mpd_run_current_song(mpd.conn);
	if (!current_song) {
		if (player.track)
			player_add_history(player.track);

		queue_empty = list_empty(&player.queue);
		if (player.loaded && player.autoplay || !queue_empty)
			player_next();
	} else {
		mpd_song_free(current_song);
	}

	mpd_status_free(status);

	/* in case autoplay loaded new track,
	 * get status and track name again.. */
	status = mpd_run_status(mpd.conn);
	if (!status) {
		PLAYER_STATUS("MPD connection reset: %s",
			mpd_connection_get_error_message(mpd.conn));
		mpd_connection_free(mpd.conn);
		mpd.conn = NULL;
		return;
	}

	current_song = mpd_run_current_song(mpd.conn);
	if (current_song) {
		free(player.track_name);
		player.track_name = mpd_loaded_track_name(current_song);
		player.loaded = true;
		player.time_pos = mpd_status_get_elapsed_time(status);
		player.time_end = mpd_song_get_duration(current_song);
		mpd_song_free(current_song);
	} else {
		free(player.track_name);
		player.track_name = NULL;
		player.loaded = false;
		player.time_pos = 0;
		player.time_end = 0;
	}

	switch (mpd_status_get_state(status)) {
	case MPD_STATE_PAUSE:
		player.state = PLAYER_STATE_PAUSED;
		break;
	case MPD_STATE_PLAY:
		player.state = PLAYER_STATE_PLAYING;
		break;
	case MPD_STATE_STOP:
		player.state = PLAYER_STATE_STOPPED;
		break;
	default:
		ASSERT(0);
	}

	player.volume = mpd_status_get_volume(status);

	if (mpd.seek_delay) {
		mpd.seek_delay -= 1;
		if (!mpd.seek_delay) player_play();
	}

	mpd_status_free(status);
}

int
player_play_track(struct track *track, bool new)
{
	int status;

	ASSERT(track != NULL);

	status = mpd_run_clear(mpd.conn);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	status = mpd_run_add(mpd.conn, track->fpath);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	status = mpd_run_play(mpd.conn);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	/* add last track to history */
	if (player.track && !link_inuse(&player.track->link_hs))
		player_add_history(player.track);

	/* new invocations result in updated history pos */
	if (new) link_pop(&track->link_hs);

	player.track = track;

	return PLAYER_OK;
}

int
player_clear_track(void)
{
	int status;

	player.track = NULL;
	status = mpd_run_clear(mpd.conn);

	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_toggle_pause(void)
{
	int status;

	status = mpd_run_toggle_pause(mpd.conn);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_pause(void)
{
	int status;

	status = mpd_run_pause(mpd.conn, true);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_resume(void)
{
	int status;

	status = mpd_run_pause(mpd.conn, false);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_play(void)
{
	int status;

	status = mpd_run_play(mpd.conn);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_stop(void)
{
	int status;

	status = mpd_run_stop(mpd.conn);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_seek(int sec)
{
	int status;

	if (!player.loaded || player.state == PLAYER_STATE_STOPPED) {
		PLAYER_STATUS("No track loaded");
		return PLAYER_ERR;
	}

	status = mpd_run_seek_current(mpd.conn, sec, false);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;

	mpd.seek_delay = 7;
	player_pause();

	return PLAYER_OK;
}

int
player_set_volume(unsigned int vol)
{
	int status;

	if (player.volume == -1) {
		PLAYER_STATUS("Volume control not supported");
		return PLAYER_ERR;
	}

	status = mpd_run_set_volume(mpd.conn, vol);
	if (!mpd_handle_status(status))
		return PLAYER_ERR;
	player.volume = vol;

	return PLAYER_OK;
}

