# AGENTS.md — FreeBSD KNF (style(9)) for this repository

This project follows FreeBSD Kernel Normal Form (KNF), as defined in
`style(9)`. Any agent generating or editing C code in this repo must
follow these rules. When in doubt, match the surrounding code rather
than introducing a new convention.

## Language standard: strict C89

This project targets **ANSI C89/C90** for portability, not C99. style(9)
itself recommends several C99 idioms (`uintXX_t`, `bool`, designated
initializers) — wherever this document conflicts with C89, **C89 wins**.
The C99-flavored advice below is kept only as historical context for why
certain compat shims exist; do not use the C99 features themselves.

Concretely, this means:

- No `<stdint.h>` / `<stdbool.h>` — provide project-local typedefs
  instead (see Types).
- No `inline` keyword — use plain `static` and let the optimizer decide.
- No `//` comments — `/* */` only, everywhere, no exceptions.
- No mixed declarations and code — all variables are declared at the
  top of their enclosing block, before any statement.
- No declaring a loop variable inside a `for (...)` init-clause.
- No designated initializers (`[0x00] = foo`) — array initializers must
  be positional. Use a `/* 0x00 */`-style comment per row to keep large
  tables (e.g. opcode dispatch tables) readable without the syntax.
- No variadic macros, no `_Static_assert`, no `//`-delimited line
  comments inside macros.

## Core identity

- Line width: 80 characters preferred. Diagnostic/error/panic strings
  may exceed it rather than being broken across lines (so they stay
  greppable).
- Indentation: 8-character tabs. Continuation/second-level indents use
  4 spaces. Never use spaces in front of tabs, and never leave trailing
  whitespace on a line.
- Identifiers: `snake_case` (`internal_underscores`), never `camelCase`
  or `PascalCase`.

## Typedefs — avoid them for structs

Do **not** typedef struct types. A typedef hides whether you're dealing
with the struct itself or a pointer to it, must be declared exactly
once (unlike an incomplete `struct foo;` forward declaration, which can
appear as many times as needed), and complicates standalone headers.

```c
/* Wrong */
typedef struct {
	uint8_t a, x, y, s, p;
	uint16_t pc;
} CPU;

/* Right */
struct cpu {
	uint8_t a, x, y, s, p;
	uint16_t pc;
};
```

If a typedef is genuinely required by convention, its name must match
the struct tag, and must **not** end in `_t` (that suffix is reserved
for Standard C / POSIX types):

```c
typedef struct bar {
	int level;
} BAR;
```

Enums are not typedef'd either — just `enum addr_mode { ... };`, used as
`enum addr_mode mode;`. Enum constants are written in `ALL_CAPS`.

## Comments

```c
/*
 * VERY important comments (multi-line, real sentences, filled like
 * a paragraph) look like this.
 */

/* Most single-line comments look like this. */
```

Under C89, `//` is not standard — some compilers accept it as an
extension, but it's not portable. Use `/* */` for single-line comments
too, with no exceptions:

```c
result -= 0xA0; /* 10 * 16 */
```

## Includes

Order: local project headers in `""`, sorted/grouped logically; then
system headers in `<>`. Within userland code: `<sys/types.h>` first if
needed, then other system headers alphabetically, then a blank line,
then `<stdio.h>`-style libc headers alphabetically, then a blank line,
then local headers in double quotes.

```c
#include "../bus.h"
#include "../cpu.h"
#include "../compat.h"   /* uint8_t/bool shims, see Types */

#include <stdio.h>
#include <string.h>
```

## Types

C89 has neither `<stdint.h>` nor `<stdbool.h>`. Provide both via a
single project-local `compat.h`, included everywhere instead of the
standard headers:

```c
/* compat.h */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;

typedef int bool;
#define true  1
#define false 0
```

(Adjust the integer widths if `int`/`long` aren't 16/32 bits on a
target platform — that's the actual portability tradeoff being bought
here.)

- Use these project-local `uint8_t`/`uint16_t`/`uint32_t`/`bool` names
  exactly as if they came from the standard headers; call sites don't
  change. These are non-struct typedefs, so they're fine under the
  no-typedef-for-structs rule and don't need to avoid the `_t` suffix
  (Standard C reserves `_t`, but since these names mimic Standard C
  names that just don't exist yet in C89, this is the one deliberate
  exception).
- No designated initializers in array/struct initializers — see the
  Language Standard section above for the dispatch-table implication.

## Functions

The return type goes on its own line above the function name. The
opening brace of the function body is on a line by itself:

```c
static uint8_t
read_byte(CPU *cpu, uint16_t addr)
{
	return bus_read(cpu->bus, addr);
}
```

- Every function is prototyped somewhere. Private/static functions are
  prototyped at the top of the file that defines them.
- Functions used only within one file must be declared `static`.
- Do not declare functions inside other functions.
- Routines returning `void *` are never cast at the call site.
- Return values in `return` statements are enclosed in parentheses:
  `return (result);`.
