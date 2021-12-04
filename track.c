#include "track.h"

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
	track->name = sanitized(track->fname);
	ASSERT(track->name != NULL);

	// TODO track_load_info(track)
	track->artist = NULL;
	track->duration = 0;
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
