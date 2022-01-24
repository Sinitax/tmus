#include "ref.h"
#include "track.h"

#include <wchar.h>
#include <string.h>
#include <sys/stat.h>


struct track *
track_init(const char *dir, const char *fname)
{
	struct track *track;
	struct stat info;
	int len;

	track = malloc(sizeof(struct track));
	ASSERT(track != NULL);

	track->fname = strdup(fname);
	ASSERT(track->fname != NULL);

	track->fpath = aprintf("%s/%s", dir, fname);
	ASSERT(track->fpath != NULL);

	len = mbstowcs(NULL, track->fname, 0);
	ASSERT(len >= 0);
	track->name = calloc(len + 1, sizeof(wchar_t));
	ASSERT(track->name != NULL);
	mbstowcs(track->name, track->fname, len + 1);

	track->fid = -1;
	if (!stat(track->fpath, &info))
		track->fid = info.st_ino;

	track->tags = LIST_HEAD;

	return track;
}

void
track_free(struct track *t)
{
	free(t->fname);
	free(t->fpath);
	free(t->name);

	refs_free(&t->tags);

	free(t);
}
