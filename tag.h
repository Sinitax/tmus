#pragma once

#include "link.h"
#include "util.h"

struct tag {
	char *name;
	char *fname, *fpath;

	struct link link;
};

struct tag_ref {
	struct tag *tag;

	struct link link;
};

int tagrefs_incl(struct link *head, struct tag *tag);
void tagrefs_add(struct link *head, struct tag *tag);
void tagrefs_rm(struct link *head, struct tag *tag);

