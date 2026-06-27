#ifndef APPLE1_OMIT_DISASM
#include "port.h"
#include <stdio.h>

#include "bus.h"
#include "disasm.h"

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

struct op_info {
	const char *name;
	enum addr_mode mode;
};

static const struct op_info op_table[256] = {
	/* 0x00 - 0x0F */
	{ "BRK", ADDR_IMP },
	{ "ORA", ADDR_IZX },
	{ "JAM", ADDR_IMP },
	{ "SLO", ADDR_IZX },
	{ "NOP", ADDR_ZP },
	{ "ORA", ADDR_ZP },
	{ "ASL", ADDR_ZP },
	{ "SLO", ADDR_ZP },
	{ "PHP", ADDR_IMP },
	{ "ORA", ADDR_IMM },
	{ "ASL", ADDR_ACC },
	{ "ANC", ADDR_IMM },
	{ "NOP", ADDR_ABS },
	{ "ORA", ADDR_ABS },
	{ "ASL", ADDR_ABS },
	{ "SLO", ADDR_ABS },
	/* 0x10 - 0x1F */
	{ "BPL", ADDR_REL },
	{ "ORA", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "SLO", ADDR_IZY },
	{ "NOP", ADDR_ZPX },
	{ "ORA", ADDR_ZPX },
	{ "ASL", ADDR_ZPX },
	{ "SLO", ADDR_ZPX },
	{ "CLC", ADDR_IMP },
	{ "ORA", ADDR_ABY },
	{ "NOP", ADDR_IMP },
	{ "SLO", ADDR_ABY },
	{ "NOP", ADDR_ABX },
	{ "ORA", ADDR_ABX },
	{ "ASL", ADDR_ABX },
	{ "SLO", ADDR_ABX },
	/* 0x20 - 0x2F */
	{ "JSR", ADDR_ABS },
	{ "AND", ADDR_IZX },
	{ "JAM", ADDR_IMP },
	{ "RLA", ADDR_IZX },
	{ "BIT", ADDR_ZP },
	{ "AND", ADDR_ZP },
	{ "ROL", ADDR_ZP },
	{ "RLA", ADDR_ZP },
	{ "PLP", ADDR_IMP },
	{ "AND", ADDR_IMM },
	{ "ROL", ADDR_ACC },
	{ "ANC", ADDR_IMM },
	{ "BIT", ADDR_ABS },
	{ "AND", ADDR_ABS },
	{ "ROL", ADDR_ABS },
	{ "RLA", ADDR_ABS },
	/* 0x30 - 0x3F */
	{ "BMI", ADDR_REL },
	{ "AND", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "RLA", ADDR_IZY },
	{ "NOP", ADDR_ZPX },
	{ "AND", ADDR_ZPX },
	{ "ROL", ADDR_ZPX },
	{ "RLA", ADDR_ZPX },
	{ "SEC", ADDR_IMP },
	{ "AND", ADDR_ABY },
	{ "NOP", ADDR_IMP },
	{ "RLA", ADDR_ABY },
	{ "NOP", ADDR_ABX },
	{ "AND", ADDR_ABX },
	{ "ROL", ADDR_ABX },
	{ "RLA", ADDR_ABX },
	/* 0x40 - 0x4F */
	{ "RTI", ADDR_IMP },
	{ "EOR", ADDR_IZX },
	{ "JAM", ADDR_IMP },
	{ "SRE", ADDR_IZX },
	{ "NOP", ADDR_ZP },
	{ "EOR", ADDR_ZP },
	{ "LSR", ADDR_ZP },
	{ "SRE", ADDR_ZP },
	{ "PHA", ADDR_IMP },
	{ "EOR", ADDR_IMM },
	{ "LSR", ADDR_ACC },
	{ "ALR", ADDR_IMM },
	{ "JMP", ADDR_ABS },
	{ "EOR", ADDR_ABS },
	{ "LSR", ADDR_ABS },
	{ "SRE", ADDR_ABS },
	/* 0x50 - 0x5F */
	{ "BVC", ADDR_REL },
	{ "EOR", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "SRE", ADDR_IZY },
	{ "NOP", ADDR_ZPX },
	{ "EOR", ADDR_ZPX },
	{ "LSR", ADDR_ZPX },
	{ "SRE", ADDR_ZPX },
	{ "CLI", ADDR_IMP },
	{ "EOR", ADDR_ABY },
	{ "NOP", ADDR_IMP },
	{ "SRE", ADDR_ABY },
	{ "NOP", ADDR_ABX },
	{ "EOR", ADDR_ABX },
	{ "LSR", ADDR_ABX },
	{ "SRE", ADDR_ABX },
	/* 0x60 - 0x6F */
	{ "RTS", ADDR_IMP },
	{ "ADC", ADDR_IZX },
	{ "JAM", ADDR_IMP },
	{ "RRA", ADDR_IZX },
	{ "NOP", ADDR_ZP },
	{ "ADC", ADDR_ZP },
	{ "ROR", ADDR_ZP },
	{ "RRA", ADDR_ZP },
	{ "PLA", ADDR_IMP },
	{ "ADC", ADDR_IMM },
	{ "ROR", ADDR_ACC },
	{ "ARR", ADDR_IMM },
	{ "JMP", ADDR_IND },
	{ "ADC", ADDR_ABS },
	{ "ROR", ADDR_ABS },
	{ "RRA", ADDR_ABS },
	/* 0x70 - 0x7F */
	{ "BVS", ADDR_REL },
	{ "ADC", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "RRA", ADDR_IZY },
	{ "NOP", ADDR_ZPX },
	{ "ADC", ADDR_ZPX },
	{ "ROR", ADDR_ZPX },
	{ "RRA", ADDR_ZPX },
	{ "SEI", ADDR_IMP },
	{ "ADC", ADDR_ABY },
	{ "NOP", ADDR_IMP },
	{ "RRA", ADDR_ABY },
	{ "NOP", ADDR_ABX },
	{ "ADC", ADDR_ABX },
	{ "ROR", ADDR_ABX },
	{ "RRA", ADDR_ABX },
	/* 0x80 - 0x8F */
	{ "SKB", ADDR_IMM },
	{ "STA", ADDR_IZX },
	{ "SKB", ADDR_IMM },
	{ "SAX", ADDR_IZX },
	{ "STY", ADDR_ZP },
	{ "STA", ADDR_ZP },
	{ "STX", ADDR_ZP },
	{ "SAX", ADDR_ZP },
	{ "DEY", ADDR_IMP },
	{ "SKB", ADDR_IMM },
	{ "TXA", ADDR_IMP },
	{ "XAA", ADDR_IMM },
	{ "STY", ADDR_ABS },
	{ "STA", ADDR_ABS },
	{ "STX", ADDR_ABS },
	{ "SAX", ADDR_ABS },
	/* 0x90 - 0x9F */
	{ "BCC", ADDR_REL },
	{ "STA", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "AHX", ADDR_IZY },
	{ "STY", ADDR_ZPX },
	{ "STA", ADDR_ZPX },
	{ "STX", ADDR_ZPY },
	{ "SAX", ADDR_ZPY },
	{ "TYA", ADDR_IMP },
	{ "STA", ADDR_ABY },
	{ "TXS", ADDR_IMP },
	{ "TAS", ADDR_ABY },
	{ "SHY", ADDR_ABX },
	{ "STA", ADDR_ABX },
	{ "SHX", ADDR_ABY },
	{ "AHX", ADDR_ABY },
	/* 0xA0 - 0xAF */
	{ "LDY", ADDR_IMM },
	{ "LDA", ADDR_IZX },
	{ "LDX", ADDR_IMM },
	{ "LAX", ADDR_IZX },
	{ "LDY", ADDR_ZP },
	{ "LDA", ADDR_ZP },
	{ "LDX", ADDR_ZP },
	{ "LAX", ADDR_ZP },
	{ "TAY", ADDR_IMP },
	{ "LDA", ADDR_IMM },
	{ "TAX", ADDR_IMP },
	{ "JAM", ADDR_IMP },
	{ "LDY", ADDR_ABS },
	{ "LDA", ADDR_ABS },
	{ "LDX", ADDR_ABS },
	{ "LAX", ADDR_ABS },
	/* 0xB0 - 0xBF */
	{ "BCS", ADDR_REL },
	{ "LDA", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "LAX", ADDR_IZY },
	{ "LDY", ADDR_ZPX },
	{ "LDA", ADDR_ZPX },
	{ "LDX", ADDR_ZPY },
	{ "LAX", ADDR_ZPY },
	{ "CLV", ADDR_IMP },
	{ "LDA", ADDR_ABY },
	{ "TSX", ADDR_IMP },
	{ "LAS", ADDR_ABY },
	{ "LDY", ADDR_ABX },
	{ "LDA", ADDR_ABX },
	{ "LDX", ADDR_ABY },
	{ "LAX", ADDR_ABY },
	/* 0xC0 - 0xCF */
	{ "CPY", ADDR_IMM },
	{ "CMP", ADDR_IZX },
	{ "SKB", ADDR_IMM },
	{ "DCP", ADDR_IZX },
	{ "CPY", ADDR_ZP },
	{ "CMP", ADDR_ZP },
	{ "DEC", ADDR_ZP },
	{ "DCP", ADDR_ZP },
	{ "INY", ADDR_IMP },
	{ "CMP", ADDR_IMM },
	{ "DEX", ADDR_IMP },
	{ "SBX", ADDR_IMM },
	{ "CPY", ADDR_ABS },
	{ "CMP", ADDR_ABS },
	{ "DEC", ADDR_ABS },
	{ "DCP", ADDR_ABS },
	/* 0xD0 - 0xDF */
	{ "BNE", ADDR_REL },
	{ "CMP", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "DCP", ADDR_IZY },
	{ "NOP", ADDR_ZPX },
	{ "CMP", ADDR_ZPX },
	{ "DEC", ADDR_ZPX },
	{ "DCP", ADDR_ZPX },
	{ "CLD", ADDR_IMP },
	{ "CMP", ADDR_ABY },
	{ "NOP", ADDR_IMP },
	{ "DCP", ADDR_ABY },
	{ "NOP", ADDR_ABX },
	{ "CMP", ADDR_ABX },
	{ "DEC", ADDR_ABX },
	{ "DCP", ADDR_ABX },
	/* 0xE0 - 0xEF */
	{ "CPX", ADDR_IMM },
	{ "SBC", ADDR_IZX },
	{ "SKB", ADDR_IMM },
	{ "ISB", ADDR_IZX },
	{ "CPX", ADDR_ZP },
	{ "SBC", ADDR_ZP },
	{ "INC", ADDR_ZP },
	{ "ISB", ADDR_ZP },
	{ "INX", ADDR_IMP },
	{ "SBC", ADDR_IMM },
	{ "NOP", ADDR_IMP },
	{ "USBC", ADDR_IMM },
	{ "CPX", ADDR_ABS },
	{ "SBC", ADDR_ABS },
	{ "INC", ADDR_ABS },
	{ "ISB", ADDR_ABS },
	/* 0xF0 - 0xFF */
	{ "BEQ", ADDR_REL },
	{ "SBC", ADDR_IZY },
	{ "JAM", ADDR_IMP },
	{ "ISB", ADDR_IZY },
	{ "NOP", ADDR_ZPX },
	{ "SBC", ADDR_ZPX },
	{ "INC", ADDR_ZPX },
	{ "ISB", ADDR_ZPX },
	{ "SED", ADDR_IMP },
	{ "SBC", ADDR_ABY },
	{ "NOP", ADDR_IMP },
	{ "ISB", ADDR_ABY },
	{ "NOP", ADDR_ABX },
	{ "SBC", ADDR_ABX },
	{ "INC", ADDR_ABX },
	{ "ISB", ADDR_ABX }
};

