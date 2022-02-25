#include "tag.h"

#include "link.h"
#include "ref.h"
#include "util.h"

#include <string.h>

struct tag *
tag_alloc(const char *path, const char *fname)
{
	struct tag *tag;
	int len;

	tag = malloc(sizeof(struct tag));
	ASSERT(tag != NULL);

	tag->fpath = aprintf("%s/%s", path, fname);
	ASSERT(tag->fpath != NULL);

	tag->name = strdup(fname);
	ASSERT(tag->name != NULL);

	tag->link = LINK_EMPTY;

	list_init(&tag->tracks);

	return tag;
}

void
tag_free(struct tag *tag)
{
	free(tag->fpath);
	free(tag->name);
	refs_free(&tag->tracks);
	free(tag);
}
