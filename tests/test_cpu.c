#include "../bus.h"
#include "../cpu.h"
#include "../dbg.h"
#include "../disasm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
run_test(const char *name, void (*test_fn)(CPU *, Bus *))
{
	Bus bus;
	CPU cpu;

	// Initialize 16KB mock RAM for testing
	if (!bus_init(&bus, 16 * 1024)) {
		fprintf(stderr, "Failed to init bus for test: %s\n", name);
		exit(1);
	}
	// We don't load a real ROM for tests, but we set reset vector manually
	// point reset vector to 0x1000
	bus.rom[0xFC] = 0x00;
	bus.rom[0xFD] = 0x10;
	bus.rom_loaded = true; // Pretend ROM is loaded to allow vector read

	cpu_init(&cpu, &bus);

	// Reset puts PC at 0x1000
	cpu_reset(&cpu);
	assert(cpu.pc == 0x1000);

	test_fn(&cpu, &bus);

	bus_free(&bus);
	printf("Test PASS: %s\n", name);
}

// LDA / STA Test
static void
test_lda_sta(CPU *cpu, Bus *bus)
{
	// Write instructions:
	// 0x1000: LDA #$42  (A9 42)
	// 0x1002: STA $0200 (8D 00 02)
	bus->ram[0x1000] = 0xA9;
	bus->ram[0x1001] = 0x42;
	bus->ram[0x1002] = 0x8D;
	bus->ram[0x1003] = 0x00;
	bus->ram[0x1004] = 0x02;

	cpu_step(cpu); // Exec LDA
	assert(cpu->a == 0x42);
	assert(!(cpu->p & FLAG_ZERO));
	assert(!(cpu->p & FLAG_NEGATIVE));

	cpu_step(cpu); // Exec STA
	assert(bus->ram[0x0200] == 0x42);
}

// Binary Arithmetic Test (ADC/SBC)
static void
test_adc_sbc_bin(CPU *cpu, Bus *bus)
{
	// 0x1000: CLC        (18)
	// 0x1001: LDA #$10   (A9 10)
	// 0x1003: ADC #$20   (69 20) -> sum = 0x30, Carry=0
	// 0x1005: SEC        (38)
	// 0x1006: SBC #$05   (E9 05) -> A = 0x30 - 0x05 = 0x2B, Carry=1
	bus->ram[0x1000] = 0x18;
	bus->ram[0x1001] = 0xA9;
	bus->ram[0x1002] = 0x10;
	bus->ram[0x1003] = 0x69;
	bus->ram[0x1004] = 0x20;
	bus->ram[0x1005] = 0x38;
	bus->ram[0x1006] = 0xE9;
	bus->ram[0x1007] = 0x05;

	cpu_step(cpu); // CLC
	assert(!(cpu->p & FLAG_CARRY));

	cpu_step(cpu); // LDA #$10
	assert(cpu->a == 0x10);

	cpu_step(cpu); // ADC #$20
	assert(cpu->a == 0x30);
	assert(!(cpu->p & FLAG_CARRY));

	cpu_step(cpu); // SEC
	assert(cpu->p & FLAG_CARRY);

	cpu_step(cpu); // SBC #$05
	assert(cpu->a == 0x2B);
	assert(cpu->p & FLAG_CARRY); // Carry remains set (no borrow)
}

