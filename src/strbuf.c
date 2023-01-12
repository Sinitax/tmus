#include "strbuf.h"

#include "util.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

void
strbuf_init(struct strbuf *strbuf)
{
	strbuf->buf = NULL;
	strbuf->cap = 0;
}

void
strbuf_deinit(struct strbuf *strbuf)
{
	free(strbuf->buf);
}

void
strbuf_clear(struct strbuf *strbuf)
{
	if (!strbuf->buf)
		strbuf_append(strbuf, "");

	strbuf->buf[0] = '\0';
}

void
strbuf_append(struct strbuf *strbuf, const char *fmt, ...)
{
	va_list ap, cpy;
	ssize_t slen;
	int blen;

	va_copy(cpy, ap);

	blen = strbuf->buf ? strlen(strbuf->buf) : 0;

	va_start(cpy, fmt);
	slen = vsnprintf(NULL, 0, fmt, cpy);
	if (slen < 0) ERROR(SYSTEM, "snprintf");
	va_end(cpy);

	if (blen + slen + 1 > strbuf->cap) {
		strbuf->cap = blen + slen + 1;
		strbuf->buf = realloc(strbuf->buf, strbuf->cap);
		if (!strbuf->buf) ERROR(SYSTEM, "realloc");
	}

	va_start(ap, fmt);
	vsnprintf(strbuf->buf + blen, slen + 1, fmt, ap);
	va_end(ap);
}
