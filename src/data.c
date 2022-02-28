#include "data.h"

#include "player.h"
#include "list.h"
#include "log.h"
#include "tui.h"

#include <fts.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdbool.h>
#include <string.h>

const char *datadir;

struct list tracks; /* struct track (link) */
struct list tags; /* struct track (link) */
struct list tags_sel; /* struct tag (link_sel) */

static int get_fid(const char *path);

static struct tag *tag_alloc(const char *path, const char *fname);
static void tag_free(struct tag *tag);

static struct track *track_alloc(const char *path, const char *fname);
static void track_free(struct track *t);
static int track_name_compare(struct link *a, struct link *b);

static void tracks_load(struct tag *tag);
static void tracks_save(struct tag *tag);

int
get_fid(const char *path)
{
	struct stat st;
	return stat(path, &st) ? -1 : st.st_ino;
}

struct tag *
tag_alloc(const char *path, const char *fname)
{
	struct tag *tag;

	tag = malloc(sizeof(struct tag));
	ASSERT(tag != NULL);

	tag->fpath = aprintf("%s/%s", path, fname);
	ASSERT(tag->fpath != NULL);

	tag->name = strdup(fname);
	ASSERT(tag->name != NULL);

	tag->link = LINK_EMPTY;
	tag->link_sel = LINK_EMPTY;

	list_init(&tag->tracks);

	return tag;
}

void
tag_free(struct tag *tag)
{
	free(tag->fpath);
	free(tag->name);
	list_clear(&tag->tracks);
	free(tag);
}

struct track *
track_alloc(const char *dir, const char *fname)
{
	struct track *track;

	track = malloc(sizeof(struct track));
	ASSERT(track != NULL);

	track->fpath = aprintf("%s/%s", dir, fname);
	ASSERT(track->fpath != NULL);

	track->name = strdup(fname);
	ASSERT(track->name != NULL);

	track->tag = NULL;

	track->link = LINK_EMPTY;
	track->link_pl = LINK_EMPTY;
	track->link_tt = LINK_EMPTY;
	track->link_pq = LINK_EMPTY;
	track->link_hs = LINK_EMPTY;

	return track;
}

void
track_free(struct track *t)
{
	free(t->fpath);
	free(t->name);
	free(t);
}

int
track_name_compare(struct link *a, struct link *b)
{
	struct track *ta, *tb;

	ta = UPCAST(a, struct track, link);
	tb = UPCAST(b, struct track, link);

	return strcmp(ta->name, tb->name);
}

void
tracks_load(struct tag *tag)
{
	char linebuf[1024];
	char *index_path;
	FILE *file;

	index_path = aprintf("%s/index", tag->fpath);
	OOM_CHECK(index_path);

	file = fopen(index_path, "r");
	if (file == NULL) {
		index_update(tag); /* create index */
		file = fopen(index_path, "r");
		ASSERT(file != NULL);
	}

	while (fgets(linebuf, sizeof(linebuf), file)) {
		if (!*linebuf) continue;
		if (linebuf[strlen(linebuf) - 1] == '\n')
			linebuf[strlen(linebuf) - 1] = '\0';
		track_add(tag, linebuf);
	}

	fclose(file);
	free(index_path);
}

void
tracks_save(struct tag *tag)
{
	struct track *track;
	struct link *link;
	char *index_path;
	FILE *file;

	/* write playlist back to index file */

	index_path = aprintf("%s/index", tag->fpath);
	OOM_CHECK(index_path);

	file = fopen(index_path, "w+");
	if (!file) {
		fprintf(stderr, "Failed to write to index file: %s\n",
			index_path);
		free(index_path);
		return;
	}

	for (LIST_ITER(&tag->tracks, link)) {
		track = UPCAST(link, struct track, link_tt);
		fprintf(file, "%s\n", track->name);
	}

	fclose(file);
	free(index_path);
}

bool
make_dir(const char *path)
{
	return mkdir(path, S_IRWXU | S_IRWXG) == 0;
}

bool
rm_dir(const char *path, bool recursive)
{
	char *files[] = { (char *) path, NULL };
	FTSENT *ent;
	FTS *fts;
	int flags;

	if (recursive) {
		flags = FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV;
		fts = fts_open(files, flags, NULL);
		if (!fts) return false;

		while ((ent = fts_read(fts))) {
			switch (ent->fts_info) {
			case FTS_NS:
			case FTS_DNR:
			case FTS_ERR:
				fts_close(fts);
				return false;
			case FTS_D:
				break;
			default:
				if (remove(ent->fts_accpath) < 0) {
					fts_close(fts);
					return false;
				}
				break;
			}
		}

		fts_close(fts);
	} else {
		if (rmdir(path) != 0)
			return false;
	}

	return true;
}

bool
rm_file(const char *path)
{
	return unlink(path) == 0;
}

bool
copy_file(const char *src, const char *dst)
{
	FILE *in, *out;
	char buf[4096];
	int nread;
	bool ok;

	ok = false;
	in = out = NULL;

	in = fopen(src, "r");
	if (in == NULL) goto cleanup;

	out = fopen(dst, "w+");
	if (out == NULL) goto cleanup;

	while ((nread = fread(buf, 1, sizeof(buf), in)) > 0) {
		fwrite(buf, 1, nread, out);
	}

	if (nread < 0)
		goto cleanup;

	ok = true;

cleanup:
	if (in) fclose(in);
	if (out) fclose(out);

	return ok;
}

bool
dup_file(const char *src, const char *dst)
{
	/* TODO add option for hard link dup if possible */
	return copy_file(src, dst);
}

bool
move_file(const char *src, const char *dst)
{
	return rename(src, dst) == 0;
}

