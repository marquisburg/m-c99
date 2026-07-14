# Haskell frontend

A C99 frontend that lowers to [libmtlc](../libmtlc/) through its C API, built
alongside the C frontend in [`../src`](../src). Both produce native x86-64 PE
executables and both pass [`tests/run_suite.ps1`](../tests/run_suite.ps1).

```bat
build_hs.bat
bin\c99mtlc-hs.exe tests\fib.c -o bin\fib.exe

rem the same suite, against either frontend
set C99MTLC=%CD%\bin\c99mtlc-hs.exe
powershell -File tests\run_suite.ps1
```

Requires GHC (via [GHCup](https://www.haskell.org/ghcup/)). Every dependency is
a GHC boot library, so `build_hs.bat` uses `ghc --make` and needs no package
index; `c99mtlc-hs.cabal` is there for `cabal build` if you prefer it.

## Layout

| | |
|---|---|
| `src/C99/Preprocess.hs` | `#include`, macros, `#if`, and the `# n "file"` line markers the lexer resyncs on |
| `src/C99/Lexer.hs` | tokens |
| `src/C99/Parser.hs` | recursive descent; carries the typedef table the C grammar needs to stay unambiguous |
| `src/C99/CType.hs` | the C type system, layout, and the usual arithmetic conversions |
| `src/C99/Ast.hs` | the AST, and the symbol table's shape |
| `src/C99/Sema.hs` | scopes, type checking, `__int128` rewriting |
| `src/C99/Lower.hs` | libmtlc IR generation |
| `src/C99/StaticRename.hs` | file-scope `static` mangling, so merged units don't collide |
| `src/Mtlc.hs`, `src/Mtlc/FFI.hs` | the libmtlc binding |
| `cbits/blob.c` | the one `MtlcType` that `build.h` has no constructor for |
| `app/Main.hs` | the driver |

The C frontend's one `Node` struct becomes three ADTs (`Expr`, `Stmt`, `Decl`),
and its in-place mutation becomes a rebuilt tree: sema returns a program in
which every expression has a type and every name resolves to a `SymId`.
Symbols are referenced by id rather than by value because sema keeps mutating
them after the reference is made — `&x` marks `x` address-taken long after `x`'s
declaration was walked.

## Bugs in the C frontend that this port exposed

Porting each pass against its C original surfaced three defects in `../src`.
The Haskell frontend does the right thing in each case; the C one still does
not.

- **Block-scope statics are not static.** `src/lower.c`'s `gen_var_decl` gives
  `static int n;` inside a function ordinary stack storage, so it is
  reinitialized on every call. `int counter(void){static int n=0; n++; return n;}`
  called four times returns 1 from the C frontend and 4 (correct) from this one.
  It also makes `include/stdlib.h`'s `_get_pgmptr` return a pointer to a dead
  stack buffer.
- **A definition can be overwritten by an `extern` re-declaration.** `sema.c`
  lets the last declaration win, so an object declared `extern` in a header and
  defined in one unit ends up flagged extern — and, if the header wrote
  `extern int t[];`, typed as a zero-length array. Here the definition is
  authoritative for both linkage and type.
- **An array bound that does not fold silently aliases offset 0.** For
  `int mix[KIND_COUNT];` where `KIND_COUNT` is `sizeof(KINDS)/sizeof(KINDS[0])`,
  neither frontend's parse-time folder can evaluate `sizeof` of an object, so
  the member's type stays incomplete. `type_struct_finish` then skips it during
  layout but leaves it in the struct at offset 0, where it overlaps the earlier
  members. This port keeps the same (incomplete) member so lookup still finds
  it, and so is bug-compatible here — the member is still misplaced. Fixing it
  properly means retaining member bound expressions and folding them in sema,
  once object types are known.

## Known divergences from the C frontend

Two are deliberate fixes, in places where the C is wrong and the Haskell is
right. Neither can change the behavior of anything the C compiles correctly:

- **Arrays of 3+ dimensions.** `src/parser.c`'s array nesting is right for 1-D
  and 2-D but not beyond: it makes `int a[2][3][4]` an *array[2] of array[4] of
  array[3]*. The Haskell nests generally, and agrees with the C exactly on 1-D
  and 2-D.
- **Abstract declarators.** `parse_abstract_declarator` is marked "simplified"
  in the C and mis-parses `(int (*)(void))` as *function returning int\**. The
  Haskell applies the same pointer-depth rule the concrete declarator path uses,
  so it is a pointer-to-function.

One is carried over deliberately, bug for bug:

- `int (*fp_arr[3])(void)` — an array of function pointers — parses as a plain
  function type, exactly as the C does. Fixing it needs a real declarator-chain
  rewrite rather than a local patch. Plain function pointers, arrays of
  pointers, and pointers to arrays are all correct.
