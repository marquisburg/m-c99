/* Macro bodies drop their comments, and #if expands macros in its operands.
 *
 * Two silent miscompiles. A #define body kept a trailing // comment, so the
 * comment landed at every expansion site and commented out the rest of the
 * line. And #if never expanded macros, reading any body that was not a bare
 * integer as 0, so it quietly took the wrong branch.
 *
 * The self-referential macros below also have to terminate: rescanning a
 * macro's own replacement used to loop forever.
 */

#define N 10 // count of things
#define M 3  /* a block comment */
#define WRAP(x) ((x) + M) // trailing comment on a function-like macro

#define VER (1 << 8)
#define INDIRECT_A INDIRECT_B
#define INDIRECT_B 1
#define SIX 6
#define ZERO 0

/* these must not hang the preprocessor */
#define selfref selfref
#define ping pong
#define pong ping

#if VER > 100
#define VER_OK 1
#else
#define VER_OK 0
#endif

#if INDIRECT_A && SIX == 6
#define INDIRECT_OK 1
#else
#define INDIRECT_OK 0
#endif

#if 1 /* a block comment the expression parser used to read as division */
#define BLOCKCMT_OK 1
#else
#define BLOCKCMT_OK 0
#endif

/* defined() has to survive expansion: ZERO is defined but expands to 0 */
#if defined(ZERO) && defined ZERO && !defined(NOT_DEFINED_ANYWHERE)
#define DEFINED_OK 1
#else
#define DEFINED_OK 0
#endif

#if SIX == 5
#define ELIF_PICK 0
#elif SIX == 6
#define ELIF_PICK 1
#else
#define ELIF_PICK 0
#endif

int main(void) {
  int a[N]; /* 10, not a parse error from the trailing comment */
  if (sizeof(a) / sizeof(a[0]) != 10) return 1;
  if (M != 3) return 2;
  if (WRAP(4) != 7) return 3;

  if (!VER_OK) return 4;       /* #if with a parenthesised shift body */
  if (!INDIRECT_OK) return 5;  /* macro that expands to another macro */
  if (!BLOCKCMT_OK) return 6;  /* block comment on the #if line */
  if (!DEFINED_OK) return 7;   /* defined() not clobbered by expansion */
  if (!ELIF_PICK) return 8;    /* #elif takes the same path as #if */

  /* a self-referential macro stays as written rather than looping */
  {
    int selfref = 9;
    int ping = 2;
    if (selfref + ping != 11) return 9;
  }

  return 42;
}
