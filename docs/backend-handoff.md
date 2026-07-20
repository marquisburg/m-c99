# Backend work handoff: libmtlc

Everything here is work in `G:\Projects\MettleToolchain`, the C source for the
`libmtlc` backend. `G:\Projects\C99Mettle` (the C99 frontend, GitHub
`marquisburg/m-c99`) vendors the built archive at `libmtlc/lib/mtlc.lib` and is
where the bugs were found.

Three tasks, in dependency order. **Task 1 blocks the other two.**

---

## Task 1: make the vendored archive reproducible (m-c99 issue #15)

**This is the blocker. Nothing else can ship until it is done.**

`C99Mettle/libmtlc/lib/mtlc.lib` is a 4.7MB prebuilt archive committed to the
frontend repo. It cannot be rebuilt from MettleToolchain at `main` (`0395abc`).
An archive built from that source makes the frontend crash while compiling
ordinary C that the shipped archive handles without complaint.

### Reproduce

Build the archive (script below), install it, relink the frontend, then:

```
$ cd G:\Projects\C99Mettle
$ bin/c99mtlc.exe -O0 -I include tests/diff/multidim.c -o out.exe
Access violation in generated code when reading 0x58
```

`tests/diff/multidim.c` is a nested loop over `int a[3][4]` summing elements.
Nothing exotic. 12 of the 25 cases in `tests/diff/` stop compiling.

### What has already been ruled out

Do not redo these:

- **Not caused by the Task 2 change.** Rebuilding with `MIR_MAX_PARAMS` back at
  its original 16 crashes identically.
- **Not one bad commit.** Built at `56d4c41^` (i.e. `856409f`, before
  *Revert the R8/R9 pool exclusion*, the likeliest suspect since it touches
  register allocation): crashes there too.
- **Not build flags.** The build uses the Makefile's own `CFLAGS` and source
  list. The size difference (4.7MB shipped against 7.7MB rebuilt) is `-g`.

### Most likely explanation

The shipped archive was built from a working tree carrying fixes that were
never committed to MettleToolchain. Notes from the session that produced it
describe three quadratic fixes, in `globals.c` (a written-name hash set in
`collect_global_constants`), `ir.c` (a symbol-lookup index) and
`mir_regalloc.c` (back-edge hoisting plus a per-register clobber-position
index), as uncommitted at the time. Some of that work later landed (the
function-pointer API and unsigned arithmetic are in `58b5815`), so the trees
have partially converged but evidently not fully.

Starting points: find what in `main` regressed relative to the archive, or
recover the original tree state. A bisect using the repro above is viable; each
build is roughly 3 to 5 minutes.

### Done when

A fresh build from a committed MettleToolchain state passes, in C99Mettle:

```
bash tests/difftest.sh tests/diff/*.c      # 24 pass, 1 fail (many_args, Task 2)
powershell -File tests/run_suite.ps1        # 41/41
```

Then also commit a build script to C99Mettle so nobody has to reconstruct the
source list from the Makefile again.

---

## Task 2: wide calls clobber the caller's locals (m-c99 issue #14)

**Root cause found, fix written and verified. It only needs Task 1 to ship.**

Already committed on branch `fix-wide-call-frame` (`afc086e`) in the
MettleToolchain tree, one line plus a comment in
`src/codegen/binary/mir.h`. **Not pushed**: that repo's remote is
`The-Mettle-Project/mettle-core`, a different org, so pushing was left as the
owner's call.

### The bug

```c
static int wide(int a, /* ... 17 params ... */) { return a; }
int main(void) {
    double keep = 3.75;
    wide(1, /* ... 17 args ... */);
    return (int)keep;          /* reads 0, not 3 */
}
```

`keep` is never passed to `wide` and never written after its initializer. The
call alone destroys it. Reproduction: `C99Mettle/tests/diff/many_args.c`.

### Cause

`MIR_MAX_PARAMS` is 16 (`src/codegen/binary/mir.h:354`). A call with more
arguments fails `mir_call_is_supported` (`mir_lower.c:988`, indirect variant at
`:808`, callee parameter list at `:1140`), which disqualifies the **whole
enclosing function** from the MIR backend and drops it onto the baseline
emitter.

