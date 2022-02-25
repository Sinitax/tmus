#include "ref.h"
#include "track.h"

#include <wchar.h>
#include <string.h>
#include <sys/stat.h>

struct track *
track_alloc(const char *dir, const char *fname, int fid)
{
	struct track *track;
	int len;

	track = malloc(sizeof(struct track));
	ASSERT(track != NULL);

	track->fpath = aprintf("%s/%s", dir, fname);
	ASSERT(track->fpath != NULL);

	track->name = strdup(fname);
	ASSERT(track->name != NULL);

	track->fid = fid;

	list_init(&track->tags);

	return track;
}

void
track_free(struct track *t)
{
	free(t->fpath);
	free(t->name);
	refs_free(&t->tags);
	free(t);
}