// BCD Arithmetic Test
static void
test_adc_sbc_bcd(CPU *cpu, Bus *bus)
{
	// 0x1000: SED        (F8)      - Enter Decimal Mode
	// 0x1001: CLC        (18)
	// 0x1002: LDA #$15   (A9 15)   - Load BCD 15
	// 0x1004: ADC #$27   (69 27)   - Add BCD 27 -> result 42, Carry=0
	// 0x1006: SEC        (38)
	// 0x1007: SBC #$27   (E9 27)   - Sub BCD 27 -> result 15, Carry=1 (no borrow)
	bus->ram[0x1000] = 0xF8;
	bus->ram[0x1001] = 0x18;
	bus->ram[0x1002] = 0xA9;
	bus->ram[0x1003] = 0x15;
	bus->ram[0x1004] = 0x69;
	bus->ram[0x1005] = 0x27;
	bus->ram[0x1006] = 0x38;
	bus->ram[0x1007] = 0xE9;
	bus->ram[0x1008] = 0x27;

	cpu_step(cpu); // SED
	assert(cpu->p & FLAG_DECIMAL);

	cpu_step(cpu); // CLC
	cpu_step(cpu); // LDA #$15
	assert(cpu->a == 0x15);

	cpu_step(cpu);		// ADC #$27
	assert(cpu->a == 0x42); // 15 + 27 = 42 in BCD
	assert(!(cpu->p & FLAG_CARRY));

	cpu_step(cpu);		// SEC
	cpu_step(cpu);		// SBC #$27
	assert(cpu->a == 0x15); // 42 - 27 = 15 in BCD
	assert(cpu->p & FLAG_CARRY);
}

// Branch Test
static void
test_branching(CPU *cpu, Bus *bus)
{
	// 0x1000: LDA #$00   (A9 00)
	// 0x1002: BNE +4     (D0 04) - should NOT branch (Z is set)
	// 0x1004: LDA #$05   (A9 05) - executed
	// 0x1006: BNE +2     (D0 02) - SHOULD branch (Z is clear)
	// 0x1008: LDA #$99   (A9 99) - skipped
	// 0x100A: NOP        (EA)    - target of branch
	bus->ram[0x1000] = 0xA9;
	bus->ram[0x1001] = 0x00;
	bus->ram[0x1002] = 0xD0;
	bus->ram[0x1003] = 0x04;
	bus->ram[0x1004] = 0xA9;
	bus->ram[0x1005] = 0x05;
	bus->ram[0x1006] = 0xD0;
	bus->ram[0x1007] = 0x02;
	bus->ram[0x1008] = 0xA9;
	bus->ram[0x1009] = 0x99;
	bus->ram[0x100A] = 0xEA;

	cpu_step(cpu); // LDA #$00
	assert(cpu->a == 0x00);
	assert(cpu->p & FLAG_ZERO);

	cpu_step(cpu); // BNE +4 (no branch)
	assert(cpu->pc == 0x1004);

	cpu_step(cpu); // LDA #$05
	assert(cpu->a == 0x05);
	assert(!(cpu->p & FLAG_ZERO));

	cpu_step(cpu); // BNE +2 (branch taken)
	// Offset +2 from next PC (0x1008) -> 0x100A
	assert(cpu->pc == 0x100A);

	cpu_step(cpu);		// NOP
	assert(cpu->a == 0x05); // A remains 0x05, 0x99 was skipped
}

// JSR / RTS Test
static void
test_jsr_rts(CPU *cpu, Bus *bus)
{
	// 0x1000: JSR $1200   (20 00 12)
	// 0x1003: LDA #$0A    (A9 0A)
	// ...
	// 0x1200: RTS         (60)
	bus->ram[0x1000] = 0x20;
	bus->ram[0x1001] = 0x00;
	bus->ram[0x1002] = 0x12;

	bus->ram[0x1003] = 0xA9;
	bus->ram[0x1004] = 0x0A;

	bus->ram[0x1200] = 0x60;

	cpu_step(cpu); // JSR $1200
	assert(cpu->pc == 0x1200);
	// Stack pointer should have moved down by 2 bytes (high & low byte of return address-1)
	// JSR pushes return address (PC - 1) = (0x1003 - 1) = 0x1002
	assert(cpu->s == 0xFB);
	assert(bus->ram[0x01FC] == 0x02); // low byte
	assert(bus->ram[0x01FD] == 0x10); // high byte

	cpu_step(cpu);		   // RTS
	assert(cpu->pc == 0x1003); // back to address after JSR
	assert(cpu->s == 0xFD);	   // stack restored

	cpu_step(cpu); // LDA #$0A
	assert(cpu->a == 0x0A);
}

