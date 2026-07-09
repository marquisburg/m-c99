#include "lower.h"

typedef struct LoopCtx {
  const char *break_l;
  const char *continue_l;
  struct LoopCtx *parent;
} LoopCtx;

typedef struct {
  Sema *sema;
  Diag *diag;
  Arena *arena;
  TypeContext *tc;
  MtlcBuilder *builder;
  MtlcFn *fn;
  LoopCtx *loop;
  int label_id;
  int tmp_id;
  int str_id;
  /* map symbol* -> MtlcValue for locals/params in current function */
  Symbol **local_syms;
  MtlcValue *local_vals;
  size_t nlocals;
  /* string literal pools: name of global pointer */
  char **str_names;
  char **str_contents;
  size_t *str_lens;
  /* current function return type */
  Type *ret_type;
  int emitted_return;
} Lower;

static const MtlcType *mtlc_of(Type *t);
static MtlcValue gen_expr(Lower *L, Node *e);
static void gen_stmt(Lower *L, Node *st);
static void gen_bool(Lower *L, Node *e, const char *true_l, const char *false_l);

static char *fresh_label(Lower *L, const char *prefix) {
  return arena_sprintf(L->arena, ".L%s%d", prefix, L->label_id++);
}

static char *fresh_tmp(Lower *L, const char *prefix) {
  return arena_sprintf(L->arena, "%s_%d", prefix, L->tmp_id++);
}

static const MtlcType *mtlc_of(Type *t) {
  if (!t)
    return mtlc_type_scalar(MTLC_TYPE_INT32);
  switch (t->kind) {
  case TY_VOID:
    return mtlc_type_scalar(MTLC_TYPE_VOID);
  case TY_BOOL:
    return mtlc_type_scalar(MTLC_TYPE_BOOL);
  case TY_CHAR:
  case TY_SCHAR:
    return mtlc_type_scalar(MTLC_TYPE_INT8);
  case TY_UCHAR:
    return mtlc_type_scalar(MTLC_TYPE_UINT8);
  case TY_SHORT:
    return mtlc_type_scalar(MTLC_TYPE_INT16);
  case TY_USHORT:
    return mtlc_type_scalar(MTLC_TYPE_UINT16);
  case TY_INT:
  case TY_ENUM:
    return mtlc_type_scalar(MTLC_TYPE_INT32);
  case TY_UINT:
    return mtlc_type_scalar(MTLC_TYPE_UINT32);
  case TY_LONG:
    return mtlc_type_scalar(MTLC_TYPE_INT32);
  case TY_ULONG:
    return mtlc_type_scalar(MTLC_TYPE_UINT32);
  case TY_LLONG:
    return mtlc_type_scalar(MTLC_TYPE_INT64);
  case TY_ULLONG:
    return mtlc_type_scalar(MTLC_TYPE_UINT64);
  case TY_FLOAT:
    return mtlc_type_scalar(MTLC_TYPE_FLOAT32);
  case TY_DOUBLE:
  case TY_LDOUBLE:
    return mtlc_type_scalar(MTLC_TYPE_FLOAT64);
  case TY_PTR:
    if (!t->base || t->base->kind == TY_VOID || t->base->kind == TY_FUNC)
      return mtlc_type_pointer(mtlc_type_scalar(MTLC_TYPE_UINT8));
    return mtlc_type_pointer(mtlc_of(t->base));
  case TY_ARRAY:
    if (!t->base)
      return mtlc_type_pointer(mtlc_type_scalar(MTLC_TYPE_UINT8));
    return mtlc_type_pointer(mtlc_of(t->base));
  case TY_FUNC:
    return mtlc_type_pointer(mtlc_type_scalar(MTLC_TYPE_UINT8));
  case TY_STRUCT:
  case TY_UNION:
    /* Aggregates are addressed as byte pointers; fields use scaled offsets. */
    return mtlc_type_pointer(mtlc_type_scalar(MTLC_TYPE_UINT8));
  }
  return mtlc_type_scalar(MTLC_TYPE_INT32);
}

static MtlcValue local_of(Lower *L, Symbol *sym) {
  for (size_t i = 0; i < L->nlocals; i++)
    if (L->local_syms[i] == sym)
      return L->local_vals[i];
  return MTLC_NO_VALUE;
}

static void bind_local(Lower *L, Symbol *sym, MtlcValue v) {
  size_t n = L->nlocals + 1;
  Symbol **syms = (Symbol **)arena_alloc(L->arena, n * sizeof(Symbol *));
  MtlcValue *vals = (MtlcValue *)arena_alloc(L->arena, n * sizeof(MtlcValue));
  if (L->nlocals) {
    memcpy(syms, L->local_syms, L->nlocals * sizeof(Symbol *));
    memcpy(vals, L->local_vals, L->nlocals * sizeof(MtlcValue));
  }
  syms[L->nlocals] = sym;
  vals[L->nlocals] = v;
  L->local_syms = syms;
  L->local_vals = vals;
  L->nlocals = n;
}

