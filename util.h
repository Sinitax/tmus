#pragma once

#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define ARRLEN(x) (sizeof(x)/sizeof((x)[0]))

#define ASSERT(x) assert((x), __FILE__, __LINE__, #x)

int strnwidth(const char *s, int n);
void assert(int cond, const char *file, int line, const char *condstr);