// Open Bus Test
static void
test_open_bus(CPU *cpu, Bus *bus)
{
	(void)cpu;
	// Disable randomize cold boot to have deterministic state
	bus->opts.randomize_cold_boot = false;
	memset(bus->ram, 0, bus->ram_size);

	// Write value to RAM (updates last bus value to 0x42)
	bus_write(bus, 0x0200, 0x42);
	uint8_t val1 = bus_read(bus, 0x0200);

	assert(val1 == 0x42);

	// Read unmapped address (should return 0x42)
	uint8_t val2 = bus_read(bus, 0x5000);

	assert(val2 == 0x42);

	// Write another value (updates last bus value to 0x99)
	bus_write(bus, 0x0300, 0x99);
	uint8_t val3 = bus_read(bus, 0x6000);

	assert(val3 == 0x99);
}

// Illegal Instructions Test (LAX, SAX, SLO, DCP)
static void
test_illegal_instructions(CPU *cpu, Bus *bus)
{
	// Disable randomize cold boot
	bus->opts.randomize_cold_boot = false;
	memset(bus->ram, 0, bus->ram_size);

	// 1. LAX Zero Page ($A7 02): load A and X from ZP $02
	bus->ram[0x0002] = 0x37;
	bus->ram[0x1000] = 0xA7;
	bus->ram[0x1001] = 0x02;

	cpu_step(cpu);
	assert(cpu->a == 0x37);
	assert(cpu->x == 0x37);
	assert(!(cpu->p & FLAG_ZERO));
	assert(!(cpu->p & FLAG_NEGATIVE));

	// Reset PC to 0x1002
	cpu->pc = 0x1002;

	// 2. SAX Zero Page ($87 03): store A & X to ZP $03
	cpu->a = 0xF0;
	cpu->x = 0x3F;
	bus->ram[0x1002] = 0x87;
	bus->ram[0x1003] = 0x03;

	cpu_step(cpu);
	assert(bus->ram[0x0003] == 0x30); // 0xF0 & 0x3F = 0x30

	cpu->pc = 0x1004;

	// 3. SLO Zero Page ($07 04): shift left ZP $04, OR with A
	bus->ram[0x0004] = 0x81;
	cpu->a = 0x05;
	bus->ram[0x1004] = 0x07;
	bus->ram[0x1005] = 0x04;

	cpu_step(cpu);
	// ZP $04 contains (0x81 << 1) & 0xFF = 0x02. Carry set (bit 7 was 1).
	assert(bus->ram[0x0004] == 0x02);
	// A contains 0x05 | 0x02 = 0x07
	assert(cpu->a == 0x07);
	assert(cpu->p & FLAG_CARRY);

	cpu->pc = 0x1006;

	// 4. DCP Zero Page ($C7 05): decrement ZP $05, compare with A
	bus->ram[0x0005] = 0x43;
	cpu->a = 0x42;
	bus->ram[0x1006] = 0xC7;
	bus->ram[0x1007] = 0x05;

	cpu_step(cpu);
	// ZP $05 decremented to 0x42
	assert(bus->ram[0x0005] == 0x42);
	// Compare A (0x42) with ZP $05 (0x42) -> Z set, C set, N clear
	assert(cpu->p & FLAG_ZERO);
	assert(cpu->p & FLAG_CARRY);
	assert(!(cpu->p & FLAG_NEGATIVE));
}

