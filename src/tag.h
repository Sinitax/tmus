#pragma once

#include "list.h"
#include "util.h"

struct tag {
	wchar_t *name;
	char *fname, *fpath;

	struct link tracks;

	struct link link;
};

struct tag *tag_init(const char *path, const char *fname);
void tag_free(struct tag *tag);
