#include "cpu.h"
#include "port.h"

/* ------------------------------------------------------------------ */
/* Memory helpers                                                       */
/* ------------------------------------------------------------------ */

PORT_STATIC_INLINE uint8_t
read_byte(struct cpu *cpu, uint16_t addr)
{
	return (bus_read(cpu->bus, addr));
}

PORT_STATIC_INLINE void
write_byte(struct cpu *cpu, uint16_t addr, uint8_t val)
{
	bus_write(cpu->bus, addr, val);
}

PORT_STATIC_INLINE void
write_byte_dummy(struct cpu *cpu, uint16_t addr, uint8_t val)
{
	bus_write_ext(cpu->bus, addr, val, true);
}

PORT_STATIC_INLINE uint16_t
read_word(struct cpu *cpu, uint16_t addr)
{
	uint8_t lo = read_byte(cpu, addr);
	uint8_t hi = read_byte(cpu, addr + 1);

	return ((uint16_t)((hi << 8) | lo));
}

/* ------------------------------------------------------------------ */
/* Stack helpers                                                        */
/* ------------------------------------------------------------------ */

PORT_STATIC_INLINE void
push_byte(struct cpu *cpu, uint8_t val)
{
	write_byte(cpu, 0x0100 + cpu->s, val);
	cpu->s--;
}

PORT_STATIC_INLINE uint8_t
pull_byte(struct cpu *cpu)
{
	cpu->s++;
	return (read_byte(cpu, 0x0100 + cpu->s));
}

PORT_STATIC_INLINE void
push_word(struct cpu *cpu, uint16_t val)
{
	push_byte(cpu, (val >> 8) & 0xFF);
	push_byte(cpu, val & 0xFF);
}

PORT_STATIC_INLINE uint16_t
pull_word(struct cpu *cpu)
{
	uint8_t lo = pull_byte(cpu);
	uint8_t hi = pull_byte(cpu);

	return ((uint16_t)((hi << 8) | lo));
}

/* ------------------------------------------------------------------ */
/* Flag helpers                                                         */
/* ------------------------------------------------------------------ */

PORT_STATIC_INLINE void
set_flag(struct cpu *cpu, uint8_t flag, bool cond)
{
	if (cond)
		cpu->p |= flag;
	else
		cpu->p &= ~flag;
}

PORT_STATIC_INLINE void
update_nz(struct cpu *cpu, uint8_t val)
{
	set_flag(cpu, FLAG_ZERO, val == 0);
	set_flag(cpu, FLAG_NEGATIVE, (val & 0x80) != 0);
}

/* ------------------------------------------------------------------ */
/* Initialise / reset                                                   */
/* ------------------------------------------------------------------ */