// Dummy Write Side-Effects Test
static void
test_dummy_write_side_effects(CPU *cpu, Bus *bus)
{
	// Disable timing throttling to run test quickly
	bus->opts.randomize_cold_boot = false;
	bus->opts.throttle_pia = false;
	bus->opts.uncapped = true;
	memset(bus->ram, 0, bus->ram_size);

	// Setup vectors and program
	// 0x1000: DCP $D011 (CF 11 D0) - RMW to kbd_control
	// 0x1003: SLO $D012 (0F 12 D0) - RMW to dsp_data
	bus->ram[0x1000] = 0xCF;
	bus->ram[0x1001] = 0x11;
	bus->ram[0x1002] = 0xD0;
	bus->ram[0x1003] = 0x0F;
	bus->ram[0x1004] = 0x12;
	bus->ram[0x1005] = 0xD0;

	// Set key-ready strobe (bit 7) and select DR/OR (bit 2)
	bus->pia.kbd_control |= 0x84;
	assert(bus->pia.kbd_control & 0x80);

	printf("[DEBUG] Before DCP: kbd_control = 0x%02X\n",
	    bus->pia.kbd_control);
	cpu_step(cpu); // Exec DCP $D011
	printf("[DEBUG] After DCP: kbd_control = 0x%02X\n",
	    bus->pia.kbd_control);
	// Dummy write to $D011 must have cleared key-ready strobe!
	assert(!(bus->pia.kbd_control & 0x80));

	// Set display busy (bit 7 clear) and select DR/OR (bit 2)
	bus->pia.dsp_control = (bus->pia.dsp_control & ~0x80) | 0x04;
	assert(!(bus->pia.dsp_control & 0x80));

	printf("[DEBUG] Before SLO: dsp_control = 0x%02X\n",
	    bus->pia.dsp_control);
	cpu_step(cpu); // Exec SLO $D012
	printf("[DEBUG] After SLO: dsp_control = 0x%02X\n",
	    bus->pia.dsp_control);
	// Dummy write to $D012 must have triggered display ready (bit 7 set)!
	assert(bus->pia.dsp_control & 0x80);
}

// Interrupts Test (IRQ and NMI)
static void
test_interrupts(CPU *cpu, Bus *bus)
{
	// Disable timing throttling
	bus->opts.randomize_cold_boot = false;
	bus->opts.throttle_pia = false;
	memset(bus->ram, 0, bus->ram_size);

	// Setup vectors:
	// NMI at 0x1300
	bus->rom[0xFA] = 0x00;
	bus->rom[0xFB] = 0x13;
	// IRQ at 0x1200
	bus->rom[0xFE] = 0x00;
	bus->rom[0xFF] = 0x12;

	// 0x1000: NOP (EA)
	bus->ram[0x1000] = 0xEA;

	// 1. Verify IRQ:
	// Clear interrupt disable flag (CLI)
	cpu->p &= ~FLAG_INTERRUPT;

	// Trigger IRQ
	cpu_irq(cpu, true);

	// Exec step (interrupt check should intercept fetch)
	uint8_t cycles = cpu_step(cpu);

	assert(cycles == 7);
	// PC jumped to IRQ vector
	assert(cpu->pc == 0x1200);
	// Interrupt disable flag is set
	assert(cpu->p & FLAG_INTERRUPT);

	// Verify stack contents: return PC (0x1000) and status P (with Break bit cleared)
	uint8_t status_byte = bus->ram[0x0100 + cpu->s + 1];

	// Bit 4 (Break) must be clear, Bit 5 (Unused) must be set
	assert(!(status_byte & FLAG_BREAK));
	assert(status_byte & FLAG_UNUSED);

	// Reset CPU for NMI test
	cpu->pc = 0x1000;
	cpu->s = 0xFD;
	cpu_irq(cpu, false);

	// 2. Verify NMI (triggers even when Interrupt flag is set)
	cpu->p |= FLAG_INTERRUPT;
	cpu_nmi(cpu);

	cycles = cpu_step(cpu);
	assert(cycles == 7);
	// PC jumped to NMI vector
	assert(cpu->pc == 0x1300);
	// Interrupt disable flag is still set
	assert(cpu->p & FLAG_INTERRUPT);

	// Verify stack contents: Break bit cleared
	status_byte = bus->ram[0x0100 + cpu->s + 1];
	assert(!(status_byte & FLAG_BREAK));
}

static uint8_t mock_read_val = 0;
static uint16_t mock_last_read_addr = 0;
static bool mock_last_read_is_dummy = false;

static uint8_t mock_last_write_val = 0;
static uint16_t mock_last_write_addr = 0;
static bool mock_last_write_is_dummy = false;