static MtlcValue cast_to(Lower *L, MtlcValue v, Type *from, Type *to) {
  if (!from || !to || type_equal(from, to))
    return v;
  if (from->kind == TY_ARRAY && to->kind == TY_PTR)
    return v; /* already decayed to ptr in gen */
  return mtlc_cast(L->fn, v, mtlc_of(to));
}

static MtlcValue gen_string(Lower *L, const char *data, size_t len) {
  /* Pool by content */
  for (size_t i = 0; i < buf_len(L->str_names); i++) {
    if (L->str_lens[i] == len && memcmp(L->str_contents[i], data, len) == 0) {
      return mtlc_global_ref(L->fn, L->str_names[i]);
    }
  }
  char *gname = arena_sprintf(L->arena, ".str.%d", L->str_id++);
  const MtlcType *i8 = mtlc_type_scalar(MTLC_TYPE_INT8);
  const MtlcType *p = mtlc_type_pointer(i8);
  mtlc_builder_global(L->builder, gname, p, 0, 0);

  /* Materialize on first use: if g==0, malloc and fill. */
  MtlcValue g = mtlc_global_ref(L->fn, gname);
  char *done = fresh_label(L, "strdone");
  char *need = fresh_label(L, "strinit");
  mtlc_branch_if_zero(L->fn, g, need);
  mtlc_jump(L->fn, done);
  mtlc_label(L->fn, need);
  {
    MtlcValue n = mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_UINT64),
                                 (long long)(len + 1));
    MtlcValue args[1] = {n};
    MtlcValue mem =
        mtlc_call(L->fn, "malloc", args, 1, mtlc_type_pointer(i8));
    for (size_t i = 0; i <= len; i++) {
      char ch = (i < len) ? data[i] : 0;
      MtlcValue off =
          mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_UINT64), (long long)i);
      MtlcValue addr =
          mtlc_binary(L->fn, "+", mem, off, mtlc_type_pointer(i8));
      MtlcValue cv = mtlc_const_int(L->fn, i8, (unsigned char)ch);
      mtlc_store(L->fn, addr, cv, i8);
    }
    mtlc_assign(L->fn, g, mem);
  }
  mtlc_label(L->fn, done);
  g = mtlc_global_ref(L->fn, gname);

  buf_push(L->str_names, gname);
  buf_push(L->str_contents, arena_strndup(L->arena, data, len));
  buf_push(L->str_lens, len);
  return g;
}

static MtlcValue decay_value(Node *e, MtlcValue v) {
  (void)e;
  return v;
}

static MtlcValue gen_lvalue_addr(Lower *L, Node *e) {
  switch (e->kind) {
  case EX_IDENT: {
    if (!e->sym)
      return MTLC_NO_VALUE;
    if (e->sym->is_global) {
      MtlcValue g = mtlc_global_ref(L->fn, e->name);
      return mtlc_address_of(L->fn, g, mtlc_type_pointer(mtlc_of(e->type)));
    }
    MtlcValue loc = local_of(L, e->sym);
    return mtlc_address_of(L->fn, loc, mtlc_type_pointer(mtlc_of(e->type)));
  }
  case EX_UNARY:
    if (e->op == OP_DEREF)
      return gen_expr(L, e->lhs);
    break;
  case EX_INDEX: {
    MtlcValue base = gen_expr(L, e->lhs);
    base = decay_value(e->lhs, base);
    MtlcValue idx = gen_expr(L, e->rhs);
    Type *et = e->type;
    size_t esz = et ? et->size : 1;
    const MtlcType *i64 = mtlc_type_scalar(MTLC_TYPE_INT64);
    MtlcValue scale = mtlc_const_int(L->fn, i64, (long long)esz);
    idx = mtlc_cast(L->fn, idx, i64);
    MtlcValue off = mtlc_binary(L->fn, "*", idx, scale, i64);
    const MtlcType *pt = mtlc_type_pointer(mtlc_of(et));
    base = mtlc_cast(L->fn, base, mtlc_type_scalar(MTLC_TYPE_UINT64));
    off = mtlc_cast(L->fn, off, mtlc_type_scalar(MTLC_TYPE_UINT64));
    MtlcValue sum =
        mtlc_binary(L->fn, "+", base, off, mtlc_type_scalar(MTLC_TYPE_UINT64));
    return mtlc_cast(L->fn, sum, pt);
  }
  case EX_MEMBER: {
    MtlcValue base;
    if (e->is_arrow)
      base = gen_expr(L, e->lhs);
    else
      base = gen_lvalue_addr(L, e->lhs);
    const MtlcType *i64 = mtlc_type_scalar(MTLC_TYPE_UINT64);
    MtlcValue off = mtlc_const_int(L->fn, i64, e->ival);
    base = mtlc_cast(L->fn, base, i64);
    MtlcValue sum = mtlc_binary(L->fn, "+", base, off, i64);
    return mtlc_cast(L->fn, sum, mtlc_type_pointer(mtlc_of(e->type)));
  }
  default:
    diag_error(L->diag, e->loc, "not an lvalue");
    break;
  }
  return MTLC_NO_VALUE;
}

