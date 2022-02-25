#include "player.h"
#include "ref.h"

#include "portaudio.h"
#include "sndfile.h"
#include "util.h"

#include <mpd/client.h>
#include <mpd/song.h>
#include <mpd/status.h>

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PLAYER_STATUS(lvl, ...) do { \
		player.status_lvl = PLAYER_STATUS_MSG_ ## lvl; \
		if (player.status) free(player.status); \
		player.status = aprintf(__VA_ARGS__); \
	} while (0)

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

static void player_clear_status(void);

static int mpd_handle_status(int status);

static bool history_contains(struct track *track, int depth);

static struct track *playlist_track_lru(int skip);

static void player_play_prev(void);
static void player_play_next(void);

static void player_add_history(struct track *track);

void
player_clear_status(void)
{
	free(player.status);
	player.status = NULL;
	player.status_lvl = PLAYER_STATUS_MSG_NONE;
}

int
mpd_handle_status(int status)
{
	const char *errstr;

	player_clear_status();

	switch (status) {
	case MPD_ERROR_SERVER:
	case MPD_ERROR_ARGUMENT:
		if (!mpd_connection_clear_error(mpd.conn))
			ERROR("PLAYER: Failed to recover from argument error");
	case MPD_ERROR_SYSTEM:
		errstr = mpd_connection_get_error_message(mpd.conn);
		PLAYER_STATUS(ERR, "ERR - %s", errstr);
		return 1;
	case MPD_ERROR_CLOSED:
		ERROR("PLAYER: Connection abruptly closed");
	}

	return 0;
}

bool
history_contains(struct track *track, int depth)
{
	struct link *link;
	struct ref *ref;

	link = list_back(&player.history);
	while (LIST_INNER(link) && depth-- > 0) {
		ref = UPCAST(link, struct ref);
		if (track == ref->data)
			return true;
		link = link->prev;
	}

	return false;
}

struct track *
playlist_track_lru(int skip)
{
	struct track *track;
	struct link *link;
	struct ref *ref;
	int len;

	track = NULL;

	len = list_len(&player.playlist);
	link = list_front(&player.playlist);
	while (skip >= 0 && LIST_INNER(link)) {
		ref = UPCAST(link, struct ref);
		track = ref->data;

		if (!history_contains(track, len - 1))
			skip -= 1;

		if (skip <= 0) break;

		link = link->next;
		if (!LIST_INNER(link))
			link = list_front(&player.playlist);
	}

	PLAYER_STATUS(INFO, "%i, %i", len - 1, skip);

	return track;
}

void
player_play_prev(void)
{
	struct link *link, *next;
	struct track *track;
	struct ref *ref;

	if (list_empty(&player.history))
		return;

	if (!player.history_sel) {
		next = list_back(&player.history);
	} else if (LIST_INNER(player.history_sel->prev)) {
		next = player.history_sel->prev;
	} else {
		return;
	}

	ref = UPCAST(next, struct ref);
	player_play_track(ref->data);

	player.history_sel = next;
}

void
player_play_next(void)
{
	struct link *link;
	struct track *track, *next_track;
	struct ref *ref;
	int index, len;
	int status;

	next_track = NULL;

	link = player.history_sel;
	if (link && LIST_INNER(link->next)) {
		player.history_sel = link->next;
		ref = UPCAST(link->next, struct ref);
		next_track = ref->data;
	} else {
		if (!list_empty(&player.queue)) {
			link = list_pop_front(&player.queue);
			ref = UPCAST(link, struct ref);
			next_track = ref->data;
			ref_free(ref);
		} else {
			if (list_empty(&player.playlist))
				return;

			if (!player.history_sel) {
				player_add_history(player.track);
				player.track = NULL;
			}
			player.history_sel = NULL;

			if (player.shuffle) {
				index = rand() % list_len(&player.playlist);
				next_track = playlist_track_lru(index + 1);
				ASSERT(next_track != NULL);
			} else {
				link = player.playlist_sel;
				if (link && LIST_INNER(link->next)) {
					ref = UPCAST(link->next, struct ref);
					next_track = ref->data;
				} else {
					status = mpd_run_clear(mpd.conn);
					mpd_handle_status(status);
					return;
				}
			}
		}
	}

	player_play_track(next_track);
}

void
player_add_history(struct track *track)
{
	struct link *link;
	struct ref *ref;

	link = list_back(&player.history);
	if (link) {
		ref = UPCAST(link, struct ref);
		if (ref->data == track) return;
	}

	ref = ref_init(track);
	list_push_back(&player.history, LINK(ref));
}

void
player_init(void)
{
	mpd.conn = NULL;
	mpd.action = PLAYER_ACTION_NONE;
	mpd.seek_delay = 0;

	list_init(&player.history);
	player.history_sel = NULL;
	list_init(&player.queue);
	player.track = NULL;
	list_init(&player.playlist);
	player.playlist_sel = NULL;
	player.loaded = 0;
	player.autoplay = true;
	player.shuffle = true;
	player.state = PLAYER_STATE_PAUSED;
	player.volume = 0;
	player.time_pos = 0;
	player.time_end = 0;
	player.status = NULL;
	player.status_lvl = PLAYER_STATUS_MSG_INFO;
}