The baseline prologue has no outgoing-argument term at all: `abi.c:1699-1708`
sums parameter homes, local homes, temp homes, the indirect-return area and
saved registers, and nothing for the arguments its own calls push. Each call
site then does its own `sub rsp` (`emit.c:3368-3383`) and the caller's locals
get overwritten. Floating-point locals go first.

By contrast the MIR path does reserve it (`mir_encode.c:1119-1148`) and does
track `outgoing_stack_bytes` per function (`mir_lower.c:3262-3264`).

So this was never about argument passing. It is a backend-selection cliff,
which is why 16 was fine and 17 was not, and why the threshold moved with frame
shape. Confirm directly:

```
METTLE_MIR_TRACE=1 bin/c99mtlc.exe -O0 -I include tests/diff/many_args.c -o out.exe
MIR-CALLBAIL    args>max
MIR-BAIL        call_unsupported        main
```

### The fix that is committed

Raise `MIR_MAX_PARAMS` from 16 to 32. Everything keyed off it is a fixed-size
array that scales (`mir.h:380`, `mir_encode.c:1339-1352`,
`mir_lower.c:1144-1171`, `:3228-3270`, `:3499-3500`).

Verified with the rebuilt archive: calls of 16, 17, 20, 24 and 32 arguments all
leave the caller's `double` intact, and the function no longer bails.

### What it does not fix

Raising the ceiling moves the cliff, it does not remove it. A call with more
than 32 arguments still falls off. The complete fix is to give the baseline
prologue an outgoing-argument term, or to stop having two frame models. Worth
deciding which you want before merging the one-liner.

Note also `tests/test_call_many_args.mettle` only exercises 8 arguments, so the
backend's own suite has no coverage of this boundary. Worth adding.

---

## Task 3: narrow values are not canonicalized into temporaries (m-c99 issue #13)

**Worked around in the frontend. The proper fix is here and is optional.**

```c
int x = 15;
(x << 28) >> 28        /* gives 15, should give -1 */
int y = x << 28; y >> 28   /* correct */
```

The opcode is right: the shift already lowers to `SAR`
(`mir_lower.c:2876-2887`). The problem is that the operand is not canonical.
The backend computes in 64-bit registers and only sign-extends a narrow value
on the way into a **named** local: `mir_dest_integer_narrow_width`
(`mir_lower.c:532-553`) returns 0 unless `dest->kind == IR_OPERAND_SYMBOL` and
the name resolves as a local or parameter. Its caller is `mir_lower.c:5128`.

So `int y = x << 28` gets a `movsxd` and `y >> 28` is right, while the
temporary holding `(x << 28)` keeps clean zeros above bit 31 and the arithmetic
shift finds no sign bit.

The same hole exists on the fallback path:
`binary_canonicalize_narrow_dest_reg` (`emit.c:4315-4321`) calls
`get_operand_type_in_context` on the destination, which returns NULL for a
temporary, and `binary_canonicalize_narrow_reg_for_type` (`emit.c:4290`)
no-ops on a NULL type.

### The proper fix

Give temporaries a type and canonicalize them. Two halves:

1. Extend the operand-type lookup (`abi.c:1087-1147`) to match
   `IR_OPERAND_TEMP` destinations, not only `IR_OPERAND_SYMBOL`. The
   builder-API path already bakes `value_type` onto the defining instruction
   (`mtlc_build.c:471-484`, copied at `ir.c:1003-1006`), so the information is
   there for frontends that use it.
2. Extend `mir_dest_integer_narrow_width` to accept those destinations.

This would also cover `<<` overflow, `+`/`*` wraparound and the `/`, `%` and
compare paths on temporaries, which are currently relying on the same luck.

### Current state

C99Mettle works around it: a signed right shift whose left operand is computed
copies it into a local first, which is exactly what the backend needs to see
(commit `8d482bd`, `src/C99/Lower.hs`). `tests/diff/shift_nested.c` passes.

If Task 3 is done properly, that workaround can be removed and the test will
still pass. It is marked as a workaround in the code comment, the commit and
the issue.

