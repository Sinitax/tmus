#pragma once

#include <stdarg.h>
#include <stdio.h>

void log_init(void);
void log_deinit(void);

void log_info(const char *fmtstr, ...);
