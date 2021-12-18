#pragma once

#include <stdlib.h>

#define OFFSET(type, attr) ((size_t) &((type *)0)->attr)
#define UPCAST(ptr, type) ({ \
	const typeof( ((type *)0)->link ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - OFFSET(type, link) ); })

#define LIST_HEAD ((struct link) { .prev = NULL, .next = NULL })
#define LINK_EMPTY ((struct link) { 0 })

#define LINK(p) (&(p)->link)

struct link {
	struct link *prev;
	struct link *next;
};

/* list_XXX functions operate on the list head */

int list_empty(struct link *head);
int list_len(struct link *head);
int list_ffind(struct link *head, struct link *link);

struct link *link_back(struct link *list);
void link_prepend(struct link *list, struct link *link);
void link_append(struct link *list, struct link *link);
struct link *link_pop(struct link *link);

struct link *link_iter(struct link *link, int n);

void list_push_back(struct link *list, struct link *link);

