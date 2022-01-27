#include "list.h"
#include "util.h"

void
list_init(struct list *list)
{
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

void
list_free(struct list *list, void (*free_item)(void *), int offset)
{
	struct link *item;

	ASSERT(list != NULL);

	while (!list_empty(list)) {
		item = link_pop(list->head.next);
		free_item(((void *) item) - offset);
	}
}

int
list_empty(struct list *list)
{
	ASSERT(list != NULL);

	return list->head.next == &list->tail;
}

int
list_len(struct list *list)
{
	struct link *iter;
	int len;

	ASSERT(list != NULL);

	len = 0;
	for (LIST_ITER(list, iter))
		len += 1;

	return len;
}

struct link *
list_at(struct list *list, int n)
{
	ASSERT(list != NULL);

	return link_iter(list->head.next, n);
}

void
list_push_front(struct list *list, struct link *link)
{
	link_append(&list->head, link);
}

void
list_push_back(struct list *list, struct link *link)
{
	link_prepend(&list->tail, link);
}

struct link *
list_pop_front(struct list *list)
{
	ASSERT(list != NULL);

	if (list_empty(list))
		return NULL;

	return link_pop(list->head.next);
}

struct link *
list_pop_back(struct list *list)
{
	ASSERT(list != NULL);

	if (list_empty(list))
		return NULL;

	return link_pop(list->tail.prev);
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
link_iter(struct link *link, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!link) return NULL;
		link = link->next;
	}

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
