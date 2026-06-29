# ===========================================================================
# Apple-1 Emulator — Portable POSIX-Compliant Makefile
# ===========================================================================
#
# USAGE:
#   make                  Build apple1 TUI target
#   make check            Build and run all unit tests
#   make clean            Remove all build artefacts
#
# ===========================================================================

CC      = cc
CFLAGS  = -O2 -g
WFLAGS  = -Wall -Wextra
STDFLAG = -std=c89
DEFS    = -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600

# Combined flags used for every translation unit
BASE_CFLAGS = $(CFLAGS) $(WFLAGS) $(STDFLAG) $(DEFS) $(EXTRA_CFLAGS)

# Core sources (excluding main.c and term/port wrappers)
CORE_SRC       = cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
CORE_NO_IO_SRC = cpu.c bus.c aci.c krusader.c disasm.c dbg.c

# Object files for the main executable
OBJ = main.o cpu.o bus.o io.o aci.o krusader.o disasm.o dbg.o term.o port.o

all: apple1

apple1: $(OBJ)
	$(CC) $(BASE_CFLAGS) -o apple1 $(OBJ)

port.o: port.c port.h port_string.c port_posix.c port_win.c port_msdos.c \
        port_plan9.c port_freertos.c port_zephyr.c port_bare.c port_os2.c \
        port_elks.c port_vxworks.c
	$(CC) $(BASE_CFLAGS) -DAPPLE1_OMIT_CHARMAP -c -o port.o port.c

term.o: term.c term_ansi.c term_dos.c term_apple1.h
	$(CC) $(BASE_CFLAGS) -DAPPLE1_OMIT_CHARMAP -c -o term.o term.c

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
			rm -f $$t.o; \
		else \
			echo "FAIL: $$t"; failed=`expr $$failed + 1`; \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$passed passed, $$failed failed"; \
	if [ $$failed -ne 0 ]; then exit 1; fi

test_cpu: tests/test_cpu.c tests/term_dummy.c $(CORE_SRC) port.c
	$(CC) $(BASE_CFLAGS) -o test_cpu tests/test_cpu.c tests/term_dummy.c $(CORE_SRC) port.c

test_aci: tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO_SRC) port.c
	$(CC) $(BASE_CFLAGS) -o test_aci tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO_SRC) port.c

test_dualram: tests/test_dualram.c tests/term_dummy.c bus.c io.c port.c
	$(CC) $(BASE_CFLAGS) -o test_dualram tests/test_dualram.c tests/term_dummy.c bus.c io.c port.c

test_bus: tests/test_bus.c tests/term_dummy.c $(CORE_SRC) port.c
	$(CC) $(BASE_CFLAGS) -o test_bus tests/test_bus.c tests/term_dummy.c $(CORE_SRC) port.c

test_tomharte: tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC) port.c
	$(CC) $(BASE_CFLAGS) -o test_tomharte tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC) port.c

test_interrupts: tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC) port.c
	$(CC) $(BASE_CFLAGS) -o test_interrupts tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC) port.c

6502_functional_test.bin:
	curl -L -o 6502_functional_test.bin https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

# Amalgamation target
amalgamation:
	python3 tools/amalgamate.py

# MS-DOS amalgamation (explicit DOS port + terminal for cross-compilers)
amalgamation-dos:
	python3 tools/amalgamate.py --port port_msdos.c --term term_dos.c

# MS-DOS cross-build — DJGPP (32-bit DPMI; needs CWSDPMI.EXE in DOSBox)
DOS_CC ?= i586-pc-msdosdjgpp-gcc
DOS_CFLAGS = -O2 -Wall -std=gnu99 -D__MSDOS__ -DAPPLE1_OMIT_CHARMAP \
             -DAPPLE1_PORT_MSDOS -DAPPLE1_TERM_DOS -march=i386 -mtune=i386

dos-djgpp: cwsdpmi.exe amalgamation-dos
	$(DOS_CC) $(DOS_CFLAGS) -o apple1.exe apple1.c

# MS-DOS cross-build — Open Watcom (wcl386 / owcc; CauseWay stub embedded)
#
# Defaults: $HOME/watcom (override with WATCOM=... if needed).
#   macOS host binaries:  $WATCOM/bino64
#   Linux host binaries:  $WATCOM/binl64
#
#   make dos-watcom
#   make dos-watcom WATCOM_CC=owcc
#
WATCOM        ?= $(HOME)/watcom
UNAME_S       := $(shell uname -s 2>/dev/null || echo Unknown)
ifeq ($(UNAME_S),Darwin)
WATCOM_BINDIR ?= $(WATCOM)/bino64
WATCOM_STUBDIR ?= $(WATCOM)/binw
else ifeq ($(UNAME_S),Linux)
WATCOM_BINDIR ?= $(WATCOM)/binl64
WATCOM_STUBDIR ?= $(WATCOM)/binw
else
WATCOM_BINDIR ?= $(WATCOM)/binnt
WATCOM_STUBDIR ?= $(WATCOM)/binw
endif
WATCOM_CC     ?= wcl386
WATCOM_CFLAGS ?= -bt=dos -ox -zq -w3 -fe=apple1.exe -l=causeway
WATCOM_DEFS   = -dMSDOS -dAPPLE1_OMIT_CHARMAP -dAPPLE1_PORT_MSDOS \
                  -dAPPLE1_TERM_DOS

dos-watcom: amalgamation-dos
	@test -x "$(WATCOM_BINDIR)/$(WATCOM_CC)" || \
	    (echo "Error: $(WATCOM_BINDIR)/$(WATCOM_CC) not found." >&2; \
	     echo "Set WATCOM and WATCOM_BINDIR (e.g. WATCOM=$(HOME)/watcom WATCOM_BINDIR=$(HOME)/watcom/bino64)" >&2; \
	     exit 1)
	@test -f "$(WATCOM_STUBDIR)/cwstub.exe" || \
	    (echo "Error: $(WATCOM_STUBDIR)/cwstub.exe not found (CauseWay stub)." >&2; \
	     exit 1)
	WATCOM="$(WATCOM)" \
	    INCLUDE="$(WATCOM)/h" \
	    LIB="$(WATCOM)/lib386/dos" \
	    PATH="$(WATCOM_BINDIR):$(WATCOM_STUBDIR):$$PATH" \
	    $(WATCOM_CC) $(WATCOM_CFLAGS) $(WATCOM_DEFS) apple1.c

apple1-watcom.exe: dos-watcom

apple1-djgpp.exe: dos-djgpp

cwsdpmi.exe:
	curl -L -o cwsdpmi.zip https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip
	unzip -o cwsdpmi.zip cwsdpmi.exe
	rm -f cwsdpmi.zip

apple1.exe: dos-djgpp

test-official: apple1 6502_functional_test.bin
	./apple1 -H -F 6502_functional_test.bin

clean:
	rm -f apple1 $(TESTS) apple1_test apple1_amal_test
	rm -f *.o tests/*.o 6502_functional_test.bin cwsdpmi.exe apple1.c apple1.h
