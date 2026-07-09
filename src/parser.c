#include "parser.h"

/* ---- token stream ---- */

static void next(Parser *P) {
  if (P->has_peek) {
    P->tok = P->peek;
    P->has_peek = 0;
  } else {
    P->tok = lexer_next(P->lexer);
  }
}

static Token lookahead(Parser *P) {
  if (!P->has_peek) {
    P->peek = lexer_next(P->lexer);
    P->has_peek = 1;
  }
  return P->peek;
}

static int check(Parser *P, TokenKind k) { return P->tok.kind == k; }

static int match(Parser *P, TokenKind k) {
  if (check(P, k)) {
    next(P);
    return 1;
  }
  return 0;
}

static void expect(Parser *P, TokenKind k) {
  if (!match(P, k)) {
    diag_error(P->diag, P->tok.loc, "expected %s, got %s", token_kind_name(k),
               token_kind_name(P->tok.kind));
  }
}

void parser_init(Parser *P, Lexer *lexer, TypeContext *tc) {
  memset(P, 0, sizeof(*P));
  P->lexer = lexer;
  P->arena = lexer->arena;
  P->diag = lexer->diag;
  P->tc = tc;
  next(P);
}

/* ---- forward decls ---- */

static Node *parse_expr(Parser *P);
static Node *parse_assign(Parser *P);
static Node *parse_cond(Parser *P);
static Node *parse_stmt(Parser *P);
static Node *parse_compound(Parser *P);
static Type *parse_type_name(Parser *P);
static Type *parse_declaration_specifiers(Parser *P, StorageClass *sc,
                                          int *saw_type);
static Type *parse_declarator(Parser *P, Type *base, char **out_name,
                              Node ***out_params, int *out_variadic);
static Node *parse_initializer(Parser *P);
static int is_type_token(Parser *P);

/* ---- expressions (Pratt / precedence climbing via layers) ---- */

static Node *parse_primary(Parser *P) {
  Token t = P->tok;
  if (match(P, TK_INT)) {
    Node *n = node_new(P->arena, EX_INT, t.loc);
    n->ival = t.ival;
    /* type assigned in sema from suffixes */
    n->str = t.is_unsigned ? (char *)"u" : NULL;
    if (t.is_long_long)
      n->enum_val = 2;
    else if (t.is_long)
      n->enum_val = 1;
    return n;
  }
  if (match(P, TK_FLOAT)) {
    Node *n = node_new(P->arena, EX_FLOAT, t.loc);
    n->fval = t.fval;
    n->enum_val = t.is_float ? 1 : 0; /* 1 = float, 0 = double */
    return n;
  }
  if (match(P, TK_CHAR)) {
    Node *n = node_new(P->arena, EX_CHAR, t.loc);
    n->ival = t.ival;
    return n;
  }
  if (match(P, TK_STRING)) {
    Node *n = node_new(P->arena, EX_STRING, t.loc);
    n->str = t.text;
    n->str_len = (size_t)t.ival;
    return n;
  }
  if (match(P, TK_IDENT)) {
    /* label? handled at stmt level. plain ident: */
    Node *n = node_new(P->arena, EX_IDENT, t.loc);
    n->name = t.text;
    return n;
  }
  if (match(P, TK_LPAREN)) {
    /* cast, compound literal, or parenthesized expr */
    if (is_type_token(P)) {
      Type *ty = parse_type_name(P);
      expect(P, TK_RPAREN);
      if (check(P, TK_LBRACE)) {
        Node *n = node_new(P->arena, EX_COMPOUND_LITERAL, t.loc);
        n->decl_type = ty;
        n->init = parse_initializer(P);
        return n;
      }
      Node *inner = parse_cond(P); /* cast has same precedence as unary */
      /* but cast is unary — we need full unary of rest. re-parse as cast of unary */
      Node *n = node_new(P->arena, EX_CAST, t.loc);
      n->decl_type = ty;
      n->lhs = inner;
      return n;
    }
    Node *e = parse_expr(P);
    expect(P, TK_RPAREN);
    return e;
  }
  diag_error(P->diag, t.loc, "expected expression");
  next(P);
  return node_new(P->arena, EX_INT, t.loc);
}

