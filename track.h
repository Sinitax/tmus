#pragma once

#include "link.h"
#include "util.h"


struct track {
	char *name;
	char *artist;
	float duration;
	struct link tags;
	char *fname, *fpath;

	struct link link;
};

struct track_ref {
	struct track *track;

	struct link link;
};


struct track *track_init(const char *dir, const char *file);
void track_free(struct track *t);
