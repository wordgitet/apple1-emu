# ===========================================================================
# Apple-1 Emulator — Handwritten GNUmakefile
# ===========================================================================
#
# USAGE
#   make                  Build both targets (apple1 GUI + apple1-cli)
#   make apple1-cli       Build CLI-only target
#   make apple1           Build SDL3 GUI target
#   make check            Build and run all unit tests
#   make clean            Remove all build artefacts
#
# COMPILE-TIME OPTIONS
#   Pass any -D flag via EXTRA_CFLAGS.  All feature knobs live in
#   apple1limit.h — read that file for the full list.  Examples:
#
#     make EXTRA_CFLAGS="-DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI"
#     make EXTRA_CFLAGS="-DAPPLE1_STATIC_RAM_SIZE=8192"
#     make EXTRA_CFLAGS="-DAPPLE1_ZERO_MALLOC -DAPPLE1_OMIT_DISKIO"
#
# SDL3 PATHS (override if pkg-config is not available)
#   make SDL3_CFLAGS="-I/opt/sdl3/include" SDL3_LIBS="-L/opt/sdl3/lib -lSDL3"
#
# ===========================================================================

CC      ?= cc
CFLAGS  ?= -O2 -g
WFLAGS   = -Wall -Wextra
STDFLAG  = -std=c99
DEFS     = -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600

# SDL3 detection — override on the command line if pkg-config is absent
SDL3_CFLAGS != pkg-config --cflags sdl3 2>/dev/null; echo ""
SDL3_LIBS   != pkg-config --libs   sdl3 2>/dev/null; echo "-lSDL3"

# Any extra -D flags supplied by the user land here
EXTRA_CFLAGS ?=

# Combined flags used for every translation unit
BASE_CFLAGS = $(CFLAGS) $(WFLAGS) $(STDFLAG) $(DEFS) $(EXTRA_CFLAGS)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

CORE_SRC = cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c

CLI_SRC  = main_cli.c $(CORE_SRC) term_ansi.c
GUI_SRC  = main.c    $(CORE_SRC) term_sdl3.c term_config.c term_debug.c

CORE_NO_IO_SRC = cpu.c bus.c aci.c krusader.c disasm.c dbg.c

# ---------------------------------------------------------------------------
# Object lists
# ---------------------------------------------------------------------------

CLI_OBJ  = $(CLI_SRC:.c=.cli.o)
GUI_OBJ  = $(GUI_SRC:.c=.gui.o)

# ---------------------------------------------------------------------------
# Primary targets
# ---------------------------------------------------------------------------

.PHONY: all apple1 apple1-cli check clean test-official

all: apple1-cli apple1

apple1-cli: $(CLI_OBJ)
	$(CC) $(BASE_CFLAGS) -o $@ $^

apple1: $(GUI_OBJ)
	$(CC) $(BASE_CFLAGS) $(SDL3_CFLAGS) -o $@ $^ $(SDL3_LIBS)

# ---------------------------------------------------------------------------
# Pattern rules — separate object namespaces for CLI vs GUI
# ---------------------------------------------------------------------------

%.cli.o: %.c
	$(CC) $(BASE_CFLAGS) -DAPPLE1_OMIT_CHARMAP -c -o $@ $<

%.gui.o: %.c
	$(CC) $(BASE_CFLAGS) $(SDL3_CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

TESTS = test_cpu test_aci test_dualram test_bus test_tomharte test_interrupts

test_cpu_SRC        = tests/test_cpu.c        tests/term_dummy.c $(CORE_SRC)
test_aci_SRC        = tests/test_aci.c        tests/term_dummy.c $(CORE_NO_IO_SRC)
test_dualram_SRC    = tests/test_dualram.c    tests/term_dummy.c bus.c io.c
test_bus_SRC        = tests/test_bus.c        tests/term_dummy.c $(CORE_SRC)
test_tomharte_SRC   = tests/test_tomharte.c   tests/term_dummy.c $(CORE_SRC)
test_interrupts_SRC = tests/test_interrupts.c tests/term_dummy.c $(CORE_SRC)

define BUILD_TEST
$(1): $$($(1)_SRC)
	$$(CC) $$(BASE_CFLAGS) -o $$@ $$^
endef

$(foreach t,$(TESTS),$(eval $(call BUILD_TEST,$(t))))

check: $(TESTS)
	@passed=0; failed=0; \
	for t in $(TESTS); do \
		if ./$$t > /dev/null 2>&1; then \
			echo "PASS: $$t"; passed=$$((passed+1)); \
		else \
			echo "FAIL: $$t"; failed=$$((failed+1)); \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$passed passed, $$failed failed"; \
	[ $$failed -eq 0 ]

# ---------------------------------------------------------------------------
# Dormann functional test (downloads test binary on first run)
# ---------------------------------------------------------------------------

DORMANN_URL = https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

6502_functional_test.bin:
	curl -L -o $@ $(DORMANN_URL)

test-official: apple1 apple1-cli 6502_functional_test.bin
	./apple1 -H --flat-bus 6502_functional_test.bin
	./apple1-cli -H -F 6502_functional_test.bin

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

clean:
	rm -f apple1 apple1-cli
	rm -f $(TESTS)
	rm -f *.cli.o *.gui.o tests/*.o
