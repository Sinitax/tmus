#pragma once

#include "list.h"
#include "util.h"

struct track {
	char *name, *fpath;
	struct list tags;
	int fid;
};

struct track *track_alloc(const char *dir, const char *file, int fid);
void track_free(struct track *t);
