#pragma once 

#include "link.h"

#include "wchar.h"

#define HISTORY_MAX 100

struct inputln {
	wchar_t *buf;
	int len, cap;
	int cur;

	struct link link;
};

struct history {
	struct link list;
	struct inputln *cmd, *query;
};

void history_init(struct history *history);
void history_free(struct history *history);

void history_submit(struct history *history);

void history_prev(struct history *history);
void history_next(struct history *history);

void history_add(struct history *history, struct inputln *line);
void history_pop(struct inputln *line);

struct inputln *inputln_init(void);
void inputln_free(struct inputln *ln);

void inputln_left(struct inputln *line);
void inputln_right(struct inputln *line);

void inputln_addch(struct inputln *line, wchar_t c);
void inputln_del(struct inputln *line, int n);
