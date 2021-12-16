#pragma once

#include "link.h"

struct ref {
	void *data;

	struct link link;
};

struct ref *ref_init(void *data);
void ref_free(struct ref *ref);

void refs_free(struct link *head);
int refs_incl(struct link *head, void *data);
void refs_rm(struct link *head, void *data);
