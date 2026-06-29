AM_CFLAGS = -Wall -Wextra -std=c89

bin_PROGRAMS = apple1

apple1_SOURCES = main.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c term.c port.c \
                 cpu.h bus.h io.h aci.h krusader.h disasm.h dbg.h term_apple1.h \
                 embedded_roms.h port.h \
                 port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                 port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                 port_vxworks.c term_ansi.c term_dos.c
apple1_CFLAGS = $(AM_CFLAGS) -DAPPLE1_OMIT_CHARMAP
apple1_LDADD =

check_PROGRAMS = test_cpu test_aci test_dualram test_bus test_tomharte test_interrupts
TESTS = $(check_PROGRAMS)

test_cpu_SOURCES = tests/test_cpu.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port.c \
                   port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                   port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                   port_vxworks.c
test_cpu_CFLAGS = $(AM_CFLAGS)

test_interrupts_SOURCES = tests/test_interrupts.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port.c \
                         port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                         port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                         port_vxworks.c
test_interrupts_CFLAGS = $(AM_CFLAGS)

test_aci_SOURCES = tests/test_aci.c tests/term_dummy.c cpu.c bus.c aci.c krusader.c disasm.c dbg.c port.c \
                   port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                   port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                   port_vxworks.c
test_aci_CFLAGS = $(AM_CFLAGS)

test_dualram_SOURCES = tests/test_dualram.c tests/term_dummy.c bus.c io.c port.c \
                       port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                       port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                       port_vxworks.c
test_dualram_CFLAGS = $(AM_CFLAGS)

test_bus_SOURCES = tests/test_bus.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port.c \
                   port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                   port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                   port_vxworks.c
test_bus_CFLAGS = $(AM_CFLAGS)

test_tomharte_SOURCES = tests/test_tomharte.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c port.c \
                       port_string.c port_posix.c port_win.c port_msdos.c port_plan9.c \
                       port_freertos.c port_zephyr.c port_bare.c port_os2.c port_elks.c \
                       port_vxworks.c
test_tomharte_CFLAGS = $(AM_CFLAGS)

# Functional Test Suite Targets
DORMANN_URL = https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

6502_functional_test.bin:
	curl -L -o $@ $(DORMANN_URL)

.PHONY: test-official

test-official: apple1 6502_functional_test.bin
	./apple1 -H -F 6502_functional_test.bin
