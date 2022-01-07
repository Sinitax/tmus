#include <stdarg.h>
#include <stdio.h>

void log_init(void);
void log_info(const char *fmtstr, ...);
void log_end(void);

extern int log_active;
extern FILE *log_file;