static const char *binop_mtlc(OpKind op) {
  switch (op) {
  case OP_ADD: return "+";
  case OP_SUB: return "-";
  case OP_MUL: return "*";
  case OP_DIV: return "/";
  case OP_MOD: return "%";
  case OP_EQ: return "==";
  case OP_NE: return "!=";
  case OP_LT: return "<";
  case OP_LE: return "<=";
  case OP_GT: return ">";
  case OP_GE: return ">=";
  case OP_BITAND: return "&";
  case OP_BITOR: return "|";
  case OP_BITXOR: return "^";
  case OP_LSHIFT: return "<<";
  case OP_RSHIFT: return ">>";
  default: return "+";
  }
}

static int is_cmp(OpKind op) {
  return op == OP_EQ || op == OP_NE || op == OP_LT || op == OP_LE ||
         op == OP_GT || op == OP_GE;
}

/* Short-circuit and general bool branch */
static void gen_bool(Lower *L, Node *e, const char *true_l,
                     const char *false_l) {
  if (e->kind == EX_BINARY && e->op == OP_AND) {
    char *mid = fresh_label(L, "and");
    gen_bool(L, e->lhs, mid, false_l);
    mtlc_label(L->fn, mid);
    gen_bool(L, e->rhs, true_l, false_l);
    return;
  }
  if (e->kind == EX_BINARY && e->op == OP_OR) {
    char *mid = fresh_label(L, "or");
    gen_bool(L, e->lhs, true_l, mid);
    mtlc_label(L->fn, mid);
    gen_bool(L, e->rhs, true_l, false_l);
    return;
  }
  if (e->kind == EX_UNARY && e->op == OP_NOT) {
    gen_bool(L, e->lhs, false_l, true_l);
    return;
  }
  MtlcValue v = gen_expr(L, e);
  mtlc_branch_if_zero(L->fn, v, false_l);
  mtlc_jump(L->fn, true_l);
}

