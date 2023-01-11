#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include "util.h"
#include "tui.h"

#include <execinfo.h>
#include <err.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char *allowed = "abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:,;-_(){}[]";

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
astrdup(const char *str)
{
	char *alloc;

	alloc = strdup(str);
	if (!alloc) err(1, "strdup");

	return alloc;
}

char *
aprintf(const char *fmtstr, ...)
{
	va_list ap, cpy;
	ssize_t size;
	char *str;

	va_copy(cpy, ap);

	va_start(ap, fmtstr);
	size = vsnprintf(NULL, 0, fmtstr, ap);
	if (size < 0) err(1, "snprintf");
	va_end(ap);

	str = malloc(size + 1);
	if (!str) err(1, "malloc");

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

	clean = astrdup(instr);
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

