#define _XOPEN_SOURCE 600

#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

int
strnwidth(const char *s, int n)
{
	mbstate_t shift_state;
	wchar_t wc;
	size_t wc_len;
	size_t width = 0;

	memset(&shift_state, '\0', sizeof shift_state);

	for (size_t i = 0; i < n; i += wc_len) {
		wc_len = mbrtowc(&wc, s + i, MB_CUR_MAX, &shift_state);
		if (!wc_len) {
			break;
		} else if (wc_len >= (size_t)-2) {
			width += MIN(n - 1, strlen(s + i));
			break;
		} else {
			width += iswcntrl(wc) ? 2 : MAX(0, wcwidth(wc));
		}
	}

done:
	return width;
}

void
assert(int cond, const char *file, int line, const char *condstr)
{
	if (cond) return;

	fprintf(stderr, "Assertion failed %s:%i (%s)", file, line, condstr);
	exit(1);
}