static MtlcValue gen_expr(Lower *L, Node *e) {
  if (!e)
    return MTLC_NO_VALUE;
  switch (e->kind) {
  case EX_INT:
  case EX_CHAR:
    return mtlc_const_int(L->fn, mtlc_of(e->type), e->ival);
  case EX_FLOAT:
    return mtlc_const_float(L->fn, mtlc_of(e->type), e->fval);
  case EX_STRING:
    return gen_string(L, e->str, e->str_len);
  case EX_IDENT: {
    if (!e->sym)
      return mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_INT32), 0);
    if (e->sym->kind == SYM_FUNC) {
      /* function designator: not a first-class value in our subset for now */
      return MTLC_NO_VALUE;
    }
    if (e->sym->is_global)
      return mtlc_global_ref(L->fn, e->name);
    MtlcValue v = local_of(L, e->sym);
    if (e->type && e->type->kind == TY_ARRAY) {
      return mtlc_address_of(L->fn, v, mtlc_type_pointer(mtlc_of(e->type->base)));
    }
    return v;
  }
  case EX_BINARY: {
    if (e->op == OP_AND || e->op == OP_OR) {
      MtlcValue r =
          mtlc_local(L->fn, fresh_tmp(L, "sc"), mtlc_type_scalar(MTLC_TYPE_INT32));
      char *t = fresh_label(L, "t");
      char *f = fresh_label(L, "f");
      char *end = fresh_label(L, "end");
      gen_bool(L, e, t, f);
      mtlc_label(L->fn, t);
      mtlc_assign(L->fn, r, mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_INT32), 1));
      mtlc_jump(L->fn, end);
      mtlc_label(L->fn, f);
      mtlc_assign(L->fn, r, mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_INT32), 0));
      mtlc_label(L->fn, end);
      return r;
    }
    MtlcValue lhs = gen_expr(L, e->lhs);
    MtlcValue rhs = gen_expr(L, e->rhs);
    Type *lt = type_decay(L->tc, e->lhs->type);
    Type *rt = type_decay(L->tc, e->rhs->type);

    /* pointer + int */
    if (e->op == OP_ADD && lt && lt->kind == TY_PTR && type_is_integer(rt)) {
      size_t esz = lt->base ? lt->base->size : 1;
      const MtlcType *u64 = mtlc_type_scalar(MTLC_TYPE_UINT64);
      MtlcValue scale = mtlc_const_int(L->fn, u64, (long long)esz);
      rhs = mtlc_cast(L->fn, rhs, u64);
      MtlcValue off = mtlc_binary(L->fn, "*", rhs, scale, u64);
      lhs = mtlc_cast(L->fn, lhs, u64);
      MtlcValue sum = mtlc_binary(L->fn, "+", lhs, off, u64);
      return mtlc_cast(L->fn, sum, mtlc_of(e->type));
    }
    if (e->op == OP_ADD && rt && rt->kind == TY_PTR && type_is_integer(lt)) {
      size_t esz = rt->base ? rt->base->size : 1;
      const MtlcType *u64 = mtlc_type_scalar(MTLC_TYPE_UINT64);
      MtlcValue scale = mtlc_const_int(L->fn, u64, (long long)esz);
      lhs = mtlc_cast(L->fn, lhs, u64);
      MtlcValue off = mtlc_binary(L->fn, "*", lhs, scale, u64);
      rhs = mtlc_cast(L->fn, rhs, u64);
      MtlcValue sum = mtlc_binary(L->fn, "+", rhs, off, u64);
      return mtlc_cast(L->fn, sum, mtlc_of(e->type));
    }
    if (e->op == OP_SUB && lt && lt->kind == TY_PTR && rt && rt->kind == TY_PTR) {
      const MtlcType *i64 = mtlc_type_scalar(MTLC_TYPE_INT64);
      lhs = mtlc_cast(L->fn, lhs, i64);
      rhs = mtlc_cast(L->fn, rhs, i64);
      MtlcValue diff = mtlc_binary(L->fn, "-", lhs, rhs, i64);
      size_t esz = lt->base ? lt->base->size : 1;
      if (esz > 1) {
        MtlcValue scale = mtlc_const_int(L->fn, i64, (long long)esz);
        diff = mtlc_binary(L->fn, "/", diff, scale, i64);
      }
      return cast_to(L, diff, L->tc->ty_llong, e->type);
    }
    if (e->op == OP_SUB && lt && lt->kind == TY_PTR && type_is_integer(rt)) {
      size_t esz = lt->base ? lt->base->size : 1;
      const MtlcType *u64 = mtlc_type_scalar(MTLC_TYPE_UINT64);
      MtlcValue scale = mtlc_const_int(L->fn, u64, (long long)esz);
      rhs = mtlc_cast(L->fn, rhs, u64);
      MtlcValue off = mtlc_binary(L->fn, "*", rhs, scale, u64);
      lhs = mtlc_cast(L->fn, lhs, u64);
      MtlcValue sum = mtlc_binary(L->fn, "-", lhs, off, u64);
      return mtlc_cast(L->fn, sum, mtlc_of(e->type));
    }

    Type *ct = e->type;
    if (is_cmp(e->op))
      ct = L->tc->ty_int;
    else if (type_is_arithmetic(lt) && type_is_arithmetic(rt))
      ct = type_usual_arith(L->tc, lt, rt);

    lhs = cast_to(L, lhs, lt, ct);
    rhs = cast_to(L, rhs, rt, ct);
    const MtlcType *rt_mtlc =
        is_cmp(e->op) ? mtlc_type_scalar(MTLC_TYPE_INT32) : mtlc_of(ct);
    return mtlc_binary(L->fn, binop_mtlc(e->op), lhs, rhs, rt_mtlc);
  }
  case EX_UNARY: {
    if (e->op == OP_ADDR)
      return gen_lvalue_addr(L, e->lhs);
    if (e->op == OP_DEREF) {
      MtlcValue p = gen_expr(L, e->lhs);
      return mtlc_load(L->fn, p, mtlc_of(e->type));
    }
    if (e->op == OP_PREINC || e->op == OP_PREDEC) {
      MtlcValue addr = gen_lvalue_addr(L, e->lhs);
      MtlcValue cur = mtlc_load(L->fn, addr, mtlc_of(e->lhs->type));
      MtlcValue one = mtlc_const_int(L->fn, mtlc_of(e->type), 1);
      MtlcValue nv =
          mtlc_binary(L->fn, e->op == OP_PREINC ? "+" : "-", cur, one,
                      mtlc_of(e->type));
      mtlc_store(L->fn, addr, nv, mtlc_of(e->lhs->type));
      return nv;
    }
    MtlcValue v = gen_expr(L, e->lhs);
    if (e->op == OP_PLUS)
      return v;
    if (e->op == OP_NEG)
      return mtlc_unary(L->fn, "-", v, mtlc_of(e->type));
    if (e->op == OP_BITNOT)
      return mtlc_unary(L->fn, "~", v, mtlc_of(e->type));
    if (e->op == OP_NOT)
      return mtlc_unary(L->fn, "!", v, mtlc_type_scalar(MTLC_TYPE_INT32));
    return v;
  }
  case EX_POSTFIX: {
    MtlcValue addr = gen_lvalue_addr(L, e->lhs);
    MtlcValue cur = mtlc_load(L->fn, addr, mtlc_of(e->lhs->type));
    MtlcValue one = mtlc_const_int(L->fn, mtlc_of(e->type), 1);
    MtlcValue nv = mtlc_binary(L->fn, e->op == OP_POSTINC ? "+" : "-", cur, one,
                               mtlc_of(e->type));
    mtlc_store(L->fn, addr, nv, mtlc_of(e->lhs->type));
    return cur;
  }
  case EX_ASSIGN: {
    MtlcValue rhs = gen_expr(L, e->rhs);
    if (e->op != OP_ASSIGN) {
      MtlcValue cur;
      MtlcValue addr = gen_lvalue_addr(L, e->lhs);
      cur = mtlc_load(L->fn, addr, mtlc_of(e->lhs->type));
      OpKind bop = OP_ADD;
      switch (e->op) {
      case OP_ADD_A: bop = OP_ADD; break;
      case OP_SUB_A: bop = OP_SUB; break;
      case OP_MUL_A: bop = OP_MUL; break;
      case OP_DIV_A: bop = OP_DIV; break;
      case OP_MOD_A: bop = OP_MOD; break;
      case OP_AND_A: bop = OP_BITAND; break;
      case OP_OR_A: bop = OP_BITOR; break;
      case OP_XOR_A: bop = OP_BITXOR; break;
      case OP_SHL_A: bop = OP_LSHIFT; break;
      case OP_SHR_A: bop = OP_RSHIFT; break;
      default: break;
      }
      rhs = mtlc_binary(L->fn, binop_mtlc(bop), cur, rhs, mtlc_of(e->type));
      mtlc_store(L->fn, addr, rhs, mtlc_of(e->lhs->type));
      return rhs;
    }
    if (e->lhs->kind == EX_IDENT && e->lhs->sym && !e->lhs->sym->is_global) {
      MtlcValue loc = local_of(L, e->lhs->sym);
      rhs = cast_to(L, rhs, e->rhs->type, e->lhs->type);
      mtlc_assign(L->fn, loc, rhs);
      return loc;
    }
    if (e->lhs->kind == EX_IDENT && e->lhs->sym && e->lhs->sym->is_global) {
      MtlcValue g = mtlc_global_ref(L->fn, e->lhs->name);
      rhs = cast_to(L, rhs, e->rhs->type, e->lhs->type);
      mtlc_assign(L->fn, g, rhs);
      return g;
    }
    MtlcValue addr = gen_lvalue_addr(L, e->lhs);
    rhs = cast_to(L, rhs, e->rhs->type, e->lhs->type);
    mtlc_store(L->fn, addr, rhs, mtlc_of(e->lhs->type));
    return rhs;
  }
  case EX_CALL: {
    const char *cname = NULL;
    if (e->lhs->kind == EX_IDENT)
      cname = e->lhs->name;
    else {
      diag_error(L->diag, e->loc, "indirect calls not supported yet");
      return MTLC_NO_VALUE;
    }
    size_t n = buf_len(e->stmts);
    MtlcValue *args =
        n ? (MtlcValue *)arena_alloc(L->arena, n * sizeof(MtlcValue)) : NULL;
    for (size_t i = 0; i < n; i++)
      args[i] = gen_expr(L, e->stmts[i]);
    const MtlcType *rt = mtlc_of(e->type);
    if (e->type && e->type->kind == TY_VOID)
      rt = mtlc_type_scalar(MTLC_TYPE_VOID);
    return mtlc_call(L->fn, cname, args, n, rt);
  }
  case EX_INDEX: {
    MtlcValue addr = gen_lvalue_addr(L, e);
    return mtlc_load(L->fn, addr, mtlc_of(e->type));
  }
  case EX_MEMBER: {
    MtlcValue addr = gen_lvalue_addr(L, e);
    return mtlc_load(L->fn, addr, mtlc_of(e->type));
  }
  case EX_CAST: {
    MtlcValue v = gen_expr(L, e->lhs);
    return mtlc_cast(L->fn, v, mtlc_of(e->type));
  }
  case EX_SIZEOF_EXPR:
  case EX_SIZEOF_TYPE:
    return mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_UINT64), e->ival);
  case EX_COND: {
    MtlcValue r = mtlc_local(L->fn, fresh_tmp(L, "cond"), mtlc_of(e->type));
    char *t = fresh_label(L, "ct");
    char *f = fresh_label(L, "cf");
    char *end = fresh_label(L, "ce");
    gen_bool(L, e->cond, t, f);
    mtlc_label(L->fn, t);
    mtlc_assign(L->fn, r, gen_expr(L, e->lhs));
    mtlc_jump(L->fn, end);
    mtlc_label(L->fn, f);
    mtlc_assign(L->fn, r, gen_expr(L, e->rhs));
    mtlc_label(L->fn, end);
    return r;
  }
  case EX_COMMA:
    gen_expr(L, e->lhs);
    return gen_expr(L, e->rhs);
  default:
    return mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_INT32), 0);
  }
}

