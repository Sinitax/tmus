#include "data.h"

#include "player.h"
#include "list.h"
#include "log.h"
#include "ref.h"
#include "track.h"
#include "tag.h"

#include <fts.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdbool.h>
#include <string.h>

const char *datadir;

struct list tracks;
struct list tags;
struct list tags_sel;

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
			tag = tag_alloc(datadir, ent->d_name);
			OOM_CHECK(tag);
			tracks_load(tag);
			list_push_back(&tags, LINK(tag));
		}

		free(path);
	}
	closedir(dir);

	list_sort(&tracks, track_fid_compare);
}

void
data_save(void)
{
	struct link *iter;
	struct tag *tag;

	for (LIST_ITER(&tags, iter)) {
		tag = UPCAST(iter, struct tag);
		tracks_save(tag);
	}
}

void
data_free(void)
{
	struct link *track_link;
	struct link *tag_link;
	struct tag *tag;

	log_info("MAIN: data_free()\n");

	refs_free(&tracks);
	refs_free(&tags_sel);

	while (!list_empty(&tags)) {
		tag_link = list_pop_front(&tags);

		tag = UPCAST(tag_link, struct tag);
		while (!list_empty(&tag->tracks)) {
			track_link = list_pop_front(&tag->tracks);
			track_free(UPCAST(track_link, struct ref)->data);
			ref_free(UPCAST(track_link, struct ref));
		}
		tag_free(tag);
	}
}

int
get_fid(const char *path)
{
	struct stat st;
	return stat(path, &st) ? -1 : st.st_ino;
}

int
track_fid_compare(struct link *a, struct link *b)
{
	struct track *ta, *tb;

	ta = UPCAST(a, struct ref)->data;
	tb = UPCAST(b, struct ref)->data;

	return ta->fid - tb->fid;
}

void
index_update(struct tag *tag)
{
	struct dirent *ent;
	struct stat st;
	char *path;
	FILE *file;
	DIR *dir;
	int fid;

	path = aprintf("%s/index", tag->fpath);
	OOM_CHECK(path);

	dir = opendir(tag->fpath);
	if (!dir) ERROR("Failed to access dir: %s\n", tag->fpath);

	file = fopen(path, "w+");
	if (!file) ERROR("Failed to create file: %s\n", path);
	free(path);

	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;
		if (!strcmp(ent->d_name, "index"))
			continue;

		/* skip files without extension */
		if (!strchr(ent->d_name + 1, '.'))
			continue;

		path = aprintf("%s/%s", tag->fpath, ent->d_name);
		OOM_CHECK(path);
		fid = get_fid(path);
		free(path);

		fprintf(file, "%i:%s\n", fid, ent->d_name);
	}

	closedir(dir);
	fclose(file);
}

bool
tracks_update(struct tag *tag)
{
	struct dirent *ent;
	struct stat st;
	struct link *link;
	struct ref *ref;
	struct track *track;
	char *path;
	DIR *dir;
	int fid;

	dir = opendir(tag->fpath);
	if (!dir) return false;

	while (!list_empty(&tag->tracks)) {
		link = list_pop_front(&tag->tracks);
		ref = UPCAST(link, struct ref);
		track = ref->data;
		ref_free(ref);
		for (LIST_ITER(&tracks, link)) {
			ref = UPCAST(link, struct ref);
			if (ref->data == track) {
				link = link_pop(link);
				ref_free(ref);
				break;
			}
		}
		if (player.track == track)
			player.track = NULL;
		track_free(track);
	}

	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, "."))
			continue;
		if (!strcmp(ent->d_name, ".."))
			continue;
		if (!strcmp(ent->d_name, "index"))
			continue;

		/* skip files without extension */
		if (!strchr(ent->d_name + 1, '.'))
			continue;

		path = aprintf("%s/%s", tag->fpath, ent->d_name);
		OOM_CHECK(path);

		fid = get_fid(path);

		track = track_alloc(tag->fpath, ent->d_name, fid);
		OOM_CHECK(track);

		ref = ref_alloc(tag);
		OOM_CHECK(ref);
		list_push_back(&track->tags, LINK(ref));

		ref = ref_alloc(track);
		OOM_CHECK(ref);
		list_push_back(&tag->tracks, LINK(ref));

		ref = ref_alloc(track);
		OOM_CHECK(ref);
		list_push_back(&tracks, LINK(ref));

		free(path);
	}

	list_sort(&tracks, track_fid_compare);

	closedir(dir);
	return true;
}

void
tracks_load(struct tag *tag)
{
	char linebuf[1024];
	struct link *link;
	struct track *track;
	struct track *track2;
	struct ref *ref;
	char *index_path;
	char *track_name, *sep;
	int track_fid;
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
		sep = strchr(linebuf, '\n');
		if (sep) *sep = '\0';

		sep = strchr(linebuf, ':');
		if (!sep) ERROR("Syntax error in index file: %s\n", index_path);
		*sep = '\0';

		track_fid = atoi(linebuf);
		track_name = sep + 1;
		track = track_alloc(tag->fpath, track_name, track_fid);

		ref = ref_alloc(tag);
		OOM_CHECK(ref);
		list_push_back(&track->tags, LINK(ref));

		ref = ref_alloc(track);
		OOM_CHECK(ref);
		list_push_back(&tag->tracks, LINK(ref));

		ref = ref_alloc(track);
		OOM_CHECK(ref);
		list_push_back(&tracks, LINK(ref));
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
		track = UPCAST(link, struct ref)->data;
		fprintf(file, "%i:%s\n", track->fid, track->name);
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
	int len, nread;
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
move_file(const char *src, const char *dst)
{
	return rename(src, dst) == 0;
}

struct tag *
tag_find(const char *query)
{
	struct link *iter;
	struct tag *tag;

	for (LIST_ITER(&tags, iter)) {
		tag = UPCAST(iter, struct tag);
		if (!strcmp(tag->name, query)) {
			return tag;
		}
	}

	return NULL;
}


