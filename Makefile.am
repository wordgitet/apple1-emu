AM_CFLAGS = -Wall -Wextra -std=c89 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600

bin_PROGRAMS = apple1

# Platform-specific port file selection
if TARGET_POSIX
PORT_SRC = port_posix.c
TERM_SRC = term_ansi.c
else
if TARGET_DOS
PORT_SRC = port_msdos.c
TERM_SRC = term_dos.c
else
if TARGET_WINDOWS
PORT_SRC = port_win.c
TERM_SRC = term_ansi.c
else
if TARGET_FREERTOS
PORT_SRC = port_freertos.c
TERM_SRC = term_ansi.c
else
if TARGET_BARE
PORT_SRC = port_bare.c
TERM_SRC = term_ansi.c
else
if TARGET_ELKS
PORT_SRC = port_elks.c
TERM_SRC = term_ansi.c
else
if TARGET_OS2
PORT_SRC = port_os2.c
TERM_SRC = term_ansi.c
else
if TARGET_PLAN9
PORT_SRC = port_plan9.c
TERM_SRC = term_ansi.c
else
if TARGET_VXWORKS
PORT_SRC = port_vxworks.c
TERM_SRC = term_ansi.c
else
if TARGET_ZEPHYR
PORT_SRC = port_zephyr.c
TERM_SRC = term_ansi.c
else
PORT_SRC = port_posix.c
TERM_SRC = term_ansi.c
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif

apple1_SOURCES = main.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c $(TERM_SRC) \
                 cpu.h bus.h io.h aci.h krusader.h disasm.h dbg.h term_apple1.h \
                 embedded_roms.h port.h port_string.c $(PORT_SRC)
apple1_CFLAGS = $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP
apple1_LDADD =

check_PROGRAMS = test_cpu test_aci test_dualram test_bus test_tomharte test_interrupts
TESTS = $(check_PROGRAMS)

test_cpu_SOURCES = tests/test_cpu.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port_string.c $(PORT_SRC)
test_cpu_CFLAGS = $(AM_CFLAGS)

test_interrupts_SOURCES = tests/test_interrupts.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port_string.c $(PORT_SRC)
test_interrupts_CFLAGS = $(AM_CFLAGS)

test_aci_SOURCES = tests/test_aci.c tests/term_dummy.c cpu.c bus.c aci.c krusader.c disasm.c dbg.c port_string.c $(PORT_SRC)
test_aci_CFLAGS = $(AM_CFLAGS)

test_dualram_SOURCES = tests/test_dualram.c tests/term_dummy.c bus.c io.c port_string.c $(PORT_SRC)
test_dualram_CFLAGS = $(AM_CFLAGS)

test_bus_SOURCES = tests/test_bus.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port_string.c $(PORT_SRC)
test_bus_CFLAGS = $(AM_CFLAGS)

test_tomharte_SOURCES = tests/test_tomharte.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port_string.c $(PORT_SRC)
test_tomharte_CFLAGS = $(AM_CFLAGS)

# Functional Test Suite Targets
DORMANN_URL = https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

6502_functional_test.bin:
	curl -L -o $@ $(DORMANN_URL)

.PHONY: test-official

test-official: apple1 6502_functional_test.bin
	./apple1 -H -F 6502_functional_test.bin
