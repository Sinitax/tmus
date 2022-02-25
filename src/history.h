#pragma once 

#include "list.h"

#define HISTORY_MAX 100

struct inputln {
	char *buf;
	int len, cap;
	int cur;

	struct link link;
};

struct history {
	struct list list;
	struct inputln *sel, *input;
};

void history_init(struct history *history);
void history_deinit(struct history *history);

void history_submit(struct history *history);

void history_prev(struct history *history);
void history_next(struct history *history);

void history_add(struct history *history, struct inputln *line);

void inputln_init(struct inputln *ln);
void inputln_deinit(struct inputln *ln);

struct inputln *inputln_alloc(void);
void inputln_free(struct inputln *ln);

void inputln_resize(struct inputln *ln, size_t size);

void inputln_left(struct inputln *line);
void inputln_right(struct inputln *line);

void inputln_addch(struct inputln *line, char c);
void inputln_del(struct inputln *line, int n);

void inputln_copy(struct inputln *dst, struct inputln *src);
void inputln_replace(struct inputln *line, const char *str);

