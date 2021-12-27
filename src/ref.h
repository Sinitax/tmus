#pragma once

#include "list.h"

struct ref {
	void *data;

	struct link link;
};

struct ref *ref_init(void *data);
void ref_free(void *ref);

void refs_free(struct link *head);
int refs_incl(struct link *head, void *data);
void refs_rm(struct link *head, void *data);
