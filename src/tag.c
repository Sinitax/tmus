#include "tag.h"
#include "link.h"
#include "ref.h"

#include <string.h>

struct tag *
tag_init(const char *path, const char *fname)
{
	struct tag *tag;

	tag = malloc(sizeof(struct tag));
	ASSERT(tag != NULL);

	tag->fname = strdup(fname);
	ASSERT(tag->fname != NULL);

	tag->fpath = aprintf("%s/%s", path, fname);
	ASSERT(tag->fpath != NULL);

	tag->name = sanitized(tag->fname);
	ASSERT(tag->name != NULL);

	tag->link = LINK_EMPTY;

	tag->tracks = LIST_HEAD;

	return tag;
}

void
tag_free(struct tag *tag)
{
	free(tag->fname);
	free(tag->fpath);
	free(tag->name);

	refs_free(&tag->tracks);

	free(tag);
}