static Node *parse_postfix(Parser *P) {
  Node *e = parse_primary(P);
  for (;;) {
    SrcLoc loc = P->tok.loc;
    if (match(P, TK_LPAREN)) {
      Node *call = node_new(P->arena, EX_CALL, loc);
      call->lhs = e;
      if (!check(P, TK_RPAREN)) {
        do {
          Node *arg = parse_assign(P);
          buf_push(call->stmts, arg);
        } while (match(P, TK_COMMA));
      }
      expect(P, TK_RPAREN);
      e = call;
    } else if (match(P, TK_LBRACKET)) {
      Node *idx = node_new(P->arena, EX_INDEX, loc);
      idx->lhs = e;
      idx->rhs = parse_expr(P);
      expect(P, TK_RBRACKET);
      e = idx;
    } else if (check(P, TK_DOT) || check(P, TK_ARROW)) {
      int is_arrow = check(P, TK_ARROW);
      next(P);
      Token id = P->tok;
      expect(P, TK_IDENT);
      Node *m = node_new(P->arena, EX_MEMBER, loc);
      m->lhs = e;
      m->name = id.text;
      m->is_arrow = is_arrow;
      e = m;
    } else if (match(P, TK_INC)) {
      Node *n = node_new(P->arena, EX_POSTFIX, loc);
      n->op = OP_POSTINC;
      n->lhs = e;
      e = n;
    } else if (match(P, TK_DEC)) {
      Node *n = node_new(P->arena, EX_POSTFIX, loc);
      n->op = OP_POSTDEC;
      n->lhs = e;
      e = n;
    } else {
      break;
    }
  }
  return e;
}

static Node *parse_unary(Parser *P) {
  SrcLoc loc = P->tok.loc;
  if (match(P, TK_INC)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_PREINC;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_DEC)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_PREDEC;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_AMP)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_ADDR;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_STAR)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_DEREF;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_PLUS)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_PLUS;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_MINUS)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_NEG;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_TILDE)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_BITNOT;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_BANG)) {
    Node *n = node_new(P->arena, EX_UNARY, loc);
    n->op = OP_NOT;
    n->lhs = parse_unary(P);
    return n;
  }
  if (match(P, TK_SIZEOF)) {
    if (match(P, TK_LPAREN) && is_type_token(P)) {
      Type *ty = parse_type_name(P);
      expect(P, TK_RPAREN);
      Node *n = node_new(P->arena, EX_SIZEOF_TYPE, loc);
      n->decl_type = ty;
      return n;
    }
    Node *n = node_new(P->arena, EX_SIZEOF_EXPR, loc);
    n->lhs = parse_unary(P);
    return n;
  }
  return parse_postfix(P);
}

static Node *parse_binop_rhs(Parser *P, Node *lhs, int min_prec);

static int binop_prec(TokenKind k) {
  switch (k) {
  case TK_OROR:
    return 1;
  case TK_ANDAND:
    return 2;
  case TK_PIPE:
    return 3;
  case TK_CARET:
    return 4;
  case TK_AMP:
    return 5;
  case TK_EQ:
  case TK_NE:
    return 6;
  case TK_LT:
  case TK_GT:
  case TK_LE:
  case TK_GE:
    return 7;
  case TK_LSHIFT:
  case TK_RSHIFT:
    return 8;
  case TK_PLUS:
  case TK_MINUS:
    return 9;
  case TK_STAR:
  case TK_SLASH:
  case TK_PERCENT:
    return 10;
  default:
    return -1;
  }
}

static Node *parse_binop_rhs(Parser *P, Node *lhs, int min_prec) {
  for (;;) {
    int prec = binop_prec(P->tok.kind);
    if (prec < min_prec)
      return lhs;
    TokenKind opk = P->tok.kind;
    SrcLoc loc = P->tok.loc;
    next(P);
    Node *rhs = parse_unary(P);
    int next_prec = binop_prec(P->tok.kind);
    if (next_prec > prec)
      rhs = parse_binop_rhs(P, rhs, prec + 1);
    Node *n = node_new(P->arena, EX_BINARY, loc);
    n->op = token_to_binop(opk);
    n->lhs = lhs;
    n->rhs = rhs;
    lhs = n;
  }
}

static Node *parse_cond(Parser *P) {
  Node *e = parse_binop_rhs(P, parse_unary(P), 0);
  if (match(P, TK_QUESTION)) {
    Node *n = node_new(P->arena, EX_COND, e->loc);
    n->cond = e;
    n->lhs = parse_expr(P);
    expect(P, TK_COLON);
    n->rhs = parse_cond(P);
    return n;
  }
  return e;
}