---

## Building the archive

There is no `make` on this machine. This mirrors the Makefile's source list and
flags exactly; it produces `bin_c99m_libmtlc.a` in the MettleToolchain root.

```bash
#!/usr/bin/env bash
set -eu
cd /g/Projects/MettleToolchain
SRC=src; OUT=obj_c99m
CFLAGS="-Wall -Wextra -std=c99 -g -O2 -D_GNU_SOURCE -Isrc -Iinclude -fno-omit-frame-pointer -pthread"

# libmtlc excludes the reference frontend's lowering TUs
LOWERING="ir/ir_lowering.c ir/ir_lower_address.c ir/ir_lower_defer.c \
ir/ir_lower_expr.c ir/ir_lower_stmt.c ir/ir_lower_support.c \
ir/ir_lower_switch_match.c ir/ir_lower_types.c"

srcs=""
for f in $SRC/ir/*.c; do
  base=${f#$SRC/}; skip=0
  for l in $LOWERING; do [ "$base" = "$l" ] && skip=1; done
  [ $skip -eq 0 ] && srcs="$srcs $f"
done
srcs="$srcs $SRC/common.c $(ls $SRC/ir/optimizer/*.c)"
srcs="$srcs $SRC/codegen/binary_emitter.c $SRC/codegen/code_generator.c"
srcs="$srcs $SRC/codegen/elf_emitter.c $SRC/codegen/ptx_emitter.c $SRC/codegen/spirv_emitter.c"
srcs="$srcs $(ls $SRC/codegen/binary/*.c) $(ls $SRC/linker/*.c)"
srcs="$srcs $SRC/debug/debug_info.c $SRC/error/error_reporter.c"
srcs="$srcs $SRC/compiler/compiler_context.c $SRC/compiler/compiler_crash.c"
srcs="$srcs $SRC/mtlc_api.c $SRC/mtlc_build.c $SRC/mtlc_lib_fallbacks.c"

mkdir -p "$OUT"
for f in $srcs; do
  o="$OUT/$(echo "${f#$SRC/}" | tr '/' '_' | sed 's/\.c$/.o/')"
  gcc $CFLAGS -c "$f" -o "$o"
done
rm -f bin_c99m_libmtlc.a
ar rcs bin_c99m_libmtlc.a $OUT/*.o
```

Install and test it:

```bash
cd /g/Projects/C99Mettle
cp libmtlc/lib/mtlc.lib /tmp/mtlc.lib.backup          # ALWAYS back it up first
cp /g/Projects/MettleToolchain/bin_c99m_libmtlc.a libmtlc/lib/mtlc.lib
export PATH="/c/ghcup/bin:$PATH"
cmd.exe /c build.bat                                   # relinks c99mtlc, ~2 min
bash tests/difftest.sh tests/diff/*.c                  # the oracle
powershell -File tests/run_suite.ps1                   # 41 cases
```

The shipped archive's SHA-256 begins `e214575958`, size 4901858 bytes. Restore
it with the backup if a rebuild misbehaves, then confirm
`git status --short libmtlc/` is empty.

---

## Working notes

- **Verify with `tests/difftest.sh`.** It compiles each case with gcc and with
  c99mtlc, runs both and compares output, at `-O0` and `-O1`. gcc is the
  oracle. A backend miscompile shows up as the two optimization levels
  disagreeing. Every case is strictly conforming C99 on purpose; keep it that
  way or a disagreement proves nothing.
- **Do not run the binaries in `G:\Projects\Mettle\examples\`.** They are
  CPU-saturating benchmark loops and they take the machine down. Building them
  is fine; running them is not.
- **Do not push MettleToolchain to its remote** without asking. It points at
  `The-Mettle-Project/mettle-core`, which is a different org from the frontend.
- **`bin/*.exe` in C99Mettle are tracked but are build output.** They show as
  dirty after every build. Leave them out of commits.
- Building the frontend needs GHC on PATH: `export PATH="/c/ghcup/bin:$PATH"`,
  then `cmd.exe /c build.bat`. Do not pipe that through `head`; on this setup
  the pipe never closes and the build appears to hang.