void
cpu_init(struct cpu *cpu, struct bus *bus)
{
	cpu->bus = bus;
	/*
	 * Real Apple-1 had no power-on reset circuit — the struct cpu starts
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
	port_memset(cpu->pc_trace, 0, sizeof(cpu->pc_trace));
}

void
cpu_reset(struct cpu *cpu)
{
	uint8_t hi, lo;

	cpu->a = 0;
	cpu->x = 0;
	cpu->y = 0;
	cpu->s = 0xFD;
	cpu->p = FLAG_UNUSED | FLAG_INTERRUPT; /* 0x24 on boot */
	cpu->nmi_pending = false;
	cpu->irq_pending = false;
	cpu->halted = false; /* RESET always un-freezes the struct cpu */

	/* Read start address from the Reset Vector (RESET_VECTOR/RESET_VECTOR+1) */
	lo = bus_read(cpu->bus, RESET_VECTOR);
	hi = bus_read(cpu->bus, RESET_VECTOR + 1);

	cpu->pc = (uint16_t)((hi << 8) | lo);

	cpu->pc_trace_idx = 0;
	cpu->prev_pc = 0;
	cpu->prev_pc_valid = false;
	port_memset(cpu->pc_trace, 0, sizeof(cpu->pc_trace));
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
addr_imm(struct cpu *cpu)
{
	return (cpu->pc++);
}

static uint16_t
addr_zp(struct cpu *cpu)
{
	return (read_byte(cpu, cpu->pc++));
}

static uint16_t
addr_zpx(struct cpu *cpu)
{
	uint8_t base = read_byte(cpu, cpu->pc++);
	read_byte(cpu, base); /* cycle 3: dummy read of unindexed zp address */
	return ((base + cpu->x) & 0xFF);
}

static uint16_t
addr_zpy(struct cpu *cpu)
{
	uint8_t base = read_byte(cpu, cpu->pc++);
	read_byte(cpu, base); /* cycle 3: dummy read of unindexed zp address */
	return ((base + cpu->y) & 0xFF);
}

static uint16_t
addr_abs(struct cpu *cpu)
{
	uint16_t a = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	return (a);
}

static uint16_t
addr_absx(struct cpu *cpu, bool *px)
{
	uint16_t a, base;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	a = base + cpu->x;
	*px = (base & 0xFF00) != (a & 0xFF00);
	if (*px != 0) {
		/* cycle 4: dummy read of uncorrected address */
		read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	}
	return (a);
}

static uint16_t
addr_absx_always(struct cpu *cpu)
{
	uint16_t a, base;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	a = base + cpu->x;
	/* cycle 4: dummy read of uncorrected address */
	read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	return (a);
}

static uint16_t
addr_absy(struct cpu *cpu, bool *px)
{
	uint16_t a, base;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	a = base + cpu->y;
	*px = (base & 0xFF00) != (a & 0xFF00);
	if (*px != 0) {
		/* cycle 4: dummy read of uncorrected address */
		read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	}
	return (a);
}

static uint16_t
addr_absy_always(struct cpu *cpu)
{
	uint16_t a, base;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	a = base + cpu->y;
	/* cycle 4: dummy read of uncorrected address */
	read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	return (a);
}

static uint16_t
addr_ind(struct cpu *cpu)
{
	uint16_t ptr = read_word(cpu, cpu->pc);

	cpu->pc += 2;
	if ((ptr & 0x00FF) == 0x00FF) {
		/* 6502 page-boundary bug */
		uint8_t lo = read_byte(cpu, ptr);
		uint8_t hi = read_byte(cpu, ptr & 0xFF00);

		return ((uint16_t)((hi << 8) | lo));
	}
	return (read_word(cpu, ptr));
}

static uint16_t
addr_izx(struct cpu *cpu)
{
	uint8_t hi;
	uint8_t lo;
	uint8_t zp;
	uint8_t zp_raw;

	zp_raw = read_byte(cpu, cpu->pc++);
	read_byte(cpu, zp_raw); /* cycle 3: dummy read before X is added */
	zp = (zp_raw + cpu->x) & 0xFF;
	lo = read_byte(cpu, zp);
	hi = read_byte(cpu, (zp + 1) & 0xFF);

	return ((uint16_t)((hi << 8) | lo));
}

static uint16_t
addr_izy(struct cpu *cpu, bool *px)
{
	uint16_t a, base;
	uint8_t hi, lo, zp;

	zp = read_byte(cpu, cpu->pc++);
	lo = read_byte(cpu, zp);
	hi = read_byte(cpu, (zp + 1) & 0xFF);
	base = (uint16_t)((hi << 8) | lo);
	a = base + cpu->y;
	*px = (base & 0xFF00) != (a & 0xFF00);
	if (*px != 0) {
		/* cycle 5: dummy read of uncorrected address */
		read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	}
	return (a);
}

static uint16_t
addr_izy_always(struct cpu *cpu)
{
	uint16_t a, base;
	uint8_t hi, lo, zp;

	zp = read_byte(cpu, cpu->pc++);
	lo = read_byte(cpu, zp);
	hi = read_byte(cpu, (zp + 1) & 0xFF);
	base = (uint16_t)((hi << 8) | lo);
	a = base + cpu->y;
	/* cycle 5: dummy read of uncorrected address */
	read_byte(cpu, (base & 0xFF00) | (a & 0x00FF));
	return (a);
}

/* ------------------------------------------------------------------ */
/* Branch helper                                                        */
/* ------------------------------------------------------------------ */

static uint8_t
do_branch(struct cpu *cpu, bool cond)
{
	uint16_t intermediate, old, target;
	int8_t off;

	off = (int8_t)read_byte(cpu, cpu->pc++);
	if (cond != 0) {
		old = cpu->pc;
		read_byte(cpu, old); /* cycle 3: dummy read of PC+2 */
		target = (uint16_t)(old + off);
		if ((old & 0xFF00) != (target & 0xFF00)) {
			/* cycle 4: dummy read of intermediate address (uncorrected page) */
			intermediate = (old & 0xFF00) | (target & 0x00FF);
			read_byte(cpu, intermediate);
			cpu->pc = target;
			return (2);
		}
		cpu->pc = target;
		return (1);
	}
	return (0);
}

/* ------------------------------------------------------------------ */
/* ADC / SBC arithmetic                                                 */
/* ------------------------------------------------------------------ */

static void
adc_bcd(struct cpu *cpu, uint8_t m)
{
	uint8_t bin_result, carry, high, low, result;

	carry = (cpu->p & FLAG_CARRY) ? 1 : 0;
	bin_result = cpu->a + m + carry;
	set_flag(cpu, FLAG_ZERO, bin_result == 0);
	low = (cpu->a & 0x0F) + (m & 0x0F) + carry;
	high = (cpu->a >> 4) + (m >> 4);
	if (low > 9) {
		low -= 10;
		high++;
	}
	result = (uint8_t)((high << 4) | (low & 0x0F));
	set_flag(cpu, FLAG_NEGATIVE, (result & 0x80) != 0);
	set_flag(cpu,
	    FLAG_OVERFLOW,
	    (~(cpu->a ^ m) & (cpu->a ^ result) & 0x80) != 0);
	if (high > 9) {
		result -= 0xA0; /* 10 * 16 */
		set_flag(cpu, FLAG_CARRY, true);
	} else {
		set_flag(cpu, FLAG_CARRY, false);
	}
	cpu->a = result;
}

static void
sbc_bcd(struct cpu *cpu, uint8_t m)
{
	uint8_t bin_result, carry, result;
	int8_t carry_inv, high, low;

	carry = (cpu->p & FLAG_CARRY) ? 1 : 0;
	bin_result = cpu->a + (m ^ 0xFF) + carry;
	set_flag(cpu, FLAG_ZERO, bin_result == 0);
	carry_inv = (carry == 0) ? 1 : 0;
	low = (int8_t)(cpu->a & 0x0F) - (int8_t)(m & 0x0F) - carry_inv;
	high = (int8_t)(cpu->a >> 4) - (int8_t)(m >> 4);
	if (low < 0) {
		low += 10;
		high--;
	}
	result = (uint8_t)(((uint8_t)high << 4) | ((uint8_t)low & 0x0F));
	set_flag(cpu, FLAG_NEGATIVE, (result & 0x80) != 0);
	set_flag(cpu,
	    FLAG_OVERFLOW,
	    ((cpu->a ^ m) & (cpu->a ^ result) & 0x80) != 0);
	if (high < 0) {
		result += 0xA0; /* 10 * 16 */
		set_flag(cpu, FLAG_CARRY, false);
	} else {
		set_flag(cpu, FLAG_CARRY, true);
	}
	cpu->a = result;
}

static void
adc_bin(struct cpu *cpu, uint8_t m)
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
sbc_bin(struct cpu *cpu, uint8_t m)
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

typedef void (*opcode_fn)(struct cpu *cpu);

/* ================================================================== */
/* Individual opcode handlers                                           */
/* Each handler reads its operand(s), performs the operation, and      */
/* writes cpu->last_cycles with the cycle count for that instruction.  */
/* ================================================================== */

/* ---- ADC ---- */
static void
op_adc_imm(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_imm(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 2;
}
static void
op_adc_zp(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_zp(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 3;
}
static void
op_adc_zpx(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_zpx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_adc_abs(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_abs(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_adc_absx(struct cpu *cpu)
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
op_adc_absy(struct cpu *cpu)
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
op_adc_izx(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_izx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, m);
	else
		adc_bin(cpu, m);
	cpu->last_cycles = 6;
}
static void
op_adc_izy(struct cpu *cpu)
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
op_and_imm(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_and_zp(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_and_zpx(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_and_abs(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_and_absx(struct cpu *cpu)
{
	bool px = false;
	cpu->a &= read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_and_absy(struct cpu *cpu)
{
	bool px = false;
	cpu->a &= read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_and_izx(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_and_izy(struct cpu *cpu)
{
	bool px = false;
	cpu->a &= read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- ASL ---- */
static void
op_asl_acc(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x80) != 0);
	cpu->a <<= 1;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_asl_zp(struct cpu *cpu)
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
op_asl_zpx(struct cpu *cpu)
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
op_asl_abs(struct cpu *cpu)
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
op_asl_absx(struct cpu *cpu)
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
op_bpl(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_NEGATIVE));
}
static void
op_bmi(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_NEGATIVE));
}
static void
op_bvc(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_OVERFLOW));
}
static void
op_bvs(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_OVERFLOW));
}
static void
op_bcc(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_CARRY));
}
static void
op_bcs(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_CARRY));
}
static void
op_bne(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, !(cpu->p & FLAG_ZERO));
}
static void
op_beq(struct cpu *cpu)
{
	cpu->last_cycles = 2 + do_branch(cpu, (cpu->p & FLAG_ZERO));
}

/* ---- BIT ---- */
static void
op_bit_zp(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_ZERO, (cpu->a & t) == 0);
	set_flag(cpu, FLAG_NEGATIVE, (t & 0x80) != 0);
	set_flag(cpu, FLAG_OVERFLOW, (t & 0x40) != 0);
	cpu->last_cycles = 3;
}
static void
op_bit_abs(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_ZERO, (cpu->a & t) == 0);
	set_flag(cpu, FLAG_NEGATIVE, (t & 0x80) != 0);
	set_flag(cpu, FLAG_OVERFLOW, (t & 0x40) != 0);
	cpu->last_cycles = 4;
}