static int is_assign_op(TokenKind k) {
  return k == TK_ASSIGN || k == TK_ADD_ASSIGN || k == TK_SUB_ASSIGN ||
         k == TK_MUL_ASSIGN || k == TK_DIV_ASSIGN || k == TK_MOD_ASSIGN ||
         k == TK_AND_ASSIGN || k == TK_OR_ASSIGN || k == TK_XOR_ASSIGN ||
         k == TK_LSHIFT_ASSIGN || k == TK_RSHIFT_ASSIGN;
}

static Node *parse_assign(Parser *P) {
  Node *e = parse_cond(P);
  if (is_assign_op(P->tok.kind)) {
    OpKind op = token_to_assignop(P->tok.kind);
    SrcLoc loc = P->tok.loc;
    next(P);
    Node *n = node_new(P->arena, EX_ASSIGN, loc);
    n->op = op;
    n->lhs = e;
    n->rhs = parse_assign(P);
    return n;
  }
  return e;
}

static Node *parse_expr(Parser *P) {
  Node *e = parse_assign(P);
  while (match(P, TK_COMMA)) {
    Node *n = node_new(P->arena, EX_COMMA, P->tok.loc);
    n->lhs = e;
    n->rhs = parse_assign(P);
    e = n;
  }
  return e;
}

/* ---- types / declarations ---- */

static int is_type_token(Parser *P) {
  switch (P->tok.kind) {
  case TK_VOID:
  case TK_CHAR_KW:
  case TK_SHORT:
  case TK_INT_KW:
  case TK_LONG:
  case TK_FLOAT_KW:
  case TK_DOUBLE:
  case TK_SIGNED:
  case TK_UNSIGNED:
  case TK_BOOL:
  case TK_STRUCT:
  case TK_UNION:
  case TK_ENUM:
  case TK_CONST:
  case TK_VOLATILE:
  case TK_RESTRICT:
    return 1;
  case TK_IDENT:
    return lexer_is_typedef(P->lexer, P->tok.text);
  default:
    return 0;
  }
}

static Type *parse_struct_or_union(Parser *P, int is_union) {
  next(P); /* struct/union */
  const char *tag = NULL;
  if (check(P, TK_IDENT)) {
    tag = P->tok.text;
    next(P);
  }
  Type *st = type_struct_create(P->tc, tag, is_union);
  if (match(P, TK_LBRACE)) {
    while (!check(P, TK_RBRACE) && !check(P, TK_EOF)) {
      StorageClass sc = SC_NONE;
      int saw = 0;
      Type *base = parse_declaration_specifiers(P, &sc, &saw);
      if (!saw) {
        diag_error(P->diag, P->tok.loc, "expected member declaration");
        break;
      }
      do {
        char *name = NULL;
        Type *ty = parse_declarator(P, base, &name, NULL, NULL);
        if (!name) {
          diag_error(P->diag, P->tok.loc, "expected member name");
        } else {
          type_struct_add_member(P->tc, st, name, ty);
        }
      } while (match(P, TK_COMMA));
      expect(P, TK_SEMI);
    }
    expect(P, TK_RBRACE);
    type_struct_finish(st);
  }
  return st;
}

static Type *parse_enum(Parser *P) {
  next(P); /* enum */
  const char *tag = NULL;
  if (check(P, TK_IDENT)) {
    tag = P->tok.text;
    next(P);
  }
  Type *et = (Type *)arena_calloc(P->arena, sizeof(Type));
  et->kind = TY_ENUM;
  et->tag = tag;
  et->size = 4;
  et->align = 4;
  if (match(P, TK_LBRACE)) {
    long long val = 0;
    while (!check(P, TK_RBRACE) && !check(P, TK_EOF)) {
      Token id = P->tok;
      expect(P, TK_IDENT);
      if (match(P, TK_ASSIGN)) {
        /* constant expr: only int literal for now */
        if (check(P, TK_INT) || check(P, TK_CHAR)) {
          val = P->tok.ival;
          next(P);
        } else if (check(P, TK_MINUS)) {
          next(P);
          if (check(P, TK_INT)) {
            val = -P->tok.ival;
            next(P);
          }
        } else {
          diag_error(P->diag, P->tok.loc, "expected integer constant in enum");
        }
      }
      /* stash enumerator as a synthetic global-ish via D_ENUM node later —
       * return list through tag type's members abuse: */
      StructMember m;
      m.name = id.text;
      m.type = et;
      m.offset = (size_t)(uint64_t)val; /* store value in offset */
      buf_push(et->members, m);
      val++;
      if (!match(P, TK_COMMA))
        break;
    }
    expect(P, TK_RBRACE);
  }
  return et;
}