static uint8_t
mock_card_read(void *ctx, uint16_t addr, bool is_dummy)
{
	(void)ctx;
	mock_last_read_addr = addr;
	mock_last_read_is_dummy = is_dummy;
	return mock_read_val;
}

static void
mock_card_write(void *ctx, uint16_t addr, uint8_t val, bool is_dummy)
{
	(void)ctx;
	mock_last_write_addr = addr;
	mock_last_write_val = val;
	mock_last_write_is_dummy = is_dummy;
}

static void
test_expansion_cards(CPU *cpu, Bus *bus)
{
	(void)cpu;

	// Reset bus
	bus->num_cards = 0;
	bus->opts.flat_bus = false;

	// Create mock card
	expansion_card_t mock_card;

	mock_card.name = "MockCard";
	mock_card.base = 0xC100;
	mock_card.mask = 0xFF00; // Matches $C100 - $C1FF
	mock_card.rom_only = false;
	mock_card.read = mock_card_read;
	mock_card.write = mock_card_write;
	mock_card.tick = NULL;
	mock_card.ctx = NULL;

	bus_add_card(bus, &mock_card);

	// 1. Verify read
	mock_read_val = 0xAA;
	uint8_t val = bus_read(bus, 0xC105);

	assert(val == 0xAA);
	assert(mock_last_read_addr == 0xC105);
	assert(mock_last_read_is_dummy == false);

	// 2. Verify write (normal)
	bus_write(bus, 0xC120, 0x55);
	assert(mock_last_write_addr == 0xC120);
	assert(mock_last_write_val == 0x55);
	assert(mock_last_write_is_dummy == false);

	// 3. Verify write (dummy)
	bus_write_ext(bus, 0xC140, 0x99, true);
	assert(mock_last_write_addr == 0xC140);
	assert(mock_last_write_val == 0x99);
	assert(mock_last_write_is_dummy == true);

	// 4. Verify mask-based match (e.g. $C1FE should match $C100/$FF00)
	mock_read_val = 0x12;
	val = bus_read(bus, 0xC1FE);
	assert(val == 0x12);
	assert(mock_last_read_addr == 0xC1FE);

	// 5. Verify non-match falls back to open bus
	bus->last_bus_value = 0x77;
	val = bus_read(bus, 0xC200); // Does not match $C100/$FF00
	assert(val == 0x77);
}

static void
test_disasm_and_breakpoints(CPU *cpu, Bus *bus)
{
	(void)cpu;
	char buf[64];
	int len;

	// Immediate: LDA #$42 -> A9 42
	bus->ram[0x2000] = 0xA9;
	bus->ram[0x2001] = 0x42;
	len = cpu_disassemble(bus, 0x2000, buf);
	assert(len == 2);
	assert(strcmp(buf, "LDA #$42") == 0);

	// Absolute: JSR $1234 -> 20 34 12
	bus->ram[0x2002] = 0x20;
	bus->ram[0x2003] = 0x34;
	bus->ram[0x2004] = 0x12;
	len = cpu_disassemble(bus, 0x2002, buf);
	assert(len == 3);
	assert(strcmp(buf, "JSR $1234") == 0);

	// Implied: NOP -> EA
	bus->ram[0x2005] = 0xEA;
	len = cpu_disassemble(bus, 0x2005, buf);
	assert(len == 1);
	assert(strcmp(buf, "NOP") == 0);

	// Relative: BNE relative offset (0x02) at 0x2006
	bus->ram[0x2006] = 0xD0;
	bus->ram[0x2007] = 0x02;
	len = cpu_disassemble(bus, 0x2006, buf);
	assert(len == 2);
	assert(strcmp(buf, "BNE $200A") == 0);

	// Breakpoint test
	debugger_t dbg;

	dbg_init(&dbg, cpu);
	assert(dbg.num_breakpoints == 0);
	assert(!dbg_has_breakpoint(&dbg, 0x1000));

	dbg_add_breakpoint(&dbg, 0x1000);
	assert(dbg.num_breakpoints == 1);
	assert(dbg_has_breakpoint(&dbg, 0x1000));

	dbg_add_breakpoint(&dbg, 0x2000);
	assert(dbg.num_breakpoints == 2);
	assert(dbg_has_breakpoint(&dbg, 0x2000));

	dbg_remove_breakpoint(&dbg, 0x1000);
	assert(dbg.num_breakpoints == 1);
	assert(!dbg_has_breakpoint(&dbg, 0x1000));
	assert(dbg_has_breakpoint(&dbg, 0x2000));
}

