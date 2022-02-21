#pragma once

#include <wchar.h>

#include <stdbool.h>

#define CMD_SET_STATUS(...) do { \
		free(cmd_status); \
		cmd_status = aprintf(__VA_ARGS__); \
	} while (0)

typedef bool (*cmd_func)(const wchar_t *args);

struct cmd {
	const wchar_t *name;
	cmd_func func;
};

void cmd_init(void);
void cmd_deinit(void);

extern const struct cmd commands[];
extern const size_t command_count;
extern char *cmd_status;
