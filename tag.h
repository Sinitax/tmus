#pragma once

#include "link.h"
#include "util.h"

struct tag {
	char *name;
	char *fname, *fpath;

	struct link link;
};