void
index_update(struct tag *tag)
{
	struct dirent *ent;
	char *index_path;
	FILE *file;
	DIR *dir;

	dir = opendir(tag->fpath);
	if (!dir) ERROR("Failed to access dir: %s\n", tag->fpath);

	index_path = aprintf("%s/index", tag->fpath);
	OOM_CHECK(index_path);

	file = fopen(index_path, "w+");
	if (!file) ERROR("Failed to create index file in dir %s\n", tag->name);
	free(index_path);

	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		/* skip files without extension */
		if (!strchr(ent->d_name + 1, '.'))
			continue;

		fprintf(file, "%s\n", ent->d_name);
	}

	closedir(dir);
	fclose(file);
}

bool
tracks_update(struct tag *tag)
{
	struct link *link;
	struct track *track;
	struct dirent *ent;
	DIR *dir;

	dir = opendir(tag->fpath);
	if (!dir) return false;

	while (!list_empty(&tag->tracks)) {
		link = list_pop_front(&tag->tracks);
		track = UPCAST(link, struct track, link_tt);
		track_rm(track, false);
	}

	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		/* skip files without extension */
		if (!strchr(ent->d_name + 1, '.'))
			continue;

		track_add(tag, ent->d_name);
	}

	closedir(dir);

	return true;
}

struct track *
tracks_vis_track(struct link *link)
{
	if (tracks_vis == &player.playlist) {
		return UPCAST(link, struct track, link_pl);
	} else {
		return UPCAST(link, struct track, link_tt);
	}
}

void
playlist_clear(void)
{
	while (!list_empty(&player.playlist))
		list_pop_front(&player.playlist);
}

void
playlist_update(bool exec)
{
	static bool update = false;
	struct link *link, *link2;
	struct track *track;
	struct tag *tag;

	if (!exec) {
		update = true;
		return;
	}

	if (!update) return;
	update = false;

	playlist_clear();

	for (LIST_ITER(&tags_sel, link)) {
		tag = UPCAST(link, struct tag, link_sel);
		for (LIST_ITER(&tag->tracks, link2)) {
			track = UPCAST(link2, struct track, link_tt);
			link_pop(&track->link_pl);
			list_push_back(&player.playlist, &track->link_pl);
		}
	}
}

struct tag *
tag_add(const char *fname)
{
	struct tag *tag;

	/* add to tags list */
	tag = tag_alloc(datadir, fname);
	OOM_CHECK(tag);
	list_push_back(&tags, &tag->link);

	return tag;
}

struct tag *
tag_find(const char *name)
{
	struct link *link;
	struct tag *tag;

	for (LIST_ITER(&tags, link)) {
		tag = UPCAST(link, struct tag, link);
		if (!strcmp(tag->name, name))
			return tag;
	}

	return NULL;
}

bool
tag_rm(struct tag *tag, bool sync_fs)
{
	struct link *link;
	struct track *track;

	/* remove contained tracks */
	while (!list_empty(&tag->tracks)) {
		link = list_pop_front(&tag->tracks);
		track = UPCAST(link, struct track, link_tt);
		if (!track_rm(track, sync_fs))
			return false;
	}

	/* delete dir and remaining non-track files */
	if (sync_fs && !rm_dir(tag->fpath, true))
		return false;

	/* remove from selected */
	link_pop(&tag->link_sel);

	/* remove from tags list */
	link_pop(&tag->link);

	tag_free(tag);

	return true;
}

struct track *
track_add(struct tag *tag, const char *fname)
{
	struct track *track;

	track = track_alloc(tag->fpath, fname);
	OOM_CHECK(track);

	track->tag = tag;

	/* insert track into sorted tracks list */
	list_insert_sorted(&tracks, &track->link, track_name_compare);

	/* add to tag's tracks list */
	list_push_back(&tag->tracks, &track->link_tt);

	/* if track's tag is selected, update playlist */
	if (link_inuse(&tag->link_sel))
		playlist_update(false);

	return track;
}

bool
track_rm(struct track *track, bool sync_fs)
{
	if (sync_fs && !rm_file(track->fpath))
		return false;

	/* remove from tracks list */
	link_pop(&track->link);

	/* remove from tag's track list */
	link_pop(&track->link_tt);

	/* remove from playlist */
	link_pop(&track->link_pl);

	/* remove from player queue */
	link_pop(&track->link_pq);

	/* remove the reference as last used track */
	if (player.track == track)
		player.track = NULL;

	track_free(track);

	return true;
}

void
data_load(void)
{
	struct dirent *ent;
	struct tag *tag;
	struct stat st;
	char *path;
	DIR *dir;

	list_init(&tracks);
	list_init(&tags);
	list_init(&tags_sel);

	datadir = getenv("TMUS_DATA");
	if (!datadir) ERROR("TMUS_DATA not set!\n");

	dir = opendir(datadir);
	if (!dir) ERROR("Failed to access dir: %s\n", datadir);

	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;

		path = aprintf("%s/%s", datadir, ent->d_name);
		OOM_CHECK(path);

		if (!stat(path, &st) && S_ISDIR(st.st_mode)) {
			tag = tag_add(ent->d_name);
			tracks_load(tag);
		}

		free(path);
	}

	closedir(dir);
}

void
data_save(void)
{
	struct link *link;
	struct tag *tag;

	for (LIST_ITER(&tags, link)) {
		tag = UPCAST(link, struct tag, link);
		tracks_save(tag);
	}
}

void
data_free(void)
{
	struct link *link;
	struct tag *tag;

	list_clear(&player.playlist);
	list_clear(&player.queue);
	list_clear(&player.history);

	while (!list_empty(&tags)) {
		link = list_pop_front(&tags);
		tag = UPCAST(link, struct tag, link);
		tag_rm(tag, false);
	}
}

