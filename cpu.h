#ifndef CPU_H
#define CPU_H

#include "port.h"

#include "bus.h"

/* 6502 Processor Status Flags */
enum CPU_FLAGS {
	FLAG_CARRY = 1 << 0,	  /* C */
	FLAG_ZERO = 1 << 1,	  /* Z */
	FLAG_INTERRUPT = 1 << 2,  /* I */
	FLAG_DECIMAL = 1 << 3,	  /* D */
	FLAG_BREAK = 1 << 4,	  /* B (push status sets/clears this) */
	FLAG_UNUSED = 1 << 5,	  /* U (always 1) */
	FLAG_OVERFLOW = 1 << 6,	  /* V */
	FLAG_NEGATIVE = 1 << 7,	  /* N */
};

typedef struct {
	uint16_t from;
	uint16_t to;
} pc_edge_t;

struct cpu {
	uint8_t a;   /* Accumulator */
	uint8_t x;   /* X Register */
	uint8_t y;   /* Y Register */
	uint8_t s;   /* Stack Pointer */
	uint8_t p;   /* Status Flags Register */
	uint16_t pc; /* Program Counter */

	struct bus *bus; /* Reference to the system memory bus */

	bool nmi_pending;    /* Edge-sensitive NMI pending status */
	bool irq_pending;    /* Level-sensitive IRQ pending status */
	bool halted;	     /* Set by JAM/KIL opcodes - struct cpu bus is frozen */
	uint8_t last_cycles; /* Cycles taken by the last executed instruction */

	pc_edge_t pc_trace[24];
	int pc_trace_idx;
	uint16_t prev_pc;
	bool prev_pc_valid;
};

/* Initialize the struct cpu with a reference to the memory bus */
void
cpu_init(struct cpu *cpu, struct bus *bus);

/* Perform a 6502 reset sequence */
void
cpu_reset(struct cpu *cpu);

/* Execute a single struct cpu instruction, returning the cycles taken */
uint8_t
cpu_step(struct cpu *cpu);

/* Assert or deassert level-sensitive IRQ interrupt line */
void
cpu_irq(struct cpu *cpu, bool assert);

/* Assert edge-sensitive NMI interrupt line */
void
cpu_nmi(struct cpu *cpu);

#endif /* CPU_H */
