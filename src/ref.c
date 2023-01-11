#include "ref.h"
#include "util.h"

#include <err.h>

struct ref *
ref_alloc(void *data)
{
	struct ref *ref;

	ref = malloc(sizeof(struct ref));
	if (!ref) err(1, "malloc");

	ref->link = LINK_EMPTY;
	ref->data = data;

	return ref;
}

void
ref_free(void *ref)
{
	free(ref);
}

void
refs_free(struct list *list)
{
	list_free(list, ref_free, LINK_OFFSET(struct ref, link));
}

int
refs_index(struct list *list, void *data)
{
	struct link *iter;
	struct ref *ref;
	int index;

	index = 0;
	for (LIST_ITER(list, iter)) {
		ref = UPCAST(iter, struct ref, link);
		if (ref->data == data)
			return index;
		index++;
	}

	return -1;
}

struct link *
refs_find(struct list *list, void *data)
{
	struct link *iter;
	struct ref *ref;

	for (LIST_ITER(list, iter)) {
		ref = UPCAST(iter, struct ref, link);
		if (ref->data == data)
			return iter;
	}

	return NULL;
}

int
refs_incl(struct list *list, void *data)
{
	struct link *ref;

	ref = refs_find(list, data);

	return ref != NULL;
}

void
refs_rm(struct list *list, void *data)
{
	struct link *ref;
	struct ref *dataref;

	ref = refs_find(list, data);
	if (!ref) return;

	dataref = UPCAST(ref, struct ref, link);
	link_pop(ref);
	free(dataref);
}

