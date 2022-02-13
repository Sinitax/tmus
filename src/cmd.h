#pragma once

#include <wchar.h>

#include <stdbool.h>

typedef bool (*cmd_func)(const wchar_t *args);

struct cmd {
	const wchar_t *name;
	cmd_func func;
};

void cmd_init(void);

extern const struct cmd commands[];
extern const size_t command_count;
extern wchar_t *cmd_status;
