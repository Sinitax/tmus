#include "tag.h"
#include "link.h"
#include "ref.h"

#include <string.h>

struct tag *
tag_init(const char *path, const char *fname)
{
	struct tag *tag;
	int len;

	tag = malloc(sizeof(struct tag));
	ASSERT(tag != NULL);

	tag->fname = strdup(fname);
	ASSERT(tag->fname != NULL);

	tag->fpath = aprintf("%s/%s", path, fname);
	ASSERT(tag->fpath != NULL);

	len = mbstowcs(NULL, tag->fname, 0);
	ASSERT(len > 0);
	tag->name = calloc(len + 1, sizeof(wchar_t));
	ASSERT(tag->name != NULL);
	mbstowcs(tag->name, tag->fname, len + 1);

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
