#include "tag.h"
#include "link.h"

static struct link *
tagrefs_ffind(struct link *head, struct tag *tag)
{
	struct link *iter;

	for (iter = head->next; iter; iter = iter->next) {
		if (UPCAST(iter, struct tag_ref)->tag == tag)
			return iter;
	}

	return NULL;
}

int
tagrefs_incl(struct link *head, struct tag *tag)
{
	struct link *ref;

	ref = tagrefs_ffind(head, tag);
	return ref != NULL;
}

void
tagrefs_add(struct link *head, struct tag *tag)
{
	struct tag_ref *ref;

	if (tagrefs_incl(head, tag))
		return;

	ref = malloc(sizeof(struct tag_ref));
	ASSERT(ref != NULL);
	ref->link = LINK_EMPTY;
	ref->tag = tag;
	link_push_back(head, &ref->link);
}

void
tagrefs_rm(struct link *head, struct tag *tag)
{
	struct link *ref;
	struct tag_ref *tagref;

	ref = tagrefs_ffind(head, tag);
	if (!ref) return;

	tagref = UPCAST(ref, struct tag_ref);
	link_pop(ref);
	free(tagref);
}
