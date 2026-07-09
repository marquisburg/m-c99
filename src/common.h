/* Shared utilities for the C99Mettle frontend. */
#ifndef C99M_COMMON_H
#define C99M_COMMON_H

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- arena ---- */

typedef struct ArenaBlock {
  struct ArenaBlock *next;
  size_t used;
  size_t cap;
  char data[];
} ArenaBlock;

typedef struct Arena {
  ArenaBlock *head;
  ArenaBlock *current;
} Arena;

void arena_init(Arena *a);
void arena_free(Arena *a);
void *arena_alloc(Arena *a, size_t size);
void *arena_calloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
char *arena_sprintf(Arena *a, const char *fmt, ...);

/* ---- source location ---- */

typedef struct {
  const char *file;
  int line;
  int col;
} SrcLoc;

/* ---- diagnostics ---- */

typedef struct {
  int error_count;
  int warning_count;
} Diag;

void diag_error(Diag *d, SrcLoc loc, const char *fmt, ...);
void diag_error_here(Diag *d, const char *file, int line, int col,
                     const char *fmt, ...);
void diag_warn(Diag *d, SrcLoc loc, const char *fmt, ...);
void fatal(const char *fmt, ...);

/* ---- stretchy buffer (pod) ---- */

#define BUF_HDR(b) (((BufHdr *)(b)) - 1)
#define buf_len(b) ((b) ? BUF_HDR(b)->len : 0)
#define buf_cap(b) ((b) ? BUF_HDR(b)->cap : 0)
#define buf_push(b, x)                                                         \
  do {                                                                         \
    if (buf_len(b) + 1 > buf_cap(b))                                           \
      (b) = buf__grow((b), buf_len(b) + 1, sizeof(*(b)));                      \
    (b)[BUF_HDR(b)->len++] = (x);                                              \
  } while (0)
#define buf_free(b) ((b) ? (free(BUF_HDR(b)), (void)0) : (void)0)

typedef struct {
  size_t len;
  size_t cap;
} BufHdr;

void *buf__grow(void *b, size_t need, size_t elemsz);

/* Read entire file into malloc'd NUL-terminated buffer. Returns NULL on error. */
char *read_file(const char *path, size_t *out_len);

#endif /* C99M_COMMON_H */
