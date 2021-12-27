#pragma once

#include <stdlib.h>

#define LINK_OFFSET(type) ((size_t) &((type *)0)->link)
#define UPCAST(ptr, type) ({ \
	const typeof( ((type *)0)->link ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - LINK_OFFSET(type) ); })

#define LIST_HEAD ((struct link) { .prev = NULL, .next = NULL })
#define LINK_EMPTY ((struct link) { 0 })

#define LINK(p) (&(p)->link)

struct link {
	struct link *prev;
	struct link *next;
};

/* list_XXX functions operate on the list head */

void list_free(struct link *head, void (*free_item)(void *), int offset);
int list_empty(struct link *head);
int list_len(struct link *head);
int list_ffind(struct link *head, struct link *link);
struct link *list_at(struct link *head, int n);
void list_push_front(struct link *head, struct link *link);
void list_push_back(struct link *head, struct link *link);
struct link *list_pop_front(struct link *head);

void link_prepend(struct link *list, struct link *link);
void link_append(struct link *list, struct link *link);
struct link *link_back(struct link *list);
struct link *link_pop(struct link *link);
struct link *link_iter(struct link *link, int n);