static void
test_reset_vectors(void)
{
	Bus bus;

	if (!bus_init(&bus, 16 * 1024)) {
		fprintf(stderr, "Failed to init bus for Reset Vectors test\n");
		exit(1);
	}

	// 1. Load real wozmon.bin ROM
	if (!bus_load_rom(&bus, "wozmon.bin")) {
		fprintf(stderr,
		    "Failed to load wozmon.bin for Reset Vectors test\n");
		bus_free(&bus);
		exit(1);
	}

	// 2. Read and verify authentic vectors
	uint16_t nmi = bus_read(&bus, 0xFFFA) | (bus_read(&bus, 0xFFFB) << 8);
	uint16_t res = bus_read(&bus, RESET_VECTOR) | (bus_read(&bus, RESET_VECTOR + 1) << 8);
	uint16_t irq = bus_read(&bus, 0xFFFE) | (bus_read(&bus, 0xFFFF) << 8);

	assert(res == 0xFF00);
	assert(nmi == 0x0F00);
	assert(irq == 0x0000);

	bus_free(&bus);
}

static const uint16_t kEntry = 0x0400;
static const uint16_t kBpAddr = 0x0404;
static const uint16_t kSpin = 0x0408;

/* clang-format off */
static const uint8_t kProgram[] = {
	0xA9, 0x01,       // LDA #$01
	0xA9, 0x02,       // LDA #$02
	0xA9, 0x03,       // LDA #$03      <-- breakpoint
	0xA9, 0x04,       // LDA #$04
	0x4C, 0x08, 0x04, // JMP $0408
};
/* clang-format on */

static void
load_program(Bus *bus)
{
	memcpy(bus->ram + kEntry, kProgram, sizeof(kProgram));
}

static int
run_emulator(CPU *cpu, debugger_t *dbg, int max_cycles)
{
	int cycles = 0;
	dbg->step_mode = false;
	while (cycles < max_cycles) {
		if (dbg_has_breakpoint(dbg, cpu->pc) || dbg->step_mode) {
			break;
		}
		cpu->bus->access_cb = dbg_check_access;
		cpu->bus->access_cb_ctx = dbg;
		dbg->current_instruction_pc = cpu->pc;
		uint8_t c = cpu_step(cpu);
		cpu->bus->access_cb = NULL;
		cycles += c;
		if (cpu->pc == kSpin) {
			break;
		}
	}
	return cycles;
}