static Type *parse_declaration_specifiers(Parser *P, StorageClass *sc,
                                          int *saw_type) {
  *sc = SC_NONE;
  *saw_type = 0;
  int is_unsigned = 0, is_signed = 0;
  int nlong = 0, nshort = 0;
  enum { BS_NONE, BS_VOID, BS_CHAR, BS_INT, BS_FLOAT, BS_DOUBLE, BS_BOOL,
         BS_STRUCT, BS_ENUM } base = BS_NONE;
  Type *struct_ty = NULL;
  int is_const = 0;

  for (;;) {
    if (match(P, TK_TYPEDEF)) {
      *sc = SC_TYPEDEF;
      continue;
    }
    if (match(P, TK_EXTERN)) {
      *sc = SC_EXTERN;
      continue;
    }
    if (match(P, TK_STATIC)) {
      *sc = SC_STATIC;
      continue;
    }
    if (match(P, TK_AUTO) || match(P, TK_REGISTER) || match(P, TK_INLINE))
      continue;
    if (match(P, TK_CONST) || match(P, TK_VOLATILE) || match(P, TK_RESTRICT)) {
      is_const = 1;
      continue;
    }
    if (match(P, TK_VOID)) {
      base = BS_VOID;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_CHAR_KW)) {
      base = BS_CHAR;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_INT_KW)) {
      base = BS_INT;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_FLOAT_KW)) {
      base = BS_FLOAT;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_DOUBLE)) {
      base = BS_DOUBLE;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_BOOL)) {
      base = BS_BOOL;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_SHORT)) {
      nshort++;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_LONG)) {
      nlong++;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_SIGNED)) {
      is_signed = 1;
      *saw_type = 1;
      continue;
    }
    if (match(P, TK_UNSIGNED)) {
      is_unsigned = 1;
      *saw_type = 1;
      continue;
    }
    if (check(P, TK_STRUCT) || check(P, TK_UNION)) {
      int is_union = check(P, TK_UNION);
      struct_ty = parse_struct_or_union(P, is_union);
      base = BS_STRUCT;
      *saw_type = 1;
      continue;
    }
    if (check(P, TK_ENUM)) {
      struct_ty = parse_enum(P);
      base = BS_ENUM;
      *saw_type = 1;
      continue;
    }
    if (check(P, TK_IDENT) && lexer_is_typedef(P->lexer, P->tok.text)) {
      /* typedef name */
      /* For simplicity, treat as int if we don't track typedef types fully —
       * parser registers typedef names; sema resolves. Store name in tag. */
      Type *t = (Type *)arena_calloc(P->arena, sizeof(Type));
      t->kind = TY_INT; /* placeholder; name in tag for sema */
      t->tag = P->tok.text;
      t->size = 4;
      t->align = 4;
      t->enum_value = 1; /* flag: typedef name ref */
      next(P);
      *saw_type = 1;
      struct_ty = t;
      base = BS_STRUCT;
      continue;
    }
    break;
  }

  Type *ty = P->tc->ty_int;
  switch (base) {
  case BS_NONE:
    if (nshort || nlong || is_signed || is_unsigned)
      ty = P->tc->ty_int;
    else
      ty = P->tc->ty_int;
    break;
  case BS_VOID:
    ty = P->tc->ty_void;
    break;
  case BS_BOOL:
    ty = P->tc->ty_bool;
    break;
  case BS_CHAR:
    if (is_unsigned)
      ty = P->tc->ty_uchar;
    else if (is_signed)
      ty = P->tc->ty_schar;
    else
      ty = P->tc->ty_char;
    break;
  case BS_FLOAT:
    ty = P->tc->ty_float;
    break;
  case BS_DOUBLE:
    ty = nlong ? P->tc->ty_ldouble : P->tc->ty_double;
    break;
  case BS_STRUCT:
  case BS_ENUM:
    ty = struct_ty;
    break;
  case BS_INT:
  default:
    if (nshort) {
      ty = is_unsigned ? P->tc->ty_ushort : P->tc->ty_short;
    } else if (nlong >= 2) {
      ty = is_unsigned ? P->tc->ty_ullong : P->tc->ty_llong;
    } else if (nlong == 1) {
      ty = is_unsigned ? P->tc->ty_ulong : P->tc->ty_long;
    } else {
      ty = is_unsigned ? P->tc->ty_uint : P->tc->ty_int;
    }
    break;
  }
  (void)is_const;
  return ty;
}

