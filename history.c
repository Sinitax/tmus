#include "history.h"
#include "util.h"

#include <string.h>
#include <wchar.h>


void
history_init(struct history *history)
{
	history->list = LIST_HEAD;
	history->query = inputln_init();
	history->cmd = history->query;
}

void
history_submit(struct history *history)
{
	/* if chose from history free query */
	if (history->cmd != history->query) {
		history_pop(history->query);
		inputln_free(history->query);
	}

	/* pop first in case already in history */
	history_pop(history->cmd);
	history_add(history, history->cmd);

	/* create new input buf and add to hist */
	history->query = inputln_init();
	history->cmd = history->query;
	history_add(history, history->cmd);
}

void
history_free(struct history *history)
{
	struct link *iter, *next;
	struct inputln *ln;

	for (iter = history->list.next; iter; iter = next) {
		next = iter->next;
		ln = UPCAST(iter, struct inputln);
		free(ln);
	}
	history->list = LIST_HEAD;
	history->query = NULL;
	history->cmd = NULL;
}

struct inputln *
history_list_prev(struct inputln *cur, const wchar_t *search)
{
	struct link *iter;
	struct inputln *ln;

	for (iter = cur->link.prev; iter && iter->prev; iter = iter->prev) {
		ln = UPCAST(iter, struct inputln);
		if (!search || !*search || wcsstr(ln->buf, search))
			return ln;
	}

	return NULL;
}

struct inputln *
history_list_next(struct inputln *cur, const wchar_t *search)
{
	struct link *iter;
	struct inputln *ln;

	for (iter = cur->link.next; iter; iter = iter->next) {
		ln = UPCAST(iter, struct inputln);
		if (!search || !*search || wcsstr(ln->buf, search))
			return ln;
	}

	return cur;
}

void
history_prev(struct history *history)
{
	history->cmd = history_list_prev(history->cmd, history->query->buf);
	if (!history->cmd) history->cmd = history->query;
}

void
history_next(struct history *history)
{
	history->cmd = history_list_next(history->cmd, history->query->buf);
}

void
history_pop(struct inputln *line)
{
	link_pop(&line->link);
}

void
history_add(struct history *history, struct inputln *line)
{
	struct inputln *ln;
	struct link *back;

	if (list_len(&history->list) == HISTORY_MAX) {
		/* pop last item to make space */
		back = link_back(&history->list);
		back->prev->next = NULL;
		ln = UPCAST(back, struct inputln);
		inputln_free(ln);
	}

	link_append(&history->list, &line->link);
}

struct inputln *
inputln_init(void)
{
	struct inputln *ln;

	ln = malloc(sizeof(struct inputln));
	ASSERT(ln != NULL);
	ln->cap = 128;
	ln->buf = malloc(ln->cap * sizeof(wchar_t));
	ASSERT(ln->buf != NULL);
	ln->len = 0;
	ln->buf[ln->len] = '\0';
	ln->cur = 0;
	ln->link.next = NULL;
	ln->link.prev = NULL;

	return ln;
}

void
inputln_free(struct inputln *ln)
{
	free(ln->buf);
	free(ln);
}

void
inputln_left(struct inputln *cmd)
{
	cmd->cur = MAX(0, cmd->cur-1);
}

void
inputln_right(struct inputln *cmd)
{
	cmd->cur = MIN(cmd->len, cmd->cur+1);
}

void
inputln_addch(struct inputln *line, wchar_t c)
{
	int i;

	if (line->len + 1 == line->cap) {
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
	dst->buf = wcsdup(src->buf);
	ASSERT(dst->buf != NULL);
	dst->cap = src->len + 1;
	dst->cur = dst->len;
}

void
inputln_replace(struct inputln *line, const wchar_t *str)
{
	line->buf = wcsdup(str);
	ASSERT(line->buf != NULL);
	line->len = wcslen(str);
	line->cap = line->len + 1;
	line->cur = line->len;
}