static void
test_cpu_breakpoint_smoke(void)
{
	// ---- Test 1: breakpoint fires before the instruction executes -----
	{
		Bus bus;
		bus_init(&bus, 16 * 1024);
		load_program(&bus);

		CPU cpu;
		cpu_init(&cpu, &bus);
		cpu.halted = false;
		cpu.pc = kEntry;

		debugger_t dbg;
		dbg_init(&dbg, &cpu);
		dbg_add_breakpoint(&dbg, kBpAddr);

		assert(dbg.num_breakpoints == 1);
		assert(dbg_has_breakpoint(&dbg, kBpAddr));

		// Generous cycle budget to reach the breakpoint.
		run_emulator(&cpu, &dbg, 200);

		// PC must be at the breakpoint address, NOT past it.
		// LDA #$02 was the last instruction to execute — A must hold $02, not $03.
		assert(cpu.pc == kBpAddr);
		assert(cpu.a == 0x02);

		bus_free(&bus);
	}

	// ---- Test 2: clearBreakpoint() resumes normal execution -----------
	{
		Bus bus;
		bus_init(&bus, 16 * 1024);
		load_program(&bus);

		CPU cpu;
		cpu_init(&cpu, &bus);
		cpu.halted = false;
		cpu.pc = kEntry;

		debugger_t dbg;
		dbg_init(&dbg, &cpu);
		dbg_add_breakpoint(&dbg, kBpAddr);

		run_emulator(&cpu, &dbg, 200);
		assert(cpu.pc == kBpAddr);

		// Disarm + resume — the spin trap at $0408 should be reached.
		dbg_remove_breakpoint(&dbg, kBpAddr);
		assert(dbg.num_breakpoints == 0);

		run_emulator(&cpu, &dbg, 200);

		// We should now be sitting on the JMP-to-self.
		assert(cpu.pc == kSpin);
		assert(cpu.a == 0x04);

		bus_free(&bus);
	}

	// ---- Test 3: manual step-over past the breakpoint ----------------
	{
		Bus bus;
		bus_init(&bus, 16 * 1024);
		load_program(&bus);

		CPU cpu;
		cpu_init(&cpu, &bus);
		cpu.halted = false;
		cpu.pc = kEntry;

		debugger_t dbg;
		dbg_init(&dbg, &cpu);
		dbg_add_breakpoint(&dbg, kBpAddr);

		run_emulator(&cpu, &dbg, 200);
		assert(cpu.pc == kBpAddr);

		// Manual step-over: step executes the breakpoint instruction once, advances PC.
		// After this LDA #$03, A should be $03 and PC at $0406.
		cpu_step(&cpu);
		assert(cpu.pc == 0x0406);
		assert(cpu.a == 0x03);

		// Resume — breakpoint stays armed, but PC has moved past it, so we now run to the spin trap.
		run_emulator(&cpu, &dbg, 200);
		assert(cpu.pc == kSpin);
		assert(cpu.a == 0x04);
		assert(dbg_has_breakpoint(&dbg,
		    kBpAddr)); // still armed for any future hit

		bus_free(&bus);
	}

	// ---- Test 4: no-breakpoint sanity (gate must not false-positive) --
	{
		Bus bus;
		bus_init(&bus, 16 * 1024);
		load_program(&bus);

		CPU cpu;
		cpu_init(&cpu, &bus);
		cpu.halted = false;
		cpu.pc = kEntry;

		debugger_t dbg;
		dbg_init(&dbg, &cpu);

		assert(dbg.num_breakpoints == 0);
		run_emulator(&cpu, &dbg, 200);
		assert(cpu.pc == kSpin);
		assert(cpu.a == 0x04);

		bus_free(&bus);
	}
}

static void
test_cpu_watchpoint_smoke(void);

static void
test_bruce_clark_decimal(void);

int
main(void)
{
	printf("Starting 6502 CPU Tests...\n");

	run_test("LDA and STA", test_lda_sta);
	run_test("Binary ADC and SBC", test_adc_sbc_bin);
	run_test("BCD ADC and SBC", test_adc_sbc_bcd);
	run_test("Branching (BNE)", test_branching);
	run_test("Subroutines (JSR/RTS)", test_jsr_rts);
	run_test("Open Bus Memory Model", test_open_bus);
	run_test("Illegal Instructions", test_illegal_instructions);
	run_test("Dummy Write Side-Effects", test_dummy_write_side_effects);
	run_test("Interrupts (IRQ & NMI)", test_interrupts);
	run_test("Expansion Cards System", test_expansion_cards);
	run_test("Disassembler and Breakpoints", test_disasm_and_breakpoints);

	// New Ported Tests
	test_reset_vectors();
	printf("Test PASS: Reset Vectors\n");

	test_cpu_breakpoint_smoke();
	printf("Test PASS: CPU Breakpoint Smoke\n");

	test_cpu_watchpoint_smoke();
	printf("Test PASS: CPU Watchpoint Smoke\n");

	test_bruce_clark_decimal();
	printf("Test PASS: Bruce Clark Decimal Test\n");

	printf("\nAll CPU tests passed successfully!\n");
	return 0;
}