static Type *parse_pointers(Parser *P, Type *base) {
  while (match(P, TK_STAR)) {
    while (match(P, TK_CONST) || match(P, TK_VOLATILE) || match(P, TK_RESTRICT))
      ;
    base = type_ptr(P->tc, base);
  }
  return base;
}

static Type *parse_postfix_declarator(Parser *P, Type *base, char **out_name,
                                      Node ***out_params, int *out_variadic);

static Type *parse_direct_declarator(Parser *P, Type *base, char **out_name,
                                     Node ***out_params, int *out_variadic) {
  Type *stub = base;
  if (match(P, TK_LPAREN)) {
    /* could be parenthesized declarator or function params after abstract */
    if (is_type_token(P) || check(P, TK_RPAREN) || check(P, TK_STAR)) {
      /* ambiguous: (type) vs (declarator). If starts with type or ) or *,
       * might still be nested declarator starting with *. Prefer nested
       * declarator if TK_STAR or IDENT or LPAREN. */
      if (check(P, TK_STAR) || check(P, TK_IDENT) ||
          (check(P, TK_LPAREN) && !is_type_token(P))) {
        Type *inner = parse_declarator(P, base, out_name, out_params, out_variadic);
        expect(P, TK_RPAREN);
        stub = parse_postfix_declarator(P, inner, out_name, out_params,
                                        out_variadic);
        return stub;
      }
    }
    /* function parameter list applied later — shouldn't get here without name */
    /* fall through as nested empty */
    expect(P, TK_RPAREN);
  }
  if (check(P, TK_IDENT)) {
    if (out_name)
      *out_name = P->tok.text;
    next(P);
  }
  return parse_postfix_declarator(P, stub, out_name, out_params, out_variadic);
}

static Type *parse_param_list(Parser *P, Type *ret, Node ***out_params,
                              int *out_variadic) {
  Type **ptypes = NULL;
  Node **pnodes = NULL;
  int variadic = 0;
  if (check(P, TK_VOID) && lookahead(P).kind == TK_RPAREN) {
    next(P);
    expect(P, TK_RPAREN);
    Type *ft = type_func(P->tc, ret, NULL, 0, 0);
    if (out_params)
      *out_params = NULL;
    if (out_variadic)
      *out_variadic = 0;
    return ft;
  }
  if (match(P, TK_RPAREN)) {
    Type *ft = type_func(P->tc, ret, NULL, 0, 0);
    ft->oldstyle = 1;
    if (out_params)
      *out_params = NULL;
    if (out_variadic)
      *out_variadic = 0;
    return ft;
  }
  for (;;) {
    if (match(P, TK_ELLIPSIS)) {
      variadic = 1;
      break;
    }
    StorageClass sc = SC_NONE;
    int saw = 0;
    Type *bs = parse_declaration_specifiers(P, &sc, &saw);
    if (!saw)
      bs = P->tc->ty_int;
    char *pname = NULL;
    Type *pt = parse_declarator(P, bs, &pname, NULL, NULL);
    pt = type_decay(P->tc, pt);
    buf_push(ptypes, pt);
    Node *pn = node_new(P->arena, D_VAR, P->tok.loc);
    pn->name = pname;
    pn->decl_type = pt;
    pn->type = pt;
    buf_push(pnodes, pn);
    if (!match(P, TK_COMMA))
      break;
  }
  expect(P, TK_RPAREN);
  Type *ft =
      type_func(P->tc, ret, ptypes, buf_len(ptypes), variadic);
  buf_free(ptypes);
  if (out_params)
    *out_params = pnodes;
  else
    buf_free(pnodes);
  if (out_variadic)
    *out_variadic = variadic;
  return ft;
}

