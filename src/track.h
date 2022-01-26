#pragma once

#include "list.h"
#include "util.h"


struct track {
	wchar_t *name;
	struct link tags;
	char *fname, *fpath;
	int fid;
};

struct track *track_init(const char *dir, const char *file, int fid);
void track_free(struct track *t);
