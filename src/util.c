#include <stdio.h>
#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include "tui.h"
#include "util.h"
#include "log.h"

#include <execinfo.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char *allowed = "abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:,;-_(){}[]";

void
panic(const char *file, int line, const char *msg, ...)
{
	va_list ap;

	if (tui_enabled())
		tui_restore();

	va_start(ap, msg);
	fprintf(stderr, "tmus: panic at %s:%i (", file, line);
	vfprintf(stderr, msg, ap);
	fprintf(stderr, ")\n");
	va_end(ap);

	abort();
}

void
assert(int cond, const char *file, int line, const char *condstr)
{
	if (cond) return;

	if (tui_enabled())
		tui_restore();

	fprintf(stderr, "tmus: assertion failed %s:%i (%s)\n",
		file, line, condstr);

	abort();
}

void
warn(bool add_error, int type, const char *fmtstr, ...)
{
	va_list ap;

	va_start(ap, fmtstr);
	if (tui_enabled()) {
		if (type != USER)
			log_info("tmus: ");
		log_infov(fmtstr, ap);
		if (add_error)
			log_info(": %s", strerror(errno));
		log_info("\n");
	} else {
		if (type != USER)
			fprintf(stderr, "tmus: ");
		vfprintf(stderr, fmtstr, ap);
		if (add_error)
			fprintf(stderr, ": %s", strerror(errno));
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

void
error(bool add_error, int type, const char *fmtstr, ...)
{
	va_list ap;

	if (tui_enabled())
		tui_restore();

	va_start(ap, fmtstr);
	if (type != USER)
		fprintf(stderr, "tmus: ");
	vfprintf(stderr, fmtstr, ap);
	if (add_error)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
	va_end(ap);

	if (type == INTERNAL)
		abort();
	else
		exit(1);
}

char *
astrdup(const char *str)
{
	char *alloc;

	alloc = strdup(str);
	if (!alloc) ERROR(SYSTEM, "strdup");

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
	if (size < 0) ERROR(SYSTEM, "snprintf");
	va_end(ap);

	str = malloc(size + 1);
	if (!str) ERROR(SYSTEM, "malloc");

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
	if (!alloc) ERROR(SYSTEM, "realloc");

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

