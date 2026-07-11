#ifndef APPLE1_OMIT_DISASM
#include "bus.h"
#include "disasm.h"
#include "port.h"

enum addr_mode {
	ADDR_IMP, /* Implied (e.g. TAX) */
	ADDR_ACC, /* Accumulator (e.g. LSR A) */
	ADDR_IMM, /* Immediate (e.g. LDA #$12) */
	ADDR_ZP,  /* Zero Page (e.g. LDA $12) */
	ADDR_ZPX, /* Zero Page, X (e.g. LDA $12,X) */
	ADDR_ZPY, /* Zero Page, Y (e.g. LDX $12,Y) */
	ADDR_ABS, /* Absolute (e.g. LDA $1234) */
	ADDR_ABX, /* Absolute, X (e.g. LDA $1234,X) */
	ADDR_ABY, /* Absolute, Y (e.g. LDA $1234,Y) */
	ADDR_IND, /* Indirect (e.g. JMP ($1234)) */
	ADDR_IZX, /* Indirect, X (e.g. LDA ($12,X)) */
	ADDR_IZY, /* Indirect, Y (e.g. LDA ($12),Y) */
	ADDR_REL  /* Relative (e.g. BNE $1234) */
};

/* Mnemonic string pool (77 unique names, 309 bytes). */
static const char op_names[] =
    "BRK\0" "ORA\0" "JAM\0" "SLO\0" "NOP\0" "ASL\0" "PHP\0" "ANC\0"
    "BPL\0" "CLC\0" "JSR\0" "AND\0" "RLA\0" "BIT\0" "ROL\0" "PLP\0"
    "BMI\0" "SEC\0" "RTI\0" "EOR\0" "SRE\0" "LSR\0" "PHA\0" "ALR\0"
    "JMP\0" "BVC\0" "CLI\0" "RTS\0" "ADC\0" "RRA\0" "ROR\0" "PLA\0"
    "ARR\0" "BVS\0" "SEI\0" "SKB\0" "STA\0" "SAX\0" "STY\0" "STX\0"
    "DEY\0" "TXA\0" "XAA\0" "BCC\0" "AHX\0" "TYA\0" "TXS\0" "TAS\0"
    "SHY\0" "SHX\0" "LDY\0" "LDA\0" "LDX\0" "LAX\0" "TAY\0" "TAX\0"
    "BCS\0" "CLV\0" "TSX\0" "LAS\0" "CPY\0" "CMP\0" "DCP\0" "DEC\0"
    "INY\0" "DEX\0" "SBX\0" "BNE\0" "CLD\0" "CPX\0" "SBC\0" "ISB\0"
    "INC\0" "INX\0" "USBC\0" "BEQ\0" "SED\0"
;

static const uint16_t op_name_off[77] = {
    0,4,8,12,16,20,24,28,
    32,36,40,44,48,52,56,60,
    64,68,72,76,80,84,88,92,
    96,100,104,108,112,116,120,124,
    128,132,136,140,144,148,152,156,
    160,164,168,172,176,180,184,188,
    192,196,200,204,208,212,216,220,
    224,228,232,236,240,244,248,252,
    256,260,264,268,272,276,280,284,
    288,292,296,301,305,
};

static const uint8_t op_mode[256] = {
    /* 0x00 */ 0,10,0,10,3,3,3,3,0,2,1,2,6,6,6,6,
    /* 0x10 */ 12,11,0,11,4,4,4,4,0,8,0,8,7,7,7,7,
    /* 0x20 */ 6,10,0,10,3,3,3,3,0,2,1,2,6,6,6,6,
    /* 0x30 */ 12,11,0,11,4,4,4,4,0,8,0,8,7,7,7,7,
    /* 0x40 */ 0,10,0,10,3,3,3,3,0,2,1,2,6,6,6,6,
    /* 0x50 */ 12,11,0,11,4,4,4,4,0,8,0,8,7,7,7,7,
    /* 0x60 */ 0,10,0,10,3,3,3,3,0,2,1,2,9,6,6,6,
    /* 0x70 */ 12,11,0,11,4,4,4,4,0,8,0,8,7,7,7,7,
    /* 0x80 */ 2,10,2,10,3,3,3,3,0,2,0,2,6,6,6,6,
    /* 0x90 */ 12,11,0,11,4,4,5,5,0,8,0,8,7,7,8,8,
    /* 0xA0 */ 2,10,2,10,3,3,3,3,0,2,0,0,6,6,6,6,
    /* 0xB0 */ 12,11,0,11,4,4,5,5,0,8,0,8,7,7,8,8,
    /* 0xC0 */ 2,10,2,10,3,3,3,3,0,2,0,2,6,6,6,6,
    /* 0xD0 */ 12,11,0,11,4,4,4,4,0,8,0,8,7,7,7,7,
    /* 0xE0 */ 2,10,2,10,3,3,3,3,0,2,0,2,6,6,6,6,
    /* 0xF0 */ 12,11,0,11,4,4,4,4,0,8,0,8,7,7,7,7,
};