/* ---- BRK ---- */
static void
op_brk(struct cpu *cpu)
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
op_clc(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, false);
	cpu->last_cycles = 2;
}
static void
op_cli(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_INTERRUPT, false);
	cpu->last_cycles = 2;
}
static void
op_clv(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_OVERFLOW, false);
	cpu->last_cycles = 2;
}
static void
op_cld(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_DECIMAL, false);
	cpu->last_cycles = 2;
}

/* ---- Set flags ---- */
static void
op_sec(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, true);
	cpu->last_cycles = 2;
}
static void
op_sei(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_INTERRUPT, true);
	cpu->last_cycles = 2;
}
static void
op_sed(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_DECIMAL, true);
	cpu->last_cycles = 2;
}

/* ---- CMP ---- */
static void
op_cmp_imm(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_imm(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 2;
}
static void
op_cmp_zp(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 3;
}
static void
op_cmp_zpx(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zpx(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4;
}
static void
op_cmp_abs(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4;
}
static void
op_cmp_absx(struct cpu *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_absx(cpu, &px));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_cmp_absy(struct cpu *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_absy(cpu, &px));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_cmp_izx(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_izx(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 6;
}
static void
op_cmp_izy(struct cpu *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_izy(cpu, &px));
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	update_nz(cpu, (uint8_t)(cpu->a - t));
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- CPX ---- */
static void
op_cpx_imm(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_imm(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->x >= t);
	update_nz(cpu, (uint8_t)(cpu->x - t));
	cpu->last_cycles = 2;
}
static void
op_cpx_zp(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->x >= t);
	update_nz(cpu, (uint8_t)(cpu->x - t));
	cpu->last_cycles = 3;
}
static void
op_cpx_abs(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->x >= t);
	update_nz(cpu, (uint8_t)(cpu->x - t));
	cpu->last_cycles = 4;
}

/* ---- CPY ---- */
static void
op_cpy_imm(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_imm(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->y >= t);
	update_nz(cpu, (uint8_t)(cpu->y - t));
	cpu->last_cycles = 2;
}
static void
op_cpy_zp(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->y >= t);
	update_nz(cpu, (uint8_t)(cpu->y - t));
	cpu->last_cycles = 3;
}
static void
op_cpy_abs(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	set_flag(cpu, FLAG_CARRY, cpu->y >= t);
	update_nz(cpu, (uint8_t)(cpu->y - t));
	cpu->last_cycles = 4;
}

