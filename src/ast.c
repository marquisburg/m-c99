#include "ast.h"

Node *node_new(Arena *a, NodeKind kind, SrcLoc loc) {
  Node *n = (Node *)arena_calloc(a, sizeof(Node));
  n->kind = kind;
  n->loc = loc;
  return n;
}

const char *op_to_string(OpKind op) {
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
  case OP_AND: return "&&";
  case OP_OR: return "||";
  case OP_BITAND: return "&";
  case OP_BITOR: return "|";
  case OP_BITXOR: return "^";
  case OP_LSHIFT: return "<<";
  case OP_RSHIFT: return ">>";
  case OP_NOT: return "!";
  case OP_NEG: return "-";
  case OP_PLUS: return "+";
  case OP_BITNOT: return "~";
  case OP_ADDR: return "&";
  case OP_DEREF: return "*";
  case OP_PREINC:
  case OP_POSTINC: return "++";
  case OP_PREDEC:
  case OP_POSTDEC: return "--";
  case OP_ASSIGN: return "=";
  case OP_ADD_A: return "+=";
  case OP_SUB_A: return "-=";
  case OP_MUL_A: return "*=";
  case OP_DIV_A: return "/=";
  case OP_MOD_A: return "%=";
  case OP_AND_A: return "&=";
  case OP_OR_A: return "|=";
  case OP_XOR_A: return "^=";
  case OP_SHL_A: return "<<=";
  case OP_SHR_A: return ">>=";
  }
  return "?";
}

OpKind token_to_binop(TokenKind k) {
  switch (k) {
  case TK_PLUS: return OP_ADD;
  case TK_MINUS: return OP_SUB;
  case TK_STAR: return OP_MUL;
  case TK_SLASH: return OP_DIV;
  case TK_PERCENT: return OP_MOD;
  case TK_EQ: return OP_EQ;
  case TK_NE: return OP_NE;
  case TK_LT: return OP_LT;
  case TK_LE: return OP_LE;
  case TK_GT: return OP_GT;
  case TK_GE: return OP_GE;
  case TK_ANDAND: return OP_AND;
  case TK_OROR: return OP_OR;
  case TK_AMP: return OP_BITAND;
  case TK_PIPE: return OP_BITOR;
  case TK_CARET: return OP_BITXOR;
  case TK_LSHIFT: return OP_LSHIFT;
  case TK_RSHIFT: return OP_RSHIFT;
  default: return OP_ADD;
  }
}

OpKind token_to_assignop(TokenKind k) {
  switch (k) {
  case TK_ASSIGN: return OP_ASSIGN;
  case TK_ADD_ASSIGN: return OP_ADD_A;
  case TK_SUB_ASSIGN: return OP_SUB_A;
  case TK_MUL_ASSIGN: return OP_MUL_A;
  case TK_DIV_ASSIGN: return OP_DIV_A;
  case TK_MOD_ASSIGN: return OP_MOD_A;
  case TK_AND_ASSIGN: return OP_AND_A;
  case TK_OR_ASSIGN: return OP_OR_A;
  case TK_XOR_ASSIGN: return OP_XOR_A;
  case TK_LSHIFT_ASSIGN: return OP_SHL_A;
  case TK_RSHIFT_ASSIGN: return OP_SHR_A;
  default: return OP_ASSIGN;
  }
}
