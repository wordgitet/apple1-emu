#include "cpu.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Memory helpers                                                       */
/* ------------------------------------------------------------------ */

static inline uint8_t
read_byte(CPU *cpu, uint16_t addr)
{
	return bus_read(cpu->bus, addr);
}

static inline void
write_byte(CPU *cpu, uint16_t addr, uint8_t val)
{
	bus_write(cpu->bus, addr, val);
}

static inline void
write_byte_dummy(CPU *cpu, uint16_t addr, uint8_t val)
{
	bus_write_ext(cpu->bus, addr, val, true);
}

static inline uint16_t
read_word(CPU *cpu, uint16_t addr)
{
	uint8_t lo = read_byte(cpu, addr);
	uint8_t hi = read_byte(cpu, addr + 1);

	return (hi << 8) | lo;
}

/* ------------------------------------------------------------------ */
/* Stack helpers                                                        */
/* ------------------------------------------------------------------ */

static inline void
push_byte(CPU *cpu, uint8_t val)
{
	write_byte(cpu, 0x0100 + cpu->s, val);
	cpu->s--;
}

static inline uint8_t
pull_byte(CPU *cpu)
{
	cpu->s++;
	return read_byte(cpu, 0x0100 + cpu->s);
}

static inline void
push_word(CPU *cpu, uint16_t val)
{
	push_byte(cpu, (val >> 8) & 0xFF);
	push_byte(cpu, val & 0xFF);
}

static inline uint16_t
pull_word(CPU *cpu)
{
	uint8_t lo = pull_byte(cpu);
	uint8_t hi = pull_byte(cpu);

	return (hi << 8) | lo;
}

/* ------------------------------------------------------------------ */
/* Flag helpers                                                         */
/* ------------------------------------------------------------------ */

static inline void
set_flag(CPU *cpu, uint8_t flag, bool cond)
{
	if (cond)
		cpu->p |= flag;
	else
		cpu->p &= ~flag;
}

static inline void
update_nz(CPU *cpu, uint8_t val)
{
	set_flag(cpu, FLAG_ZERO, val == 0);
	set_flag(cpu, FLAG_NEGATIVE, (val & 0x80) != 0);
}

/* ------------------------------------------------------------------ */
/* Initialise / reset                                                   */
/* ------------------------------------------------------------------ */

void
cpu_init(CPU *cpu, Bus *bus)
{
	cpu->bus = bus;
	/*
	 * Real Apple-1 had no power-on reset circuit — the CPU starts
	 * in an undefined/frozen state until the user presses RESET.
	 * We model this with halted=true; cpu_reset() clears it.
	 */
	cpu->a = 0;
	cpu->x = 0;
	cpu->y = 0;
	cpu->s = 0;
	cpu->p = FLAG_UNUSED;
	cpu->pc = 0;
	cpu->nmi_pending = false;
	cpu->irq_pending = false;
	cpu->halted = true;
	cpu->last_cycles = 0;

	cpu->pc_trace_idx = 0;
	cpu->prev_pc = 0;
	cpu->prev_pc_valid = false;
	memset(cpu->pc_trace, 0, sizeof(cpu->pc_trace));
}

void
cpu_reset(CPU *cpu)
{
	cpu->a = 0;
	cpu->x = 0;
	cpu->y = 0;
	cpu->s = 0xFD;
	cpu->p = FLAG_UNUSED | FLAG_INTERRUPT; /* 0x24 on boot */
	cpu->nmi_pending = false;
	cpu->irq_pending = false;
	cpu->halted = false; /* RESET always un-freezes the CPU */

	/* Read start address from the Reset Vector (RESET_VECTOR/RESET_VECTOR+1) */
	uint8_t lo = bus_read(cpu->bus, RESET_VECTOR);
	uint8_t hi = bus_read(cpu->bus, RESET_VECTOR + 1);

	cpu->pc = (hi << 8) | lo;

	cpu->pc_trace_idx = 0;
	cpu->prev_pc = 0;
	cpu->prev_pc_valid = false;
	memset(cpu->pc_trace, 0, sizeof(cpu->pc_trace));
}

/* ------------------------------------------------------------------ */
/* Addressing mode helpers                                              */
/*                                                                      */
/* Mode identifiers:                                                    */
/*  0: Immediate                                                        */
/*  1: Zero Page                                                        */
/*  2: Zero Page,X                                                      */
/*  3: Zero Page,Y                                                      */
/*  4: Absolute                                                         */
/*  5: Absolute,X                                                       */
/*  6: Absolute,Y                                                       */
/*  7: Indirect (JMP only)                                              */
/*  8: Indexed Indirect (Indirect,X)                                    */
/*  9: Indirect Indexed ((Indirect),Y)                                  */
/* ------------------------------------------------------------------ */

static uint16_t
addr_imm(CPU *cpu)
{
	return cpu->pc++;
}

static uint16_t
addr_zp(CPU *cpu)
{
	return read_byte(cpu, cpu->pc++);
}

static uint16_t
addr_zpx(CPU *cpu)
{
	uint8_t base = read_byte(cpu, cpu->pc++);
	read_byte(cpu, base); /* cycle 3: dummy read of unindexed zp address */
	return (base + cpu->x) & 0xFF;
}

static uint16_t
addr_zpy(CPU *cpu)
{
	uint8_t base = read_byte(cpu, cpu->pc++);
	read_byte(cpu, base); /* cycle 3: dummy read of unindexed zp address */
	return (base + cpu->y) & 0xFF;
}

static uint16_t
addr_abs(CPU *cpu)
{
	uint16_t a = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	return a;
}

static uint16_t
addr_absx(CPU *cpu, bool *px)
{
	uint16_t base = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	uint16_t a = base + cpu->x;

	*px = (base & 0xFF00) != (a & 0xFF00);
	if (*px) {
		/* cycle 4: dummy read of uncorrected address */
		read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	}
	return a;
}

static uint16_t
addr_absx_always(CPU *cpu)
{
	uint16_t base = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	uint16_t a = base + cpu->x;

	/* cycle 4: dummy read of uncorrected address */
	read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	return a;
}

static uint16_t
addr_absy(CPU *cpu, bool *px)
{
	uint16_t base = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	uint16_t a = base + cpu->y;

	*px = (base & 0xFF00) != (a & 0xFF00);
	if (*px) {
		/* cycle 4: dummy read of uncorrected address */
		read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	}
	return a;
}

static uint16_t
addr_absy_always(CPU *cpu)
{
	uint16_t base = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	uint16_t a = base + cpu->y;

	/* cycle 4: dummy read of uncorrected address */
	read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	return a;
}

static uint16_t
addr_ind(CPU *cpu)
{
	uint16_t ptr = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	if ((ptr & 0x00FF) == 0x00FF) {
		/* 6502 page-boundary bug */
		uint8_t lo = read_byte(cpu, ptr);
		uint8_t hi = read_byte(cpu, ptr & 0xFF00);

		return (hi << 8) | lo;
	}
	return read_word(cpu, ptr);
}

static uint16_t
addr_izx(CPU *cpu)
{
	uint8_t zp_raw = read_byte(cpu, cpu->pc++);
	read_byte(cpu, zp_raw); /* cycle 3: dummy read before X is added */
	uint8_t zp = (zp_raw + cpu->x) & 0xFF;
	uint8_t lo = read_byte(cpu, zp);
	uint8_t hi = read_byte(cpu, (zp + 1) & 0xFF);

	return (hi << 8) | lo;
}

static uint16_t
addr_izy(CPU *cpu, bool *px)
{
	uint8_t zp = read_byte(cpu, cpu->pc++);
	uint8_t lo = read_byte(cpu, zp);
	uint8_t hi = read_byte(cpu, (zp + 1) & 0xFF);
	uint16_t base = (hi << 8) | lo;
	uint16_t a = base + cpu->y;

	*px = (base & 0xFF00) != (a & 0xFF00);
	if (*px) {
		/* cycle 5: dummy read of uncorrected address */
		read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	}
	return a;
}

