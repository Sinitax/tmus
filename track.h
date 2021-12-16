#pragma once

#include "list.h"
#include "util.h"


struct track {
	wchar_t *name;
	struct link tags;
	char *fname, *fpath;

	struct link link;
};

struct track *track_init(const char *dir, const char *file);
void track_free(struct track *t);
