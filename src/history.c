#define _GNU_SOURCE

#include "history.h"
#include "util.h"

#include <string.h>

static struct inputln *history_list_prev(
	struct inputln  *cur, const char *search);

static struct inputln *history_list_next(
	struct inputln  *cur, const char *search);

struct inputln *
history_list_prev(struct inputln *cur, const char *search)
{
	struct link *iter;
	struct inputln *ln;

	for (iter = cur->link.prev; iter && iter->prev; iter = iter->prev) {
		ln = UPCAST(iter, struct inputln, link);
		if (!search || !*search || strcasestr(ln->buf, search))
			return ln;
	}

	return cur;
}

struct inputln *
history_list_next(struct inputln *cur, const char *search)
{
	struct link *iter;
	struct inputln *ln;

	iter = cur->link.next;
	while (LIST_INNER(iter)) {
		ln = UPCAST(iter, struct inputln, link);
		if (!search || !*search || strcasestr(ln->buf, search))
			return ln;
		iter = iter->next;
	}

	return cur;
}

void
history_init(struct history *history)
{
	list_init(&history->list);
	history->input = inputln_alloc();
	history->sel = history->input;
}

void
history_deinit(struct history *history)
{
	struct link *link;
	struct inputln *ln;

	link = link_pop(&history->input->link);
	ln = UPCAST(link, struct inputln, link);
	inputln_free(ln);
	history->input = NULL;

	list_free(&history->list, (link_free_func) inputln_free,
		LINK_OFFSET(struct inputln, link));

	history->sel = NULL;
}

void
history_submit(struct history *history)
{
	/* if chose from history free input */
	if (history->sel != history->input) {
		link_pop(&history->input->link);
		inputln_free(history->input);
	}

	/* pop first in case already in history */
	link_pop(&history->sel->link);
	history_add(history, history->sel);

	/* create new input buf and add to hist */
	history->input = inputln_alloc();
	history->sel = history->input;
	history_add(history, history->sel);
}

void
history_prev(struct history *history)
{
	history->sel = history_list_prev(history->sel, history->input->buf);
}

void
history_next(struct history *history)
{
	history->sel = history_list_next(history->sel, history->input->buf);
}

void
history_add(struct history *history, struct inputln *line)
{
	struct inputln *ln;
	struct link *back;

	if (list_len(&history->list) == HISTORY_MAX) {
		/* pop last item to make space */
		back = list_pop_back(&history->list);
		ln = UPCAST(back, struct inputln, link);
		inputln_free(ln);
	}

	list_push_front(&history->list, &line->link);
}

void
inputln_init(struct inputln *ln)
{
	ln->buf = NULL;
	ln->len = 0;
	ln->cap = 0;
	ln->cur = 0;
	ln->link = LINK_EMPTY;

	inputln_resize(ln, 128);
}

void
inputln_deinit(struct inputln *ln)
{
	free(ln->buf);
}

struct inputln *
inputln_alloc(void)
{
	struct inputln *ln;

	ln = malloc(sizeof(struct inputln));
	if (!ln) ERROR(SYSTEM, "malloc");
	inputln_init(ln);

	return ln;
}

void
inputln_free(struct inputln *ln)
{
	inputln_deinit(ln);
	free(ln);
}

void
inputln_resize(struct inputln *ln, size_t size)
{
	ASSERT(size != 0);

	ln->cap = size;
	ln->buf = realloc(ln->buf, ln->cap * sizeof(char));
	if (!ln->buf) ERROR(SYSTEM, "realloc");
	ln->len = MIN(ln->len, ln->cap-1);
	ln->buf[ln->len] = '\0';
}

void
inputln_left(struct inputln *ln)
{
	ln->cur = MAX(0, ln->cur-1);
}

void
inputln_right(struct inputln *ln)
{
	ln->cur = MIN(ln->len, ln->cur+1);
}

void
inputln_addch(struct inputln *line, char c)
{
	int i;

	if (line->len + 1 >= line->cap) {
		line->cap *= 2;
		line->buf = realloc(line->buf, line->cap);
	}

	for (i = line->len; i > line->cur; i--)
		line->buf[i] = line->buf[i-1];
	line->buf[line->cur] = c;

	line->len++;
	line->cur++;

	line->buf[line->len] = '\0';
}

void
inputln_del(struct inputln *line, int n)
{
	int i;

	if (!line->cur) return;

	n = MIN(n, line->cur);
	for (i = line->cur; i <= line->len; i++)
		line->buf[i-n] = line->buf[i];

	for (i = line->len - n; i <= line->len; i++)
		line->buf[i] = 0;

	line->len -= n;
	line->cur -= n;
}

void
inputln_copy(struct inputln *dst, struct inputln *src)
{
	if (dst->buf) {
		free(dst->buf);
		dst->buf = NULL;
	}
	dst->len = src->len;
	dst->buf = astrdup(src->buf);
	dst->cap = src->len + 1;
	dst->cur = dst->len;
}

void
inputln_replace(struct inputln *line, const char *str)
{
	line->len = strlen(str);
	if (line->cap <= line->len)
		inputln_resize(line, line->len + 1);
	strncpy(line->buf, str, line->len + 1);
	line->cur = line->len;
}