static uint16_t
addr_izy_always(CPU *cpu)
{
	uint8_t zp = read_byte(cpu, cpu->pc++);
	uint8_t lo = read_byte(cpu, zp);
	uint8_t hi = read_byte(cpu, (zp + 1) & 0xFF);
	uint16_t base = (hi << 8) | lo;
	uint16_t a = base + cpu->y;

	/* cycle 5: dummy read of uncorrected address */
	read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	return a;
}

/* ------------------------------------------------------------------ */
/* Branch helper                                                        */
/* ------------------------------------------------------------------ */

static uint8_t
do_branch(CPU *cpu, bool cond)
{
	int8_t off = (int8_t)read_byte(cpu, cpu->pc++);

	if (cond) {
		uint16_t old = cpu->pc;
		read_byte(cpu, old); /* cycle 3: dummy read of PC+2 */

		uint16_t target = old + off;
		if ((old & 0xFF00) != (target & 0xFF00)) {
			/* cycle 4: dummy read of intermediate address (uncorrected page) */
			uint16_t intermediate = (old & 0xFF00) |
			    (target & 0x00FF);
			read_byte(cpu, intermediate);
			cpu->pc = target;
			return 2;
		}
		cpu->pc = target;
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* ADC / SBC arithmetic                                                 */
/* ------------------------------------------------------------------ */

static void
adc_bcd(CPU *cpu, uint8_t m)
{
	uint8_t carry = (cpu->p & FLAG_CARRY) ? 1 : 0;
	uint8_t bin_result = cpu->a + m + carry;
	set_flag(cpu, FLAG_ZERO, bin_result == 0);

	uint8_t low = (cpu->a & 0x0F) + (m & 0x0F) + carry;
	uint8_t high = (cpu->a >> 4) + (m >> 4);
	if (low > 9) {
		low -= 10;
		high++;
	}

	uint8_t result = (high << 4) | (low & 0x0F);
	set_flag(cpu, FLAG_NEGATIVE, (result & 0x80) != 0);
	set_flag(cpu,
	    FLAG_OVERFLOW,
	    (~(cpu->a ^ m) & (cpu->a ^ result) & 0x80) != 0);

	if (high > 9) {
		result -= 0xA0; // 10 * 16
		set_flag(cpu, FLAG_CARRY, true);
	} else {
		set_flag(cpu, FLAG_CARRY, false);
	}

	cpu->a = result;
}

static void
sbc_bcd(CPU *cpu, uint8_t m)
{
	uint8_t carry = (cpu->p & FLAG_CARRY) ? 1 : 0;
	uint8_t bin_result = cpu->a + (m ^ 0xFF) + carry;
	set_flag(cpu, FLAG_ZERO, bin_result == 0);

	int8_t carry_inv = (carry == 0) ? 1 : 0;
	int8_t low = (int8_t)(cpu->a & 0x0F) - (int8_t)(m & 0x0F) - carry_inv;
	int8_t high = (int8_t)(cpu->a >> 4) - (int8_t)(m >> 4);

	if (low < 0) {
		low += 10;
		high--;
	}

	uint8_t result = ((uint8_t)high << 4) | ((uint8_t)low & 0x0F);
	set_flag(cpu, FLAG_NEGATIVE, (result & 0x80) != 0);
	set_flag(cpu,
	    FLAG_OVERFLOW,
	    ((cpu->a ^ m) & (cpu->a ^ result) & 0x80) != 0);

	if (high < 0) {
		result += 0xA0; // 10 * 16
		set_flag(cpu, FLAG_CARRY, false);
	} else {
		set_flag(cpu, FLAG_CARRY, true);
	}

	cpu->a = result;
}

static void
adc_bin(CPU *cpu, uint8_t m)
{
	int c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	int sum = cpu->a + m + c;

	set_flag(cpu, FLAG_CARRY, sum > 0xFF);
	set_flag(cpu, FLAG_ZERO, (sum & 0xFF) == 0);
	set_flag(cpu,
	    FLAG_OVERFLOW,
	    (~(cpu->a ^ m) & (cpu->a ^ sum) & 0x80) != 0);
	set_flag(cpu, FLAG_NEGATIVE, (sum & 0x80) != 0);
	cpu->a = sum & 0xFF;
}

static void
sbc_bin(CPU *cpu, uint8_t m)
{
	int c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	int val = m ^ 0xFF;
	int sum = cpu->a + val + c;

	set_flag(cpu, FLAG_CARRY, sum > 0xFF);
	set_flag(cpu, FLAG_ZERO, (sum & 0xFF) == 0);
	set_flag(cpu,
	    FLAG_OVERFLOW,
	    (~(cpu->a ^ val) & (cpu->a ^ sum) & 0x80) != 0);
	set_flag(cpu, FLAG_NEGATIVE, (sum & 0x80) != 0);
	cpu->a = sum & 0xFF;
}

/* ------------------------------------------------------------------ */
/* Opcode handler typedef                                               */
/* ------------------------------------------------------------------ */

typedef void (*opcode_fn)(CPU *cpu);

/* ================================================================== */
/* Individual opcode handlers                                           */
/* Each handler reads its operand(s), performs the operation, and      */
/* writes cpu->last_cycles with the cycle count for that instruction.  */
/* ================================================================== */

/* ---- ADC ---- */
static void
op_adc_imm(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_imm(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 2;
}
static void
op_adc_zp(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_zp(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 3;
}
static void
op_adc_zpx(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_zpx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_adc_abs(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_abs(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_adc_absx(CPU *cpu)
{
	bool px = false;
	uint8_t m = read_byte(cpu, addr_absx(cpu, &px));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_adc_absy(CPU *cpu)
{
	bool px = false;
	uint8_t m = read_byte(cpu, addr_absy(cpu, &px));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_adc_izx(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_izx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 6;
}
static void
op_adc_izy(CPU *cpu)
{
	bool px = false;
	uint8_t m = read_byte(cpu, addr_izy(cpu, &px));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- AND ---- */
static void
op_and_imm(CPU *cpu)
{
	cpu->a &= read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_and_zp(CPU *cpu)
{
	cpu->a &= read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_and_zpx(CPU *cpu)
{
	cpu->a &= read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_and_abs(CPU *cpu)
{
	cpu->a &= read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_and_absx(CPU *cpu)
{
	bool px = false;
	cpu->a &= read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_and_absy(CPU *cpu)
{
	bool px = false;
	cpu->a &= read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_and_izx(CPU *cpu)
{
	cpu->a &= read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_and_izy(CPU *cpu)
{
	bool px = false;
	cpu->a &= read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- ASL ---- */
static void
op_asl_acc(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x80) != 0);
	cpu->a <<= 1;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_asl_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 5;
}
static void
op_asl_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_asl_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_asl_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 7;
}

/* ---- Branch ---- */
static void
op_bpl(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_NEGATIVE));
}
static void
op_bmi(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_NEGATIVE));
}
static void
op_bvc(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_OVERFLOW));
}
static void
op_bvs(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_OVERFLOW));
}
static void
op_bcc(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_CARRY));
}
static void
op_bcs(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_CARRY));
}
static void
op_bne(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_ZERO));
}
static void
op_beq(CPU *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_ZERO));
}

/* ---- BIT ---- */
static void
op_bit_zp(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_ZERO, (cpu->a & t) == 0);
	set_flag(cpu, FLAG_NEGATIVE, (t & 0x80) != 0);
	set_flag(cpu, FLAG_OVERFLOW, (t & 0x40) != 0);
	cpu->last_cycles = 3;
}
static void
op_bit_abs(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_ZERO, (cpu->a & t) == 0);
	set_flag(cpu, FLAG_NEGATIVE, (t & 0x80) != 0);
	set_flag(cpu, FLAG_OVERFLOW, (t & 0x40) != 0);
	cpu->last_cycles = 4;
}

