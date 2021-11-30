#pragma once

#include <stdlib.h>

#define OFFSET(type, attr) ((size_t) &((type *)0)->attr)
#define UPCAST(ptr, type) ({ \
	const typeof( ((type *)0)->link ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - OFFSET(type, link) ); })

#define LIST_HEAD ((struct link) { .prev = NULL, .next = NULL })

struct link {
	struct link *prev, *next;
};

int list_len(struct link *list);
struct link* list_back(struct link *list);

void link_push_back(struct link *list, struct link *link);

void link_prepend(struct link *list, struct link *link);
void link_append(struct link *list, struct link *link);
void link_pop(struct link *link);
