#include "list.h"
#include "util.h"

void
list_free(struct link *head, void (*free_item)(void *), int offset)
{
	struct link *item;

	while (head->next) {
		item = link_pop(head->next);
		free_item(((void *) item) - offset);
	}
}

int
list_empty(struct link *head)
{
	ASSERT(head != NULL);
	return head->next == NULL;
}

int
list_len(struct link *head)
{
	struct link *iter;
	int len;

	ASSERT(head != NULL);

	len = 0;
	for (iter = head->next; iter; iter = iter->next)
		len += 1;

	return len;
}

int
list_ffind(struct link *head, struct link *link)
{
	struct link *iter;

	ASSERT(head != NULL);
	for (iter = head->next; iter && iter != link; iter = iter->next);
	return (iter == link);
}

struct link *
list_at(struct link *head, int n)
{
	ASSERT(head != NULL);
	return link_iter(head->next, n);
}

void
list_push_front(struct link *head, struct link *link)
{
	link_append(head, link);
}

void
list_push_back(struct link *cur, struct link *link)
{
	struct link *back;

	back = link_back(cur);
	link_append(back, link);
}

struct link *
list_pop_front(struct link *head)
{
	ASSERT(head != NULL);
	return link_pop(head->next);
}

void
link_prepend(struct link *cur, struct link *link)
{
	ASSERT(cur != NULL && link != NULL);

	link->prev = cur->prev;
	link->next = cur;

	if (link->prev)
		link->prev->next = link;
	if (link->next)
		link->next->prev = link;
}

void
link_append(struct link *cur, struct link *link)
{
	ASSERT(cur != NULL && link != NULL);

	link->prev = cur;
	link->next = cur->next;

	if (link->prev)
		link->prev->next = link;
	if (link->next)
		link->next->prev = link;
}

struct link *
link_back(struct link *link)
{
	ASSERT(link != NULL);

	for (; link->next; link = link->next);

	return link;
}

struct link *
link_pop(struct link *link)
{
	ASSERT(link != NULL);

	if (link->prev)
		link->prev->next = link->next;
	if (link->next)
		link->next->prev = link->prev;

	return link;
}

struct link *
link_iter(struct link *link, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!link) return NULL;
		link = link->next;
	}

	return link;
}
