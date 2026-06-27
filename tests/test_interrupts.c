#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bus.h"
#include "../cpu.h"

/*
 * Interrupt Test Suite
 *
 * Covers the NMOS 6502 IRQ and NMI interrupt mechanism as verified by
 * Visual6502 silicon simulation and documented in primary sources:
 *
 *   NESdev wiki "struct cpu interrupts" -- cycle table, stack layout, I-flag timing
 *   Michael Steil / pagetable.com -- forced-$00 into IR, PC increment suppressed
 *   6502.org/tutorials/interrupts -- BRK padding byte, return-address detail
 *   NESdev "The B flag" -- B=0 for IRQ/NMI; B=1 for BRK/PHP; no physical B reg
 *
 * Memory layout used throughout:
 *   $1000  preempted code (NOP $EA, then BRK $00 in test 6)
 *   $1200  IRQ/BRK handler (RTI)
 *   $1300  NMI handler     (RTI)
 *
 * Vectors (ROM shadow, offsets from $FF00):
 *   $FFFA/$FFFB  NMI -> $1300
 *   $FFFE/$FFFF  IRQ -> $1200
 *
 * Stack starts at S=$FD; 3 pushes leave:
 *   $01FD = PCH  (first push)
 *   $01FC = PCL
 *   $01FB = P    (last push)
 */

static uint8_t test_ram[65536];
static struct bus bus;
static struct cpu cpu;

static void
setup(void)
{
	if (bus_init(&bus, test_ram, 16 * 1024) == false) {
		fprintf(stderr, "bus_init failed\n");
		exit(1);
	}
	bus.opts.randomize_cold_boot = false;
	bus.opts.throttle_pia        = false;

	/* Reset vector -> $1000 */
	bus.rom[0xFC] = 0x00;
	bus.rom[0xFD] = 0x10;
	bus.rom_loaded = true;

	/* NMI vector -> $1300 */
	bus.rom[0xFA] = 0x00;
	bus.rom[0xFB] = 0x13;

	/* IRQ/BRK vector -> $1200 */
	bus.rom[0xFE] = 0x00;
	bus.rom[0xFF] = 0x12;

	cpu_init(&cpu, &bus);
	cpu_reset(&cpu);

	/* ISR stubs: RTI ($40) */
	bus.ram[0x1200] = 0x40;
	bus.ram[0x1300] = 0x40;

	/*
	 * Preempted instruction is NOP ($EA), deliberately NOT $00.
	 * Real silicon discards whatever byte is at PC and forces $00
	 * into the instruction register unconditionally — it never peeks
	 * at memory to decide whether to interrupt.
	 */
	bus.ram[0x1000] = 0xEA;
}

/*
 * Test 1: IRQ — vector jump, 7-cycle count, I flag set after sequence.
 */
static void
test_irq_basic(void)
{
	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p &= ~FLAG_INTERRUPT; /* I=0: IRQ unmasked */
	cpu_irq(&cpu, true);

	uint8_t cycles = cpu_step(&cpu);

	assert(cycles == 7);           /* IRQ/NMI always 7 cycles */
	assert(cpu.pc == 0x1200);      /* jumped to IRQ handler   */
	assert(cpu.p & FLAG_INTERRUPT);/* I set by sequence        */

	cpu_irq(&cpu, false);
	printf("PASS: IRQ basic (vector jump, 7 cycles, I flag)\n");
}

/*
 * Test 2: PC held — return address on stack is the preempted address.
 *
 * "The struct cpu suppresses the PC increment on the initial fetch, so the
 *  saved PC is the address of the instruction that would have run next."
 *  -- Steil / Visual6502 / pagetable.com
 *
 * Stack layout (S=$FD, 3 pushes):
 *   $01FD = PCH   $01FC = PCL   $01FB = P
 */
static void
test_irq_pc_held(void)
{
	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p &= ~FLAG_INTERRUPT;
	cpu_irq(&cpu, true);

	cpu_step(&cpu);

	uint16_t stk_pc = ((uint16_t)bus.ram[0x01FD] << 8) | bus.ram[0x01FC];
	assert(stk_pc == 0x1000); /* preempted NOP, NOT $1001 */

	cpu_irq(&cpu, false);
	printf("PASS: IRQ PC held (return addr == preempted addr)\n");
}

/*
 * Test 3: B=0 and UNUSED=1 in pushed P for hardware IRQ.
 *
 * "The struct cpu pushes 0 for hardware interrupts, 1 for BRK/PHP."
 * -- NESdev "The B flag".  Bit 5 (UNUSED) is always 1 on any push.
 */
static void
test_irq_b_flag(void)
{
	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p &= ~FLAG_INTERRUPT;
	cpu_irq(&cpu, true);

	cpu_step(&cpu);

	uint8_t stk_p = bus.ram[0x01FB];
	assert(!(stk_p & FLAG_BREAK)); /* B=0 for hardware IRQ */
	assert(stk_p   & FLAG_UNUSED); /* bit 5 always 1 on push */

	cpu_irq(&cpu, false);
	printf("PASS: IRQ B=0, UNUSED=1 in pushed P\n");
}

