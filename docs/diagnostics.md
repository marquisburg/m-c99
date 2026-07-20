# Diagnostics

What a c99mtlc diagnostic looks like, and how to control it.

## The shape

```
error[E0102]: undeclared identifier 'coutner'
  --> hello.c:6:12
  |
5 | int main(void) {
6 |     return coutner + 1;
  |            ^^^^^^^ not found in this scope
7 | }
   = help: did you mean 'counter'?

error: could not compile `hello.c` due to 1 previous error
```

Five parts, each optional except the first two:

| Part | What it is |
|---|---|
| `error[E0102]:` | severity, a stable code, and the sentence |
| `--> file:line:col` | where, in a form editors can jump to |
| the frame | one line of context either side, with the line underlined |
| `^^^^ label` | what is wrong with *that* span specifically |
| `= help:` | what to do about it |

A diagnostic that points at a second place in the source prints it as a note
with its own frame:

```
error[E0100]: redefinition of 'x'
  --> a.c:2:1
  |
1 | int x = 1;
2 | int x = 2;
  |     ^ redefined here
3 | int main(void) { return x; }
note: previous declaration of 'x' is here
  --> a.c:1:1
  |
1 | int x = 1;
  |     ^
```

The output is ASCII on purpose (`-->`, `|`, `^`, `=`). Box-drawing characters
turn into mojibake on a stock Windows console, and this compiler is built
there.

Everything goes to stderr, so it never pollutes a `-E` pipeline.

## Error codes

Codes are stable: once published, a number keeps its meaning. Ask what one
means:

```
c99mtlc --explain E0102
```

Case and brackets are ignored, so `--explain e0102` and `--explain [E0102]`
both work. `--explain list` prints the index.

## Warnings

Every group is on by default. Turn one off with `-Wno-<group>`, and see the
list with `--help-warnings`.

| Group | Fires on |
|---|---|
| `unused` | a block-scope variable nothing reads |

`-Werror` turns every warning that is still on into an error, and the build
fails. `-w` silences all of them.

The false-positive budget for a warning is zero. `unused` therefore says
nothing about a parameter (an unused one is often required by a signature),
nothing about anything `extern`, `static` or global (another unit may read it),
and nothing about a name starting with `_`, which is how you say "declared on
purpose, not used". The suggested fix names the exact rename:

```
warning: unused variable 'scratch'
  --> a.c:3:5
  |
2 |     int used = 1;
3 |     int scratch = 2;
  |         ^^^^^^^ declared here and never read
4 |     return used;
   = help: remove it, or rename it to '_scratch' to keep it and say so on purpose
```

## Colour

`--color=auto` (the default) uses colour when stderr is a terminal. The
environment overrides it in both directions, in the order everyone else
settled on:

1. `CLICOLOR_FORCE` set and not `0`: colour, even when piped
2. `NO_COLOR` set: no colour
3. `TERM=dumb`: no colour
4. otherwise: colour when stderr is a terminal

`--color=always` and `--color=never` skip all of that.

## Machine-readable output

`--error-format=json` writes one JSON object per line:

```json
{"severity":"error","file":"hello.c","line":6,"column":12,"length":7,
 "message":"undeclared identifier 'coutner'","code":"E0102",
 "label":"not found in this scope","help":"did you mean 'counter'?"}
```

`length` is the caret run, so an editor can underline the same span. Notes
appear as a nested `notes` array.

## How many errors you get

One mistake should report once. Three things enforce that:

- **The parser recovers.** After an error it skips to the next `;`, `}` or
  keyword that can start a construct, so a missing semicolon does not produce
  an error for every token that follows it.
- **The lexer keeps going.** A stray character is skipped and reported;
  it does not end the token stream, which used to discard the rest of the
  file silently.
- **Duplicates are dropped** and diagnostics are sorted by position before
  printing, so the order matches the file rather than the pass that ran.

Every input file is parsed even when an earlier one failed, so one run tells
you the state of the whole build.

Printing stops after 100 errors and says so. `--max-errors=N` changes the
limit; `--max-errors=0` removes it.

## Adding a diagnostic

`C99.Common` holds the message type. Only a severity, a location and a
sentence are required; everything else is a combinator on top:

```haskell
emit
  . withHelp "did you mean 'counter'?"
  . withLabel "not found in this scope"
  . withCode "E0102"
  . withSnap name          -- move the caret onto this identifier
  . withLen (length name)
  $ diag Error loc ("undeclared identifier '" ++ name ++ "'")
```

`withSnap` exists because a declaration's location is the start of its *type*:
`int scratch` reported at the declaration would otherwise underline `int scr`.
The renderer looks for the name on the line as a whole word and moves the
caret there, falling back to the raw span when it cannot find it.

Add the code to `C99.Explain` at the same time. An entry is meaning, then an
example, then the fix, because the reader is stuck and wants the third part.

## Testing a diagnostic

`tests/run_suite.ps1` asserts with regexes, not golden files, so a case pins
the load-bearing content without breaking when a sentence is reworded:

```powershell
Need (Run-Diag "diag/undeclared" @("tests\diag\undeclared.c") $false `
  @("error\[E0102\]: undeclared identifier 'coutner'",
    "\^\^\^\^\^\^\^ not found in this scope",
    "= help: did you mean 'counter'\?") $null)
```

Asserting the caret run literally pins the column, the length and the label in
one pattern.

The last argument is what must *not* appear, and it is the only way to test
that something reports once rather than five times:

```powershell
Need (Run-Diag "diag/cascade" @("tests\diag\cascade.c") $false `
  @("due to 1 previous error") `
  @("expected a declaration", "due to [2-9] previous"))
```
