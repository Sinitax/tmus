#pragma once

#include <stdio.h>
#include <wchar.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define ARRLEN(x) (sizeof(x)/sizeof((x)[0]))

#define PANIC(...) panic(__FILE__, __LINE__, "" __VA_ARGS__)
#define ASSERT(x) assert((x), __FILE__, __LINE__, #x)
#define OOM_CHECK(x) assert((x) != NULL, __FILE__, __LINE__, "Out of Memory!")
#define ERROR(...) error("" __VA_ARGS__)

#define LINK(p) (&(p)->link)
#define UPCAST(iter, type) LINK_UPCAST(iter, type, link)

int strnwidth(const char *s, int n);

void panic(const char *file, int line, const char *msg, ...);
void assert(int cond, const char *file, int line, const char *condstr);
void error(const char *fmtstr, ...);

char *aprintf(const char *fmtstr, ...);
wchar_t *awprintf(const wchar_t *fmtstr, ...);
char *appendstrf(char *alloc, const char *fmtstr, ...);

char *sanitized(const char *instr);

const char *timestr(unsigned int seconds);
