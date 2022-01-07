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

static void player_clear_msg(void);
static int handle_mpd_status(int status);

static void
player_clear_msg(void)
{
	free(player->msg);
	player->msg = NULL;
	player->msglvl = PLAYER_MSG_NONE;
}

static int
handle_mpd_status(int status)
{
	player_clear_msg();
	switch (status) {
	case MPD_ERROR_SERVER:
	case MPD_ERROR_ARGUMENT:
		if (!mpd_connection_clear_error(player->conn))
			PANIC("Player failed to recover from error");
	case MPD_ERROR_SYSTEM:
		PLAYER_STATUS(PLAYER_MSG_ERR, "%s",
			mpd_connection_get_error_message(player->conn));
		return 1;
	case MPD_ERROR_CLOSED:
		PANIC("Player encountered non-recoverable error");
	}
	return 0;
}

void
player_init(void)
{
	player = malloc(sizeof(struct player));
	ASSERT(player != NULL);

	player->conn = mpd_connection_new(NULL, 0, 0);
	ASSERT(player->conn != NULL);

	player->queue = LIST_HEAD;
	player->history = LIST_HEAD;

	player->playlist = LIST_HEAD;

	player->track = NULL;
	player->state = PLAYER_STATE_PAUSED;

	player->autoplay = 0;

	player->shuffle = 0;

	player->action = PLAYER_ACTION_NONE;

	player->seek_delay = 0;

	player->volume = 0;
	player->time_pos = 0;
	player->time_end = 0;

	player->msg = NULL;
	player->msglvl = PLAYER_MSG_INFO;
}

void
player_free(void)
{
	struct link *iter;

	if (!player->conn) return;

	refs_free(&player->history);
	refs_free(&player->queue);

	//mpd_run_clear(player->conn);
	mpd_connection_free(player->conn);
}

void
player_play_next(struct track *prev)
{
	struct link *iter;
	struct track *track;
	int index;

	if (list_empty(&player->playlist))
		return;

	iter = NULL;
	if (player->shuffle) {
		/* TODO better algorithm for random sequence */
		index = rand() % list_len(&player->playlist);
		iter = link_iter(player->playlist.next, index);
		ASSERT(iter != NULL);
	} else if (player->loaded) {
		iter = player->playlist.next;
		for (; iter; iter = iter->next) {
			track = UPCAST(iter, struct ref)->data;
			if (track == prev)
				break;
		}
		if (iter) iter = iter->next;
	}

	if (!iter) iter = player->playlist.next;
	track = UPCAST(iter, struct ref)->data;

	player_play_track(track);
}

void
player_update(void)
{
	struct mpd_status *status;
	struct mpd_song *song;
	struct ref *ref;
	const char *tmp;

	status = mpd_run_status(player->conn);
	ASSERT(status != NULL);

	if (!mpd_run_current_song(player->conn)) {
		/* if autoplay and another track just finished,
		 * or there are tracks in queue to be played */
		if (player->track && player->autoplay
				|| !list_empty(&player->queue)) {
			player->action = PLAYER_ACTION_PLAY_NEXT;
		}
	}

	free(status);

	if (player->action != PLAYER_ACTION_NONE) {
		handle_mpd_status(mpd_run_clear(player->conn));

		ref = NULL;
		switch (player->action) {
		case PLAYER_ACTION_PLAY_PREV:
			if (list_empty(&player->history))
				break;
			ref = UPCAST(list_pop_front(&player->history),
				struct ref);

			/* TODO keep index instead until new track is played */
			/* TODO create slimmer player_backend interface */

			/* dont add current song to history */
			player->track = NULL;

			player_play_track(ref->data);
			ref_free(ref);
			break;
		case PLAYER_ACTION_PLAY_NEXT:
			if (!list_empty(&player->queue)) {
				ref = UPCAST(list_pop_front(&player->queue),
					struct ref);
				player_play_track(ref->data);
				ref_free(ref);
			} else {
				player_play_next(player->track);
			}
			break;
		default:
			PANIC();
		}
		player->action = PLAYER_ACTION_NONE;
	}

	/* TODO move prev / next handling to own functions */

	status = mpd_run_status(player->conn);
	ASSERT(status != NULL);

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
		PANIC();
	}
	player->volume = mpd_status_get_volume(status);

	if (player->seek_delay) {
		player->seek_delay--;
		if (!player->seek_delay)
			player_play();
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
	struct link *link;
	int status;

	if (player->track && player->track != track) {
		list_push_front(&player->history,
			LINK(ref_init(player->track)));
	}

	handle_mpd_status(mpd_run_clear(player->conn));

	status = mpd_run_add(player->conn, track->fpath);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	status = mpd_run_play(player->conn);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	player->track = track;

	return PLAYER_OK;
}

int
player_toggle_pause(void)
{
	int status;

	status = mpd_run_toggle_pause(player->conn);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_pause(void)
{
	int status;

	status = mpd_run_pause(player->conn, true);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_resume(void)
{
	int status;

	status = mpd_run_pause(player->conn, false);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_next(void)
{
	player->action = PLAYER_ACTION_PLAY_NEXT;

	return PLAYER_OK;
}

int
player_prev(void)
{
	player->action = PLAYER_ACTION_PLAY_PREV;

	return PLAYER_OK;
}

int
player_play(void)
{
	int status;

	status = mpd_run_play(player->conn);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_stop(void)
{
	int status;

	status = mpd_run_stop(player->conn);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

int
player_seek(int sec)
{
	int status;

	player_clear_msg();
	if (!player->loaded || player->state == PLAYER_STATE_STOPPED) {
		PLAYER_STATUS(PLAYER_MSG_INFO, "No track loaded");
		return PLAYER_ERR;
	}

	status = mpd_run_seek_current(player->conn, sec, false);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	player->seek_delay = 7;
	player_pause();

	return PLAYER_OK;
}

int
player_set_volume(unsigned int vol)
{
	int status;

	player_clear_msg();
	if (player->volume == -1) {
		PLAYER_STATUS(PLAYER_MSG_INFO, "Volume control not supported");
		return PLAYER_ERR;
	}

	status = mpd_run_set_volume(player->conn, vol);
	if (handle_mpd_status(status))
		return PLAYER_ERR;

	return PLAYER_OK;
}

