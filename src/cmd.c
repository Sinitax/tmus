#include "cmd.h"

#include "data.h"
#include "list.h"
#include "player.h"
#include "ref.h"
#include "tag.h"
#include "track.h"
#include "tui.h"
#include "util.h"

#include <stdbool.h>
#include <string.h>

#define CMD_ERROR(...) do { \
		CMD_SET_STATUS(__VA_ARGS__); \
		return false; \
	} while (0)

static const struct cmd *last_cmd;
static char *last_args;

static bool cmd_save(const char *args);
static bool cmd_move(const char *args);
static bool cmd_copy(const char *args);
static bool cmd_reindex(const char *args);
static bool cmd_add_tag(const char *args);
static bool cmd_rm_tag(const char *args);

const struct cmd commands[] = {
	{ "save", cmd_save },
	{ "move", cmd_move },
	{ "copy", cmd_copy },
	{ "reindex", cmd_reindex },
	{ "addtag", cmd_add_tag },
	{ "rmtag", cmd_rm_tag },
};

const size_t command_count = ARRLEN(commands);

bool
cmd_save(const char *args)
{
	data_save();
	return 0;
}

bool
cmd_move(const char *name)
{
	struct link *link;
	struct track *track;
	struct tag *tag;
	char *newpath;

	tag = tag_find(name);
	if (!tag) CMD_ERROR("Tag not found");

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) CMD_ERROR("No track selected");
	track = UPCAST(link, struct ref)->data;

	newpath = aprintf("%s/%s", tag->fpath, track->name);
	OOM_CHECK(newpath);

	if (!move_file(track->fpath, newpath))
		CMD_ERROR("Failed to move file");
	free(track->fpath);
	track->fpath = newpath;

	link_pop(link);
	list_push_back(&tag->tracks, link);

	playlist_update();

	return 1;
}

bool
cmd_copy(const char *name)
{
	struct link *link;
	struct track *track;
	struct ref *ref;
	struct tag *tag;
	char *newpath;

	tag = tag_find(name);
	if (!tag) return 0;

	link = list_at(&player.playlist, track_nav.sel);
	if (!link) return 0;
	track = UPCAST(link, struct ref)->data;

	newpath = aprintf("%s/%s", tag->fpath, track->name);
	OOM_CHECK(newpath);

	copy_file(track->fpath, newpath);
	track->fpath = newpath;

	track = track_alloc(tag->fpath, track->name, get_fid(tag->fpath));
	ref = ref_alloc(track);
	list_push_back(&tag->tracks, &ref->link);

	return 1;
}

bool
cmd_reindex(const char *name)
{
	struct link *link;
	struct tag *tag;
	struct list matches;

	list_init(&matches);

	if (!*name) {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return false;
		tag = UPCAST(link, struct tag);
		list_push_back(&matches, LINK(ref_alloc(tag)));
	} else if (!strcmp(name, "*")) {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag);
			list_push_back(&matches, LINK(ref_alloc(tag)));
		}
	} else {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag);
			if (!strcmp(tag->name, name)) {
				list_push_back(&matches, LINK(ref_alloc(tag)));
				break;
			}
		}
	}

	if (list_empty(&matches)) return false;

	for (LIST_ITER(&matches, link)) {
		tag = UPCAST(link, struct ref)->data;
		if (!tracks_update(tag))
			return false;
	}

	refs_free(&matches);

	return true;
}

bool
cmd_add_tag(const char *name)
{
	struct link *link;
	struct tag *tag;
	char *fname, *fpath;

	for (LIST_ITER(&tags, link)) {
		tag = UPCAST(link, struct tag);
		if (!strcmp(tag->name, name))
			CMD_ERROR("Tag already exists");
	}

	fname = aprintf("%s", name);
	OOM_CHECK(fname);

	fpath = aprintf("%s/%s", datadir, fname);
	OOM_CHECK(fpath);

	if (!make_dir(fpath)) {
		CMD_SET_STATUS("Failed to create dir");
		free(fname);
		free(fpath);
		return false;
	}

	tag = tag_alloc(datadir, fname);
	OOM_CHECK(tag);

	list_push_back(&tags, LINK(tag));

	free(fname);
	free(fpath);

	return true;
}

bool
cmd_rm_tag(const char *name)
{
	struct link *link;
	struct tag *tag;
	char *fname, *fpath;

	if (!*name) {
		link = list_at(&tags, tag_nav.sel);
		if (!link) return false;
		tag = UPCAST(link, struct tag);
	} else  {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag);
			if (!strcmp(tag->name, name))
				break;
		}

		if (!LIST_INNER(link))
			CMD_ERROR("No such tag");
	}

	if (!rm_dir(tag->fpath, true)) {
		tracks_update(tag); /* in case some deleted, some not */
		CMD_ERROR("Failed to remove dir");
	}

	link_pop(LINK(tag));
	tag_free(tag);

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
	bool success;

	*found = false;
	sep = strchr(query, ' ');
	cmdlen = sep ? sep - query : strlen(query);
	for (i = 0; i < command_count; i++) {
		if (!strncmp(commands[i].name, query, cmdlen)) {
			last_cmd = &commands[i];
			args = sep ? sep + 1 : "";

			free(last_args);
			last_args = strdup(args);
			OOM_CHECK(last_args);

			*found = true;
			return commands[i].func(args);
		}
	}

	return false;
}

bool
cmd_rerun(void)
{
	if (!last_cmd || !last_args)
		CMD_ERROR("No command to repeat");
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
