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
history_contains(struct track *cmp, int depth)
{
	struct link *link;
	struct track *track;

	link = list_back(&player.history);
	while (LIST_INNER(link) && depth-- > 0) {
		track = UPCAST(link, struct track, link_hs);
		if (track == cmp)
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
	int len;

	track = NULL;

	len = list_len(&player.playlist);
	link = list_front(&player.playlist);
	while (skip >= 0 && LIST_INNER(link)) {
		track = UPCAST(link, struct track, link_pl);

		if (!history_contains(track, len - 1))
			skip -= 1;

		if (skip <= 0) break;

		link = link->next;
		if (!LIST_INNER(link))
			link = list_front(&player.playlist);
	}

	return track;
}

struct track *
player_next_from_playlist(void)
{
	struct link *link;
	int index;

	if (list_empty(&player.playlist))
		return NULL;

	if (player.track)
		player_add_history(player.track);

	if (player.shuffle) {
		index = rand() % list_len(&player.playlist);
		return playlist_track_lru(index);
	} else {
		if (player.track && link_inuse(&player.track->link_pl)) {
			link = player.track->link_pl.next;
			if (!link) list_front(&player.playlist);
		} else {
			link = list_front(&player.playlist);
		}
		return UPCAST(link, struct track, link_pl);
	}

	return NULL;
}

void
player_play_prev(void)
{
	struct link *next;
	struct track *track;

	if (list_empty(&player.history))
		return;

	if (!player.track || !link_inuse(&player.track->link_hs)) {
		next = list_back(&player.history);
	} else if (LIST_INNER(player.track->link_hs.prev)) {
		next = player.track->link_hs.prev;
	} else {
		return;
	}

	track = UPCAST(next, struct track, link_hs);
	player_play_track(track);
}

void
player_play_next(void)
{
	struct track *next_track;
	struct link *link;

	next_track = NULL;

	if (player.track && link_inuse(&player.track->link_hs)
			&& LIST_INNER(player.track->link_hs.next)) {
		next_track = UPCAST(player.track->link_hs.next,
			struct track, link_hs);
	} else if (!list_empty(&player.queue)) {
		link = list_pop_front(&player.queue);
		next_track = UPCAST(link, struct track, link_pq);
	} else {
		next_track = player_next_from_playlist();
	}

	if (!next_track) goto clear;

	player_play_track(next_track);
	return;

clear:
	player.track = NULL;
	mpd_run_clear(mpd.conn);
}

void
player_add_history(struct track *new)
{
	struct link *link;
	struct track *track;

	link = list_back(&player.history);
	if (link) {
		track = UPCAST(link, struct track, link_hs);
		if (track == new) return;
	}

	link_pop(&new->link_hs);
	list_push_back(&player.history, &new->link_hs);
}

void
player_init(void)
{
	mpd.conn = NULL;
	mpd.action = PLAYER_ACTION_NONE;
	mpd.seek_delay = 0;

	list_init(&player.playlist);
	list_init(&player.history);
	list_init(&player.queue);

	player.track = NULL;

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
	list_clear(&player.playlist);
	list_clear(&player.queue);
	list_clear(&player.history);
	if (player.status) free(player.status);

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
		if (player.track)
			player_add_history(player.track);

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
	int status;

	status = mpd_run_clear(mpd.conn);
	mpd_handle_status(status);

	status = mpd_run_add(mpd.conn, track->fpath);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	status = mpd_run_play(mpd.conn);
	if (mpd_handle_status(status))
		return PLAYER_STATUS_ERR;

	/* add last track to history */
	if (player.track && !link_inuse(&player.track->link_hs))
		player_add_history(player.track);

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

