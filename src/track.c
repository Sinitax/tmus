#include "track.h"

#include <wchar.h>
#include <string.h>
#include <sys/stat.h>


struct track *
track_init(const char *dir, const char *fname)
{
	struct track *track;
	struct stat info;

	track = malloc(sizeof(struct track));

	ASSERT(track != NULL);
	track->fname = strdup(fname);
	ASSERT(track->fname != NULL);
	track->fpath = aprintf("%s/%s", dir, fname);
	ASSERT(track->fpath != NULL);
	track->name = calloc(strlen(track->fname) + 1, sizeof(wchar_t));
	ASSERT(track->name != NULL);
	mbstowcs(track->name, track->fname, strlen(track->fname) + 1);

	if (!stat(track->fpath, &info)) {
		track->fid = info.st_ino;
	} else {
		track->fid = -1;
	}

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
