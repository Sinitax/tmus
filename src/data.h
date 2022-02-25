#pragma once

#include "tag.h"

void data_load(void);
void data_save(void);
void data_free(void);

int get_fid(const char *path);
int track_fid_compare(struct link *a, struct link *b);
void index_update(struct tag *tag);
bool tracks_update(struct tag *tag);
void tracks_load(struct tag *tag);
void tracks_save(struct tag *tag);

bool make_dir(const char *path);
bool rm_dir(const char *path, bool recursive);
bool rm_file(const char *path);
bool copy_file(const char *dst, const char *src);
bool move_file(const char *dst, const char *src);

struct tag *tag_find(const char *query);

extern const char *datadir;

extern struct list tracks; /* struct ref */
extern struct list tags; /* struct tag */
extern struct list tags_sel; /* struct ref */
