#ifndef C99M_PARSER_H
#define C99M_PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
  Lexer *lexer;
  Arena *arena;
  Diag *diag;
  TypeContext *tc;
  Token tok;
  Token peek;
  int has_peek;
} Parser;

void parser_init(Parser *P, Lexer *lexer, TypeContext *tc);
Program *parse_program(Parser *P);

#endif /* C99M_PARSER_H */
