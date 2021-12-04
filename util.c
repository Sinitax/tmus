#define _XOPEN_SOURCE 600

#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static const char *allowed = "abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:,;-_(){}[]";

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

char *
sanitized(const char *instr)
{
	const char *p;
	char *clean;
	int i;

	clean = strdup(instr);
	ASSERT(clean != NULL);
	for (i = 0, p = instr; *p; p++) {
		if (strchr(allowed, *p))
			clean[i++] = *p;
	}
	ASSERT(i != 0);
	clean[i] = '\0';

	return clean;
}

char *
aprintf(const char *fmtstr, ...)
{
	va_list ap, cpy;
	size_t size;
	char *str;

	va_copy(cpy, ap);

	va_start(ap, fmtstr);
	size = vsnprintf(NULL, 0, fmtstr, ap);
	va_end(ap);

	str = malloc(size + 1);
	ASSERT(str != NULL);

	va_start(cpy, fmtstr);
	vsnprintf(str, size + 1, fmtstr, cpy);
	va_end(cpy);

	return str;
}