void
player_deinit(void)
{
	struct link *iter;

	if (!mpd.conn) return;

	refs_free(&player.queue);
	refs_free(&player.history);
	refs_free(&player.playlist);
	if (player.status) free(player.status);

	if (mpd.conn) mpd_connection_free(mpd.conn);
}

void
player_update(void)
{
	struct mpd_status *status;
	struct mpd_song *current_song;
	struct ref *ref;
	bool queue_empty;

	if (!mpd.conn) {
		mpd.conn = mpd_connection_new(NULL, 0, 0);
		if (!mpd.conn) ERROR("MPD: Connect to server failed\n");
	}

	status = mpd_run_status(mpd.conn);
	if (!status) {
		PLAYER_STATUS(ERR, "Resetting MPD server connection");
		mpd_connection_free(mpd.conn);
		mpd.conn = NULL;
		return;
	}

	current_song = mpd_run_current_song(mpd.conn);
	if (!current_song) {
		if (player.track && !player.history_sel) {
			player_add_history(player.track);
			player.track = NULL;
		}

		queue_empty = list_empty(&player.queue);
		if (player.loaded && player.autoplay || !queue_empty)
			mpd.action = PLAYER_ACTION_PLAY_NEXT;
	} else {
		mpd_song_free(current_song);
	}

	mpd_status_free(status);

	if (mpd.action != PLAYER_ACTION_NONE) {
		switch (mpd.action) {
		case PLAYER_ACTION_PLAY_PREV:
			player_play_prev();
			break;
		case PLAYER_ACTION_PLAY_NEXT:
			player_play_next();
			break;
		default:
			PANIC();
		}

		mpd.action = PLAYER_ACTION_NONE;
	}

	status = mpd_run_status(mpd.conn);
	if (!status) {
		PLAYER_STATUS(ERR, "Resetting MPD server connection");
		mpd_connection_free(mpd.conn);
		mpd.conn = NULL;
		return;
	}

	current_song = mpd_run_current_song(mpd.conn);
	if (current_song) {
		player.loaded = true;
		player.time_pos = mpd_status_get_elapsed_time(status);
		player.time_end = mpd_song_get_duration(current_song);
		mpd_song_free(current_song);
	} else {
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
		PANIC();
	}

	player.volume = mpd_status_get_volume(status);

	if (mpd.seek_delay) {
		mpd.seek_delay -= 1;
		if (!mpd.seek_delay) player_play();
	}

	mpd_status_free(status);
}

int
player_play_track(struct track *track)
{
	struct link *link;
	struct ref *ref;
	int status;

	status = mpd_run_clear(mpd.conn);
	mpd_handle_status(status);

	status = mpd_run_add(mpd.conn, track->fpath);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	status = mpd_run_play(mpd.conn);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	if (player.track && !player.history_sel) {
		player_add_history(player.track);
		player.track = NULL;
	}

	player.history_sel = NULL;
	/* re-assigning history_sel done in calling code */

	player.playlist_sel = NULL;
	for (LIST_ITER(&player.playlist, link)) {
		ref = UPCAST(link, struct ref);
		if (ref->data == track) {
			player.playlist_sel = link;
			break;
		}
	}

	player.track = track;

	return PLAYER_STATUS_OK;
}

int
player_toggle_pause(void)
{
	int status;

	status = mpd_run_toggle_pause(mpd.conn);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	return PLAYER_STATUS_OK;
}

int
player_pause(void)
{
	int status;

	status = mpd_run_pause(mpd.conn, true);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	return PLAYER_STATUS_OK;
}

int
player_resume(void)
{
	int status;

	status = mpd_run_pause(mpd.conn, false);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	return PLAYER_STATUS_OK;
}

int
player_next(void)
{
	mpd.action = PLAYER_ACTION_PLAY_NEXT;

	return PLAYER_STATUS_OK;
}

int
player_prev(void)
{
	mpd.action = PLAYER_ACTION_PLAY_PREV;

	return PLAYER_STATUS_OK;
}

int
player_play(void)
{
	int status;

	status = mpd_run_play(mpd.conn);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	return PLAYER_STATUS_OK;
}

int
player_stop(void)
{
	int status;

	status = mpd_run_stop(mpd.conn);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	return PLAYER_STATUS_OK;
}

int
player_seek(int sec)
{
	int status;

	player_clear_status();
	if (!player.loaded || player.state == PLAYER_STATE_STOPPED) {
		PLAYER_STATUS(ERR, "No track loaded");
		return PLAYER_STATUS_ERR;
	}

	status = mpd_run_seek_current(mpd.conn, sec, false);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	mpd.seek_delay = 7;
	player_pause();

	return PLAYER_STATUS_OK;
}

int
player_set_volume(unsigned int vol)
{
	int status;

	player_clear_status();
	if (player.volume == -1) {
		PLAYER_STATUS(ERR, "Volume control not supported");
		return PLAYER_STATUS_ERR;
	}

	status = mpd_run_set_volume(mpd.conn, vol);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	return PLAYER_STATUS_OK;
}

