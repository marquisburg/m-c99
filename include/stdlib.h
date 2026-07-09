/* C99 <stdlib.h> */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code);
int abs(int x);
long labs(long x);
void abort(void);
int atoi(const char *s);
long atol(const char *s);

#endif
