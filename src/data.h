#pragma once

#include "tag.h"

void data_load(void);
void data_save(void);
void data_free(void);

int get_fid(const char *path);
void index_update(struct tag *tag);
void tracks_load(struct tag *tag);
void tracks_save(struct tag *tag);

void rm_file(const char *path);
void copy_file(const char *dst, const char *src);
void move_file(const char *dst, const char *src);

struct tag *tag_find(const wchar_t *query);

extern const char *datadir;

extern struct list tracks;
extern struct list tags;
extern struct list tags_sel;
