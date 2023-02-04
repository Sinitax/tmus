#include "cmd.h"

#include "data.h"
#include "list.h"
#include "player.h"
#include "ref.h"
#include "tui.h"
#include "util.h"

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

static const struct cmd *last_cmd;
static char *last_args;

static void cmd_status_from_errno(int err);

static bool cmd_save(const char *args);
static bool cmd_move(const char *args);
static bool cmd_copy(const char *args);
static bool cmd_reindex(const char *args);
static bool cmd_add_tag(const char *args);
static bool cmd_rm_tag(const char *args);
static bool cmd_rename(const char *args);

const struct cmd commands[] = {
	{ "save", cmd_save },
	{ "move", cmd_move },
	{ "copy", cmd_copy },
	{ "reindex", cmd_reindex },
	{ "addtag", cmd_add_tag },
	{ "rmtag", cmd_rm_tag },
	{ "rename", cmd_rename },
};

const size_t command_count = ARRLEN(commands);

void
cmd_status_from_errno(int err)
{
	if (err == EACCES || err == EPERM)
		USER_STATUS("Missing permissions");
	else if (err == EEXIST)
		USER_STATUS("Path already exists");
	else
		USER_STATUS("Unknown error");
}

bool
cmd_save(const char *args)
{
	data_save();
	return true;
}

bool
cmd_move(const char *name)
{
	struct link *link;
	struct track *track;
	struct tag *tag;

	tag = tag_find(name);
	if (!tag) {
		USER_STATUS("Tag not found");
		return false;
	}

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) {
		USER_STATUS("No track selected");
		return false;
	}
	track = tracks_vis_track(link);

	if (track->tag == tag) {
		USER_STATUS("Same tag");
		return false;
	}

	if (!track_move(track, tag)) {
		cmd_status_from_errno(errno);
		return false;
	}

	return true;
}

bool
cmd_copy(const char *name)
{
	struct link *link;
	struct track *track, *new;
	struct tag *tag;
	char *newpath;

	tag = tag_find(name);
	if (!tag) {
		USER_STATUS("Tag not found");
		return false;
	}

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) {
		USER_STATUS("No track selected");
		return false;
	}
	track = tracks_vis_track(link);

	if (track->tag == tag) {
		USER_STATUS("Same tag");
		return false;
	}

	newpath = aprintf("%s/%s", tag->fpath, track->name);
	if (path_exists(newpath)) {
		free(newpath);
		USER_STATUS("File already exists");
		return false;
	}

	if (!dup_file(track->fpath, newpath)) {
		free(newpath);
		USER_STATUS("Failed to copy track");
		return false;
	}
	free(newpath);

	new = track_add(tag, track->name);
	if (!new) {
		rm_file(track->fpath);
		USER_STATUS("Failed to copy track");
		return false;
	}

	return true;
}

bool
cmd_reindex(const char *name)
{
	struct track *track;
	struct link *link;
	struct tag *tag;
	struct ref *ref;
	struct list matches;
	struct tag *playing_tag;
	char *playing_name;
	bool status;

	status = false;
	playing_tag = NULL;
	playing_name = NULL;
	list_init(&matches);

	if (!*name) {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return false;
		tag = UPCAST(link, struct tag, link);
		ref = ref_alloc(tag);
		list_push_back(&matches, &ref->link);
	} else if (!strcmp(name, "*")) {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag, link);
			ref = ref_alloc(tag);
			list_push_back(&matches, &ref->link);
		}
	} else {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag, link);
			if (!strcmp(tag->name, name)) {
				ref = ref_alloc(tag);
				list_push_back(&matches, &ref->link);
				break;
			}
		}
	}

	if (list_empty(&matches))
		return false;

	/* save old playing track */
	if (player.track) {
		playing_tag = player.track->tag;
		playing_name = astrdup(player.track->name);
		player.track = NULL;
	}

	/* update each tag specified */
	for (LIST_ITER(&matches, link)) {
		ref = UPCAST(link, struct ref, link);
		if (!tracks_update(ref->data))
			goto cleanup;
	}

	/* try to find old playing track among reindexed tracks */
	if (playing_tag) {
		for (LIST_ITER(&playing_tag->tracks, link)) {
			track = UPCAST(link, struct track, link_tt);
			if (!strcmp(track->name, playing_name)) {
				player.track = track;
				break;
			}
		}
	}

	status = true;

cleanup:
	refs_free(&matches);
	free(playing_name);

	return status;
}

bool
cmd_add_tag(const char *name)
{
	struct tag *tag;

	tag = tag_find(name);
	if (tag) {
		USER_STATUS("Tag already exists");
		return false;
	}

	tag = tag_create(name);
	if (!tag) {
		USER_STATUS("Failed to create tag");
		return false;
	}

	return true;
}

bool
cmd_rm_tag(const char *name)
{
	struct link *link;
	struct tag *tag;

	if (!*name) {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return false;
		tag = UPCAST(link, struct tag, link);
	} else  {
		tag = tag_find(name);
		if (!tag) {
			USER_STATUS("No such tag");
			return false;
		}
	}

	if (!tag_rm(tag, true)) {
		USER_STATUS("Failed to remove tag");
		return false;
	}

	return true;
}

bool
cmd_rename(const char *name)
{
	struct link *link;
	struct track *track;
	struct tag *tag;

	if (!*name) {
		USER_STATUS("Supply a name");
		return false;
	}

	ASSERT(pane_sel == cmd_pane);

	if (pane_after_cmd == track_pane) {
		link = list_at(tracks_vis, track_nav.sel);
		if (!link) return false;
		track = tracks_vis_track(link);
		if (!track_rename(track, name))
			return false;
	} else if (pane_after_cmd == tag_pane) {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return false;
		tag = UPCAST(link, struct tag, link);
		if (!tag_rename(tag, name))
			return false;
	}

	return true;
}

void
cmd_init(void)
{
	last_cmd = NULL;
	last_args = NULL;
}

void
cmd_deinit(void)
{
	free(last_args);
}

bool
cmd_run(const char *query, bool *found)
{
	const char *sep, *args;
	int i, cmdlen;

	*found = false;
	sep = strchr(query, ' ');
	cmdlen = sep ? sep - query : strlen(query);
	for (i = 0; i < command_count; i++) {
		if (!strncmp(commands[i].name, query, cmdlen)) {
			last_cmd = &commands[i];
			args = sep ? sep + 1 : "";

			free(last_args);
			last_args = astrdup(args);

			*found = true;
			return commands[i].func(args);
		}
	}

	return false;
}

bool
cmd_rerun(void)
{
	if (!last_cmd || !last_args) {
		USER_STATUS("No command to repeat");
		return false;
	}
	return last_cmd->func(last_args);
}

const struct cmd *
cmd_get(const char *name)
{
	int i;

	for (i = 0; i < command_count; i++) {
		if (!strcmp(commands[i].name, name))
			return &commands[i];
	}

	return NULL;
}

const struct cmd *
cmd_find(const char *name)
{
	int i;

	for (i = 0; i < command_count; i++) {
		if (strcmp(commands[i].name, name))
			return &commands[i];
	}

	return NULL;
}