static void gen_var_decl(Lower *L, Node *d) {
  if (!d || d->kind != D_VAR || !d->sym)
    return;
  Type *ty = d->type;
  if (ty->kind == TY_ARRAY) {
    /* allocate with malloc */
    size_t bytes = ty->size ? ty->size : (ty->array_len * (ty->base ? ty->base->size : 1));
    MtlcValue n =
        mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_UINT64), (long long)bytes);
    MtlcValue args[1] = {n};
    MtlcValue mem = mtlc_call(L->fn, "malloc", args, 1,
                              mtlc_type_pointer(mtlc_type_scalar(MTLC_TYPE_UINT8)));
    MtlcValue loc = mtlc_local(L->fn, d->name, mtlc_type_pointer(mtlc_of(ty->base)));
    mtlc_assign(L->fn, loc, mtlc_cast(L->fn, mem, mtlc_type_pointer(mtlc_of(ty->base))));
    bind_local(L, d->sym, loc);
    if (d->init && d->init->kind == EX_INIT_LIST) {
      for (size_t i = 0; i < buf_len(d->init->stmts); i++) {
        MtlcValue v = gen_expr(L, d->init->stmts[i]);
        size_t esz = ty->base->size;
        MtlcValue off = mtlc_const_int(L->fn, mtlc_type_scalar(MTLC_TYPE_UINT64),
                                       (long long)(i * esz));
        MtlcValue base = mtlc_cast(L->fn, loc, mtlc_type_scalar(MTLC_TYPE_UINT64));
        MtlcValue addr = mtlc_binary(L->fn, "+", base, off,
                                     mtlc_type_scalar(MTLC_TYPE_UINT64));
        addr = mtlc_cast(L->fn, addr, mtlc_type_pointer(mtlc_of(ty->base)));
        mtlc_store(L->fn, addr, v, mtlc_of(ty->base));
      }
    }
    return;
  }
  MtlcValue loc = mtlc_local(L->fn, d->name, mtlc_of(ty));
  bind_local(L, d->sym, loc);
  if (d->init) {
    MtlcValue v = gen_expr(L, d->init);
    v = cast_to(L, v, d->init->type, ty);
    mtlc_assign(L->fn, loc, v);
  }
}

