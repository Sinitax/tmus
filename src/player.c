#include "player.h"

#include "list.h"
#include "data.h"

bool
player_history_contains(struct track *cmp, int depth)
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
playlist_track_next_unused(struct link *start)
{
	struct track *track;
	struct link *link;
	bool in_history;
	int len;

	track = NULL;

	len = list_len(&player.playlist);
	if (!len) return NULL;

	link = start;
	while (LIST_INNER(link)) {
		track = UPCAST(link, struct track, link_pl);

		in_history = player_history_contains(track, len - 2);
		if (track != player.track && !in_history)
			break;

		link = link->next;
		if (!LIST_INNER(link))
			link = list_front(&player.playlist);

		if (link == start)
			return NULL;
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

	if (player.shuffle) {
		index = rand() % list_len(&player.playlist);
		link = list_at(&player.playlist, index);
		return playlist_track_next_unused(link);
	} else {
		if (player.track && link_inuse(&player.track->link_pl)) {
			index = list_index(&player.playlist,
				&player.track->link_pl);
			if (index < 0) PANIC();
			if (++index == list_len(&player.playlist))
				return NULL;
		} else {
			index = 0;
		}
		link = list_at(&player.playlist, index);
		return UPCAST(link, struct track, link_pl);
	}

	return NULL;
}

int
player_prev(void)
{
	struct link *next;
	struct track *track;

	if (list_empty(&player.history))
		return PLAYER_STATUS_ERR;

	if (!player.track || !link_inuse(&player.track->link_hs)) {
		next = list_back(&player.history);
	} else if (LIST_INNER(player.track->link_hs.prev)) {
		next = player.track->link_hs.prev;
	} else {
		return PLAYER_STATUS_ERR;
	}

	track = UPCAST(next, struct track, link_hs);
	player_play_track(track, false);

	return PLAYER_STATUS_OK;
}

int
player_next(void)
{
	struct track *next_track;
	struct link *link;
	bool new_entry;

	if (player.track && link_inuse(&player.track->link_hs)
			&& LIST_INNER(player.track->link_hs.next)) {
		next_track = UPCAST(player.track->link_hs.next,
			struct track, link_hs);
		new_entry = false;
	} else if (!list_empty(&player.queue)) {
		link = list_pop_front(&player.queue);
		next_track = UPCAST(link, struct track, link_pq);
		new_entry = true;
	} else {
		next_track = player_next_from_playlist();
		if (!next_track) goto clear;
		new_entry = true;
	}

	player_play_track(next_track, new_entry);
	return PLAYER_STATUS_OK;

clear:
	player_clear_track();
	return PLAYER_STATUS_ERR;
}


