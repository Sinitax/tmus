#include "cmd.h"

#include "data.h"
#include "list.h"
#include "player.h"
#include "ref.h"
#include "track.h"
#include "tui.h"
#include "util.h"

#include <stdbool.h>

#define CMD_ERROR(...) do { \
		free(cmd_status); \
		cmd_status = awprintf(__VA_ARGS__); \
		return false; \
	} while (0)

static bool cmd_save(const wchar_t *args);
static bool cmd_move(const wchar_t *args);
static bool cmd_add(const wchar_t *args);

const struct cmd commands[] = {
	{ L"save", cmd_save },
	{ L"move", cmd_move },
	{ L"add", cmd_add },
};

const size_t command_count = ARRLEN(commands);

wchar_t *cmd_status;

void
cmd_init(void)
{
	cmd_status = NULL;
}

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
	if (!tag) CMD_ERROR(L"Tag not found");

	link = list_at(tracks_vis, track_nav.sel);
	if (!link) CMD_ERROR(L"No track selected");
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

	link = list_at(&player->playlist, track_nav.sel);
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
