AM_CPPFLAGS = -DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI
AM_CFLAGS = -Wall -Wextra -std=c89
AM_LDFLAGS =

# Included by port.c / term.c — not separate translation units.
PORT_SHIM = port_string.c port_path.c port_posix.c port_tcc_va.c port_posix_inc.h

TERM_SHIM = term_ansi.c

CORE_SRC = cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
CORE_NO_IO = cpu.c bus.c aci.c krusader.c disasm.c dbg.c

bin_PROGRAMS = apple1

apple1_SOURCES = main.c cli_config.c $(CORE_SRC) term.c port.c \
                 cpu.h bus.h io.h aci.h krusader.h disasm.h dbg.h term_apple1.h \
                 cli_config.h embedded_roms.h port.h apple1limit.h
apple1_CFLAGS = $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP

# Stripped optimized build target
apple1-opt: apple1
	strip apple1

check_PROGRAMS = test_cpu test_aci test_dualram test_bus test_tomharte \
                 test_interrupts test_vfs test_config test_mem
TESTS = $(check_PROGRAMS)

test_cpu_SOURCES = tests/test_cpu.c tests/term_dummy.c $(CORE_SRC) port.c
test_cpu_CFLAGS = $(AM_CFLAGS) -g

test_interrupts_SOURCES = tests/test_interrupts.c tests/term_dummy.c \
                          $(CORE_SRC) port.c
test_interrupts_CFLAGS = $(AM_CFLAGS) -g

test_aci_SOURCES = tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO) port.c
test_aci_CFLAGS = $(AM_CFLAGS) -g

test_dualram_SOURCES = tests/test_dualram.c tests/term_dummy.c bus.c io.c port.c
test_dualram_CFLAGS = $(AM_CFLAGS) -g

test_bus_SOURCES = tests/test_bus.c tests/term_dummy.c $(CORE_SRC) port.c
test_bus_CFLAGS = $(AM_CFLAGS) -g

test_tomharte_SOURCES = tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC) \
                        port.c
test_tomharte_CFLAGS = $(AM_CFLAGS) -g

test_vfs_SOURCES = tests/test_vfs.c tests/term_dummy.c bus.c io.c port.c
test_vfs_CFLAGS = $(AM_CFLAGS) -g

test_config_SOURCES = tests/test_config.c cli_config.c port.c
test_config_CFLAGS = $(AM_CFLAGS) -g

test_mem_SOURCES = tests/test_mem.c port.c
test_mem_CFLAGS = $(AM_CFLAGS) -g

EXTRA_DIST = $(PORT_SHIM) $(TERM_SHIM) port_attrs.h port_stdarg.h \
             port_stdarg_libc.h apple1limit_checks.h \
             Makefile.uw \
             tools/amalgamate.py \
             tests/term_dummy.c tests/test_cpu.c tests/test_aci.c \
             tests/test_dualram.c tests/test_bus.c tests/test_tomharte.c \
             tests/test_interrupts.c tests/test_vfs.c tests/test_config.c \
             tests/test_mem.c

# Functional Test Suite Targets
DORMANN_URL = https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

6502_functional_test.bin:
	curl -L -o $@ $(DORMANN_URL)

test-official: apple1 6502_functional_test.bin
	./apple1 -Hu -F 6502_functional_test.bin

# ---------------------------------------------------------------------------
# Single-file amalgamation
#
#   make single              # HOST=posix (Linux, macOS, *BSD)
# ---------------------------------------------------------------------------

amalgamation:
	python3 tools/amalgamate.py

single: amalgamation
	$(CC) $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP $(AM_CPPFLAGS) apple1.c -o apple1

check-single: amalgamation
	$(CC) $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP $(AM_CPPFLAGS) \
	    apple1.c -o apple1_amal_test
	@echo "PASS: amalgamation links (HOST=posix)"
	@rm -f apple1_amal_test

# ---------------------------------------------------------------------------
# Alternate C compilers (hosted POSIX) — all use portable $(CC) -c rules.
# ---------------------------------------------------------------------------

tcc:
	$(MAKE) clean
	$(MAKE) CC=tcc AM_CFLAGS= CFLAGS=-O2 apple1

pcc:
	$(MAKE) clean
	$(MAKE) CC=pcc apple1

lacc:
	$(MAKE) clean
	$(MAKE) CC=lacc apple1

nwcc:
	$(MAKE) clean
	$(MAKE) CC=nwcc AM_CFLAGS= CFLAGS=-g apple1

antcc:
	$(MAKE) clean
	$(MAKE) CC=antcc apple1

