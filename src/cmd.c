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

#define CMD_ERROR(...) do { \
		CMD_SET_STATUS(__VA_ARGS__); \
		return false; \
	} while (0)

static const struct cmd *last_cmd;
static wchar_t *last_args;

static bool cmd_save(const wchar_t *args);
static bool cmd_move(const wchar_t *args);
static bool cmd_add(const wchar_t *args);
static bool cmd_reindex(const wchar_t *args);

const struct cmd commands[] = {
	{ L"save", cmd_save },
	{ L"move", cmd_move },
	{ L"add", cmd_add },
	{ L"reindex", cmd_reindex },
};

const size_t command_count = ARRLEN(commands);

bool
cmd_save(const wchar_t *args)
{
	data_save();
	return 0;
}

bool
cmd_move(const wchar_t *name)
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

	newpath = aprintf("%s/%s", tag->fpath, track->fname);
	OOM_CHECK(newpath);

	move_file(track->fpath, newpath);
	free(track->fpath);
	track->fpath = newpath;

	link_pop(link);
	list_push_back(&tag->tracks, link);

	return 1;
}

bool
cmd_add(const wchar_t *name)
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

	newpath = aprintf("%s/%s", tag->fpath, track->fname);
	OOM_CHECK(newpath);

	copy_file(track->fpath, newpath);
	track->fpath = newpath;

	track = track_alloc(tag->fpath, track->fname, get_fid(tag->fpath));
	ref = ref_init(track);
	list_push_back(&tag->tracks, &ref->link);

	return 1;
}

bool
cmd_reindex(const wchar_t *name)
{
	struct link *link;
	struct tag *tag;
	struct list matches;

	list_init(&matches);

	if (!*name) {
		link = list_at(&tags, tag_nav.sel);
		tag = UPCAST(link, struct tag);
		if (tag == NULL) return false;
		list_push_back(&matches, LINK(ref_init(tag)));
	} else if (!wcscmp(name, L"*")) {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag);
			list_push_back(&matches, LINK(ref_init(tag)));
		}
	} else {
		for (LIST_ITER(&tags, link)) {
			tag = UPCAST(link, struct tag);
			if (!wcscmp(tag->name, name)) {
				list_push_back(&matches, LINK(ref_init(tag)));
				break;
			}
		}
	}

	if (list_empty(&matches)) return false;

	for (LIST_ITER(&matches, link)) {
		tag = UPCAST(link, struct ref)->data;
		index_update(tag);
	}

	refs_free(&matches);

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
cmd_run(const wchar_t *query)
{
	const wchar_t *sep, *args;
	int i, cmdlen;
	bool success;

	sep = wcschr(query, L' ');
	cmdlen = sep ? sep - query : wcslen(query);
	for (i = 0; i < command_count; i++) {
		if (!wcsncmp(commands[i].name, query, cmdlen)) {
			last_cmd = &commands[i];
			args = sep ? sep + 1 : L"";
			last_args = wcsdup(args);
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
cmd_get(const wchar_t *name)
{
	int i;

	for (i = 0; i < command_count; i++) {
		if (!wcscmp(commands[i].name, name))
			return &commands[i];
	}

	return NULL;
}

const struct cmd *
cmd_find(const wchar_t *name)
{
	int i;

	for (i = 0; i < command_count; i++) {
		if (wcsstr(commands[i].name, name))
			return &commands[i];
	}

	return NULL;
}
