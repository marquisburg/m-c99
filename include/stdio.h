/* C99 <stdio.h> — declarations; definitions come from the host CRT at link. */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

typedef struct _C99MTLC_FILE FILE;

int putchar(int c);
int getchar(void);
int puts(const char *s);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
void perror(const char *s);

#endif
