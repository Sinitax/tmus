#pragma once

#include <stdlib.h>
#include <stdbool.h>

#define CMD_SET_STATUS(...) do { \
		free(cmd_status); \
		cmd_status = aprintf(__VA_ARGS__); \
	} while (0)

typedef bool (*cmd_func)(const char *args);

struct cmd {
	const char *name;
	cmd_func func;
};

void cmd_init(void);
void cmd_deinit(void);

bool cmd_run(const char *name, bool *found);
bool cmd_rerun(void);

const struct cmd *cmd_get(const char *name);
const struct cmd *cmd_find(const char *name);

extern const struct cmd commands[];
extern const size_t command_count;
