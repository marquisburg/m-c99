/* C99 <string.h> */
#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
void *memset(void *p, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
char *strchr(const char *s, int c);

#ifdef C99MTLC_STRING_IMPL

size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (ca != cb)
      return (int)ca - (int)cb;
    if (ca == 0)
      return 0;
  }
  return 0;
}

char *strcpy(char *d, const char *s) {
  char *r = d;
  while ((*d++ = *s++))
    ;
  return r;
}

char *strncpy(char *d, const char *s, size_t n) {
  size_t i;
  for (i = 0; i < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = 0;
  return d;
}

void *memset(void *p, int c, size_t n) {
  unsigned char *b = (unsigned char *)p;
  size_t i;
  for (i = 0; i < n; i++)
    b[i] = (unsigned char)c;
  return p;
}

void *memcpy(void *d, const void *s, size_t n) {
  unsigned char *dd = (unsigned char *)d;
  const unsigned char *ss = (const unsigned char *)s;
  size_t i;
  for (i = 0; i < n; i++)
    dd[i] = ss[i];
  return d;
}

void *memmove(void *d, const void *s, size_t n) {
  unsigned char *dd = (unsigned char *)d;
  const unsigned char *ss = (const unsigned char *)s;
  size_t i;
  if (dd < ss) {
    for (i = 0; i < n; i++)
      dd[i] = ss[i];
  } else {
    i = n;
    while (i) {
      i--;
      dd[i] = ss[i];
    }
  }
  return d;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *aa = (const unsigned char *)a;
  const unsigned char *bb = (const unsigned char *)b;
  size_t i;
  for (i = 0; i < n; i++) {
    if (aa[i] != bb[i])
      return (int)aa[i] - (int)bb[i];
  }
  return 0;
}

char *strchr(const char *s, int c) {
  while (*s) {
    if ((unsigned char)*s == (unsigned char)c)
      return (char *)s;
    s++;
  }
  if (c == 0)
    return (char *)s;
  return 0;
}

#endif /* C99MTLC_STRING_IMPL */

#endif /* _STRING_H */