/* ---- DEC ---- */
static void
op_dec_zp(struct cpu *cpu)
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
op_dec_zpx(struct cpu *cpu)
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
op_dec_abs(struct cpu *cpu)
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
op_dec_absx(struct cpu *cpu)
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
op_dex(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x--;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_dey(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->y--;
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}

/* ---- EOR ---- */
static void
op_eor_imm(struct cpu *cpu)
{
	cpu->a ^= read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_eor_zp(struct cpu *cpu)
{
	cpu->a ^= read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_eor_zpx(struct cpu *cpu)
{
	cpu->a ^= read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_eor_abs(struct cpu *cpu)
{
	cpu->a ^= read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_eor_absx(struct cpu *cpu)
{
	bool px = false;
	cpu->a ^= read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_eor_absy(struct cpu *cpu)
{
	bool px = false;
	cpu->a ^= read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_eor_izx(struct cpu *cpu)
{
	cpu->a ^= read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_eor_izy(struct cpu *cpu)
{
	bool px = false;
	cpu->a ^= read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- INC ---- */
static void
op_inc_zp(struct cpu *cpu)
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
op_inc_zpx(struct cpu *cpu)
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
op_inc_abs(struct cpu *cpu)
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
op_inc_absx(struct cpu *cpu)
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
op_inx(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x++;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_iny(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->y++;
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}

/* ---- JMP ---- */
static void
op_jmp_abs(struct cpu *cpu)
{
	cpu->pc = addr_abs(cpu);
	cpu->last_cycles = 3;
}
static void
op_jmp_ind(struct cpu *cpu)
{
	cpu->pc = addr_ind(cpu);
	cpu->last_cycles = 5;
}

/* ---- JSR / RTS / RTI ---- */
static void
op_jsr(struct cpu *cpu)
{
	uint8_t hi;
	uint8_t lo;

	lo = read_byte(cpu, cpu->pc++);	 /* cycle 2: addr low */
	read_byte(cpu, 0x0100 + cpu->s); /* cycle 3: dummy stack read */
	push_word(cpu,
	    cpu->pc); /* cycles 4-5: push return addr (PC-1 already advanced) */
	hi = read_byte(cpu, cpu->pc); /* cycle 6: addr high */
	cpu->pc = (uint16_t)((hi << 8) | lo);
	cpu->last_cycles = 6;
}
static void
op_rts(struct cpu *cpu)
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
op_rti(struct cpu *cpu)
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
op_lda_imm(struct cpu *cpu)
{
	cpu->a = read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_lda_zp(struct cpu *cpu)
{
	cpu->a = read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_lda_zpx(struct cpu *cpu)
{
	cpu->a = read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_lda_abs(struct cpu *cpu)
{
	cpu->a = read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_lda_absx(struct cpu *cpu)
{
	bool px = false;
	cpu->a = read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_lda_absy(struct cpu *cpu)
{
	bool px = false;
	cpu->a = read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_lda_izx(struct cpu *cpu)
{
	cpu->a = read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_lda_izy(struct cpu *cpu)
{
	bool px = false;
	cpu->a = read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- LDX ---- */
static void
op_ldx_imm(struct cpu *cpu)
{
	cpu->x = read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_ldx_zp(struct cpu *cpu)
{
	cpu->x = read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 3;
}
static void
op_ldx_zpy(struct cpu *cpu)
{
	cpu->x = read_byte(cpu, addr_zpy(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 4;
}
static void
op_ldx_abs(struct cpu *cpu)
{
	cpu->x = read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 4;
}
static void
op_ldx_absy(struct cpu *cpu)
{
	bool px = false;
	cpu->x = read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- LDY ---- */
static void
op_ldy_imm(struct cpu *cpu)
{
	cpu->y = read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}
static void
op_ldy_zp(struct cpu *cpu)
{
	cpu->y = read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 3;
}
static void
op_ldy_zpx(struct cpu *cpu)
{
	cpu->y = read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 4;
}
static void
op_ldy_abs(struct cpu *cpu)
{
	cpu->y = read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 4;
}
static void
op_ldy_absx(struct cpu *cpu)
{
	bool px = false;
	cpu->y = read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- LSR ---- */
static void
op_lsr_acc(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x01) != 0);
	cpu->a >>= 1;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_lsr_zp(struct cpu *cpu)
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
op_lsr_zpx(struct cpu *cpu)
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
op_lsr_abs(struct cpu *cpu)
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
op_lsr_absx(struct cpu *cpu)
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
op_nop(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->last_cycles = 2;
}

/* ---- ORA ---- */
static void
op_ora_imm(struct cpu *cpu)
{
	cpu->a |= read_byte(cpu, addr_imm(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_ora_zp(struct cpu *cpu)
{
	cpu->a |= read_byte(cpu, addr_zp(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 3;
}
static void
op_ora_zpx(struct cpu *cpu)
{
	cpu->a |= read_byte(cpu, addr_zpx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_ora_abs(struct cpu *cpu)
{
	cpu->a |= read_byte(cpu, addr_abs(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_ora_absx(struct cpu *cpu)
{
	bool px = false;
	cpu->a |= read_byte(cpu, addr_absx(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_ora_absy(struct cpu *cpu)
{
	bool px = false;
	cpu->a |= read_byte(cpu, addr_absy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
static void
op_ora_izx(struct cpu *cpu)
{
	cpu->a |= read_byte(cpu, addr_izx(cpu));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 6;
}
static void
op_ora_izy(struct cpu *cpu)
{
	bool px = false;
	cpu->a |= read_byte(cpu, addr_izy(cpu, &px));
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}

/* ---- PHA / PHP / PLA / PLP ---- */
static void
op_pha(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	push_byte(cpu, cpu->a);	 /* cycle 3 */
	cpu->last_cycles = 3;
}
static void
op_php(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	push_byte(cpu, cpu->p | FLAG_BREAK | FLAG_UNUSED); /* cycle 3 */
	cpu->last_cycles = 3;
}
static void
op_pla(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	read_byte(cpu,
	    0x0100 + cpu->s);	 /* cycle 3: dummy read before pre-increment */
	cpu->a = pull_byte(cpu); /* cycle 4 */
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 4;
}
static void
op_plp(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read of PC+1 */
	read_byte(cpu,
	    0x0100 + cpu->s); /* cycle 3: dummy read before pre-increment */
	cpu->p = (pull_byte(cpu) & ~FLAG_BREAK) | FLAG_UNUSED; /* cycle 4 */
	cpu->last_cycles = 4;
}

/* ---- ROL ---- */
static void
op_rol_acc(struct cpu *cpu)
{
	uint8_t old_c;

	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x80) != 0);
	cpu->a = (uint8_t)((cpu->a << 1) | old_c);
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_rol_zp(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_zp(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (uint8_t)((t << 1) | old_c);
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 5;
}
static void
op_rol_zpx(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_zpx(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (uint8_t)((t << 1) | old_c);
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_rol_abs(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_abs(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (uint8_t)((t << 1) | old_c);
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_rol_absx(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_absx_always(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (uint8_t)((t << 1) | old_c);
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 7;
}

/* ---- ROR ---- */
static void
op_ror_acc(struct cpu *cpu)
{
	uint8_t old_c;

	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x01) != 0);
	cpu->a = (cpu->a >> 1) | old_c;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_ror_zp(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_zp(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 5;
}
static void
op_ror_zpx(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_zpx(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_ror_abs(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_abs(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_ror_absx(struct cpu *cpu)
{
	uint16_t a;
	uint8_t old_c, t;

	a = addr_absx_always(cpu);
	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t); /* NMOS dummy write of unmodified value */
	old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	write_byte(cpu, a, t);
	update_nz(cpu, t);
	cpu->last_cycles = 7;
}

/* ---- SBC ---- */
static void
op_sbc_imm(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_imm(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 2;
}
static void
op_sbc_zp(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_zp(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 3;
}
static void
op_sbc_zpx(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_zpx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_sbc_abs(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_abs(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 4;
}
static void
op_sbc_absx(struct cpu *cpu)
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
op_sbc_absy(struct cpu *cpu)
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
op_sbc_izx(struct cpu *cpu)
{
	uint8_t m = read_byte(cpu, addr_izx(cpu));
	if (cpu->p & FLAG_DECIMAL)
		sbc_bcd(cpu, m);
	else
		sbc_bin(cpu, m);
	cpu->last_cycles = 6;
}
static void
op_sbc_izy(struct cpu *cpu)
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
op_sta_zp(struct cpu *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->a);
	cpu->last_cycles = 3;
}
static void
op_sta_zpx(struct cpu *cpu)
{
	write_byte(cpu, addr_zpx(cpu), cpu->a);
	cpu->last_cycles = 4;
}
static void
op_sta_abs(struct cpu *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->a);
	cpu->last_cycles = 4;
}
static void
op_sta_absx(struct cpu *cpu)
{
	uint16_t a;
	bool px;

	px = false;
	a = addr_absx(cpu, &px);
	if (px == 0) {
		read_byte(cpu, a);
	}
	write_byte(cpu, a, cpu->a);
	cpu->last_cycles = 5;
}
static void
op_sta_absy(struct cpu *cpu)
{
	uint16_t a;
	bool px;

	px = false;
	a = addr_absy(cpu, &px);
	if (px == 0) {
		read_byte(cpu, a);
	}
	write_byte(cpu, a, cpu->a);
	cpu->last_cycles = 5;
}
static void
op_sta_izx(struct cpu *cpu)
{
	write_byte(cpu, addr_izx(cpu), cpu->a);
	cpu->last_cycles = 6;
}
static void
op_sta_izy(struct cpu *cpu)
{
	uint16_t a;
	bool px;

	px = false;
	a = addr_izy(cpu, &px);
	if (px == 0) {
		read_byte(cpu, a);
	}
	write_byte(cpu, a, cpu->a);
	cpu->last_cycles = 6;
}

/* ---- STX ---- */
static void
op_stx_zp(struct cpu *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->x);
	cpu->last_cycles = 3;
}
static void
op_stx_zpy(struct cpu *cpu)
{
	write_byte(cpu, addr_zpy(cpu), cpu->x);
	cpu->last_cycles = 4;
}
static void
op_stx_abs(struct cpu *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->x);
	cpu->last_cycles = 4;
}

/* ---- STY ---- */
static void
op_sty_zp(struct cpu *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->y);
	cpu->last_cycles = 3;
}
static void
op_sty_zpx(struct cpu *cpu)
{
	write_byte(cpu, addr_zpx(cpu), cpu->y);
	cpu->last_cycles = 4;
}
static void
op_sty_abs(struct cpu *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->y);
	cpu->last_cycles = 4;
}

/* ---- Register transfers ---- */
static void
op_tax(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x = cpu->a;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_tay(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->y = cpu->a;
	update_nz(cpu, cpu->y);
	cpu->last_cycles = 2;
}
static void
op_tsx(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->x = cpu->s;
	update_nz(cpu, cpu->x);
	cpu->last_cycles = 2;
}
static void
op_txa(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->a = cpu->x;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}
static void
op_txs(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->s = cpu->x;
	cpu->last_cycles = 2;
} /* no flags */
static void
op_tya(struct cpu *cpu)
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
op_lax_izx(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_izx(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 6;
}
static void
op_lax_zp(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zp(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 3;
}
static void
op_lax_abs(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_abs(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 4;
}
static void
op_lax_izy(struct cpu *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_izy(cpu, &px));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 5 + (px ? 1 : 0);
}
static void
op_lax_zpy(struct cpu *cpu)
{
	uint8_t t = read_byte(cpu, addr_zpy(cpu));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 4;
}
static void
op_lax_absy(struct cpu *cpu)
{
	bool px = false;
	uint8_t t = read_byte(cpu, addr_absy(cpu, &px));
	cpu->a = cpu->x = t;
	update_nz(cpu, t);
	cpu->last_cycles = 4 + (px ? 1 : 0);
}

/* ---- SAX ---- */
static void
op_sax_izx(struct cpu *cpu)
{
	write_byte(cpu, addr_izx(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 6;
}
static void
op_sax_zp(struct cpu *cpu)
{
	write_byte(cpu, addr_zp(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 3;
}
static void
op_sax_abs(struct cpu *cpu)
{
	write_byte(cpu, addr_abs(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 4;
}
static void
op_sax_zpy(struct cpu *cpu)
{
	write_byte(cpu, addr_zpy(cpu), cpu->a & cpu->x);
	cpu->last_cycles = 4;
}

/* ---- SLO (ASL+ORA, RMW) ---- */
static void
op_slo_izx(struct cpu *cpu)
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
op_slo_zp(struct cpu *cpu)
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
op_slo_abs(struct cpu *cpu)
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
op_slo_izy(struct cpu *cpu)
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
op_slo_zpx(struct cpu *cpu)
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
op_slo_absy(struct cpu *cpu)
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
op_slo_absx(struct cpu *cpu)
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
rla_core(struct cpu *cpu, uint16_t a)
{
	uint8_t old_c, t;

	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x80) != 0);
	t = (uint8_t)((t << 1) | old_c);
	cpu->a &= t;
	update_nz(cpu, cpu->a);
	write_byte(cpu, a, t);
}
static void
op_rla_izx(struct cpu *cpu)
{
	rla_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_rla_zp(struct cpu *cpu)
{
	rla_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_rla_abs(struct cpu *cpu)
{
	rla_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_rla_izy(struct cpu *cpu)
{
	rla_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_rla_zpx(struct cpu *cpu)
{
	rla_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_rla_absy(struct cpu *cpu)
{
	rla_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_rla_absx(struct cpu *cpu)
{
	rla_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- SRE (LSR+EOR, RMW) ---- */
static void
sre_core(struct cpu *cpu, uint16_t a)
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
op_sre_izx(struct cpu *cpu)
{
	sre_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_sre_zp(struct cpu *cpu)
{
	sre_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_sre_abs(struct cpu *cpu)
{
	sre_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_sre_izy(struct cpu *cpu)
{
	sre_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_sre_zpx(struct cpu *cpu)
{
	sre_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_sre_absy(struct cpu *cpu)
{
	sre_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_sre_absx(struct cpu *cpu)
{
	sre_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- RRA (ROR+ADC, RMW) ---- */
static void
rra_core(struct cpu *cpu, uint16_t a)
{
	uint8_t old_c, t;

	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	old_c = (cpu->p & FLAG_CARRY) ? 0x80 : 0;
	set_flag(cpu, FLAG_CARRY, (t & 0x01) != 0);
	t = (t >> 1) | old_c;
	if (cpu->p & FLAG_DECIMAL)
		adc_bcd(cpu, t);
	else
		adc_bin(cpu, t);
	write_byte(cpu, a, t);
}
static void
op_rra_izx(struct cpu *cpu)
{
	rra_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_rra_zp(struct cpu *cpu)
{
	rra_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_rra_abs(struct cpu *cpu)
{
	rra_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_rra_izy(struct cpu *cpu)
{
	rra_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_rra_zpx(struct cpu *cpu)
{
	rra_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_rra_absy(struct cpu *cpu)
{
	rra_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_rra_absx(struct cpu *cpu)
{
	rra_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- DCP (DEC+CMP, RMW) ---- */
static void
dcp_core(struct cpu *cpu, uint16_t a)
{
	uint8_t t;
	int diff;

	t = read_byte(cpu, a);
	write_byte_dummy(cpu, a, t);
	t--;
	diff = cpu->a - t;
	set_flag(cpu, FLAG_CARRY, cpu->a >= t);
	set_flag(cpu, FLAG_ZERO, (diff & 0xFF) == 0);
	set_flag(cpu, FLAG_NEGATIVE, (diff & 0x80) != 0);
	write_byte(cpu, a, t);
}
static void
op_dcp_izx(struct cpu *cpu)
{
	dcp_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_dcp_zp(struct cpu *cpu)
{
	dcp_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_dcp_abs(struct cpu *cpu)
{
	dcp_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_dcp_izy(struct cpu *cpu)
{
	dcp_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_dcp_zpx(struct cpu *cpu)
{
	dcp_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_dcp_absy(struct cpu *cpu)
{
	dcp_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_dcp_absx(struct cpu *cpu)
{
	dcp_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- ISC (INC+SBC, RMW) ---- */
static void
isc_core(struct cpu *cpu, uint16_t a)
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
op_isc_izx(struct cpu *cpu)
{
	isc_core(cpu, addr_izx(cpu));
	cpu->last_cycles = 8;
}
static void
op_isc_zp(struct cpu *cpu)
{
	isc_core(cpu, addr_zp(cpu));
	cpu->last_cycles = 5;
}
static void
op_isc_abs(struct cpu *cpu)
{
	isc_core(cpu, addr_abs(cpu));
	cpu->last_cycles = 6;
}
static void
op_isc_izy(struct cpu *cpu)
{
	isc_core(cpu, addr_izy_always(cpu));
	cpu->last_cycles = 8;
}
static void
op_isc_zpx(struct cpu *cpu)
{
	isc_core(cpu, addr_zpx(cpu));
	cpu->last_cycles = 6;
}
static void
op_isc_absy(struct cpu *cpu)
{
	isc_core(cpu, addr_absy_always(cpu));
	cpu->last_cycles = 7;
}
static void
op_isc_absx(struct cpu *cpu)
{
	isc_core(cpu, addr_absx_always(cpu));
	cpu->last_cycles = 7;
}

/* ---- Undocumented NOPs ---- */
static void
op_nop_imp(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc); /* cycle 2: dummy read */
	cpu->last_cycles = 2;
}
static void
op_nop_zp(struct cpu *cpu)
{
	(void)read_byte(cpu, addr_zp(cpu));
	cpu->last_cycles = 3;
}
static void
op_nop_zpx(struct cpu *cpu)
{
	(void)read_byte(cpu, addr_zpx(cpu));
	cpu->last_cycles = 4;
}
static void
op_nop_abs(struct cpu *cpu)
{
	(void)read_byte(cpu, addr_abs(cpu));
	cpu->last_cycles = 4;
}
static void
op_nop_absx(struct cpu *cpu)
{
	bool px = false;
	(void)read_byte(cpu, addr_absx(cpu, &px));
	cpu->last_cycles = 4 + (px ? 1 : 0);
}
/* SKB: 2-byte NOP that reads+discards immediate operand */
static void
op_skb(struct cpu *cpu)
{
	read_byte(cpu, cpu->pc++); /* cycle 2: read and discard immediate */
	cpu->last_cycles = 2;
}

/* ---- JAM / KIL: freeze the struct cpu bus ---- */
static void
op_jam(struct cpu *cpu)
{
	uint8_t opcode = read_byte(cpu, cpu->pc - 1); /* already fetched */
	cpu->pc--; /* PC stays on the JAM opcode */
	cpu->halted = true;
	{
		char msg[128];
		port_snprintf(msg,
		    sizeof(msg),
		    "JAM: halted by opcode 0x%02X at PC=0x%04X. "
		    "RESET required.",
		    opcode,
		    (uint16_t)cpu->pc);
		BUS_LOG(cpu->bus, BUS_LOG_WARN, msg);
	}
	cpu->last_cycles = 1;
}

/* ---- Unstable Opcodes ---- */

/* SHX: X AND (HighByte(Address) + 1) -> Address */
static void
op_shx(struct cpu *cpu)
{
	uint16_t addr, base;
	uint8_t h, val;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	addr = base + cpu->y;
	h = (base >> 8) + 1;
	val = cpu->x & h;
	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */
	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (uint16_t)((val << 8) | (addr & 0xFF));
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* SHY: Y AND (HighByte(Address) + 1) -> Address */
static void
op_shy(struct cpu *cpu)
{
	uint16_t addr, base;
	uint8_t h, val;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	addr = base + cpu->x;
	h = (base >> 8) + 1;
	val = cpu->y & h;
	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */
	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (uint16_t)((val << 8) | (addr & 0xFF));
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* AHX: A AND X AND (HighByte(Address) + 1) -> Address (Indirect Y) */
static void
op_ahx_izy(struct cpu *cpu)
{
	uint16_t addr, base;
	uint8_t h, hi, lo, val, zp;

	zp = read_byte(cpu, cpu->pc++);
	lo = read_byte(cpu, zp);
	hi = read_byte(cpu, (zp + 1) & 0xFF);
	base = (uint16_t)((hi << 8) | lo);
	addr = base + cpu->y;
	h = (base >> 8) + 1;
	val = cpu->a & cpu->x & h;
	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 5: dummy read of uncorrected address */
	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (uint16_t)((val << 8) | (addr & 0xFF));
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 6;
}

/* AHX: A AND X AND (HighByte(Address) + 1) -> Address (Absolute Y) */
static void
op_ahx_absy(struct cpu *cpu)
{
	uint16_t addr, base;
	uint8_t h, val;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	addr = base + cpu->y;
	h = (base >> 8) + 1;
	val = cpu->a & cpu->x & h;
	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */
	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (uint16_t)((val << 8) | (addr & 0xFF));
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* TAS: Transfer A AND X to SP, then store S AND (HighByte(Address) + 1) -> Address */
static void
op_tas(struct cpu *cpu)
{
	uint16_t addr, base;
	uint8_t h, val;

	base = read_word(cpu, cpu->pc);
	cpu->pc += 2;
	addr = base + cpu->y;
	cpu->s = cpu->a & cpu->x;
	h = (base >> 8) + 1;
	val = cpu->s & h;
	read_byte(cpu,
	    (base & 0xFF00) |
		(addr & 0xFF)); /* cycle 4: dummy read of uncorrected address */
	if ((base & 0xFF00) != (addr & 0xFF00)) {
		addr = (uint16_t)((val << 8) | (addr & 0xFF));
	}
	write_byte(cpu, addr, val);
	cpu->last_cycles = 5;
}

/* XAA (ANE): Transfer X to A AND immediate (unstable magic constant) */
static void
op_xaa(struct cpu *cpu)
{
	uint8_t magic, val;

	val = read_byte(cpu, cpu->pc++);
	magic =
	    0xEE; /* UNSTABLE: varies by chip/temp. 0xEE is the standard emulator approximation. */
	cpu->a = (cpu->a | magic) & cpu->x & val;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* LXA (LAX immediate / ATX / OAL): A, X := (A | magic) & immediate */
static void
op_lxa(struct cpu *cpu)
{
	uint8_t magic, val;

	val = read_byte(cpu, cpu->pc++);
	magic =
	    0xEE; /* UNSTABLE: varies by chip/temp. 0xEE is the standard emulator approximation. */
	cpu->a = cpu->x = (cpu->a | magic) & val;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* LAS: Transfer SP AND memory to A, X, and SP */
static void
op_las_unstable(struct cpu *cpu)
{
	uint8_t v;
	bool px;

	px = false;
	v = read_byte(cpu, addr_absy(cpu, &px)) & cpu->s;
	cpu->a = cpu->x = cpu->s = v;
	update_nz(cpu, v);
	cpu->last_cycles = 4 + (px != 0 ? 1 : 0);
}

/* ---- ANC: AND #imm, copy bit 7 to Carry ---- */
static void
op_anc(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, cpu->pc++);
	update_nz(cpu, cpu->a);
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x80) != 0);
	cpu->last_cycles = 2;
}

/* ---- ALR: AND #imm, then LSR A ---- */
static void
op_alr(struct cpu *cpu)
{
	cpu->a &= read_byte(cpu, cpu->pc++);
	set_flag(cpu, FLAG_CARRY, (cpu->a & 0x01) != 0);
	cpu->a >>= 1;
	update_nz(cpu, cpu->a);
	cpu->last_cycles = 2;
}

/* ---- ARR: AND #imm, then ROR A (with special C/V) ---- */
static void
op_arr(struct cpu *cpu)
{
	uint8_t adjusted, m, old_c, result, value;

	m = read_byte(cpu, cpu->pc++);
	value = cpu->a & m;
	old_c = (cpu->p & FLAG_CARRY) ? 1 : 0;
	result = (uint8_t)((value >> 1) | (old_c << 7));
	/* Compute binary flags first */
	set_flag(cpu, FLAG_CARRY, (result & 0x40) != 0);
	set_flag(cpu, FLAG_ZERO, result == 0);
	set_flag(cpu, FLAG_NEGATIVE, (result & 0x80) != 0);
	set_flag(cpu, FLAG_OVERFLOW, ((result >> 6) ^ (result >> 5)) & 1);
	if (cpu->p & FLAG_DECIMAL) {
		adjusted = result;
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
op_sbx(struct cpu *cpu)
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
op_usbc(struct cpu *cpu)
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
	/* 0x00 */ op_brk,
	/* 0x01 */ op_ora_izx,
	/* 0x02 */ op_jam,
	/* 0x03 */ op_slo_izx,
	/* 0x04 */ op_nop_zp,
	/* 0x05 */ op_ora_zp,
	/* 0x06 */ op_asl_zp,
	/* 0x07 */ op_slo_zp,
	/* 0x08 */ op_php,
	/* 0x09 */ op_ora_imm,
	/* 0x0A */ op_asl_acc,
	/* 0x0B */ op_anc,
	/* 0x0C */ op_nop_abs,
	/* 0x0D */ op_ora_abs,
	/* 0x0E */ op_asl_abs,
	/* 0x0F */ op_slo_abs,
	/* 0x10 */ op_bpl,
	/* 0x11 */ op_ora_izy,
	/* 0x12 */ op_jam,
	/* 0x13 */ op_slo_izy,
	/* 0x14 */ op_nop_zpx,
	/* 0x15 */ op_ora_zpx,
	/* 0x16 */ op_asl_zpx,
	/* 0x17 */ op_slo_zpx,
	/* 0x18 */ op_clc,
	/* 0x19 */ op_ora_absy,
	/* 0x1A */ op_nop_imp,
	/* 0x1B */ op_slo_absy,
	/* 0x1C */ op_nop_absx,
	/* 0x1D */ op_ora_absx,
	/* 0x1E */ op_asl_absx,
	/* 0x1F */ op_slo_absx,
	/* 0x20 */ op_jsr,
	/* 0x21 */ op_and_izx,
	/* 0x22 */ op_jam,
	/* 0x23 */ op_rla_izx,
	/* 0x24 */ op_bit_zp,
	/* 0x25 */ op_and_zp,
	/* 0x26 */ op_rol_zp,
	/* 0x27 */ op_rla_zp,
	/* 0x28 */ op_plp,
	/* 0x29 */ op_and_imm,
	/* 0x2A */ op_rol_acc,
	/* 0x2B */ op_anc,
	/* 0x2C */ op_bit_abs,
	/* 0x2D */ op_and_abs,
	/* 0x2E */ op_rol_abs,
	/* 0x2F */ op_rla_abs,
	/* 0x30 */ op_bmi,
	/* 0x31 */ op_and_izy,
	/* 0x32 */ op_jam,
	/* 0x33 */ op_rla_izy,
	/* 0x34 */ op_nop_zpx,
	/* 0x35 */ op_and_zpx,
	/* 0x36 */ op_rol_zpx,
	/* 0x37 */ op_rla_zpx,
	/* 0x38 */ op_sec,
	/* 0x39 */ op_and_absy,
	/* 0x3A */ op_nop_imp,
	/* 0x3B */ op_rla_absy,
	/* 0x3C */ op_nop_absx,
	/* 0x3D */ op_and_absx,
	/* 0x3E */ op_rol_absx,
	/* 0x3F */ op_rla_absx,
	/* 0x40 */ op_rti,
	/* 0x41 */ op_eor_izx,
	/* 0x42 */ op_jam,
	/* 0x43 */ op_sre_izx,
	/* 0x44 */ op_nop_zp,
	/* 0x45 */ op_eor_zp,
	/* 0x46 */ op_lsr_zp,
	/* 0x47 */ op_sre_zp,
	/* 0x48 */ op_pha,
	/* 0x49 */ op_eor_imm,
	/* 0x4A */ op_lsr_acc,
	/* 0x4B */ op_alr,
	/* 0x4C */ op_jmp_abs,
	/* 0x4D */ op_eor_abs,
	/* 0x4E */ op_lsr_abs,
	/* 0x4F */ op_sre_abs,
	/* 0x50 */ op_bvc,
	/* 0x51 */ op_eor_izy,
	/* 0x52 */ op_jam,
	/* 0x53 */ op_sre_izy,
	/* 0x54 */ op_nop_zpx,
	/* 0x55 */ op_eor_zpx,
	/* 0x56 */ op_lsr_zpx,
	/* 0x57 */ op_sre_zpx,
	/* 0x58 */ op_cli,
	/* 0x59 */ op_eor_absy,
	/* 0x5A */ op_nop_imp,
	/* 0x5B */ op_sre_absy,
	/* 0x5C */ op_nop_absx,
	/* 0x5D */ op_eor_absx,
	/* 0x5E */ op_lsr_absx,
	/* 0x5F */ op_sre_absx,
	/* 0x60 */ op_rts,
	/* 0x61 */ op_adc_izx,
	/* 0x62 */ op_jam,
	/* 0x63 */ op_rra_izx,
	/* 0x64 */ op_nop_zp,
	/* 0x65 */ op_adc_zp,
	/* 0x66 */ op_ror_zp,
	/* 0x67 */ op_rra_zp,
	/* 0x68 */ op_pla,
	/* 0x69 */ op_adc_imm,
	/* 0x6A */ op_ror_acc,
	/* 0x6B */ op_arr,
	/* 0x6C */ op_jmp_ind,
	/* 0x6D */ op_adc_abs,
	/* 0x6E */ op_ror_abs,
	/* 0x6F */ op_rra_abs,
	/* 0x70 */ op_bvs,
	/* 0x71 */ op_adc_izy,
	/* 0x72 */ op_jam,
	/* 0x73 */ op_rra_izy,
	/* 0x74 */ op_nop_zpx,
	/* 0x75 */ op_adc_zpx,
	/* 0x76 */ op_ror_zpx,
	/* 0x77 */ op_rra_zpx,
	/* 0x78 */ op_sei,
	/* 0x79 */ op_adc_absy,
	/* 0x7A */ op_nop_imp,
	/* 0x7B */ op_rra_absy,
	/* 0x7C */ op_nop_absx,
	/* 0x7D */ op_adc_absx,
	/* 0x7E */ op_ror_absx,
	/* 0x7F */ op_rra_absx,
	/* 0x80 */ op_skb,
	/* 0x81 */ op_sta_izx,
	/* 0x82 */ op_skb,
	/* 0x83 */ op_sax_izx,
	/* 0x84 */ op_sty_zp,
	/* 0x85 */ op_sta_zp,
	/* 0x86 */ op_stx_zp,
	/* 0x87 */ op_sax_zp,
	/* 0x88 */ op_dey,
	/* 0x89 */ op_skb,
	/* 0x8A */ op_txa,
	/* 0x8B */ op_xaa,
	/* 0x8C */ op_sty_abs,
	/* 0x8D */ op_sta_abs,
	/* 0x8E */ op_stx_abs,
	/* 0x8F */ op_sax_abs,
	/* 0x90 */ op_bcc,
	/* 0x91 */ op_sta_izy,
	/* 0x92 */ op_jam,
	/* 0x93 */ op_ahx_izy,
	/* 0x94 */ op_sty_zpx,
	/* 0x95 */ op_sta_zpx,
	/* 0x96 */ op_stx_zpy,
	/* 0x97 */ op_sax_zpy,
	/* 0x98 */ op_tya,
	/* 0x99 */ op_sta_absy,
	/* 0x9A */ op_txs,
	/* 0x9B */ op_tas,
	/* 0x9C */ op_shy,
	/* 0x9D */ op_sta_absx,
	/* 0x9E */ op_shx,
	/* 0x9F */ op_ahx_absy,
	/* 0xA0 */ op_ldy_imm,
	/* 0xA1 */ op_lda_izx,
	/* 0xA2 */ op_ldx_imm,
	/* 0xA3 */ op_lax_izx,
	/* 0xA4 */ op_ldy_zp,
	/* 0xA5 */ op_lda_zp,
	/* 0xA6 */ op_ldx_zp,
	/* 0xA7 */ op_lax_zp,
	/* 0xA8 */ op_tay,
	/* 0xA9 */ op_lda_imm,
	/* 0xAA */ op_tax,
	/* 0xAB */ op_lxa,
	/* 0xAC */ op_ldy_abs,
	/* 0xAD */ op_lda_abs,
	/* 0xAE */ op_ldx_abs,
	/* 0xAF */ op_lax_abs,
	/* 0xB0 */ op_bcs,
	/* 0xB1 */ op_lda_izy,
	/* 0xB2 */ op_jam,
	/* 0xB3 */ op_lax_izy,
	/* 0xB4 */ op_ldy_zpx,
	/* 0xB5 */ op_lda_zpx,
	/* 0xB6 */ op_ldx_zpy,
	/* 0xB7 */ op_lax_zpy,
	/* 0xB8 */ op_clv,
	/* 0xB9 */ op_lda_absy,
	/* 0xBA */ op_tsx,
	/* 0xBB */ op_las_unstable,

	/* 0xBC */ op_ldy_absx,
	/* 0xBD */ op_lda_absx,
	/* 0xBE */ op_ldx_absy,
	/* 0xBF */ op_lax_absy,
	/* 0xC0 */ op_cpy_imm,
	/* 0xC1 */ op_cmp_izx,
	/* 0xC2 */ op_skb,
	/* 0xC3 */ op_dcp_izx,
	/* 0xC4 */ op_cpy_zp,
	/* 0xC5 */ op_cmp_zp,
	/* 0xC6 */ op_dec_zp,
	/* 0xC7 */ op_dcp_zp,
	/* 0xC8 */ op_iny,
	/* 0xC9 */ op_cmp_imm,
	/* 0xCA */ op_dex,
	/* 0xCB */ op_sbx,
	/* 0xCC */ op_cpy_abs,
	/* 0xCD */ op_cmp_abs,
	/* 0xCE */ op_dec_abs,
	/* 0xCF */ op_dcp_abs,
	/* 0xD0 */ op_bne,
	/* 0xD1 */ op_cmp_izy,
	/* 0xD2 */ op_jam,
	/* 0xD3 */ op_dcp_izy,
	/* 0xD4 */ op_nop_zpx,
	/* 0xD5 */ op_cmp_zpx,
	/* 0xD6 */ op_dec_zpx,
	/* 0xD7 */ op_dcp_zpx,
	/* 0xD8 */ op_cld,
	/* 0xD9 */ op_cmp_absy,
	/* 0xDA */ op_nop_imp,
	/* 0xDB */ op_dcp_absy,
	/* 0xDC */ op_nop_absx,
	/* 0xDD */ op_cmp_absx,
	/* 0xDE */ op_dec_absx,
	/* 0xDF */ op_dcp_absx,
	/* 0xE0 */ op_cpx_imm,
	/* 0xE1 */ op_sbc_izx,
	/* 0xE2 */ op_skb,
	/* 0xE3 */ op_isc_izx,
	/* 0xE4 */ op_cpx_zp,
	/* 0xE5 */ op_sbc_zp,
	/* 0xE6 */ op_inc_zp,
	/* 0xE7 */ op_isc_zp,
	/* 0xE8 */ op_inx,
	/* 0xE9 */ op_sbc_imm,
	/* 0xEA */ op_nop,
	/* 0xEB */ op_usbc,
	/* 0xEC */ op_cpx_abs,
	/* 0xED */ op_sbc_abs,
	/* 0xEE */ op_inc_abs,
	/* 0xEF */ op_isc_abs,
	/* 0xF0 */ op_beq,
	/* 0xF1 */ op_sbc_izy,
	/* 0xF2 */ op_jam,
	/* 0xF3 */ op_isc_izy,
	/* 0xF4 */ op_nop_zpx,
	/* 0xF5 */ op_sbc_zpx,
	/* 0xF6 */ op_inc_zpx,
	/* 0xF7 */ op_isc_zpx,
	/* 0xF8 */ op_sed,
	/* 0xF9 */ op_sbc_absy,
	/* 0xFA */ op_nop_imp,
	/* 0xFB */ op_isc_absy,
	/* 0xFC */ op_nop_absx,
	/* 0xFD */ op_sbc_absx,
	/* 0xFE */ op_inc_absx,
	/* 0xFF */ op_isc_absx,
};

/* ================================================================== */
/* cpu_step                                                             */
/* ================================================================== */

uint8_t
cpu_step(struct cpu *cpu)
{
	static uint32_t dram_cycle_acc = 0;
	uint8_t cycles, opcode;
	int delta;

	/* If the struct cpu has been halted by a JAM opcode, keep the bus frozen. */
	if (cpu->halted != 0)
		return (1);

	/* Record non-sequential transitions */
	if (cpu->prev_pc_valid != 0) {
		delta = (int)cpu->pc - (int)cpu->prev_pc;
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
	 * After the previous instruction completes, the struct cpu samples its
	 * interrupt lines.  If an interrupt is pending it performs the
	 * opcode fetch for the *next* instruction (cycle 1) but discards
	 * the byte and forces $00 (BRK) into the instruction register
	 * regardless of what was in memory.  The PC is NOT incremented
	 * during this fetch, so when the ISR eventually RTIs, execution
	 * resumes at exactly the preempted instruction's address.
	 *
	 * The "hijacking" term refers to a *different* edge case: an NMI
	 * that arrives while a BRK/IRQ sequence is already mid-flight
	 * (during the vector pull) takes over the vector -- our 7-cycle
	 * handler is atomic at the step level so we don't model that
	 * sub-instruction race here.
	 *
	 * Either way: no peek at memory, no conditional pc++.
	 */
	if (cpu->nmi_pending != 0) {
		cpu->nmi_pending = false;
		read_byte(cpu, cpu->pc); /* cycle 1: fetch discarded, PC held */
		push_word(cpu, cpu->pc);
		push_byte(cpu, (cpu->p & ~FLAG_BREAK) | FLAG_UNUSED);
		set_flag(cpu, FLAG_INTERRUPT, true);
		cpu->pc = read_word(cpu, 0xFFFA);
		return (7);
	}
	if (cpu->irq_pending != 0 && (cpu->p & FLAG_INTERRUPT) == 0) {
		cpu->irq_pending = false;
		read_byte(cpu, cpu->pc); /* cycle 1: fetch discarded, PC held */
		push_word(cpu, cpu->pc);
		push_byte(cpu, (cpu->p & ~FLAG_BREAK) | FLAG_UNUSED);
		set_flag(cpu, FLAG_INTERRUPT, true);
		cpu->pc = read_word(cpu, 0xFFFE);
		return (7);
	}

	/* 2. Fetch opcode */
	opcode = read_byte(cpu, cpu->pc++);

	/* 3. Dispatch */
	if (dispatch[opcode] != NULL) {
		dispatch[opcode](cpu);
	} else {
		/* Unimplemented slot -- treat as JAM */
		cpu->pc--;
		cpu->halted = true;
		{
			char msg[128];
			port_snprintf(msg,
			    sizeof(msg),
			    "JAM: unimplemented opcode 0x%02X at PC=0x%04X. "
			    "RESET required.",
			    opcode,
			    (uint16_t)cpu->pc);
			BUS_LOG(cpu->bus, BUS_LOG_WARN, msg);
		}
		cpu->last_cycles = 1;
	}

	cycles = cpu->last_cycles;

	/* 4. DRAM refresh cycle stealing (optional) */
	if (cpu->bus->opts.emulate_dram_refresh != 0) {
		dram_cycle_acc += cycles * 4;
		while (dram_cycle_acc >= 61) {
			dram_cycle_acc -= 61;
			cycles++;
		}
	}

	return (cycles);
}

/* ================================================================== */
/* IRQ / NMI                                                           */
/* ================================================================== */

void
cpu_irq(struct cpu *cpu, bool assert)
{
	cpu->irq_pending = assert;
}

void
cpu_nmi(struct cpu *cpu)
{
	cpu->nmi_pending = true;
}