static void gen_stmt(Lower *L, Node *st) {
  if (!st)
    return;
  switch (st->kind) {
  case ST_NULL:
    break;
  case ST_EXPR:
    gen_expr(L, st->lhs);
    break;
  case ST_COMPOUND:
    for (size_t i = 0; i < buf_len(st->stmts); i++)
      gen_stmt(L, st->stmts[i]);
    break;
  case ST_IF: {
    char *t = fresh_label(L, "then");
    char *f = fresh_label(L, "else");
    char *end = fresh_label(L, "endif");
    gen_bool(L, st->cond, t, f);
    mtlc_label(L->fn, t);
    gen_stmt(L, st->body);
    mtlc_jump(L->fn, end);
    mtlc_label(L->fn, f);
    if (st->els)
      gen_stmt(L, st->els);
    mtlc_label(L->fn, end);
    break;
  }
  case ST_WHILE: {
    char *top = fresh_label(L, "wtop");
    char *body = fresh_label(L, "wbody");
    char *end = fresh_label(L, "wend");
    LoopCtx ctx = {end, top, L->loop};
    L->loop = &ctx;
    mtlc_label(L->fn, top);
    gen_bool(L, st->cond, body, end);
    mtlc_label(L->fn, body);
    gen_stmt(L, st->body);
    mtlc_jump(L->fn, top);
    mtlc_label(L->fn, end);
    L->loop = ctx.parent;
    break;
  }
  case ST_DO: {
    char *body = fresh_label(L, "dbody");
    char *cond = fresh_label(L, "dcond");
    char *end = fresh_label(L, "dend");
    LoopCtx ctx = {end, cond, L->loop};
    L->loop = &ctx;
    mtlc_label(L->fn, body);
    gen_stmt(L, st->body);
    mtlc_label(L->fn, cond);
    gen_bool(L, st->cond, body, end);
    mtlc_label(L->fn, end);
    L->loop = ctx.parent;
    break;
  }
  case ST_FOR: {
    char *top = fresh_label(L, "ftop");
    char *body = fresh_label(L, "fbody");
    char *inc = fresh_label(L, "finc");
    char *end = fresh_label(L, "fend");
    if (st->init)
      gen_stmt(L, st->init);
    LoopCtx ctx = {end, inc, L->loop};
    L->loop = &ctx;
    mtlc_label(L->fn, top);
    if (st->cond)
      gen_bool(L, st->cond, body, end);
    else
      mtlc_jump(L->fn, body);
    mtlc_label(L->fn, body);
    gen_stmt(L, st->body);
    mtlc_label(L->fn, inc);
    if (st->inc)
      gen_expr(L, st->inc);
    mtlc_jump(L->fn, top);
    mtlc_label(L->fn, end);
    L->loop = ctx.parent;
    break;
  }
  case ST_BREAK:
    if (L->loop)
      mtlc_jump(L->fn, L->loop->break_l);
    break;
  case ST_CONTINUE:
    if (L->loop)
      mtlc_jump(L->fn, L->loop->continue_l);
    break;
  case ST_RETURN: {
    if (st->lhs) {
      MtlcValue v = gen_expr(L, st->lhs);
      if (L->ret_type)
        v = cast_to(L, v, st->lhs->type, L->ret_type);
      mtlc_return(L->fn, v);
    } else {
      mtlc_return(L->fn, MTLC_NO_VALUE);
    }
    L->emitted_return = 1;
    break;
  }
  case ST_SWITCH: {
    /* naive chain of comparisons */
    MtlcValue cv = gen_expr(L, st->cond);
    char *end = fresh_label(L, "swend");
    LoopCtx ctx = {end, L->loop ? L->loop->continue_l : end, L->loop};
    L->loop = &ctx;
    /* walk body for case labels — simplified: only compound of cases */
    if (st->body && st->body->kind == ST_COMPOUND) {
      char **labels = NULL;
      Node **cases = NULL;
      Node *def_body = NULL;
      char *def_l = NULL;
      for (size_t i = 0; i < buf_len(st->body->stmts); i++) {
        Node *s = st->body->stmts[i];
        if (s->kind == ST_CASE) {
          char *l = fresh_label(L, "case");
          buf_push(labels, l);
          buf_push(cases, s);
        } else if (s->kind == ST_DEFAULT) {
          def_l = fresh_label(L, "default");
          def_body = s;
        }
      }
      for (size_t i = 0; i < buf_len(cases); i++) {
        MtlcValue k = gen_expr(L, cases[i]->lhs);
        MtlcValue eq =
            mtlc_binary(L->fn, "==", cv, k, mtlc_type_scalar(MTLC_TYPE_INT32));
        char *next = fresh_label(L, "swnext");
        mtlc_branch_if_zero(L->fn, eq, next);
        mtlc_jump(L->fn, labels[i]);
        mtlc_label(L->fn, next);
      }
      if (def_l)
        mtlc_jump(L->fn, def_l);
      else
        mtlc_jump(L->fn, end);
      for (size_t i = 0; i < buf_len(cases); i++) {
        mtlc_label(L->fn, labels[i]);
        gen_stmt(L, cases[i]->body);
        /* fallthrough to next case body intentionally not chained fully —
         * each case node includes following stmts via parse; our parse puts
         * only one stmt in body. Accept limited switch. */
      }
      if (def_l) {
        mtlc_label(L->fn, def_l);
        gen_stmt(L, def_body->body);
      }
      buf_free(labels);
      buf_free(cases);
    } else {
      gen_stmt(L, st->body);
    }
    mtlc_label(L->fn, end);
    L->loop = ctx.parent;
    break;
  }
  case ST_CASE:
  case ST_DEFAULT:
    gen_stmt(L, st->body);
    break;
  case ST_GOTO:
    mtlc_jump(L->fn, arena_sprintf(L->arena, ".G%s", st->name));
    break;
  case ST_LABEL:
    mtlc_label(L->fn, arena_sprintf(L->arena, ".G%s", st->name));
    gen_stmt(L, st->body);
    break;
  case ST_DECL:
    for (Node *d = st->lhs; d; d = d->next)
      gen_var_decl(L, d);
    break;
  default:
    break;
  }
}