static Type *parse_postfix_declarator(Parser *P, Type *base, char **out_name,
                                      Node ***out_params, int *out_variadic) {
  (void)out_name;
  for (;;) {
    if (match(P, TK_LPAREN)) {
      base = parse_param_list(P, base, out_params, out_variadic);
    } else if (match(P, TK_LBRACKET)) {
      size_t len = 0;
      if (check(P, TK_INT)) {
        len = (size_t)P->tok.ival;
        next(P);
      }
      expect(P, TK_RBRACKET);
      /* wrap: array of base, but declarators nest inside-out.
       * For `int a[3][4]`, we see [3] then [4]; result should be array[3] of
       * array[4] of int. So each new array wraps the current type as element...
       * Actually C declarators: T D[N] means array N of T where T is what D
       * describes. Building left-to-right after name: first [3] makes array[3]
       * of base, then [4] makes array[4] of (array[3] of base) — wrong.
       * Correct: int a[3][4] is array[3] of array[4] of int.
       * First bracket is outermost. So we need to nest: new_array(old).
       * First [3]: base = array[3] of int
       * Second [4]: should be array[3] of array[4] of int.
       * So we replace: array[3] of (array[4] of element).
       */
      if (base->kind == TY_ARRAY) {
        /* deepen: outermost stays, add inner */
        Type *inner = type_array(P->tc, base->base, len);
        base = type_array(P->tc, inner, base->array_len);
      } else if (base->kind == TY_FUNC) {
        /* function returning array — rare */
        base = type_array(P->tc, base, len);
      } else {
        base = type_array(P->tc, base, len);
      }
    } else {
      break;
    }
  }
  return base;
}

static Type *parse_declarator(Parser *P, Type *base, char **out_name,
                              Node ***out_params, int *out_variadic) {
  base = parse_pointers(P, base);
  return parse_direct_declarator(P, base, out_name, out_params, out_variadic);
}

static Type *parse_abstract_declarator(Parser *P, Type *base) {
  base = parse_pointers(P, base);
  if (match(P, TK_LPAREN)) {
    if (is_type_token(P) || check(P, TK_RPAREN)) {
      /* function type */
      /* push back — reparse as postfix on base */
      /* simplified: treat as function params */
      base = parse_param_list(P, base, NULL, NULL);
      return parse_postfix_declarator(P, base, NULL, NULL, NULL);
    }
    Type *inner = parse_abstract_declarator(P, base);
    expect(P, TK_RPAREN);
    return parse_postfix_declarator(P, inner, NULL, NULL, NULL);
  }
  return parse_postfix_declarator(P, base, NULL, NULL, NULL);
}

static Type *parse_type_name(Parser *P) {
  StorageClass sc;
  int saw = 0;
  Type *base = parse_declaration_specifiers(P, &sc, &saw);
  if (!saw)
    base = P->tc->ty_int;
  return parse_abstract_declarator(P, base);
}

static Node *parse_initializer(Parser *P) {
  if (match(P, TK_LBRACE)) {
    Node *n = node_new(P->arena, EX_INIT_LIST, P->tok.loc);
    if (!check(P, TK_RBRACE)) {
      do {
        if (check(P, TK_RBRACE))
          break;
        buf_push(n->stmts, parse_initializer(P));
      } while (match(P, TK_COMMA));
    }
    expect(P, TK_RBRACE);
    return n;
  }
  return parse_assign(P);
}

/* ---- statements ---- */

static Node *parse_decl_or_stmt(Parser *P);

static Node *parse_if(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_IF);
  expect(P, TK_LPAREN);
  Node *n = node_new(P->arena, ST_IF, loc);
  n->cond = parse_expr(P);
  expect(P, TK_RPAREN);
  n->body = parse_stmt(P);
  if (match(P, TK_ELSE))
    n->els = parse_stmt(P);
  return n;
}

static Node *parse_while(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_WHILE);
  expect(P, TK_LPAREN);
  Node *n = node_new(P->arena, ST_WHILE, loc);
  n->cond = parse_expr(P);
  expect(P, TK_RPAREN);
  n->body = parse_stmt(P);
  return n;
}

static Node *parse_do(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_DO);
  Node *n = node_new(P->arena, ST_DO, loc);
  n->body = parse_stmt(P);
  expect(P, TK_WHILE);
  expect(P, TK_LPAREN);
  n->cond = parse_expr(P);
  expect(P, TK_RPAREN);
  expect(P, TK_SEMI);
  return n;
}

static Node *parse_for(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_FOR);
  expect(P, TK_LPAREN);
  Node *n = node_new(P->arena, ST_FOR, loc);
  if (!check(P, TK_SEMI)) {
    if (is_type_token(P))
      n->init = parse_decl_or_stmt(P);
    else {
      n->init = node_new(P->arena, ST_EXPR, P->tok.loc);
      n->init->lhs = parse_expr(P);
      expect(P, TK_SEMI);
    }
  } else {
    expect(P, TK_SEMI);
  }
  if (!check(P, TK_SEMI))
    n->cond = parse_expr(P);
  expect(P, TK_SEMI);
  if (!check(P, TK_RPAREN))
    n->inc = parse_expr(P);
  expect(P, TK_RPAREN);
  n->body = parse_stmt(P);
  return n;
}

