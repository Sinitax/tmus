#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include "util.h"
#include "tui.h"

#include <execinfo.h>

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

const wchar_t *
wcscasestr(const wchar_t *haystack, const wchar_t *needle)
{
	int hslen, nlen, i;

	nlen = wcslen(needle);
	hslen = wcslen(haystack) - nlen;
	for (i = 0; i < hslen; i++) {
		if (!wcsncasecmp(haystack + i, needle, nlen))
			return haystack + i;
	}

	return NULL;
}

void
panic(const char *file, int line, const char *msg, ...)
{
	va_list ap;

	tui_restore();

	fprintf(stderr, "Panic at %s:%i (", file, line);
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fprintf(stderr, ")\n");

	exit(1);
}

void
assert(int cond, const char *file, int line, const char *condstr)
{
	if (cond) return;

	tui_restore();

	fprintf(stderr, "Assertion failed %s:%i (%s)\n", file, line, condstr);

	exit(1);
}

void
error(const char *fmtstr, ...)
{
	va_list ap;

	tui_restore();

	va_start(ap, fmtstr);
	vfprintf(stderr, fmtstr, ap);
	va_end(ap);

	exit(1);
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
	if (!str) return NULL;

	va_start(cpy, fmtstr);
	vsnprintf(str, size + 1, fmtstr, cpy);
	va_end(cpy);

	return str;
}

char *
appendstrf(char *alloc, const char *fmtstr, ...)
{
	va_list ap, cpy;
	size_t size, prevlen;

	va_copy(cpy, ap);

	va_start(ap, fmtstr);
	size = vsnprintf(NULL, 0, fmtstr, ap);
	va_end(ap);

	prevlen = alloc ? strlen(alloc) : 0;
	alloc = realloc(alloc, prevlen + size + 1);
	if (!alloc) return NULL;

	va_start(cpy, fmtstr);
	vsnprintf(alloc + prevlen, size + 1, fmtstr, cpy);
	va_end(cpy);

	return alloc;
}

char *
sanitized(const char *instr)
{
	const char *p;
	char *clean;
	int i;

	clean = strdup(instr);
	OOM_CHECK(clean);

	for (i = 0, p = instr; *p; p++) {
		if (strchr(allowed, *p))
			clean[i++] = *p;
	}
	ASSERT(i != 0);
	clean[i] = '\0';

	return clean;
}

const char *
timestr(unsigned int secs)
{
	static char buf[16];
	unsigned int mins, hours;

	hours = secs / 3600;
	mins = secs / 60 % 60;
	secs = secs % 60;

	if (hours) {
		snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hours, mins, secs);
	} else {
		snprintf(buf, sizeof(buf), "%02u:%02u", mins, secs);
	}

	return buf;
}

