#pragma once

#include "list.h"

struct ref {
	void *data;

	struct link link;
};

struct ref *ref_alloc(void *data);
void ref_free(void *ref);

void refs_free(struct list *list);
int refs_index(struct list *list, void *data);
int refs_incl(struct list *list, void *data);
void refs_rm(struct list *list, void *data);

