# ===========================================================================
# Apple-1 Emulator — Portable POSIX-Compliant Makefile
# ===========================================================================
#
# USAGE:
#   make                  Build apple1 TUI target
#   make check            Build and run all unit tests
#   make clean            Remove all build artefacts
#
# COMPILE-TIME OPTIONS:
#   Pass any -D flag via EXTRA_CFLAGS.  All feature knobs live in
#   apple1limit.h — read that file for the full list.
#
# ===========================================================================

CC      = cc
CFLAGS  = -O2 -g
WFLAGS  = -Wall -Wextra
STDFLAG = -std=c89
DEFS    = -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600

# Combined flags used for every translation unit
BASE_CFLAGS = $(CFLAGS) $(WFLAGS) $(STDFLAG) $(DEFS) $(EXTRA_CFLAGS)

# Platform port selection (one port_*.c + shared port_string.c)
# Override with: make PORT_SRC=port_bare.c
ifeq ($(PORT_SRC),)
    ifeq ($(OS),Windows_NT)
        PORT_SRC = port_win.c
        TERM_SRC = term_ansi.c
    else
        UNAME := $(shell uname -s)
        ifeq ($(UNAME),Haiku)
            PORT_SRC = port_posix.c
            TERM_SRC = term_ansi.c
        else ifeq ($(UNAME),OS/2)
            PORT_SRC = port_os2.c
            TERM_SRC = term_ansi.c
        else ifeq ($(UNAME),Plan 9)
            PORT_SRC = port_plan9.c
            TERM_SRC = term_ansi.c
        else
            PORT_SRC = port_posix.c
            TERM_SRC = term_ansi.c
        endif
    endif
endif

PORT_STRING = port_string.c
PORT_OBJ    = port_string.o $(PORT_SRC:.c=.o)

# Sources
CORE_SRC       = cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
CORE_NO_IO_SRC = cpu.c bus.c aci.c krusader.c disasm.c dbg.c
SRC            = main.c $(CORE_SRC) term_ansi.c

# Object files
OBJ = main.o cpu.o bus.o io.o aci.o krusader.o disasm.o dbg.o term_ansi.o $(PORT_OBJ)

all: apple1

apple1: $(OBJ)
	$(CC) $(BASE_CFLAGS) -o apple1 $(OBJ)

port_string.o port_posix.o port_win.o port_os2.o port_bare.o: port.h

.c.o:
	$(CC) $(BASE_CFLAGS) -DAPPLE1_OMIT_CHARMAP -c -o $@ $<

# Test targets
TESTS = test_cpu test_aci test_dualram test_bus test_tomharte test_interrupts

check: $(TESTS)
	@passed=0; failed=0; \
	for t in $(TESTS); do \
		if ./$$t >/dev/null 2>&1; then \
			echo "PASS: $$t"; passed=`expr $$passed + 1`; \
			rm -f $$t; \
		else \
			echo "FAIL: $$t"; failed=`expr $$failed + 1`; \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$passed passed, $$failed failed"; \
	if [ $$failed -ne 0 ]; then exit 1; fi

test_cpu: tests/test_cpu.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)
	$(CC) $(BASE_CFLAGS) -o test_cpu tests/test_cpu.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)

test_aci: tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO_SRC) $(PORT_STRING) $(PORT_SRC)
	$(CC) $(BASE_CFLAGS) -o test_aci tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO_SRC) $(PORT_STRING) $(PORT_SRC)

test_dualram: tests/test_dualram.c tests/term_dummy.c bus.c io.c $(PORT_STRING) $(PORT_SRC)
	$(CC) $(BASE_CFLAGS) -o test_dualram tests/test_dualram.c tests/term_dummy.c bus.c io.c $(PORT_STRING) $(PORT_SRC)

test_bus: tests/test_bus.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)
	$(CC) $(BASE_CFLAGS) -o test_bus tests/test_bus.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)

test_tomharte: tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)
	$(CC) $(BASE_CFLAGS) -o test_tomharte tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)

test_interrupts: tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)
	$(CC) $(BASE_CFLAGS) -o test_interrupts tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC) $(PORT_STRING) $(PORT_SRC)

6502_functional_test.bin:
	curl -L -o 6502_functional_test.bin https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

# MS-DOS cross-build (requires i586-pc-msdosdjgpp-gcc on PATH)
DOS_CC ?= i586-pc-msdosdjgpp-gcc
DOS_CFLAGS = -O2 -Wall -std=gnu99 -D__MSDOS__ -DAPPLE1_OMIT_CHARMAP -march=i386 -mtune=i386

dos: cwsdpmi.exe
	python3 tools/amalgamate.py --port port_msdos.c --term term_dos.c
	$(DOS_CC) $(DOS_CFLAGS) -o apple1.exe apple1.c

cwsdpmi.exe:
	curl -L -o cwsdpmi.zip https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip
	unzip -o cwsdpmi.zip cwsdpmi.exe
	rm -f cwsdpmi.zip

apple1.exe: dos

test-official: apple1 6502_functional_test.bin
	./apple1 -H -F 6502_functional_test.bin

clean:
	rm -f apple1 $(TESTS)
	rm -f *.o port_*.o tests/*.o 6502_functional_test.bin cwsdpmi.exe