/* ---- BRK ---- */
static void
op_brk(CPU *cpu)
{
	read_byte(cpu, cpu->pc++); /* cycle 2: fetch and discard padding byte */
	push_word(cpu, cpu->pc);
	push_byte(cpu, cpu->p | FLAG_BREAK | FLAG_UNUSED);
	set_flag(cpu, FLAG_INTERRUPT, true);
	cpu->pc = read_word(cpu, 0xFFFE);
	cpu->last_cycles = 7;
}

/* ---- Clear flags ---- */
static void
op_clc(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, false);
	cpu->last_cycles = 2;
}
static void
op_cli(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_INTERRUPT, false);
	cpu->last_cycles = 2;
}
static void
op_clv(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_OVERFLOW, false);
	cpu->last_cycles = 2;
}
static void
op_cld(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_DECIMAL, false);
	cpu->last_cycles = 2;
}

/* ---- Set flags ---- */
static void
op_sec(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, true);
	cpu->last_cycles = 2;
}
static void
op_sei(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_INTERRUPT, true);
	cpu->last_cycles = 2;
}
static void
op_sed(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_DECIMAL, true);
	cpu->last_cycles = 2;
}

/* ---- CMP ---- */
static void
op_cmp_imm(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_imm(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 2;
}
static void
op_cmp_zp(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 3;
}
static void
op_cmp_zpx(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zpx(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4;
}
static void
op_cmp_abs(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4;
}
static void
op_cmp_absx(CPU *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_absx(cpu, &px));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_cmp_absy(CPU *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_absy(cpu, &px));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_cmp_izx(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_izx(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 6;
}
static void
op_cmp_izy(CPU *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_izy(cpu, &px));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- CPX ---- */
static void
op_cpx_imm(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_imm(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->x >= t);
	update_nz(cpu, (uint8_t)(cpu->x - t));
	cpu->last_cycles = 2;
}
static void
op_cpx_zp(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->x >= t);
	update_nz(cpu, (uint8_t)(cpu->x - t));
	cpu->last_cycles = 3;
}
static void
op_cpx_abs(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->x >= t);
	update_nz(cpu, (uint8_t)(cpu->x - t));
	cpu->last_cycles = 4;
}

/* ---- CPY ---- */
static void
op_cpy_imm(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_imm(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->y >= t);
	update_nz(cpu, (uint8_t)(cpu->y - t));
	cpu->last_cycles = 2;
}
static void
op_cpy_zp(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->y >= t);
	update_nz(cpu, (uint8_t)(cpu->y - t));
	cpu->last_cycles = 3;
}
static void
op_cpy_abs(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->y >= t);
	update_nz(cpu, (uint8_t)(cpu->y - t));
	cpu->last_cycles = 4;
}

/* ---- DEC ---- */
static void
op_dec_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig - 1);
	update_nz(cpu, orig - 1);
	cpu->last_cycles = 5;
}
static void
op_dec_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig - 1);
	update_nz(cpu, orig - 1);
	cpu->last_cycles = 6;
}
static void
op_dec_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig - 1);
	update_nz(cpu, orig - 1);
	cpu->last_cycles = 6;
}
static void
op_dec_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig - 1);
	update_nz(cpu, orig - 1);
	cpu->last_cycles = 7;
}