- No K&R-style (pre-ANSI) function definitions — forbidden outright.
- No `inline` keyword (it's a C99 addition). Use plain `static` for
  small helpers and trust the optimizer; don't reach for a
  compiler-specific extension like `__inline__` unless a specific
  target compiler genuinely requires it.

## Variable declarations

Local variables go first in a function, sorted by size then
alphabetically, followed by **one** blank line, then the first
statement:

```c
static void
example(void)
{
	struct foo one, *two;
	double four;
	int *five, six;
	char *seven, eight;

	six = compute();
	...
}
```

Struct members follow the same sort (by use if applicable, then size
largest-to-smallest, then alphabetical), one per line, with comments
aligned:

```c
struct foo {
	struct foo *next;     /* List of active foo. */
	struct mumble amumble; /* Comment for mumble. */
	int bar;               /* Try to align the comments. */
};
```

C89 forbids mixing declarations and statements within a block, and
forbids declaring a loop variable in a `for (...)` init-clause. Both
patterns are common in C99-trained generated code — hoist any such
declaration to the top of the enclosing block:

```c
/* Wrong under C89 */
for (uint32_t i = 0; i < frame_count; i++) { ... }

/* Right */
uint32_t i;

for (i = 0; i < frame_count; i++) { ... }
```

## Braces and control flow

- `if` / `while` / `for` / `return` / `switch` get a space before the
  opening parenthesis.
- For `if`/`else` chains: closing and opening braces go on the **same
  line** as `else`. Braces around single statements are optional —
  pick one convention (always-brace or only-when-needed-for-clarity)
  and stay consistent within a function.

```c
if (test)
	stmt;
else if (bar) {
	stmt;
	stmt;
} else
	stmt;
```

- Infinite loops use `for (;;)`, never `while (1)`.
- `switch` is indented; `case` labels are not. Case bodies are indented
  one tab. Mark intentional fallthrough with `/* FALLTHROUGH */`.

```c
switch (ch) {
case 'a':
	aflag = 1;
	/* FALLTHROUGH */
case 'b':
	bflag = 1;
	break;
default:
	usage();
}
```

## Spacing and operators

- No space after a function name before `(`.
- No space after `(` or `[`, none before `)` or `]`.
- Comma is followed by a space.
- Unary operators: no space. Binary operators: space on both sides.
- Avoid parentheses not required for precedence — but use them if the
  expression would otherwise be genuinely confusing.
- `sizeof` is always written with parentheses, and is not followed by a
  space; same for casts.

```c
error = function(a1, a2);
if (error != 0)
	exit(error);
```

## Comparisons

- Compare explicitly; don't rely on implicit truthiness.

```c
if (count != 0)      /* not: if (count) */
if (*p == '\0')      /* not: if (!*p) */
if ((p = f()) == NULL)
```

- `NULL` is the preferred null-pointer constant. Cast it explicitly
  only where the compiler can't infer the type (e.g. variadic args).

## Macros

- Macro names for manifest constants and "unsafe" macros (those with
  side effects) are `ALL_CAPS`.
- If a macro expands to a compound statement, wrap it in `do { ... }
  while (0)` so it's safe inside an `if` with no braces, and let the
  call site supply the terminating semicolon.

```c
#define MACRO(x, y) do {						\
	variable = (x) + (y);						\
	(y) += 2;							\
} while (0)
```

## Error handling

- Use `err(3)`/`warn(3)` (or this project's equivalent) instead of
  hand-rolled error printing where available.
- Exit codes: `0` on success, `1` on failure. Don't comment the
  obvious (`exit(0); /* success */` is noise).

## What NOT to do (common LLM defaults to avoid)

These are patterns models default to from general training data that
conflict with this project's KNF style — actively avoid them:

- `typedef struct { ... } Foo;` — use `struct foo { ... };` instead.
- `_t`-suffixed custom types — reserved namespace, avoid for your own
  types.
- `camelCase`/`PascalCase` identifiers — use `snake_case`.
- Opening braces on the same line as the function signature — function
  bodies get the brace on its own line.
- Implicit truthiness checks (`if (ptr)`, `if (!count)`) — compare
  explicitly against `NULL` / `0`.
- Bare `while (1)` for infinite loops — use `for (;;)`.
- `#include <stdint.h>` / `#include <stdbool.h>` — these don't exist
  in C89; use this project's `compat.h` typedefs instead.
- `inline` on small helper functions — not a C89 keyword; use `static`.
- Declaring a variable inside a `for (...)` init-clause, or anywhere
  other than the top of a block — C89 requires all declarations before
  any statement in that block.
- Designated initializers (`[N] = value`) in array/struct initializers
  — C89 only allows positional initialization.
- `//` line comments — `/* */` only.

## Verifying compliance

Treat any new or edited C file as non-compliant by default until
checked against this document. When making a substantial edit to a
function or file, prefer bringing the whole unit into compliance in
that pass rather than leaving it half-converted — but keep pure
style-only changes in their own commit, separate from functional
changes.
