# Differential tests

Each `.c` file here is compiled twice, once by gcc and once by c99mtlc, run
both ways, and the output compared. gcc is the oracle.

```
bash tests/difftest.sh tests/diff/*.c
```

Every case must be strictly conforming C99 with no undefined or
implementation-defined behaviour, or gcc's answer is not authoritative and a
disagreement proves nothing.

The harness runs c99mtlc at both `-O0` and `-O1`, so it catches a backend
miscompile (the two levels disagree) as well as a frontend one (both disagree
with gcc).

## Current state

One of these fails, and it is checked in deliberately: it is a real,
reproduced miscompile and the file is the smallest program that shows it.

| File | What is wrong | Issue |
|---|---|---|
| `many_args.c` | a call with many arguments clobbers the caller's floating-point locals | #14 |

That one is a backend fault. The fix is known and is one line in
MettleToolchain, but it cannot be shipped from here yet; see the issue.

The other twenty-four pass, including the eight that were failing when this
directory was created: `++`/`--` on a scalar global, signed bit-fields,
`int >> unsigned`, a nested shift, a `case` in a nested block (and Duff's
device), flexible array members, a function pointer returning a struct, and a
variadic call destroying a `double`.

## Adding a case

Print results rather than returning them: an exit code is one byte and hides
the interesting half of a difference. Print integers, or the bit pattern of a
float, so the comparison does not depend on how a libc formats `%f`.