static void declare_runtime(MtlcBuilder *b) {
  const MtlcType *i32 = mtlc_type_scalar(MTLC_TYPE_INT32);
  const MtlcType *i64 = mtlc_type_scalar(MTLC_TYPE_INT64);
  const MtlcType *u64 = mtlc_type_scalar(MTLC_TYPE_UINT64);
  const MtlcType *i8 = mtlc_type_scalar(MTLC_TYPE_INT8);
  const MtlcType *pvoid = mtlc_type_pointer(i8);
  const MtlcType *v = mtlc_type_scalar(MTLC_TYPE_VOID);

  const char *pn1[] = {"n"};
  const MtlcType *pt1[] = {u64};
  mtlc_builder_function(b, "malloc", pvoid, pn1, pt1, 1, 1);

  const char *pf1[] = {"p"};
  const MtlcType *pft1[] = {pvoid};
  mtlc_builder_function(b, "free", v, pf1, pft1, 1, 1);

  const char *pc1[] = {"c"};
  const MtlcType *pct1[] = {i32};
  mtlc_builder_function(b, "putchar", i32, pc1, pct1, 1, 1);
  mtlc_builder_function(b, "getchar", i32, NULL, NULL, 0, 1);

  /* printf is variadic — declare as single pointer for limited use */
  const char *pp1[] = {"fmt"};
  const MtlcType *ppt1[] = {pvoid};
  mtlc_builder_function(b, "printf", i32, pp1, ppt1, 1, 1);

  const char *pe1[] = {"code"};
  const MtlcType *pet1[] = {i32};
  mtlc_builder_function(b, "exit", v, pe1, pet1, 1, 1);

  (void)i64;
}