/*
 * Test 4: IRQ is masked when I=1 — struct cpu must execute the real instruction.
 *
 * IRQ is level-sensitive and suppressed while I=1.  The struct cpu must run the
 * next real opcode (NOP, 2 cycles) rather than servicing the interrupt.
 * -- NESdev "struct cpu interrupts"
 */
static void
test_irq_masked(void)
{
	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p |= FLAG_INTERRUPT; /* I=1: IRQ masked */
	cpu_irq(&cpu, true);

	uint8_t cycles = cpu_step(&cpu); /* must run NOP, not IRQ */

	assert(cycles == 2);        /* NOP = 2 cycles; 7 means IRQ fired (bug) */
	assert(cpu.pc == 0x1001);   /* advanced past NOP */

	cpu_irq(&cpu, false);
	printf("PASS: IRQ masked by I=1 (NOP executed instead)\n");
}

/*
 * Test 5: NMI is non-maskable — fires even when I=1.
 *         Also verifies: PC held, B=0, UNUSED=1 in NMI stack frame.
 */
static void
test_nmi_nonmaskable(void)
{
	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p |= FLAG_INTERRUPT; /* I=1 — would suppress IRQ, not NMI */
	cpu_nmi(&cpu);

	uint8_t cycles = cpu_step(&cpu);

	assert(cycles == 7);
	assert(cpu.pc == 0x1300); /* NMI handler */

	uint8_t  stk_p  = bus.ram[0x01FB];
	uint16_t stk_pc = ((uint16_t)bus.ram[0x01FD] << 8) | bus.ram[0x01FC];

	assert(!(stk_p & FLAG_BREAK)); /* B=0 for NMI */
	assert(stk_p   & FLAG_UNUSED);
	assert(stk_pc  == 0x1000);    /* PC held */

	printf("PASS: NMI non-maskable (fires with I=1, PC held, B=0)\n");
}

/*
 * Test 6: BRK pushes B=1 and returns to BRK+2 (not BRK+1).
 *
 * BRK reads the opcode (PC++) then reads the padding byte (PC++) before
 * pushing — so stacked PC = BRK+2.  The B=1 flag lets an ISR distinguish
 * BRK from IRQ on the shared $FFFE/$FFFF vector.
 * -- 6502.org/tutorials/interrupts, NESdev "The B flag"
 */
static void
test_brk_b_flag_and_return_addr(void)
{
	bus.ram[0x1000] = 0x00; /* BRK opcode */
	bus.ram[0x1001] = 0x42; /* padding byte (signature; discarded by struct cpu) */

	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p &= ~FLAG_INTERRUPT;

	uint8_t cycles = cpu_step(&cpu);

	assert(cycles == 7);
	assert(cpu.pc == 0x1200); /* IRQ/BRK shared vector */

	uint16_t stk_pc = ((uint16_t)bus.ram[0x01FD] << 8) | bus.ram[0x01FC];
	uint8_t  stk_p  = bus.ram[0x01FB];

	assert(stk_pc == 0x1002);      /* BRK+2: past opcode AND padding byte */
	assert(stk_p  & FLAG_BREAK);   /* B=1 for BRK */
	assert(stk_p  & FLAG_UNUSED);  /* bit 5 always 1 */

	/* restore NOP for subsequent tests */
	bus.ram[0x1000] = 0xEA;

	printf("PASS: BRK B=1, return addr BRK+2\n");
}

/*
 * Test 7: RTI round-trip — IRQ fires, ISR executes RTI, struct cpu resumes at
 *         the preempted address.
 *
 * RTI pops P, then PCL, then PCH (reverse push order).  The B bit read
 * from the stack is discarded — there is no physical B register inside
 * the struct cpu.  RTI itself takes 6 cycles.
 * -- NESdev "struct cpu interrupts", Visual6502
 */
static void
test_rti_roundtrip(void)
{
	cpu.pc = 0x1000;
	cpu.s  = 0xFD;
	cpu.p  = FLAG_UNUSED; /* I=0, all other flags clear */
	cpu_irq(&cpu, true);

	/* Step 1: IRQ fires, jumps to $1200 */
	uint8_t cycles = cpu_step(&cpu);
	assert(cycles == 7);
	assert(cpu.pc == 0x1200);
	cpu_irq(&cpu, false); /* deassert before RTI restores I=0 */

	/* Step 2: RTI at $1200 restores P (I=0) and pops return PC */
	cycles = cpu_step(&cpu);
	assert(cycles == 6);       /* RTI = 6 cycles */
	assert(cpu.pc == 0x1000);  /* back at the preempted NOP */

	printf("PASS: RTI round-trip (IRQ -> ISR -> RTI -> preempted addr)\n");
}

int
main(void)
{
	setup();

	test_irq_basic();
	test_irq_pc_held();
	test_irq_b_flag();
	test_irq_masked();
	test_nmi_nonmaskable();
	test_brk_b_flag_and_return_addr();
	test_rti_roundtrip();

	bus_free(&bus);
	printf("All interrupt tests passed.\n");
	return (0);
}