/* ---- DEX / DEY ---- */
static void
op_dex(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x--;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_dey(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->y--;
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}

/* ---- EOR ---- */
static void
op_eor_imm(CPU *cpu)
{
	cpu->a ^= read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_eor_zp(CPU *cpu)
{
	cpu->a ^= read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_eor_zpx(CPU *cpu)
{
	cpu->a ^= read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_eor_abs(CPU *cpu)
{
	cpu->a ^= read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_eor_absx(CPU *cpu)
{
	bool px = false;
	cpu->a ^= read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_eor_absy(CPU *cpu)
{
	bool px = false;
	cpu->a ^= read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_eor_izx(CPU *cpu)
{
	cpu->a ^= read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_eor_izy(CPU *cpu)
{
	bool px = false;
	cpu->a ^= read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- INC ---- */
static void
op_inc_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig + 1);
	update_nz(cpu, orig + 1);
	cpu->last_cycles = 5;
}
static void
op_inc_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig + 1);
	update_nz(cpu, orig + 1);
	cpu->last_cycles = 6;
}
static void
op_inc_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig + 1);
	update_nz(cpu, orig + 1);
	cpu->last_cycles = 6;
}
static void
op_inc_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t orig = read_byte(cpu, a);
	write_byte_dummy(cpu,
	    a,
	    orig); /* NMOS dummy write of unmodified value */
	write_byte(cpu, a, orig + 1);
	update_nz(cpu, orig + 1);
	cpu->last_cycles = 7;
}

/* ---- INX / INY ---- */
static void
op_inx(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x++;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_iny(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->y++;
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}

/* ---- JMP ---- */
static void
op_jmp_abs(CPU *cpu)
{
	cpu->pc = addr_abs(cpu);
	cpu->last_cycles = 3;
}
static void
op_jmp_ind(CPU *cpu)
{
	cpu->pc = addr_ind(cpu);
	cpu->last_cycles = 5;
}

/* ---- JSR / RTS / RTI ---- */
static void
op_jsr(CPU *cpu)
{
	uint8_t lo = read_byte(cpu, cpu->pc++); /* cycle 2: addr low */
	read_byte(cpu, 0x0100 + cpu->s);	/* cycle 3: dummy stack read */
	push_word(cpu,
	    cpu->pc); /* cycles 4-5: push return addr (PC-1 already advanced) */
	uint8_t hi = read_byte(cpu, cpu->pc); /* cycle 6: addr high */
	cpu->pc = (hi << 8) | lo;
	cpu->last_cycles = 6;
}
static void
op_rts(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	read_byte(cpu,
	    0x0100 +
		cpu->s); /* cycle 3: dummy read of stack before pre-increment */
	cpu->pc = pull_word(cpu) + 1; /* cycles 4-5: pull return addr */
	read_byte(cpu,
	    cpu->pc - 1); /* cycle 6: dummy read before PC increment */
	cpu->last_cycles = 6;
}
static void
op_rti(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	read_byte(cpu,
	    0x0100 +
		cpu->s); /* cycle 3: dummy read of stack before pre-increment */
	cpu->p = (pull_byte(cpu) & ~FLAG_BREAK) | FLAG_UNUSED; /* cycle 4 */
	cpu->pc = pull_word(cpu);			       /* cycles 5-6 */
	cpu->last_cycles = 6;
}

/* ---- LDA ---- */
static void
op_lda_imm(CPU *cpu)
{
	cpu->a = read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_lda_zp(CPU *cpu)
{
	cpu->a = read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_lda_zpx(CPU *cpu)
{
	cpu->a = read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_lda_abs(CPU *cpu)
{
	cpu->a = read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_lda_absx(CPU *cpu)
{
	bool px = false;
	cpu->a = read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_lda_absy(CPU *cpu)
{
	bool px = false;
	cpu->a = read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_lda_izx(CPU *cpu)
{
	cpu->a = read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_lda_izy(CPU *cpu)
{
	bool px = false;
	cpu->a = read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- LDX ---- */
static void
op_ldx_imm(CPU *cpu)
{
	cpu->x = read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_ldx_zp(CPU *cpu)
{
	cpu->x = read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 3;
}
static void
op_ldx_zpy(CPU *cpu)
{
	cpu->x = read_byte(cpu, addr_zpy(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 4;
}
static void
op_ldx_abs(CPU *cpu)
{
	cpu->x = read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 4;
}
static void
op_ldx_absy(CPU *cpu)
{
	bool px = false;
	cpu->x = read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- LDY ---- */
static void
op_ldy_imm(CPU *cpu)
{
	cpu->y = read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}
static void
op_ldy_zp(CPU *cpu)
{
	cpu->y = read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 3;
}
static void
op_ldy_zpx(CPU *cpu)
{
	cpu->y = read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 4;
}
static void
op_ldy_abs(CPU *cpu)
{
	cpu->y = read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 4;
}
static void
op_ldy_absx(CPU *cpu)
{
	bool px = false;
	cpu->y = read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- LSR ---- */
static void
op_lsr_acc(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x01) != 0);
	cpu->a >>= 1;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_lsr_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t >>= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 5;
}
static void
op_lsr_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t >>= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_lsr_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t >>= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_lsr_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t >>= 1;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 7;
}

/* ---- NOP ---- */
static void
op_nop(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->last_cycles = 2;
}

/* ---- ORA ---- */
static void
op_ora_imm(CPU *cpu)
{
	cpu->a |= read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_ora_zp(CPU *cpu)
{
	cpu->a |= read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_ora_zpx(CPU *cpu)
{
	cpu->a |= read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_ora_abs(CPU *cpu)
{
	cpu->a |= read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_ora_absx(CPU *cpu)
{
	bool px = false;
	cpu->a |= read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_ora_absy(CPU *cpu)
{
	bool px = false;
	cpu->a |= read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_ora_izx(CPU *cpu)
{
	cpu->a |= read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_ora_izy(CPU *cpu)
{
	bool px = false;
	cpu->a |= read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- PHA / PHP / PLA / PLP ---- */
static void
op_pha(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	push_byte(cpu, cpu->a);	 /* cycle 3 */
	cpu->last_cycles = 3;
}
static void
op_php(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	push_byte(cpu, cpu->p | FLAG_BREAK | FLAG_UNUSED); /* cycle 3 */
	cpu->last_cycles = 3;
}
static void
op_pla(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	read_byte(cpu,
	    0x0100 + cpu->s);	 /* cycle 3: dummy read before pre-increment */
	cpu->a = pull_byte(cpu); /* cycle 4 */
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_plp(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	read_byte(cpu,
	    0x0100 + cpu->s); /* cycle 3: dummy read before pre-increment */
	cpu->p = (pull_byte(cpu) & ~FLAG_BREAK) | FLAG_UNUSED; /* cycle 4 */
	cpu->last_cycles = 4;
}

/* ---- ROL ---- */
static void
op_rol_acc(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x80) != 0);
	cpu->a = (cpu->a << 1) | old_c;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_rol_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (t << 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 5;
}
static void
op_rol_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (t << 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_rol_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (t << 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_rol_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (t << 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 7;
}

/* ---- ROR ---- */
static void
op_ror_acc(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x01) != 0);
	cpu->a = (cpu->a >> 1) | old_c;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_ror_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 5;
}
static void
op_ror_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_ror_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_ror_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 7;
}

/* ---- SBC ---- */
static void
op_sbc_imm(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_imm(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 2;
}
static void
op_sbc_zp(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_zp(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 3;
}
static void
op_sbc_zpx(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_zpx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_sbc_abs(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_abs(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_sbc_absx(CPU *cpu)
{
	bool px = false;
	uint8_t m = read_byte(cpu, addr_absx(cpu, &px));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_sbc_absy(CPU *cpu)
{
	bool px = false;
	uint8_t m = read_byte(cpu, addr_absy(cpu, &px));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_sbc_izx(CPU *cpu)
{
	uint8_t m = read_byte(cpu, addr_izx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 6;
}
static void
op_sbc_izy(CPU *cpu)
{
	bool px = false;
	uint8_t m = read_byte(cpu, addr_izy(cpu, &px));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- STA ---- */
static void
op_sta_zp(CPU *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->a);
	cpu->last_cycles = 3;
}
static void
op_sta_zpx(CPU *cpu)
{
	write_byte(cpu, addr_zpx(cpu), cpu->a);
	cpu->last_cycles = 4;
}
static void
op_sta_abs(CPU *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->a);
	cpu->last_cycles = 4;
}
static void
op_sta_absx(CPU *cpu)
{
	bool px = false;
	uint16_t a = addr_absx(cpu, &px);
	if (!px) {
		read_byte(cpu, a);
	}
	write_byte(cpu, a, cpu->a);
	cpu->last_cycles = 5;
}
static void
op_sta_absy(CPU *cpu)
{
	bool px = false;
	uint16_t a = addr_absy(cpu, &px);
	if (!px) {
		read_byte(cpu, a);
	}
	write_byte(cpu, a, cpu->a);
	cpu->last_cycles = 5;
}
static void
op_sta_izx(CPU *cpu)
{
	write_byte(cpu, addr_izx(cpu), cpu->a);
	cpu->last_cycles = 6;
}
static void
op_sta_izy(CPU *cpu)
{
	bool px = false;
	uint16_t a = addr_izy(cpu, &px);
	if (!px) {
		read_byte(cpu, a);
	}
	write_byte(cpu, a, cpu->a);
	cpu->last_cycles = 6;
}

/* ---- STX ---- */
static void
op_stx_zp(CPU *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->x);
	cpu->last_cycles = 3;
}
static void
op_stx_zpy(CPU *cpu)
{
	write_byte(cpu, addr_zpy(cpu), cpu->x);
	cpu->last_cycles = 4;
}
static void
op_stx_abs(CPU *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->x);
	cpu->last_cycles = 4;
}

/* ---- STY ---- */
static void
op_sty_zp(CPU *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->y);
	cpu->last_cycles = 3;
}
static void
op_sty_zpx(CPU *cpu)
{
	write_byte(cpu, addr_zpx(cpu), cpu->y);
	cpu->last_cycles = 4;
}
static void
op_sty_abs(CPU *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->y);
	cpu->last_cycles = 4;
}

/* ---- Register transfers ---- */
static void
op_tax(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x = cpu->a;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_tay(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->y = cpu->a;
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}
static void
op_tsx(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x = cpu->s;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_txa(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->a = cpu->x;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_txs(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->s = cpu->x;
	cpu->last_cycles = 2;
} /* no flags */
static void
op_tya(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->a = cpu->y;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* ================================================================== */
/* Undocumented / illegal opcodes                                       */
/* ================================================================== */

/* ---- LAX ---- */
static void
op_lax_izx(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_izx(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_lax_zp(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 3;
}
static void
op_lax_abs(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 4;
}
static void
op_lax_izy(CPU *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_izy(cpu, &px));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}
static void
op_lax_zpy(CPU *cpu)
{
	uint8_t t = read_byte(cpu, addr_zpy(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 4;
}
static void
op_lax_absy(CPU *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_absy(cpu, &px));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- SAX ---- */
static void
op_sax_izx(CPU *cpu)
{
	write_byte(cpu, addr_izx(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 6;
}
static void
op_sax_zp(CPU *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 3;
}
static void
op_sax_abs(CPU *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 4;
}
static void
op_sax_zpy(CPU *cpu)
{
	write_byte(cpu, addr_zpy(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 4;
}

/* ---- SLO (ASL+ORA, RMW) ---- */
static void
op_slo_izx(CPU *cpu)
{
	uint16_t a = addr_izx(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 8;
}
static void
op_slo_zp(CPU *cpu)
{
	uint16_t a = addr_zp(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 5;
}
static void
op_slo_abs(CPU *cpu)
{
	uint16_t a = addr_abs(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 6;
}
static void
op_slo_izy(CPU *cpu)
{
	uint16_t a = addr_izy_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 8;
}
static void
op_slo_zpx(CPU *cpu)
{
	uint16_t a = addr_zpx(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 6;
}
static void
op_slo_absy(CPU *cpu)
{
	uint16_t a = addr_absy_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 7;
}
static void
op_slo_absx(CPU *cpu)
{
	uint16_t a = addr_absx_always(cpu);
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t <<= 1;
	cpu->a |= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
	cpu->last_cycles = 7;
}

/* ---- RLA (ROL+AND, RMW) ---- */
static void
rla_core(CPU *cpu, uint16_t a)
{
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (t << 1) | old_c;
	cpu->a &= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
}
static void
op_rla_izx(CPU *cpu)
{
	rla_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_rla_zp(CPU *cpu)
{
	rla_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_rla_abs(CPU *cpu)
{
	rla_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_rla_izy(CPU *cpu)
{
	rla_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_rla_zpx(CPU *cpu)
{
	rla_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_rla_absy(CPU *cpu)
{
	rla_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_rla_absx(CPU *cpu)
{
	rla_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- SRE (LSR+EOR, RMW) ---- */
static void
sre_core(CPU *cpu, uint16_t a)
{
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t >>= 1;
	cpu->a ^= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
}
static void
op_sre_izx(CPU *cpu)
{
	sre_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_sre_zp(CPU *cpu)
{
	sre_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_sre_abs(CPU *cpu)
{
	sre_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_sre_izy(CPU *cpu)
{
	sre_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_sre_zpx(CPU *cpu)
{
	sre_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_sre_absy(CPU *cpu)
{
	sre_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_sre_absx(CPU *cpu)
{
	sre_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- RRA (ROR+ADC, RMW) ---- */
static void
rra_core(CPU *cpu, uint16_t a)
{
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, t);
	else
		adc_bin(cpu, t);
	write_byte(cpu, a, t);
}
static void
op_rra_izx(CPU *cpu)
{
	rra_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_rra_zp(CPU *cpu)
{
	rra_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_rra_abs(CPU *cpu)
{
	rra_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_rra_izy(CPU *cpu)
{
	rra_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_rra_zpx(CPU *cpu)
{
	rra_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_rra_absy(CPU *cpu)
{
	rra_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_rra_absx(CPU *cpu)
{
	rra_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- DCP (DEC+CMP, RMW) ---- */
static void
dcp_core(CPU *cpu, uint16_t a)
{
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	t--;
	int diff = cpu->a - t;
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	set_flag(cpu, FLAG_ZERO, (diff & 0xFF) == 0);
	set_flag(cpu, FLAG_NEGATIVE, (diff & 0x80) != 0);
	write_byte(cpu, a, t);
}
static void
op_dcp_izx(CPU *cpu)
{
	dcp_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_dcp_zp(CPU *cpu)
{
	dcp_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_dcp_abs(CPU *cpu)
{
	dcp_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_dcp_izy(CPU *cpu)
{
	dcp_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_dcp_zpx(CPU *cpu)
{
	dcp_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_dcp_absy(CPU *cpu)
{
	dcp_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_dcp_absx(CPU *cpu)
{
	dcp_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- ISC (INC+SBC, RMW) ---- */
static void
isc_core(CPU *cpu, uint16_t a)
{
	uint8_t t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	t++;
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, t);
	else
		sbc_bin(cpu, t);
	write_byte(cpu, a, t);
}
static void
op_isc_izx(CPU *cpu)
{
	isc_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_isc_zp(CPU *cpu)
{
	isc_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_isc_abs(CPU *cpu)
{
	isc_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_isc_izy(CPU *cpu)
{
	isc_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_isc_zpx(CPU *cpu)
{
	isc_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_isc_absy(CPU *cpu)
{
	isc_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_isc_absx(CPU *cpu)
{
	isc_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- Undocumented NOPs ---- */
static void
op_nop_imp(CPU *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->last_cycles = 2;
}
static void
op_nop_zp(CPU *cpu)
{
	(void)read_byte(cpu, addr_zp(cpu));
	cpu->last_cycles = 3;
}
static void
op_nop_zpx(CPU *cpu)
{
	(void)read_byte(cpu, addr_zpx(cpu));
	cpu->last_cycles = 4;
}
static void
op_nop_abs(CPU *cpu)
{
	(void)read_byte(cpu, addr_abs(cpu));
	cpu->last_cycles = 4;
}
static void
op_nop_absx(CPU *cpu)
{
	bool px = false;
	(void)read_byte(cpu, addr_absx(cpu, &px));
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
/* SKB: 2-byte NOP that reads+discards immediate operand */
static void
op_skb(CPU *cpu)
{
	read_byte(cpu, cpu->pc++); /* cycle 2: read and discard immediate */
	cpu->last_cycles = 2;
}

/* ---- JAM / KIL: freeze the CPU bus ---- */
static void
op_jam(CPU *cpu)
{
	uint8_t opcode = read_byte(cpu, cpu->pc - 1); /* already fetched */
	cpu->pc--; /* PC stays on the JAM opcode */
	cpu->halted = true;
	fprintf(stderr,
	    "\nJAM: CPU halted by opcode 0x%02X at PC=0x%04X. "
	    "RESET required.\n",
	    opcode,
	    (uint16_t)cpu->pc);
	cpu->last_cycles = 1;
}

/* ---- Unstable Opcodes ---- */

/* SHX: X AND (HighByte(Address) + 1) -> Address */
static void
op_shx(CPU *cpu)
{
	uint16_t base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t addr = base + cpu->y;
	uint8_t h = (base >> 8) + 1;
	uint8_t val = cpu->x & h;

	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */

	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (val << 8) | (addr & 0xFF);
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* SHY: Y AND (HighByte(Address) + 1) -> Address */
static void
op_shy(CPU *cpu)
{
	uint16_t base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t addr = base + cpu->x;
	uint8_t h = (base >> 8) + 1;
	uint8_t val = cpu->y & h;

	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */

	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (val << 8) | (addr & 0xFF);
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* AHX: A AND X AND (HighByte(Address) + 1) -> Address (Indirect Y) */
static void
op_ahx_izy(CPU *cpu)
{
	uint8_t zp = read_byte(cpu, cpu->pc++);
	uint8_t lo = read_byte(cpu, zp);
	uint8_t hi = read_byte(cpu, (zp + 1) & 0xFF);
	uint16_t base = (hi << 8) | lo;
	uint16_t addr = base + cpu->y;
	uint8_t h = (base >> 8) + 1;
	uint8_t val = cpu->a & cpu->x & h;

	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 5: dummy read of uncorrected address */

	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (val << 8) | (addr & 0xFF);
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 6;
}

/* AHX: A AND X AND (HighByte(Address) + 1) -> Address (Absolute Y) */
static void
op_ahx_absy(CPU *cpu)
{
	uint16_t base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t addr = base + cpu->y;
	uint8_t h = (base >> 8) + 1;
	uint8_t val = cpu->a & cpu->x & h;

	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */

	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (val << 8) | (addr & 0xFF);
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* TAS: Transfer A AND X to SP, then store S AND (HighByte(Address) + 1) -> Address */
static void
op_tas(CPU *cpu)
{
	uint16_t base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t addr = base + cpu->y;

	cpu->s = cpu->a & cpu->x;
	uint8_t h = (base >> 8) + 1;
	uint8_t val = cpu->s & h;

	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */

	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (val << 8) | (addr & 0xFF);
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* XAA (ANE): Transfer X to A AND immediate (unstable magic constant) */
static void
op_xaa(CPU *cpu)
{
	uint8_t val = read_byte(cpu, cpu->pc++);
	uint8_t magic =
	    0xEE; /* UNSTABLE: varies by chip/temp. 0xEE is the standard emulator approximation. */

	cpu->a = (cpu->a | magic) & cpu->x & val;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* LXA (LAX immediate / ATX / OAL): A, X := (A | magic) & immediate */
static void
op_lxa(CPU *cpu)
{
	uint8_t val = read_byte(cpu, cpu->pc++);
	uint8_t magic =
	    0xEE; /* UNSTABLE: varies by chip/temp. 0xEE is the standard emulator approximation. */

	cpu->a = cpu->x = (cpu->a | magic) & val;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* LAS: Transfer SP AND memory to A, X, and SP */
static void
op_las_unstable(CPU *cpu)
{
	bool px = false;
	uint8_t v = read_byte(cpu, addr_absy(cpu, &px)) & cpu->s;

	cpu->a = cpu->x = cpu->s = v;
	update_nz(cpu, v);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- ANC: AND #imm, copy bit 7 to Carry ---- */
static void
op_anc(CPU *cpu)
{
	cpu->a &= read_byte(cpu, cpu->pc++);
	update_nz(cpu, cpu->a);
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x80) != 0);
	cpu->last_cycles = 2;
}

/* ---- ALR: AND #imm, then LSR A ---- */
static void
op_alr(CPU *cpu)
{
	cpu->a &= read_byte(cpu, cpu->pc++);
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x01) != 0);
	cpu->a >>= 1;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* ---- ARR: AND #imm, then ROR A (with special C/V) ---- */
static void
op_arr(CPU *cpu)
{
	uint8_t m = read_byte(cpu, cpu->pc++);
	uint8_t value = cpu->a & m;
	uint8_t old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	uint8_t result = (value >> 1) | (old_c << 7);

	// Compute binary flags first
	set_flag(cpu, FLAG_CARRY, (result & 0x40) != 0);
	set_flag(cpu, FLAG_ZERO, result == 0);
	set_flag(cpu, FLAG_NEGATIVE, (result & 0x80) != 0);
	set_flag(cpu, FLAG_OVERFLOW, ((result >> 6) ^ (result >> 5)) & 1);

	if (cpu->p & FLAG_DECIMAL) {
		uint8_t adjusted = result;
		if (((value & 0x0F) + (value & 0x01)) > 5) {
			adjusted = (adjusted & 0xF0) |
			    ((adjusted + 0x06) & 0x0F);
		}
		if (((value & 0xF0) + (value & 0x10)) > 0x50) {
			adjusted += 0x60;
			set_flag(cpu, FLAG_CARRY, true);
		} else {
			set_flag(cpu, FLAG_CARRY, false);
		}
		cpu->a = adjusted;
	} else {
		cpu->a = result;
	}
	cpu->last_cycles = 2;
}

/* ---- SBX (AXS): X = (A & X) - #imm ---- */
static void
op_sbx(CPU *cpu)
{
	uint8_t m = read_byte(cpu, cpu->pc++);
	uint8_t ax = cpu->a & cpu->x;

	set_flag(cpu, FLAG_CARRY, ax >= m);
	cpu->x = ax - m;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}

/* ---- USBC: unofficial SBC #imm ---- */
static void
op_usbc(CPU *cpu)
{
	uint8_t m = read_byte(cpu, cpu->pc++);

	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 2;
}

/* ================================================================== */
/* Dispatch table                                                       */
/* ================================================================== */

static const opcode_fn dispatch[256] = {
	/* 0x00 */ [0x00] = op_brk,
	/* 0x01 */ [0x01] = op_ora_izx,
	/* 0x02 */ [0x02] = op_jam,
	/* 0x03 */ [0x03] = op_slo_izx,
	/* 0x04 */ [0x04] = op_nop_zp,
	/* 0x05 */ [0x05] = op_ora_zp,
	/* 0x06 */ [0x06] = op_asl_zp,
	/* 0x07 */ [0x07] = op_slo_zp,
	/* 0x08 */ [0x08] = op_php,
	/* 0x09 */ [0x09] = op_ora_imm,
	/* 0x0A */ [0x0A] = op_asl_acc,
	/* 0x0B */ [0x0B] = op_anc,
	/* 0x0C */ [0x0C] = op_nop_abs,
	/* 0x0D */ [0x0D] = op_ora_abs,
	/* 0x0E */ [0x0E] = op_asl_abs,
	/* 0x0F */ [0x0F] = op_slo_abs,
	/* 0x10 */ [0x10] = op_bpl,
	/* 0x11 */ [0x11] = op_ora_izy,
	/* 0x12 */ [0x12] = op_jam,
	/* 0x13 */ [0x13] = op_slo_izy,
	/* 0x14 */ [0x14] = op_nop_zpx,
	/* 0x15 */ [0x15] = op_ora_zpx,
	/* 0x16 */ [0x16] = op_asl_zpx,
	/* 0x17 */ [0x17] = op_slo_zpx,
	/* 0x18 */ [0x18] = op_clc,
	/* 0x19 */ [0x19] = op_ora_absy,
	/* 0x1A */ [0x1A] = op_nop_imp,
	/* 0x1B */ [0x1B] = op_slo_absy,
	/* 0x1C */ [0x1C] = op_nop_absx,
	/* 0x1D */ [0x1D] = op_ora_absx,
	/* 0x1E */ [0x1E] = op_asl_absx,
	/* 0x1F */ [0x1F] = op_slo_absx,
	/* 0x20 */ [0x20] = op_jsr,
	/* 0x21 */ [0x21] = op_and_izx,
	/* 0x22 */ [0x22] = op_jam,
	/* 0x23 */ [0x23] = op_rla_izx,
	/* 0x24 */ [0x24] = op_bit_zp,
	/* 0x25 */ [0x25] = op_and_zp,
	/* 0x26 */ [0x26] = op_rol_zp,
	/* 0x27 */ [0x27] = op_rla_zp,
	/* 0x28 */ [0x28] = op_plp,
	/* 0x29 */ [0x29] = op_and_imm,
	/* 0x2A */ [0x2A] = op_rol_acc,
	/* 0x2B */ [0x2B] = op_anc,
	/* 0x2C */ [0x2C] = op_bit_abs,
	/* 0x2D */ [0x2D] = op_and_abs,
	/* 0x2E */ [0x2E] = op_rol_abs,
	/* 0x2F */ [0x2F] = op_rla_abs,
	/* 0x30 */ [0x30] = op_bmi,
	/* 0x31 */ [0x31] = op_and_izy,
	/* 0x32 */ [0x32] = op_jam,
	/* 0x33 */ [0x33] = op_rla_izy,
	/* 0x34 */ [0x34] = op_nop_zpx,
	/* 0x35 */ [0x35] = op_and_zpx,
	/* 0x36 */ [0x36] = op_rol_zpx,
	/* 0x37 */ [0x37] = op_rla_zpx,
	/* 0x38 */ [0x38] = op_sec,
	/* 0x39 */ [0x39] = op_and_absy,
	/* 0x3A */ [0x3A] = op_nop_imp,
	/* 0x3B */ [0x3B] = op_rla_absy,
	/* 0x3C */ [0x3C] = op_nop_absx,
	/* 0x3D */ [0x3D] = op_and_absx,
	/* 0x3E */ [0x3E] = op_rol_absx,
	/* 0x3F */ [0x3F] = op_rla_absx,
	/* 0x40 */ [0x40] = op_rti,
	/* 0x41 */ [0x41] = op_eor_izx,
	/* 0x42 */ [0x42] = op_jam,
	/* 0x43 */ [0x43] = op_sre_izx,
	/* 0x44 */ [0x44] = op_nop_zp,
	/* 0x45 */ [0x45] = op_eor_zp,
	/* 0x46 */ [0x46] = op_lsr_zp,
	/* 0x47 */ [0x47] = op_sre_zp,
	/* 0x48 */ [0x48] = op_pha,
	/* 0x49 */ [0x49] = op_eor_imm,
	/* 0x4A */ [0x4A] = op_lsr_acc,
	/* 0x4B */ [0x4B] = op_alr,
	/* 0x4C */ [0x4C] = op_jmp_abs,
	/* 0x4D */ [0x4D] = op_eor_abs,
	/* 0x4E */ [0x4E] = op_lsr_abs,
	/* 0x4F */ [0x4F] = op_sre_abs,
	/* 0x50 */ [0x50] = op_bvc,
	/* 0x51 */ [0x51] = op_eor_izy,
	/* 0x52 */ [0x52] = op_jam,
	/* 0x53 */ [0x53] = op_sre_izy,
	/* 0x54 */ [0x54] = op_nop_zpx,
	/* 0x55 */ [0x55] = op_eor_zpx,
	/* 0x56 */ [0x56] = op_lsr_zpx,
	/* 0x57 */ [0x57] = op_sre_zpx,
	/* 0x58 */ [0x58] = op_cli,
	/* 0x59 */ [0x59] = op_eor_absy,
	/* 0x5A */ [0x5A] = op_nop_imp,
	/* 0x5B */ [0x5B] = op_sre_absy,
	/* 0x5C */ [0x5C] = op_nop_absx,
	/* 0x5D */ [0x5D] = op_eor_absx,
	/* 0x5E */ [0x5E] = op_lsr_absx,
	/* 0x5F */ [0x5F] = op_sre_absx,
	/* 0x60 */ [0x60] = op_rts,
	/* 0x61 */ [0x61] = op_adc_izx,
	/* 0x62 */ [0x62] = op_jam,
	/* 0x63 */ [0x63] = op_rra_izx,
	/* 0x64 */ [0x64] = op_nop_zp,
	/* 0x65 */ [0x65] = op_adc_zp,
	/* 0x66 */ [0x66] = op_ror_zp,
	/* 0x67 */ [0x67] = op_rra_zp,
	/* 0x68 */ [0x68] = op_pla,
	/* 0x69 */ [0x69] = op_adc_imm,
	/* 0x6A */ [0x6A] = op_ror_acc,
	/* 0x6B */ [0x6B] = op_arr,
	/* 0x6C */ [0x6C] = op_jmp_ind,
	/* 0x6D */ [0x6D] = op_adc_abs,
	/* 0x6E */ [0x6E] = op_ror_abs,
	/* 0x6F */ [0x6F] = op_rra_abs,
	/* 0x70 */ [0x70] = op_bvs,
	/* 0x71 */ [0x71] = op_adc_izy,
	/* 0x72 */ [0x72] = op_jam,
	/* 0x73 */ [0x73] = op_rra_izy,
	/* 0x74 */ [0x74] = op_nop_zpx,
	/* 0x75 */ [0x75] = op_adc_zpx,
	/* 0x76 */ [0x76] = op_ror_zpx,
	/* 0x77 */ [0x77] = op_rra_zpx,
	/* 0x78 */ [0x78] = op_sei,
	/* 0x79 */ [0x79] = op_adc_absy,
	/* 0x7A */ [0x7A] = op_nop_imp,
	/* 0x7B */ [0x7B] = op_rra_absy,
	/* 0x7C */ [0x7C] = op_nop_absx,
	/* 0x7D */ [0x7D] = op_adc_absx,
	/* 0x7E */ [0x7E] = op_ror_absx,
	/* 0x7F */ [0x7F] = op_rra_absx,
	/* 0x80 */ [0x80] = op_skb,
	/* 0x81 */ [0x81] = op_sta_izx,
	/* 0x82 */ [0x82] = op_skb,
	/* 0x83 */ [0x83] = op_sax_izx,
	/* 0x84 */ [0x84] = op_sty_zp,
	/* 0x85 */ [0x85] = op_sta_zp,
	/* 0x86 */ [0x86] = op_stx_zp,
	/* 0x87 */ [0x87] = op_sax_zp,
	/* 0x88 */ [0x88] = op_dey,
	/* 0x89 */ [0x89] = op_skb,
	/* 0x8A */ [0x8A] = op_txa,
	/* 0x8B */ [0x8B] = op_xaa,
	/* 0x8C */ [0x8C] = op_sty_abs,
	/* 0x8D */ [0x8D] = op_sta_abs,
	/* 0x8E */ [0x8E] = op_stx_abs,
	/* 0x8F */ [0x8F] = op_sax_abs,
	/* 0x90 */ [0x90] = op_bcc,
	/* 0x91 */ [0x91] = op_sta_izy,
	/* 0x92 */ [0x92] = op_jam,
	/* 0x93 */ [0x93] = op_ahx_izy,
	/* 0x94 */ [0x94] = op_sty_zpx,
	/* 0x95 */ [0x95] = op_sta_zpx,
	/* 0x96 */ [0x96] = op_stx_zpy,
	/* 0x97 */ [0x97] = op_sax_zpy,
	/* 0x98 */ [0x98] = op_tya,
	/* 0x99 */ [0x99] = op_sta_absy,
	/* 0x9A */ [0x9A] = op_txs,
	/* 0x9B */ [0x9B] = op_tas,
	/* 0x9C */ [0x9C] = op_shy,
	/* 0x9D */ [0x9D] = op_sta_absx,
	/* 0x9E */ [0x9E] = op_shx,
	/* 0x9F */ [0x9F] = op_ahx_absy,
	/* 0xA0 */ [0xA0] = op_ldy_imm,
	/* 0xA1 */ [0xA1] = op_lda_izx,
	/* 0xA2 */ [0xA2] = op_ldx_imm,
	/* 0xA3 */ [0xA3] = op_lax_izx,
	/* 0xA4 */ [0xA4] = op_ldy_zp,
	/* 0xA5 */ [0xA5] = op_lda_zp,
	/* 0xA6 */ [0xA6] = op_ldx_zp,
	/* 0xA7 */ [0xA7] = op_lax_zp,
	/* 0xA8 */ [0xA8] = op_tay,
	/* 0xA9 */ [0xA9] = op_lda_imm,
	/* 0xAA */ [0xAA] = op_tax,
	/* 0xAB */ [0xAB] = op_lxa,
	/* 0xAC */ [0xAC] = op_ldy_abs,
	/* 0xAD */ [0xAD] = op_lda_abs,
	/* 0xAE */ [0xAE] = op_ldx_abs,
	/* 0xAF */ [0xAF] = op_lax_abs,
	/* 0xB0 */ [0xB0] = op_bcs,
	/* 0xB1 */ [0xB1] = op_lda_izy,
	/* 0xB2 */ [0xB2] = op_jam,
	/* 0xB3 */ [0xB3] = op_lax_izy,
	/* 0xB4 */ [0xB4] = op_ldy_zpx,
	/* 0xB5 */ [0xB5] = op_lda_zpx,
	/* 0xB6 */ [0xB6] = op_ldx_zpy,
	/* 0xB7 */ [0xB7] = op_lax_zpy,
	/* 0xB8 */ [0xB8] = op_clv,
	/* 0xB9 */ [0xB9] = op_lda_absy,
	/* 0xBA */ [0xBA] = op_tsx,
	/* 0xBB */ [0xBB] = op_las_unstable,

	/* 0xBC */ [0xBC] = op_ldy_absx,
	/* 0xBD */ [0xBD] = op_lda_absx,
	/* 0xBE */ [0xBE] = op_ldx_absy,
	/* 0xBF */ [0xBF] = op_lax_absy,
	/* 0xC0 */ [0xC0] = op_cpy_imm,
	/* 0xC1 */ [0xC1] = op_cmp_izx,
	/* 0xC2 */ [0xC2] = op_skb,
	/* 0xC3 */ [0xC3] = op_dcp_izx,
	/* 0xC4 */ [0xC4] = op_cpy_zp,
	/* 0xC5 */ [0xC5] = op_cmp_zp,
	/* 0xC6 */ [0xC6] = op_dec_zp,
	/* 0xC7 */ [0xC7] = op_dcp_zp,
	/* 0xC8 */ [0xC8] = op_iny,
	/* 0xC9 */ [0xC9] = op_cmp_imm,
	/* 0xCA */ [0xCA] = op_dex,
	/* 0xCB */ [0xCB] = op_sbx,
	/* 0xCC */ [0xCC] = op_cpy_abs,
	/* 0xCD */ [0xCD] = op_cmp_abs,
	/* 0xCE */ [0xCE] = op_dec_abs,
	/* 0xCF */ [0xCF] = op_dcp_abs,
	/* 0xD0 */ [0xD0] = op_bne,
	/* 0xD1 */ [0xD1] = op_cmp_izy,
	/* 0xD2 */ [0xD2] = op_jam,
	/* 0xD3 */ [0xD3] = op_dcp_izy,
	/* 0xD4 */ [0xD4] = op_nop_zpx,
	/* 0xD5 */ [0xD5] = op_cmp_zpx,
	/* 0xD6 */ [0xD6] = op_dec_zpx,
	/* 0xD7 */ [0xD7] = op_dcp_zpx,
	/* 0xD8 */ [0xD8] = op_cld,
	/* 0xD9 */ [0xD9] = op_cmp_absy,
	/* 0xDA */ [0xDA] = op_nop_imp,
	/* 0xDB */ [0xDB] = op_dcp_absy,
	/* 0xDC */ [0xDC] = op_nop_absx,
	/* 0xDD */ [0xDD] = op_cmp_absx,
	/* 0xDE */ [0xDE] = op_dec_absx,
	/* 0xDF */ [0xDF] = op_dcp_absx,
	/* 0xE0 */ [0xE0] = op_cpx_imm,
	/* 0xE1 */ [0xE1] = op_sbc_izx,
	/* 0xE2 */ [0xE2] = op_skb,
	/* 0xE3 */ [0xE3] = op_isc_izx,
	/* 0xE4 */ [0xE4] = op_cpx_zp,
	/* 0xE5 */ [0xE5] = op_sbc_zp,
	/* 0xE6 */ [0xE6] = op_inc_zp,
	/* 0xE7 */ [0xE7] = op_isc_zp,
	/* 0xE8 */ [0xE8] = op_inx,
	/* 0xE9 */ [0xE9] = op_sbc_imm,
	/* 0xEA */ [0xEA] = op_nop,
	/* 0xEB */ [0xEB] = op_usbc,
	/* 0xEC */ [0xEC] = op_cpx_abs,
	/* 0xED */ [0xED] = op_sbc_abs,
	/* 0xEE */ [0xEE] = op_inc_abs,
	/* 0xEF */ [0xEF] = op_isc_abs,
	/* 0xF0 */ [0xF0] = op_beq,
	/* 0xF1 */ [0xF1] = op_sbc_izy,
	/* 0xF2 */ [0xF2] = op_jam,
	/* 0xF3 */ [0xF3] = op_isc_izy,
	/* 0xF4 */ [0xF4] = op_nop_zpx,
	/* 0xF5 */ [0xF5] = op_sbc_zpx,
	/* 0xF6 */ [0xF6] = op_inc_zpx,
	/* 0xF7 */ [0xF7] = op_isc_zpx,
	/* 0xF8 */ [0xF8] = op_sed,
	/* 0xF9 */ [0xF9] = op_sbc_absy,
	/* 0xFA */ [0xFA] = op_nop_imp,
	/* 0xFB */ [0xFB] = op_isc_absy,
	/* 0xFC */ [0xFC] = op_nop_absx,
	/* 0xFD */ [0xFD] = op_sbc_absx,
	/* 0xFE */ [0xFE] = op_inc_absx,
	/* 0xFF */ [0xFF] = op_isc_absx,
};

/* ================================================================== */
/* cpu_step                                                             */
/* ================================================================== */

uint8_t
cpu_step(CPU *cpu)
{
	/* If the CPU has been halted by a JAM opcode, keep the bus frozen. */
	if (cpu->halted)
		return 1;

	/* Record non-sequential transitions */
	if (cpu->prev_pc_valid) {
		int delta = (int)cpu->pc - (int)cpu->prev_pc;
		if (delta < 0 || delta > 3) {
			cpu->pc_trace[cpu->pc_trace_idx].from = cpu->prev_pc;
			cpu->pc_trace[cpu->pc_trace_idx].to = cpu->pc;
			cpu->pc_trace_idx = (cpu->pc_trace_idx + 1) % 24;
		}
	}
	cpu->prev_pc = cpu->pc;
	cpu->prev_pc_valid = true;

	/* 1. Check for hardware interrupts before instruction fetch.
	 *
	 * Real NMOS 6502 interrupt mechanism (per Visual6502 / NESdev):
	 *
	 * After the previous instruction completes, the CPU samples its
	 * interrupt lines.  If an interrupt is pending it performs the
	 * opcode fetch for the *next* instruction (cycle 1) but discards
	 * the byte and forces $00 (BRK) into the instruction register
	 * regardless of what was in memory.  The PC is NOT incremented
	 * during this fetch, so when the ISR eventually RTIs, execution
	 * resumes at exactly the preempted instruction's address.
	 *
	 * The "hijacking" term refers to a *different* edge case: an NMI
	 * that arrives while a BRK/IRQ sequence is already mid-flight
	 * (during the vector pull) takes over the vector — our 7-cycle
	 * handler is atomic at the step level so we don't model that
	 * sub-instruction race here.
	 *
	 * Either way: no peek at memory, no conditional pc++.
	 */
	if (cpu->nmi_pending) {
		cpu->nmi_pending = false;
		read_byte(cpu, cpu->pc); /* cycle 1: fetch discarded, PC held */
		push_word(cpu, cpu->pc);
		push_byte(cpu, (cpu->p & ~FLAG_BREAK) | FLAG_UNUSED);
		set_flag(cpu, FLAG_INTERRUPT, true);
		cpu->pc = read_word(cpu, 0xFFFA);
		return 7;
	}
	if (cpu->irq_pending && !(cpu->p & FLAG_INTERRUPT)) {
		cpu->irq_pending = false;
		read_byte(cpu, cpu->pc); /* cycle 1: fetch discarded, PC held */
		push_word(cpu, cpu->pc);
		push_byte(cpu, (cpu->p & ~FLAG_BREAK) | FLAG_UNUSED);
		set_flag(cpu, FLAG_INTERRUPT, true);
		cpu->pc = read_word(cpu, 0xFFFE);
		return 7;
	}

	/* 2. Fetch opcode */
	uint8_t opcode = read_byte(cpu, cpu->pc++);

	/* 3. Dispatch */
	if (dispatch[opcode] != NULL) {
		dispatch[opcode](cpu);
	} else {
		/* Unimplemented slot — treat as JAM */
		cpu->pc--;
		cpu->halted = true;
		fprintf(stderr,
		    "\nJAM: unimplemented opcode 0x%02X at PC=0x%04X. "
		    "RESET required.\n",
		    opcode,
		    (uint16_t)cpu->pc);
		cpu->last_cycles = 1;
	}

	uint8_t cycles = cpu->last_cycles;

	/* 4. DRAM refresh cycle stealing (optional) */
	static uint32_t dram_cycle_acc = 0;

	if (cpu->bus->opts.emulate_dram_refresh) {
		dram_cycle_acc += cycles * 4;
		while (dram_cycle_acc >= 61) {
			dram_cycle_acc -= 61;
			cycles++;
		}
	}

	return cycles;
}

/* ================================================================== */
/* IRQ / NMI                                                           */
/* ================================================================== */

void
cpu_irq(CPU *cpu, bool assert)
{
	cpu->irq_pending = assert;
}

void
cpu_nmi(CPU *cpu)
{
	cpu->nmi_pending = true;
}
