#pragma once

#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define ARRLEN(x) (sizeof(x)/sizeof((x)[0]))

#define PANIC(...) panic(__FILE__, __LINE__, "" __VA_ARGS__)
#define ASSERT(x) assert((x), __FILE__, __LINE__, #x)

int strnwidth(const char *s, int n);

void panic(const char *file, int line, const char *msg, ...);
void assert(int cond, const char *file, int line, const char *condstr);

char *aprintf(const char *fmtstr, ...);
char *appendstrf(char *alloc, const char *fmtstr, ...);

char *sanitized(const char *instr);

const char *timestr(unsigned int seconds);
