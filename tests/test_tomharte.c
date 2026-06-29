#include "../bus.h"
#include "../cpu.h"
#include "../port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * test_tomharte.c -- Cycle-exact Tom Harte test runner using binary fixture.
 *
 * Inspired by and credited to the POM1 emulator's test harness design.
 * Reads tests/harte_6502.bin which packages up to 10,000 cases for each of
 * the 244 non-JAM opcodes.
 */

static uint8_t test_ram[65536];

typedef struct {
	uint16_t addr;
	uint8_t val;
} ram_val_t;

typedef struct {
	uint16_t pc;
	uint8_t s, a, x, y, p;
	ram_val_t ram[256];
	uint16_t ram_count;
} state_t;

typedef struct {
	uint8_t op;
	state_t initial;
	state_t final;
	uint8_t cycles;
} test_case_t;

int actual_cycles_count = 0;

static void
access_callback(void *ctx, uint16_t addr, bool is_write, uint8_t val)
{
	(void)ctx;
	(void)addr;
	(void)is_write;
	(void)val;
	actual_cycles_count++;
}

static bool
read_state(FILE *f, state_t *s)
{
	uint8_t regs[5];
	int i;

	if (fread(&s->pc, 2, 1, f) != 1)
		return (false);
	if (fread(regs, 1, 5, f) != 5)
		return (false);
	s->s = regs[0];
	s->a = regs[1];
	s->x = regs[2];
	s->y = regs[3];
	s->p = regs[4];

	if (fread(&s->ram_count, 2, 1, f) != 1)
		return (false);
	for (i = 0; i < s->ram_count; i++) {
		if (fread(&s->ram[i].addr, 2, 1, f) != 1)
			return (false);
		if (fread(&s->ram[i].val, 1, 1, f) != 1)
			return (false);
	}
	return (true);
}

static bool
read_test_case(FILE *f, test_case_t *t)
{
	if (fread(&t->op, 1, 1, f) != 1)
		return (false);
	if (!read_state(f, &t->initial))
		return (false);
	if (!read_state(f, &t->final))
		return (false);
	if (fread(&t->cycles, 1, 1, f) != 1)
		return (false);
	return (true);
}

static void
skip_state(FILE *f)
{
	uint16_t ram_count;

	fseek(f, 2 + 5, SEEK_CUR); /* skip pc and regs */
	if (fread(&ram_count, 2, 1, f) == 1) {
		fseek(f, ram_count * 3, SEEK_CUR); /* skip addr + val */
	}
}

static void
skip_test_case(FILE *f)
{
	fseek(f, 1, SEEK_CUR); /* skip op */
	skip_state(f);
	skip_state(f);
	fseek(f, 1, SEEK_CUR); /* skip cycles */
}

