#include "list.h"
#include "util.h"

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
link_back(struct link *link)
{
	ASSERT(link != NULL);

	for (; link->next; link = link->next);

	return link;
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

void
list_push_back(struct link *cur, struct link *link)
{
	struct link *back;

	back = link_back(cur);
	link_append(back, link);
}
