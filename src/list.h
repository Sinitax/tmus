#pragma once

#include <stdlib.h>

#define LINK_OFFSET(type) ((size_t) &((type *)0)->link)
#define UPCAST(ptr, type) ({ \
	const typeof( ((type *)0)->link ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - LINK_OFFSET(type) ); })

#define LINK_EMPTY ((struct link) { 0 })

#define LINK(p) (&(p)->link)

#define LIST_INNER(list, link) \
	(((link) != &(list)->head) && ((link) != &(list)->tail))

#define LIST_ITER(list, iter) (iter) = (list)->head.next; \
	(iter) != &(list)->tail; (iter) = (iter)->next

typedef void (*link_free_func)(void *p);

struct link {
	struct link *prev;
	struct link *next;
};

struct list {
	struct link head;
	struct link tail;
};

void list_init(struct list *list);
void list_free(struct list *list, void (*free_item)(void *), int offset);
int list_empty(struct list *list);
int list_len(struct list *list);
struct link *list_at(struct list *list, int n);
void list_push_front(struct list *list, struct link *link);
void list_push_back(struct list *list, struct link *link);
struct link *list_pop_front(struct list *list);
struct link *list_pop_back(struct list *list);

void link_prepend(struct link *list, struct link *link);
void link_append(struct link *list, struct link *link);
struct link *link_iter(struct link *link, int n);
struct link *link_pop(struct link *link);

