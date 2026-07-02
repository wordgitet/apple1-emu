AM_CPPFLAGS = -DAPPLE1_PORT_POSIX -DAPPLE1_TERM_ANSI
AM_CFLAGS = -Wall -Wextra -std=c89

# Included by port.c / term.c — not separate translation units.
PORT_SHIM = port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
            port_freertos.c port_zephyr.c port_bare.c port_os2.c \
            port_vxworks.c port_tcc_va.c

TERM_SHIM = term_ansi.c term_dos.c term_vt100.c

CORE_SRC = cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
CORE_NO_IO = cpu.c bus.c aci.c krusader.c disasm.c dbg.c

bin_PROGRAMS = apple1

apple1_SOURCES = main.c cli_config.c $(CORE_SRC) term.c port.c \
                 cpu.h bus.h io.h aci.h krusader.h disasm.h dbg.h term_apple1.h \
                 cli_config.h embedded_roms.h port.h apple1limit.h
apple1_CFLAGS = $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP

check_PROGRAMS = test_cpu test_aci test_dualram test_bus test_tomharte \
                 test_interrupts test_vfs test_config test_mem
TESTS = $(check_PROGRAMS)

test_cpu_SOURCES = tests/test_cpu.c tests/term_dummy.c $(CORE_SRC) port.c
test_cpu_CFLAGS = $(AM_CFLAGS)

test_interrupts_SOURCES = tests/test_interrupts.c tests/term_dummy.c \
                          $(CORE_SRC) port.c
test_interrupts_CFLAGS = $(AM_CFLAGS)

test_aci_SOURCES = tests/test_aci.c tests/term_dummy.c $(CORE_NO_IO) port.c
test_aci_CFLAGS = $(AM_CFLAGS)

test_dualram_SOURCES = tests/test_dualram.c tests/term_dummy.c bus.c io.c port.c
test_dualram_CFLAGS = $(AM_CFLAGS)

test_bus_SOURCES = tests/test_bus.c tests/term_dummy.c $(CORE_SRC) port.c
test_bus_CFLAGS = $(AM_CFLAGS)

test_tomharte_SOURCES = tests/test_tomharte.c tests/term_dummy.c $(CORE_SRC) \
                        port.c
test_tomharte_CFLAGS = $(AM_CFLAGS)

test_vfs_SOURCES = tests/test_vfs.c tests/term_dummy.c bus.c io.c port.c
test_vfs_CFLAGS = $(AM_CFLAGS)

test_config_SOURCES = tests/test_config.c cli_config.c port.c
test_config_CFLAGS = $(AM_CFLAGS)

test_mem_SOURCES = tests/test_mem.c port.c
test_mem_CFLAGS = $(AM_CFLAGS)

EXTRA_DIST = $(PORT_SHIM) $(TERM_SHIM) port_attrs.h port_stdarg.h \
             port_stdarg_libc.h port_stdarg_plan9.h apple1limit_checks.h \
             port_posix_inc.h mkfile Makefile.msc tools/amalgamate.py \
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
# Single-file amalgamation and cross-builds
#
#   make single              # HOST=posix (Linux, macOS, *BSD)
#   make single HOST=dos       # MS-DOS via DJGPP cross-compiler
#   make single HOST=watcom    # MS-DOS via Open Watcom
#   make single HOST=win       # Windows via MinGW cross-compiler
#
# Plan 9 / 9front: use mkfile (native multi-file 6c build) or mk amalg (no Python).
# ---------------------------------------------------------------------------

HOST ?= posix

amalgamation:
	python3 tools/amalgamate.py

amalgamation-dos:
	python3 tools/amalgamate.py --port port_msdos.c --term term_dos.c

amalgamation-win:
	python3 tools/amalgamate.py --port port_win.c --term term_ansi.c

amalgamation-plan9:
	python3 tools/amalgamate.py --port port_plan9.c --term term_vt100.c

single:
	@case "$(HOST)" in \
	posix) $(MAKE) single-posix ;; \
	dos|djgpp) $(MAKE) single-dos ;; \
	watcom) $(MAKE) single-watcom ;; \
	win|mingw) $(MAKE) single-win ;; \
	plan9|9front) $(MAKE) single-plan9 ;; \
	*) echo "Unknown HOST=$(HOST). Use: posix dos watcom win plan9" >&2; exit 1 ;; \
	esac

single-posix: amalgamation
	$(CC) $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP $(AM_CPPFLAGS) apple1.c -o apple1

single-dos: amalgamation-dos
	$(DOS_CC) $(DOS_CFLAGS) -o apple1.exe apple1.c

single-watcom: dos-watcom

WIN_CC ?= x86_64-w64-mingw32-gcc
WIN_CFLAGS = -O2 -Wall -Wextra -std=c89 -DAPPLE1_OMIT_CHARMAP \
             -DAPPLE1_PORT_WIN -DAPPLE1_TERM_ANSI

single-win: amalgamation-win
	$(WIN_CC) $(WIN_CFLAGS) -o apple1.exe apple1.c

# Plan 9 APIs — amalgamate on POSIX, compile on Plan 9 with 6c (see mkfile).
single-plan9: amalgamation-plan9
	@echo "Plan 9 amalgamation: apple1.c + apple1.h"
	@echo "On Plan 9: mk amalg && mk amal-single   (no Python)"
	@echo "Or: 6c -DAPPLE1_OMIT_CHARMAP -DAPPLE1_PORT_PLAN9 -DAPPLE1_TERM_VT100 apple1.c"

check-single:
	@$(MAKE) amalgamation
	$(CC) $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP $(AM_CPPFLAGS) \
	    apple1.c -o apple1_amal_test
	@echo "PASS: amalgamation links (HOST=posix)"
	@rm -f apple1_amal_test