static Node *parse_switch(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_SWITCH);
  expect(P, TK_LPAREN);
  Node *n = node_new(P->arena, ST_SWITCH, loc);
  n->cond = parse_expr(P);
  expect(P, TK_RPAREN);
  n->body = parse_stmt(P);
  return n;
}

static Node *parse_return(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_RETURN);
  Node *n = node_new(P->arena, ST_RETURN, loc);
  if (!check(P, TK_SEMI))
    n->lhs = parse_expr(P);
  expect(P, TK_SEMI);
  return n;
}

static Node *parse_compound(Parser *P) {
  SrcLoc loc = P->tok.loc;
  expect(P, TK_LBRACE);
  Node *n = node_new(P->arena, ST_COMPOUND, loc);
  while (!check(P, TK_RBRACE) && !check(P, TK_EOF)) {
    buf_push(n->stmts, parse_decl_or_stmt(P));
  }
  expect(P, TK_RBRACE);
  return n;
}

static Node *parse_stmt(Parser *P) {
  switch (P->tok.kind) {
  case TK_LBRACE:
    return parse_compound(P);
  case TK_IF:
    return parse_if(P);
  case TK_WHILE:
    return parse_while(P);
  case TK_DO:
    return parse_do(P);
  case TK_FOR:
    return parse_for(P);
  case TK_SWITCH:
    return parse_switch(P);
  case TK_BREAK: {
    Node *n = node_new(P->arena, ST_BREAK, P->tok.loc);
    next(P);
    expect(P, TK_SEMI);
    return n;
  }
  case TK_CONTINUE: {
    Node *n = node_new(P->arena, ST_CONTINUE, P->tok.loc);
    next(P);
    expect(P, TK_SEMI);
    return n;
  }
  case TK_RETURN:
    return parse_return(P);
  case TK_GOTO: {
    Node *n = node_new(P->arena, ST_GOTO, P->tok.loc);
    next(P);
    Token id = P->tok;
    expect(P, TK_IDENT);
    n->name = id.text;
    expect(P, TK_SEMI);
    return n;
  }
  case TK_CASE: {
    Node *n = node_new(P->arena, ST_CASE, P->tok.loc);
    next(P);
    n->lhs = parse_cond(P);
    expect(P, TK_COLON);
    n->body = parse_stmt(P);
    return n;
  }
  case TK_DEFAULT: {
    Node *n = node_new(P->arena, ST_DEFAULT, P->tok.loc);
    next(P);
    expect(P, TK_COLON);
    n->body = parse_stmt(P);
    return n;
  }
  case TK_SEMI: {
    Node *n = node_new(P->arena, ST_NULL, P->tok.loc);
    next(P);
    return n;
  }
  case TK_IDENT: {
    /* label? */
    if (lookahead(P).kind == TK_COLON) {
      Node *n = node_new(P->arena, ST_LABEL, P->tok.loc);
      n->name = P->tok.text;
      next(P);
      expect(P, TK_COLON);
      n->body = parse_stmt(P);
      return n;
    }
    /* fallthrough to expr */
  }
  /* fall through */
  default: {
    Node *n = node_new(P->arena, ST_EXPR, P->tok.loc);
    n->lhs = parse_expr(P);
    expect(P, TK_SEMI);
    return n;
  }
  }
}

static Node *parse_function_definition(Parser *P, StorageClass sc, Type *base,
                                       char *name, Type *ftype, Node **params) {
  (void)base;
  Node *fn = node_new(P->arena, D_FUNC, P->tok.loc);
  fn->name = name;
  fn->decl_type = ftype;
  fn->type = ftype;
  fn->storage = sc;
  fn->params = params;
  fn->is_definition = 1;
  fn->is_variadic = ftype->is_variadic;
  fn->body = parse_compound(P);
  return fn;
}

