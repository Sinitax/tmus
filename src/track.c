#include "track.h"

#include <wchar.h>
#include <string.h>


struct track *
track_init(const char *dir, const char *file)
{
	struct track *track;

	track = malloc(sizeof(struct track));

	ASSERT(track != NULL);
	track->fname = strdup(file);
	ASSERT(track->fname != NULL);
	track->fpath = aprintf("%s/%s", dir, file);
	ASSERT(track->fpath != NULL);
	track->name = calloc(strlen(track->fname) + 1, sizeof(wchar_t));
	mbstowcs(track->name, track->fname, strlen(track->fname) + 1);

	track->link = LINK_EMPTY;
	track->tags = LIST_HEAD;

	return track;
}

void
track_free(struct track *t)
{
	free(t->fname);
	free(t->fpath);
	free(t->name);
}
