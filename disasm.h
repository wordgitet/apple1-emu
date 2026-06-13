#ifndef DISASM_H
#define DISASM_H

#include "bus.h"
#include <stdint.h>

// Disassembles one 6502 instruction at memory address pc.
// Writes the human-readable representation into out_str.
// Returns the number of bytes consumed by the instruction (1, 2, or 3).
int
cpu_disassemble(Bus *bus, uint16_t pc, char *out_str);

#endif // DISASM_H