static Node *parse_decl_or_stmt(Parser *P) {
  if (!is_type_token(P) && P->tok.kind != TK_TYPEDEF &&
      P->tok.kind != TK_EXTERN && P->tok.kind != TK_STATIC &&
      P->tok.kind != TK_AUTO && P->tok.kind != TK_REGISTER &&
      P->tok.kind != TK_INLINE) {
    return parse_stmt(P);
  }

  StorageClass sc = SC_NONE;
  int saw = 0;
  Type *base = parse_declaration_specifiers(P, &sc, &saw);
  if (!saw && sc == SC_NONE) {
    return parse_stmt(P);
  }

  /* struct/enum tag-only: struct S { ... }; */
  if (check(P, TK_SEMI)) {
    next(P);
    Node *n = node_new(P->arena, D_STRUCT, P->tok.loc);
    n->decl_type = base;
    n->type = base;
    return n;
  }

  char *name = NULL;
  Node **params = NULL;
  int variadic = 0;
  Type *ty = parse_declarator(P, base, &name, &params, &variadic);

  /* function definition */
  if (ty->kind == TY_FUNC && check(P, TK_LBRACE)) {
    if (sc == SC_TYPEDEF) {
      diag_error(P->diag, P->tok.loc, "typedef function definition");
    }
    return parse_function_definition(P, sc, base, name, ty, params);
  }

  /* declaration list */
  Node *first = NULL;
  Node *prev = NULL;
  for (;;) {
    Node *d;
    if (sc == SC_TYPEDEF) {
      d = node_new(P->arena, D_TYPEDEF, P->tok.loc);
      if (name)
        lexer_add_typedef(P->lexer, name);
    } else if (ty->kind == TY_FUNC) {
      d = node_new(P->arena, D_FUNC, P->tok.loc);
      d->params = params;
      d->is_variadic = ty->is_variadic;
    } else {
      d = node_new(P->arena, D_VAR, P->tok.loc);
    }
    d->name = name;
    d->decl_type = ty;
    d->type = ty;
    d->storage = sc;
    if (match(P, TK_ASSIGN))
      d->init = parse_initializer(P);

    if (!first)
      first = d;
    else
      prev->next = d;
    prev = d;

    if (!match(P, TK_COMMA))
      break;
    name = NULL;
    params = NULL;
    variadic = 0;
    ty = parse_declarator(P, base, &name, &params, &variadic);
  }
  expect(P, TK_SEMI);

  Node *stmt = node_new(P->arena, ST_DECL, first->loc);
  stmt->lhs = first;
  return stmt;
}

Program *parse_program(Parser *P) {
  Program *prog = (Program *)arena_calloc(P->arena, sizeof(Program));
  while (!check(P, TK_EOF)) {
    /* top-level: only declarations */
    StorageClass sc = SC_NONE;
    int saw = 0;

    if (!is_type_token(P) && P->tok.kind != TK_TYPEDEF &&
        P->tok.kind != TK_EXTERN && P->tok.kind != TK_STATIC &&
        P->tok.kind != TK_INLINE) {
      diag_error(P->diag, P->tok.loc, "expected declaration");
      next(P);
      continue;
    }

    Type *base = parse_declaration_specifiers(P, &sc, &saw);
    if (check(P, TK_SEMI)) {
      next(P);
      Node *n = node_new(P->arena, D_STRUCT, P->tok.loc);
      n->decl_type = base;
      buf_push(prog->decls, n);
      continue;
    }

    char *name = NULL;
    Node **params = NULL;
    int variadic = 0;
    Type *ty = parse_declarator(P, base, &name, &params, &variadic);

    if (ty->kind == TY_FUNC && check(P, TK_LBRACE)) {
      buf_push(prog->decls,
               parse_function_definition(P, sc, base, name, ty, params));
      continue;
    }

    for (;;) {
      Node *d;
      if (sc == SC_TYPEDEF) {
        d = node_new(P->arena, D_TYPEDEF, P->tok.loc);
        if (name)
          lexer_add_typedef(P->lexer, name);
      } else if (ty->kind == TY_FUNC) {
        d = node_new(P->arena, D_FUNC, P->tok.loc);
        d->params = params;
        d->is_variadic = ty->is_variadic;
      } else {
        d = node_new(P->arena, D_VAR, P->tok.loc);
      }
      d->name = name;
      d->decl_type = ty;
      d->type = ty;
      d->storage = sc;
      if (match(P, TK_ASSIGN))
        d->init = parse_initializer(P);
      buf_push(prog->decls, d);

      /* also register enum constants from enum types in base */
      if (base && base->kind == TY_ENUM) {
        for (size_t i = 0; i < buf_len(base->members); i++) {
          Node *en = node_new(P->arena, D_ENUM, d->loc);
          en->name = base->members[i].name;
          en->ival = (long long)base->members[i].offset;
          en->type = base;
          en->decl_type = base;
          buf_push(prog->decls, en);
        }
      }

      if (!match(P, TK_COMMA))
        break;
      name = NULL;
      params = NULL;
      ty = parse_declarator(P, base, &name, &params, &variadic);
    }
    expect(P, TK_SEMI);
  }
  return prog;
}
