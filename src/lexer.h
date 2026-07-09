#ifndef C99M_LEXER_H
#define C99M_LEXER_H

#include "token.h"

typedef struct {
  const char *path;
  const char *src;
  size_t len;
  size_t pos;
  int line;
  int col;
  Arena *arena;
  Diag *diag;
  /* typedef names known so far (parser updates this for the lexer-less model;
   * we keep a simple set for optional future lexer type-name mode). */
  char **typedef_names; /* stretchy, arena strings */
} Lexer;

void lexer_init(Lexer *L, Arena *arena, Diag *diag, const char *path,
                const char *src, size_t len);
Token lexer_next(Lexer *L);
void lexer_add_typedef(Lexer *L, const char *name);
int lexer_is_typedef(Lexer *L, const char *name);

#endif /* C99M_LEXER_H */