static void
test_cpu_watchpoint_smoke(void)
{
	/* clang-format off */
	static const uint8_t wp_program[] = {
		0xA9, 0x42,       // LDA #$42
		0x8D, 0x00, 0x02, // STA $0200
		0xAD, 0x00, 0x02, // LDA $0200
		0x4C, 0x08, 0x04  // JMP $0408
	};
	/* clang-format on */

	// ---- Test 1: Write watchpoint fires on STA -----
	{
		Bus bus;
		bus_init(&bus, 16 * 1024);
		memcpy(bus.ram + 0x0400, wp_program, sizeof(wp_program));

		CPU cpu;
		cpu_init(&cpu, &bus);
		cpu.halted = false;
		cpu.pc = 0x0400;

		debugger_t dbg;
		dbg_init(&dbg, &cpu);
		dbg.step_mode = false;

		dbg_add_watchpoint(&dbg, 0x0200, WP_WRITE);
		assert(dbg.num_watchpoints == 1);

		// Run emulator
		run_emulator(&cpu, &dbg, 100);

		// Watchpoint should trigger at STA $0200 (PC = 0x0402).
		// STA $0200 is 3 bytes, so PC is advanced to 0x0405.
		// Since watchpoint is triggered, emulator breaks.
		assert(dbg.step_mode == true);
		assert(cpu.pc == 0x0405);
		assert(bus.ram[0x0200] == 0x42);

		bus_free(&bus);
	}

	// ---- Test 2: Read watchpoint fires on LDA $0200 -----
	{
		Bus bus;
		bus_init(&bus, 16 * 1024);
		memcpy(bus.ram + 0x0400, wp_program, sizeof(wp_program));

		CPU cpu;
		cpu_init(&cpu, &bus);
		cpu.halted = false;
		cpu.pc = 0x0400;

		debugger_t dbg;
		dbg_init(&dbg, &cpu);
		dbg.step_mode = false;

		dbg_add_watchpoint(&dbg, 0x0200, WP_READ);
		assert(dbg.num_watchpoints == 1);

		// Run emulator
		run_emulator(&cpu, &dbg, 100);

		// STA $0200 executes (doesn't trigger since it's a write, not a read).
		// LDA $0200 executes (which reads from 0x0200, triggering read watchpoint).
		// PC should be advanced to 0x0408.
		assert(dbg.step_mode == true);
		assert(cpu.pc == 0x0408);
		assert(cpu.a == 0x42);

		bus_free(&bus);
	}
}

static void
test_bruce_clark_decimal(void)
{
	Bus bus;
	if (!bus_init(&bus, 65536)) {
		fprintf(stderr, "Failed to init 64KB bus for Bruce Clark Decimal Test\n");
		exit(1);
	}
	bus.opts.flat_bus = true;

	if (!bus_load_bin(&bus, "tests/6502_decimal_test.bin", 0x0400)) {
		fprintf(stderr, "Failed to load tests/6502_decimal_test.bin\n");
		exit(1);
	}

	CPU cpu;
	cpu_init(&cpu, &bus);
	cpu.halted = false;
	cpu.pc = 0x0400;

	uint16_t prev_pc = 0;
	int limit = 100000000;
	int steps = 0;
	while (steps < limit) {
		cpu_step(&cpu);
		steps++;
		if (cpu.pc == prev_pc) {
			break;
		}
		prev_pc = cpu.pc;
	}

	uint8_t error = bus.ram[0x000B];
	if (error != 0) {
		fprintf(stderr, "Bruce Clark Decimal Test FAILED: ERROR = %d\n", error);
		fprintf(stderr, "N1=%02X, N2=%02X, actual=%02X, predicted=%02X\n",
		    bus.ram[0x0000], bus.ram[0x0001], bus.ram[0x0004], bus.ram[0x0006]);
		exit(1);
	}

	bus_free(&bus);
}
