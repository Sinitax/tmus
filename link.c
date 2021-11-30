#include "link.h"

int
list_len(struct link *list)
{
	struct link *iter;
	int len;

	len = 0;
	for (iter = list; iter; iter = iter->next)
		len += 1;

	return len;
}

struct link *
list_back(struct link *link)
{
	for (; link->next; link = link->next);
	return link;
}

void
link_prepend(struct link *list, struct link *link)
{
	link->prev = list->prev;
	link->next = list;

	if (link->prev)
		link->prev->next = link;
	if (link->next)
		link->next->prev = link;
}

void
link_append(struct link *list, struct link *link)
{
	link->prev = list;
	link->next = list->next;

	if (link->prev)
		link->prev->next = link;
	if (link->next)
		link->next->prev = link;
}

void
link_pop(struct link *link)
{
	if (link->prev)
		link->prev->next = link->next;
	if (link->next)
		link->next->prev = link->prev;
}
