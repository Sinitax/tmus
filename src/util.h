#pragma once

#include <stdbool.h>
#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define ARRLEN(x) (sizeof(x)/sizeof((x)[0]))

#define PANIC(...) panic(__FILE__, __LINE__, "" __VA_ARGS__)
#define ASSERT(x) assert((x), __FILE__, __LINE__, #x)
#define WARNX(...) warn(false, __VA_ARGS__);
#define WARN(...) warn(true, __VA_ARGS__);
#define ERRORX(...) error(false, __VA_ARGS__);
#define ERROR(...) error(true, __VA_ARGS__);

#define UPCAST(iter, type, link) LINK_UPCAST(iter, type, link)

enum {
	USER,
	SYSTEM,
	INTERNAL
};

void panic(const char *file, int line, const char *msg, ...);
void assert(int cond, const char *file, int line, const char *condstr);
void warn(bool add_error, int type, const char *fmtstr, ...);
void error(bool add_error, int type, const char *fmtstr, ...);

char *astrdup(const char *str);
char *aprintf(const char *fmtstr, ...);
char *appendstrf(char *alloc, const char *fmtstr, ...);

char *sanitized(const char *instr);

const char *timestr(unsigned int seconds);