static void gen_function(Lower *L, Node *fn) {
  if (!fn->is_definition)
    return;

  Type *ft = fn->type;
  size_t nparams = buf_len(fn->params);
  const char **pnames =
      nparams ? (const char **)arena_alloc(L->arena, nparams * sizeof(char *))
              : NULL;
  const MtlcType **ptypes =
      nparams ? (const MtlcType **)arena_alloc(L->arena, nparams * sizeof(MtlcType *))
              : NULL;
  for (size_t i = 0; i < nparams; i++) {
    pnames[i] = fn->params[i]->name ? fn->params[i]->name
                                    : arena_sprintf(L->arena, "arg%zu", i);
    ptypes[i] = mtlc_of(fn->params[i]->type);
  }

  MtlcFn *mf = mtlc_builder_function(
      L->builder, fn->name, mtlc_of(ft->base), pnames, ptypes, nparams, 0);
  if (!mf)
    return;

  L->fn = mf;
  L->nlocals = 0;
  L->local_syms = NULL;
  L->local_vals = NULL;
  L->ret_type = ft->base;
  L->emitted_return = 0;
  L->loop = NULL;

  for (size_t i = 0; i < nparams; i++) {
    MtlcValue p = mtlc_fn_param(mf, i);
    /* bind as local copy for mutability if needed — params are already storage */
    if (fn->params[i]->sym)
      bind_local(L, fn->params[i]->sym, p);
  }

  gen_stmt(L, fn->body);

  if (!L->emitted_return) {
    if (ft->base && ft->base->kind == TY_VOID)
      mtlc_return(mf, MTLC_NO_VALUE);
    else
      mtlc_return(mf, mtlc_const_int(mf, mtlc_of(ft->base), 0));
  }
}

MtlcModule *lower_program(Sema *S, Program *prog, Diag *diag) {
  Lower L;
  memset(&L, 0, sizeof(L));
  L.sema = S;
  L.diag = diag;
  L.arena = S->arena;
  L.tc = S->tc;
  L.builder = mtlc_builder_create();
  if (!L.builder) {
    diag_error(diag, (SrcLoc){0}, "failed to create IR builder");
    return NULL;
  }

  declare_runtime(L.builder);

  /* globals first */
  for (size_t i = 0; i < buf_len(prog->decls); i++) {
    Node *d = prog->decls[i];
    if (d->kind == D_VAR && d->name) {
      long long init = 0;
      if (d->init && d->init->kind == EX_INT)
        init = d->init->ival;
      int is_extern = d->storage == SC_EXTERN;
      mtlc_builder_global(L.builder, d->name, mtlc_of(d->type), init, is_extern);
    } else if (d->kind == D_FUNC && !d->is_definition) {
      Type *ft = d->type;
      size_t nparams = ft->param_count;
      const char **pnames =
          nparams ? (const char **)arena_alloc(L.arena, nparams * sizeof(char *))
                  : NULL;
      const MtlcType **ptypes =
          nparams
              ? (const MtlcType **)arena_alloc(L.arena, nparams * sizeof(MtlcType *))
              : NULL;
      for (size_t j = 0; j < nparams; j++) {
        pnames[j] = arena_sprintf(L.arena, "p%zu", j);
        ptypes[j] = mtlc_of(ft->params[j]);
      }
      mtlc_builder_function(L.builder, d->name, mtlc_of(ft->base), pnames, ptypes,
                            nparams, 1);
    }
  }

  /* function bodies */
  for (size_t i = 0; i < buf_len(prog->decls); i++) {
    Node *d = prog->decls[i];
    if (d->kind == D_FUNC && d->is_definition)
      gen_function(&L, d);
  }

  MtlcModule *mod = mtlc_builder_finish(L.builder);
  if (!mod)
    diag_error(diag, (SrcLoc){0}, "IR builder failed");
  return mod;
}