DOS_CC ?= i586-pc-msdosdjgpp-gcc
DOS_CFLAGS = -O2 -Wall -std=gnu99 -D__MSDOS__ -DAPPLE1_OMIT_CHARMAP \
             -DAPPLE1_PORT_MSDOS -DAPPLE1_TERM_DOS -march=i386 -mtune=i386

dos-djgpp: cwsdpmi.exe amalgamation-dos
	$(DOS_CC) $(DOS_CFLAGS) -o apple1.exe apple1.c

WATCOM        ?= $(HOME)/watcom
WATCOM_CC     ?= wcl386
WATCOM_CFLAGS ?= -bt=dos -ox -zq -w3 -fe=apple1.exe -l=causeway
WATCOM_DEFS   = -dMSDOS -dAPPLE1_OMIT_CHARMAP -dAPPLE1_PORT_MSDOS \
                  -dAPPLE1_TERM_DOS

dos-watcom: amalgamation-dos
	@watcom_bindir="$(WATCOM)/binl64"; \
	watcom_stubdir="$(WATCOM)/binw"; \
	case "$$(uname -s 2>/dev/null)" in \
	Darwin) watcom_bindir="$(WATCOM)/bino64";; \
	MINGW*|MSYS*|CYGWIN*) watcom_bindir="$(WATCOM)/binnt";; \
	esac; \
	test -x "$$watcom_bindir/$(WATCOM_CC)" || \
	    (echo "Error: $$watcom_bindir/$(WATCOM_CC) not found." >&2; \
	     echo "Set WATCOM=... if needed." >&2; \
	     exit 1); \
	test -f "$$watcom_stubdir/cwstub.exe" || \
	    (echo "Error: $$watcom_stubdir/cwstub.exe not found (CauseWay stub)." >&2; \
	     exit 1); \
	WATCOM="$(WATCOM)" \
	    INCLUDE="$(WATCOM)/h" \
	    LIB="$(WATCOM)/lib386/dos" \
	    PATH="$$watcom_bindir:$$watcom_stubdir:$$PATH" \
	    $(WATCOM_CC) $(WATCOM_CFLAGS) $(WATCOM_DEFS) apple1.c

apple1-watcom.exe: dos-watcom

apple1-djgpp.exe: dos-djgpp

cwsdpmi.exe:
	curl -L -o cwsdpmi.zip https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip
	unzip -o cwsdpmi.zip cwsdpmi.exe
	rm -f cwsdpmi.zip

apple1.exe: dos-djgpp

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

# ---------------------------------------------------------------------------
# FreeRTOS POSIX host build (libc I/O, no FreeRTOS kernel — see FREERTOS_DEMO.md)
# ---------------------------------------------------------------------------

FREERTOS_CFLAGS = -Wall -Wextra -std=c89 -O2 \
	-DAPPLE1_PORT_FREERTOS -DAPPLE1_TERM_ANSI -DAPPLE1_OMIT_CHARMAP \
	-DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER

FREERTOS_OBJS = freertos-cpu.o freertos-bus.o freertos-io.o freertos-cli_config.o \
	freertos-term.o freertos-port.o freertos-main.o

freertos: apple1_freertos

apple1_freertos: $(FREERTOS_OBJS)
	@echo "Linking apple1_freertos (POSIX host, no FreeRTOS kernel)..."
	$(CC) -o apple1_freertos $(FREERTOS_OBJS)
	@echo "Done: ./apple1_freertos (see documentation/FREERTOS_DEMO.md)"

freertos-cpu.o: cpu.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ cpu.c

freertos-bus.o: bus.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ bus.c

freertos-io.o: io.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ io.c

freertos-cli_config.o: cli_config.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ cli_config.c

freertos-term.o: term.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ term.c

freertos-port.o: port.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ port.c

freertos-main.o: main.c
	$(CC) $(FREERTOS_CFLAGS) -c -o $@ main.c


clean-local:
	-rm -f apple1.c apple1.h apple1.exe apple1-djgpp.exe apple1-watcom.exe
	-rm -f apple1_amal_test cwsdpmi.exe cwsdpmi.zip 6502_functional_test.bin
	-rm -f apple1_freertos freertos-*.o
	-rm -f apple1.vxe apple1_full.vxe
	-rm -f /tmp/apple1-vx-*.o

distclean-local:
	-rm -f Makefile.in aclocal.m4 configure
	-rm -rf autom4te.cache


# port.c / term.c #include shim sources — explicit rules (automake cannot
# express included-file dependencies without overriding its own recipes).
apple1-port.o: port.c $(PORT_SHIM) port_attrs.h port_stdarg.h \
                port_stdarg_libc.h port_stdarg_plan9.h apple1limit_checks.h \
                port_posix_inc.h
	$(AM_V_CC)$(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	    $(CPPFLAGS) $(apple1_CFLAGS) $(CFLAGS) -c -o apple1-port.o \
	    `test -f 'port.c' || echo '$(srcdir)/'`port.c

apple1-term.o: term.c $(TERM_SHIM) term_apple1.h
	$(AM_V_CC)$(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	    $(CPPFLAGS) $(apple1_CFLAGS) $(CFLAGS) -c -o apple1-term.o \
	    `test -f 'term.c' || echo '$(srcdir)/'`term.c

.PHONY: test-official amalgamation amalgamation-dos amalgamation-win \
        single single-posix single-dos single-watcom single-win check-single \
        dos-djgpp dos-watcom apple1-watcom.exe apple1-djgpp.exe apple1.exe \
        cwsdpmi.exe tcc pcc lacc freertos