static const uint8_t op_name_idx[256] = {
    /* 0x00 */ 0,1,2,3,4,1,5,3,6,1,5,7,4,1,5,3,
    /* 0x10 */ 8,1,2,3,4,1,5,3,9,1,4,3,4,1,5,3,
    /* 0x20 */ 10,11,2,12,13,11,14,12,15,11,14,7,13,11,14,12,
    /* 0x30 */ 16,11,2,12,4,11,14,12,17,11,4,12,4,11,14,12,
    /* 0x40 */ 18,19,2,20,4,19,21,20,22,19,21,23,24,19,21,20,
    /* 0x50 */ 25,19,2,20,4,19,21,20,26,19,4,20,4,19,21,20,
    /* 0x60 */ 27,28,2,29,4,28,30,29,31,28,30,32,24,28,30,29,
    /* 0x70 */ 33,28,2,29,4,28,30,29,34,28,4,29,4,28,30,29,
    /* 0x80 */ 35,36,35,37,38,36,39,37,40,35,41,42,38,36,39,37,
    /* 0x90 */ 43,36,2,44,38,36,39,37,45,36,46,47,48,36,49,44,
    /* 0xA0 */ 50,51,52,53,50,51,52,53,54,51,55,2,50,51,52,53,
    /* 0xB0 */ 56,51,2,53,50,51,52,53,57,51,58,59,50,51,52,53,
    /* 0xC0 */ 60,61,35,62,60,61,63,62,64,61,65,66,60,61,63,62,
    /* 0xD0 */ 67,61,2,62,4,61,63,62,68,61,4,62,4,61,63,62,
    /* 0xE0 */ 69,70,35,71,69,70,72,71,73,70,4,74,69,70,72,71,
    /* 0xF0 */ 75,70,2,71,4,70,72,71,76,70,4,71,4,70,72,71,
};

static const char *
op_mnemonic(uint8_t idx)
{
	return (&op_names[op_name_off[idx]]);
}

int
cpu_disassemble(struct bus *bus, uint16_t pc, char *out_str)
{
	const char *name;
	enum addr_mode mode;
	uint8_t op;
	int bytes;

	op = bus_read(bus, pc);
	mode = (enum addr_mode)op_mode[op];
	name = op_mnemonic(op_name_idx[op]);
	bytes = 1;

	switch (mode) {
	case ADDR_IMP:
		port_snprintf(out_str, 64, "%s", name);
		bytes = 1;
		break;
	case ADDR_ACC:
		port_snprintf(out_str, 64, "%s A", name);
		bytes = 1;
		break;
	case ADDR_IMM: {
		uint8_t val = bus_read(bus, pc + 1);

		port_snprintf(out_str, 64, "%s #$%02X", name, val);
		bytes = 2;
		break;
	}
	case ADDR_ZP: {
		uint8_t val = bus_read(bus, pc + 1);

		port_snprintf(out_str, 64, "%s $%02X", name, val);
		bytes = 2;
		break;
	}
	case ADDR_ZPX: {
		uint8_t val = bus_read(bus, pc + 1);

		port_snprintf(out_str, 64, "%s $%02X,X", name, val);
		bytes = 2;
		break;
	}
	case ADDR_ZPY: {
		uint8_t val = bus_read(bus, pc + 1);

		port_snprintf(out_str, 64, "%s $%02X,Y", name, val);
		bytes = 2;
		break;
	}
	case ADDR_ABS: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (uint16_t)((hi << 8) | lo);

		port_snprintf(out_str, 64, "%s $%04X", name, val);
		bytes = 3;
		break;
	}
	case ADDR_ABX: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (uint16_t)((hi << 8) | lo);

		port_snprintf(out_str, 64, "%s $%04X,X", name, val);
		bytes = 3;
		break;
	}
	case ADDR_ABY: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (uint16_t)((hi << 8) | lo);

		port_snprintf(out_str, 64, "%s $%04X,Y", name, val);
		bytes = 3;
		break;
	}
	case ADDR_IND: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (uint16_t)((hi << 8) | lo);

		port_snprintf(out_str, 64, "%s ($%04X)", name, val);
		bytes = 3;
		break;
	}
	case ADDR_IZX: {
		uint8_t val = bus_read(bus, pc + 1);

		port_snprintf(out_str, 64, "%s ($%02X,X)", name, val);
		bytes = 2;
		break;
	}
	case ADDR_IZY: {
		uint8_t val = bus_read(bus, pc + 1);

		port_snprintf(out_str, 64, "%s ($%02X),Y", name, val);
		bytes = 2;
		break;
	}
	case ADDR_REL: {
		int8_t offset = (int8_t)bus_read(bus, pc + 1);
		uint16_t dest = (uint16_t)((pc + 2) + offset);

		port_snprintf(out_str, 64, "%s $%04X", name, dest);
		bytes = 2;
		break;
	}
	}

	return (bytes);
}

#endif /* APPLE1_OMIT_DISASM */
