# Customization Rules for apple1-emu

The following behavioral and coding rules must be strictly followed when working on this project:

## style(9) Compliance Rules

All new C code and modifications to existing code must follow the FreeBSD style(9) formatting and structure guidelines:

1. **No Mixed Declarations and Statements**
   - Declarations must appear at the top of a block/function, **before** any code statements. Mixed declarations and code are not allowed.

2. **Variable Declaration Sorting & Formatting**
   - Within functions or structures, variables must be declared sorted by size (largest to smallest), then alphabetically by name.

3. **Explicit Pointer & Scalar Comparisons**
   - Pointers must be explicitly tested against `NULL` (no implicit `!ptr` or `if (ptr)`).
   - Scalars/counts must be explicitly tested against `0` (e.g. `val == 0`).
   - Booleans must be explicitly tested against `true` or `false` (e.g. `flag == true`).

4. **Return Statement Parentheses**
   - Values in `return` statements must be enclosed in parentheses. Example: `return (0);` or `return (cycles);`.

5. **Loop Statements**
   - Forever loops must use `for (;;)` instead of `while (1)` or `while (true)`.

6. **Switch-Case Alignment**
   - `case` labels must not be indented relative to the `switch` statement (they align vertically with `switch`). Case bodies must use one tab indent.

7. **Structure Type Names**
   - Do not use `typedef` for structure types. Standard tags must be used (e.g. use `struct bus` instead of `Bus`, `struct cpu` instead of `CPU`, `struct expansion_card` instead of `expansion_card_t`, etc.).

8. **Header Include Sorting**
   - System headers first, sorted alphabetically, followed by local/project headers, sorted alphabetically, with a blank line between the groups.

9. **Comment Style**
   - Do not use C++ line comments (`//`). Always use C block comments (`/* ... */`).
