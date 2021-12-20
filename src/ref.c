#include "ref.h"
#include "util.h"

struct ref *
ref_init(void *data)
{
	struct ref *ref;

	ref = malloc(sizeof(struct ref));
	ASSERT(ref != NULL);
	ref->link = LINK_EMPTY;
	ref->data = data;
	return ref;
}

void
ref_free(struct ref *ref)
{
	free(ref);
}

void
refs_free(struct link *head)
{
	struct link *cur;

	while (head->next) {
		cur = link_pop(head->next);
		ref_free(UPCAST(cur, struct ref));
	}
}

static struct link *
refs_ffind(struct link *head, void *data)
{
	struct link *iter;

	for (iter = head->next; iter; iter = iter->next) {
		if (UPCAST(iter, struct ref)->data == data)
			return iter;
	}

	return NULL;
}

int
refs_incl(struct link *head, void *data)
{
	struct link *ref;

	ref = refs_ffind(head, data);
	return ref != NULL;
}

void
refs_rm(struct link *head, void *data)
{
	struct link *ref;
	struct ref *dataref;

	ref = refs_ffind(head, data);
	if (!ref) return;

	dataref = UPCAST(ref, struct ref);
	link_pop(ref);
	free(dataref);
}

