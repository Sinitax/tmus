#pragma once

#include "list.h"

#include <stdbool.h>

struct tag {
	char *name, *fpath;
	struct list tracks;

	struct link link;     /* tags list */
	struct link link_sel; /* selected tags list */ 
};

struct track {
	char *name, *fpath;
	struct tag *tag;

	struct link link;    /* tracks list */
	struct link link_pl; /* player playlist */
	struct link link_tt; /* tag tracks list */
	struct link link_pq; /* player queue */
	struct link link_hs; /* player history */
};

bool path_exists(const char *path);
bool make_dir(const char *path);
bool rm_dir(const char *path, bool recursive);
bool rm_file(const char *path);
bool copy_file(const char *dst, const char *src);
bool dup_file(const char *dst, const char *src);
bool move_file(const char *dst, const char *src);

void index_update(struct tag *tag);
bool tracks_update(struct tag *tag);

struct track *tracks_vis_track(struct link *link);

void playlist_clear(void);
void playlist_update(bool exec);

struct tag *tag_add(const char *fname);
struct tag *tag_find(const char *name);
bool tag_rm(struct tag *tag, bool sync_fs);
bool tag_rename(struct tag *tag, const char *name);

struct track *track_add(struct tag *tag, const char *fname);
bool track_rm(struct track *track, bool sync_fs);
bool track_rename(struct track *track, const char *name);

bool acquire_lock(const char *path);
bool release_lock(const char *path);

void data_load(void);
void data_save(void);
void data_free(void);

extern const char *datadir;

extern struct list tracks; /* struct ref */
extern struct list tags; /* struct tag */
extern struct list tags_sel; /* struct ref */

