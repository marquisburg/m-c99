#include "common.h"

#define ARENA_BLOCK_SIZE (1u << 16)

void arena_init(Arena *a) {
  a->head = NULL;
  a->current = NULL;
}

void arena_free(Arena *a) {
  ArenaBlock *b = a->head;
  while (b) {
    ArenaBlock *n = b->next;
    free(b);
    b = n;
  }
  a->head = a->current = NULL;
}

static ArenaBlock *arena_new_block(size_t min_cap) {
  size_t cap = ARENA_BLOCK_SIZE;
  if (min_cap + 64 > cap)
    cap = min_cap + 64;
  ArenaBlock *b = (ArenaBlock *)malloc(sizeof(ArenaBlock) + cap);
  if (!b)
    fatal("out of memory");
  b->next = NULL;
  b->used = 0;
  b->cap = cap;
  return b;
}

void *arena_alloc(Arena *a, size_t size) {
  size_t align = sizeof(void *);
  size = (size + align - 1) & ~(align - 1);
  if (!a->current || a->current->used + size > a->current->cap) {
    ArenaBlock *b = arena_new_block(size);
    if (!a->head)
      a->head = b;
    else
      a->current->next = b;
    a->current = b;
  }
  void *p = a->current->data + a->current->used;
  a->current->used += size;
  return p;
}

void *arena_calloc(Arena *a, size_t size) {
  void *p = arena_alloc(a, size);
  memset(p, 0, size);
  return p;
}

char *arena_strdup(Arena *a, const char *s) {
  size_t n = strlen(s);
  char *p = (char *)arena_alloc(a, n + 1);
  memcpy(p, s, n + 1);
  return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
  char *p = (char *)arena_alloc(a, n + 1);
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

char *arena_sprintf(Arena *a, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0)
    fatal("vsnprintf failed");
  char *p = (char *)arena_alloc(a, (size_t)n + 1);
  vsnprintf(p, (size_t)n + 1, fmt, ap2);
  va_end(ap2);
  return p;
}

void *buf__grow(void *b, size_t need, size_t elemsz) {
  size_t cap = buf_cap(b);
  size_t ncap = cap ? cap * 2 : 8;
  while (ncap < need)
    ncap *= 2;
  BufHdr *h = (BufHdr *)realloc(b ? BUF_HDR(b) : NULL,
                                sizeof(BufHdr) + ncap * elemsz);
  if (!h)
    fatal("out of memory");
  if (!b)
    h->len = 0;
  h->cap = ncap;
  return h + 1;
}

static void diag_vprint(const char *kind, SrcLoc loc, const char *fmt,
                        va_list ap) {
  if (loc.file)
    fprintf(stderr, "%s:%d:%d: %s: ", loc.file, loc.line, loc.col, kind);
  else
    fprintf(stderr, "%s: ", kind);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void diag_error(Diag *d, SrcLoc loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_vprint("error", loc, fmt, ap);
  va_end(ap);
  if (d)
    d->error_count++;
}

void diag_error_here(Diag *d, const char *file, int line, int col,
                     const char *fmt, ...) {
  SrcLoc loc = {file, line, col};
  va_list ap;
  va_start(ap, fmt);
  diag_vprint("error", loc, fmt, ap);
  va_end(ap);
  if (d)
    d->error_count++;
}

void diag_warn(Diag *d, SrcLoc loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_vprint("warning", loc, fmt, ap);
  va_end(ap);
  if (d)
    d->warning_count++;
}

void fatal(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "fatal: ");
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  exit(1);
}

char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = '\0';
  if (out_len)
    *out_len = n;
  return buf;
}
