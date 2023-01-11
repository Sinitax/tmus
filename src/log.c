#include "log.h"
#include "util.h"

#include <err.h>
#include <stdbool.h>
#include <stdlib.h>

static int log_active;
static FILE *log_file;

void
log_init(void)
{
	const char *envstr;

	log_active = false;

	envstr = getenv("TMUS_LOG");
	if (!envstr) return;

	log_file = fopen(envstr, "w+");
	if (!log_file) err(1, "fopen %s", envstr);

	log_active = true;
}

void
log_deinit(void)
{
	if (!log_active) return;

	fclose(log_file);
	log_active = 0;
}

void
log_info(const char *fmtstr, ...)
{
	va_list ap;

	if (!log_active) return;

	va_start(ap, fmtstr);
	vfprintf(log_file, fmtstr, ap);
	va_end(ap);

	fflush(log_file);
}