int
main(int argc, char **argv)
{
	const char *bin_path = "tests/harte_6502.bin";
	int limit = 10000;
	FILE *f;
	char header[4];
	uint32_t total_cases;
	int opcodes_count;
	int cases_per_op;
	struct bus bus;
	struct cpu cpu;
	int passed_ops;
	int failed_ops;
	int op_idx;
	int i;
	int r;
	int run_count;
	int skip_count;
	uint8_t expected_p;
	uint8_t actual_p;
	uint16_t addr;
	uint8_t val;
	bool ram_ok;
	bool first_case;
	uint8_t op_val;
	test_case_t t;

	if (argc >= 2) {
		bin_path = argv[1];
	}
	if (argc >= 3) {
		limit = atoi(argv[2]);
	}

	f = fopen(bin_path, "rb");
	if (!f) {
		fprintf(stderr,
		    "Error: Could not open test fixture %s\n",
		    bin_path);
		return (1);
	}

	if (fread(header, 1, 4, f) != 4 || memcmp(header, "HRT1", 4) != 0) {
		fprintf(stderr,
		    "Error: Invalid test fixture header in %s\n",
		    bin_path);
		fclose(f);
		return (1);
	}

	if (fread(&total_cases, 4, 1, f) != 1) {
		fprintf(stderr, "Error: Could not read total case count\n");
		fclose(f);
		return (1);
	}

	opcodes_count = 244;
	cases_per_op = total_cases / opcodes_count;

	printf("Loaded %s: %u total cases (~%d per opcode)\n",
	    bin_path,
	    total_cases,
	    cases_per_op);
	if (limit < cases_per_op) {
		printf("Limiting execution to first %d cases per opcode\n",
		    limit);
	}

	if (bus_init(&bus, test_ram, 65536) != PORT_OK) {
		fclose(f);
		return (1);
	}
	bus.opts.flat_bus = true;
	bus.access_cb = access_callback;

	cpu_init(&cpu, &bus);

	passed_ops = 0;
	failed_ops = 0;

	for (op_idx = 0; op_idx < opcodes_count; op_idx++) {
		run_count = limit < cases_per_op ? limit : cases_per_op;
		skip_count = cases_per_op - run_count;

		first_case = true;
		op_val = 0;

		for (i = 0; i < run_count; i++) {
			if (!read_test_case(f, &t)) {
				fprintf(stderr,
				    "Error: Premature end of file reading case "
				    "%d of opcode index %d\n",
				    i,
				    op_idx);
				bus_free(&bus);
				fclose(f);
				return (1);
			}

			if (first_case) {
				op_val = t.op;
				printf("Running tests for opcode %02X... ",
				    op_val);
				fflush(stdout);
				first_case = false;
			}

			/* Setup struct cpu initial state */
			cpu.pc = t.initial.pc;
			cpu.s = t.initial.s;
			cpu.a = t.initial.a;
			cpu.x = t.initial.x;
			cpu.y = t.initial.y;
			cpu.p = t.initial.p | FLAG_UNUSED;
			cpu.halted = false;

			/* Setup initial RAM */
			memset(bus.ram, 0, 65536);
			for (r = 0; r < t.initial.ram_count; r++) {
				bus.ram[t.initial.ram[r].addr] =
				    t.initial.ram[r].val;
			}

			actual_cycles_count = 0;

			cpu_step(&cpu);

			/* Verify final state */
			expected_p = t.final.p | FLAG_UNUSED;
			actual_p = cpu.p | FLAG_UNUSED;
			expected_p &= ~0x10; /* Ignore B flag differences */
			actual_p &= ~0x10;

			if (cpu.pc != t.final.pc || cpu.s != t.final.s ||
			    cpu.a != t.final.a || cpu.x != t.final.x ||
			    cpu.y != t.final.y || actual_p != expected_p) {
				printf("FAILED (State mismatch in case %d)\n",
				    i);
				fprintf(stderr,
				    "Expected: PC=%04X S=%02X A=%02X X=%02X "
				    "Y=%02X P=%02X\n",
				    t.final.pc,
				    t.final.s,
				    t.final.a,
				    t.final.x,
				    t.final.y,
				    expected_p);
				fprintf(stderr,
				    "Actual  : PC=%04X S=%02X A=%02X X=%02X "
				    "Y=%02X P=%02X\n",
				    cpu.pc,
				    cpu.s,
				    cpu.a,
				    cpu.x,
				    cpu.y,
				    actual_p);
				failed_ops++;
				break;
			}

			/* Verify RAM */
			ram_ok = true;
			for (r = 0; r < t.final.ram_count; r++) {
				addr = t.final.ram[r].addr;
				val = t.final.ram[r].val;
				if (bus.ram[addr] != val) {
					printf("FAILED (RAM mismatch in case "
					       "%d at $%04X: expected %02X, "
					       "got %02X)\n",
					    i,
					    addr,
					    val,
					    bus.ram[addr]);
					ram_ok = false;
					break;
				}
			}
			if (!ram_ok) {
				failed_ops++;
				break;
			}

			/* Verify cycles count */
			if (actual_cycles_count != t.cycles) {
				printf("FAILED (Cycles mismatch in case %d: "
				       "expected %d, got %d)\n",
				    i,
				    t.cycles,
				    actual_cycles_count);
				failed_ops++;
				break;
			}
		}

		if (failed_ops > 0) {
			break;
		}

		printf("PASSED\n");
		passed_ops++;

		/* Skip remaining cases for this opcode */
		for (i = 0; i < skip_count; i++) {
			skip_test_case(f);
		}
	}

	bus_free(&bus);
	fclose(f);

	if (failed_ops > 0) {
		return (1);
	}

	printf("\nAll %d opcodes passed successfully (%d cases each)!\n",
	    passed_ops,
	    limit < cases_per_op ? limit : cases_per_op);
	return (0);
}
