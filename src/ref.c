#include "ref.h"
#include "util.h"

struct ref *
ref_init(void *data)
{
	struct ref *ref;

	ref = malloc(sizeof(struct ref));
	if (!ref) return NULL;
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

static struct link *
refs_ffind(struct list *list, void *data)
{
	struct link *iter;
	struct ref *ref;

	for (LIST_ITER(list, iter)) {
		ref = UPCAST(iter, struct ref);
		if (ref->data == data)
			return iter;
	}

	return NULL;
}

int
refs_index(struct list *list, void *data)
{
	struct link *iter;
	struct ref *ref;
	int index;

	index = 0;
	for (LIST_ITER(list, iter)) {
		ref = UPCAST(iter, struct ref);
		if (ref->data == data)
			return index;
		index++;
	}

	return -1;
}

int
refs_incl(struct list *list, void *data)
{
	struct link *ref;

	ref = refs_ffind(list, data);
	return ref != NULL;
}

void
refs_rm(struct list *list, void *data)
{
	struct link *ref;
	struct ref *dataref;

	ref = refs_ffind(list, data);
	if (!ref) return;

	dataref = UPCAST(ref, struct ref);
	link_pop(ref);
	free(dataref);
}