cproc:
	$(MAKE) clean
	$(MAKE) CC=cproc AM_CFLAGS= CFLAGS="-D__cproc__ -g -O2" apple1

cparser:
	$(MAKE) clean
	$(MAKE) CC=cparser apple1

chibicc:
	$(MAKE) clean
	$(MAKE) CC=chibicc AM_CFLAGS= CFLAGS="-g" apple1

ccomp:
	$(MAKE) clean
	$(MAKE) CC=ccomp AM_CFLAGS="-std=c99 -Wall" apple1



# ---------------------------------------------------------------------------
# Minimal feature POSIX build (omit flags only — CPU, bus, I/O, terminal, Wozmon)
#
#   make minimal
#
# Output: ./apple1_minimal
# ---------------------------------------------------------------------------

MINIMAL_CFLAGS = -Wall -Wextra -std=c89 -Os \
	-DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI \
	-DAPPLE1_OMIT_CHARMAP \
	-DAPPLE1_OMIT_DEBUGGER \
	-DAPPLE1_OMIT_ACI \
	-DAPPLE1_OMIT_KRUSADER \
	-DAPPLE1_OMIT_PASTE \
	-DAPPLE1_OMIT_PIA_THROTTLE \
	-DAPPLE1_OMIT_KBD_BOUNCE \
	-DAPPLE1_OMIT_DISKIO \
	-DAPPLE1_OMIT_BUS_ACCESS_CB \
	-DAPPLE1_ZERO_MALLOC

MINIMAL_OBJS = minimal-cpu.o minimal-bus.o minimal-io.o minimal-cli_config.o \
	minimal-term.o minimal-port.o minimal-main.o

minimal: apple1_minimal

apple1_minimal: $(MINIMAL_OBJS)
	@echo "Linking apple1_minimal (minimal omit-flag build)..."
	$(CC) -o apple1_minimal $(MINIMAL_OBJS)
	strip apple1_minimal
	@echo "Done: ./apple1_minimal"

minimal-cpu.o: cpu.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ cpu.c

minimal-bus.o: bus.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ bus.c

minimal-io.o: io.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ io.c

minimal-cli_config.o: cli_config.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ cli_config.c

minimal-term.o: term.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ term.c

minimal-port.o: port.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ port.c

minimal-main.o: main.c
	$(CC) $(MINIMAL_CFLAGS) -c -o $@ main.c




clean-local:
	-rm -f apple1.c apple1.h apple1.exe
	-rm -f apple1_amal_test 6502_functional_test.bin

distclean-local:
	-rm -f Makefile.in aclocal.m4 configure
	-rm -rf autom4te.cache

# clang-format — uses .clang-format (KNF / style(9)).  Skips generated amalgamation
# and port.c (nested #include "*.c" shims make clang-format hang).
CLANG_FORMAT ?= clang-format

CLANG_FORMAT_SKIP = ! -name apple1.c ! -name apple1.h ! -name port.c

clang-format:
	@find . -type f \( -name '*.c' -o -name '*.h' \) \
	    $(CLANG_FORMAT_SKIP) ! -path './.git/*' -print0 | \
	    while IFS= read -r -d '' f; do \
	        printf 'clang-format %s\n' "$$f"; \
	        $(CLANG_FORMAT) -i "$$f"; \
	    done

clang-format-check:
	@find . -type f \( -name '*.c' -o -name '*.h' \) \
	    $(CLANG_FORMAT_SKIP) ! -path './.git/*' -print0 | \
	    while IFS= read -r -d '' f; do \
	        printf 'clang-format-check %s\n' "$$f"; \
	        $(CLANG_FORMAT) --dry-run -Werror "$$f" || exit 1; \
	    done


# port.c / term.c #include shim sources — explicit rules (automake cannot
# express included-file dependencies without overriding its own recipes).
apple1-port.o: port.c $(PORT_SHIM) port_attrs.h port_stdarg.h \
                port_stdarg_libc.h apple1limit_checks.h \
                port_posix_inc.h
	$(AM_V_CC)$(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	    $(CPPFLAGS) $(apple1_CFLAGS) $(CFLAGS) -c -o apple1-port.o \
	    `test -f 'port.c' || echo '$(srcdir)/'`port.c

apple1-term.o: term.c $(TERM_SHIM) term_apple1.h
	$(AM_V_CC)$(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	    $(CPPFLAGS) $(apple1_CFLAGS) $(CFLAGS) -c -o apple1-term.o \
	    `test -f 'term.c' || echo '$(srcdir)/'`term.c

.PHONY: test-official amalgamation single check-single \
        tcc pcc lacc nwcc antcc cproc cparser chibicc ccomp apple1-opt \
        clang-format clang-format-check minimal