int
cpu_disassemble(struct bus *bus, uint16_t pc, char *out_str)
{
	uint8_t op = bus_read(bus, pc);
	struct op_info info = op_table[op];
	int bytes = 1;

	switch (info.mode) {
	case ADDR_IMP:
		sprintf(out_str, "%s", info.name);
		bytes = 1;
		break;
	case ADDR_ACC:
		sprintf(out_str, "%s A", info.name);
		bytes = 1;
		break;
	case ADDR_IMM: {
		uint8_t val = bus_read(bus, pc + 1);

		sprintf(out_str, "%s #$%02X", info.name, val);
		bytes = 2;
		break;
	}
	case ADDR_ZP: {
		uint8_t val = bus_read(bus, pc + 1);

		sprintf(out_str, "%s $%02X", info.name, val);
		bytes = 2;
		break;
	}
	case ADDR_ZPX: {
		uint8_t val = bus_read(bus, pc + 1);

		sprintf(out_str, "%s $%02X,X", info.name, val);
		bytes = 2;
		break;
	}
	case ADDR_ZPY: {
		uint8_t val = bus_read(bus, pc + 1);

		sprintf(out_str, "%s $%02X,Y", info.name, val);
		bytes = 2;
		break;
	}
	case ADDR_ABS: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (hi << 8) | lo;

		sprintf(out_str, "%s $%04X", info.name, val);
		bytes = 3;
		break;
	}
	case ADDR_ABX: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (hi << 8) | lo;

		sprintf(out_str, "%s $%04X,X", info.name, val);
		bytes = 3;
		break;
	}
	case ADDR_ABY: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (hi << 8) | lo;

		sprintf(out_str, "%s $%04X,Y", info.name, val);
		bytes = 3;
		break;
	}
	case ADDR_IND: {
		uint8_t lo = bus_read(bus, pc + 1);
		uint8_t hi = bus_read(bus, pc + 2);
		uint16_t val = (hi << 8) | lo;

		sprintf(out_str, "%s ($%04X)", info.name, val);
		bytes = 3;
		break;
	}
	case ADDR_IZX: {
		uint8_t val = bus_read(bus, pc + 1);

		sprintf(out_str, "%s ($%02X,X)", info.name, val);
		bytes = 2;
		break;
	}
	case ADDR_IZY: {
		uint8_t val = bus_read(bus, pc + 1);

		sprintf(out_str, "%s ($%02X),Y", info.name, val);
		bytes = 2;
		break;
	}
	case ADDR_REL: {
		int8_t offset = (int8_t)bus_read(bus, pc + 1);
		uint16_t dest = (pc + 2) + offset;

		sprintf(out_str, "%s $%04X", info.name, dest);
		bytes = 2;
		break;
	}
	}

	return (bytes);
}

#endif /* APPLE1_OMIT_DISASM */
