#include <stdlib.h>

struct strbuf {
	char *buf;
	size_t cap;
};

void strbuf_init(struct strbuf *strbuf);
void strbuf_deinit(struct strbuf *strbuf);

void strbuf_clear(struct strbuf *strbuf);
void strbuf_append(struct strbuf *strbuf, const char *fmt, ...);
