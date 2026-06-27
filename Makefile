# ===========================================================================
# Apple-1 Emulator — Portable POSIX-Compliant Makefile
# ===========================================================================
#
# USAGE:
#   make                  Build both targets (apple1 GUI + apple1-cli)
#   make apple1-cli       Build CLI-only target
#   make apple1           Build SDL3 GUI target
#   make check            Build and run all unit tests
#   make clean            Remove all build artefacts
#
# COMPILE-TIME OPTIONS:
#   Pass any -D flag via EXTRA_CFLAGS.  All feature knobs live in
#   apple1limit.h — read that file for the full list.  Examples:
#
#     make EXTRA_CFLAGS="-DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI"
#     make EXTRA_CFLAGS="-DAPPLE1_STATIC_RAM_SIZE=8192"
#     make EXTRA_CFLAGS="-DAPPLE1_ZERO_MALLOC -DAPPLE1_OMIT_DISKIO"
#
# ===========================================================================

CC      = cc
CFLAGS  = -O2 -g
WFLAGS  = -Wall -Wextra
STDFLAG = -std=c99
DEFS    = -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600

# Combined flags used for every translation unit
BASE_CFLAGS = $(CFLAGS) $(WFLAGS) $(STDFLAG) $(DEFS) $(EXTRA_CFLAGS)

# SDL3 detection — standard POSIX style
# Backticks are evaluated by the shell during command execution
SDL_CFLAGS_CMD = pkg-config --cflags sdl3 2>/dev/null || echo ""
SDL_LIBS_CMD   = pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3"

# Sources
CORE_SRC = cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
CORE_NO_IO_SRC = cpu.c bus.c aci.c krusader.c disasm.c dbg.c

CLI_SRC  = main_cli.c $(CORE_SRC) term_ansi.c
GUI_SRC  = main.c    $(CORE_SRC) term_sdl3.c term_config.c term_debug.c

# Object lists (POSIX standard suffix replacement)
CLI_OBJ = main_cli.cli.o cpu.cli.o bus.cli.o io.cli.o aci.cli.o krusader.cli.o disasm.cli.o dbg.cli.o term_ansi.cli.o
GUI_OBJ = main.gui.o cpu.gui.o bus.gui.o io.gui.o aci.gui.o krusader.gui.o disasm.gui.o dbg.gui.o term_sdl3.gui.o term_config.gui.o term_debug.gui.o

.SUFFIXES: .c .cli.o .gui.o

all: apple1-cli apple1

apple1-cli: $(CLI_OBJ)
	$(CC) $(BASE_CFLAGS) -o apple1-cli $(CLI_OBJ)

apple1: $(GUI_OBJ)
	$(CC) $(BASE_CFLAGS) `$(SDL_CFLAGS_CMD)` -o apple1 $(GUI_OBJ) `$(SDL_LIBS_CMD)`

# Suffix rules
.c.cli.o:
	$(CC) $(BASE_CFLAGS) -DAPPLE1_OMIT_CHARMAP -c -o $@ $<

.c.gui.o:
	$(CC) $(BASE_CFLAGS) `$(SDL_CFLAGS_CMD)` -c -o $@ $<

# Test targets
TESTS = test_cpu test_aci test_dualram test_bus test_tomharte test_interrupts

check: $(TESTS)
	@passed=0; failed=0; \
	for t in $(TESTS); do \
		if ./$$t >/dev/null 2>&1; then \
			echo "PASS: $$t"; passed=`expr $$passed + 1`; \
		else \
			echo "FAIL: $$t"; failed=`expr $$failed + 1`; \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$passed passed, $$failed failed"; \
	if [ $$failed -ne 0 ]; then exit 1; fi

test_cpu: tests/test_cpu.c tests/term_dummy.c $(CORE_SRC)
	$(CC) $(BASE_CFLAGS) -o test_cpu tests/test_cpu.c tests/term_dummy.c $(CORE_SRC)

test_aci: tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO_SRC)
	$(CC) $(BASE_CFLAGS) -o test_aci tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO_SRC)

test_dualram: tests/test_dualram.c tests/term_dummy.c bus.c io.c
	$(CC) $(BASE_CFLAGS) -o test_dualram tests/test_dualram.c tests/term_dummy.c bus.c io.c

test_bus: tests/test_bus.c tests/term_dummy.c $(CORE_SRC)
	$(CC) $(BASE_CFLAGS) -o test_bus tests/test_bus.c tests/term_dummy.c $(CORE_SRC)

test_tomharte: tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC)
	$(CC) $(BASE_CFLAGS) -o test_tomharte tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC)

test_interrupts: tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC)
	$(CC) $(BASE_CFLAGS) -o test_interrupts tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC)

6502_functional_test.bin:
	curl -L -o 6502_functional_test.bin https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

test-official: apple1 apple1-cli 6502_functional_test.bin
	./apple1 -H --flat-bus 6502_functional_test.bin
	./apple1-cli -H -F 6502_functional_test.bin

clean:
	rm -f apple1 apple1-cli $(TESTS)
	rm -f *.o *.cli.o *.gui.o tests/*.o
