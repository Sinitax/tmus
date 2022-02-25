#pragma once

#include "list.h"

struct tag {
	char *name, *fpath;

	struct list tracks;

	struct link link;
};

struct tag *tag_alloc(const char *path, const char *fname);
void tag_free(struct tag *tag);
