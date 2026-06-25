AM_CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -D_DARWIN_C_SOURCE

bin_PROGRAMS = apple1

apple1_SOURCES = main.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c term_sdl3.c term_config.c term_debug.c \
                 cpu.h bus.h io.h aci.h krusader.h disasm.h dbg.h term_apple1.h term_config.h term_debug.h term_internal.h \
                 embedded_roms.h font5x7.h stb_image_write.h
apple1_CFLAGS = $(AM_CFLAGS) $(SDL3_CFLAGS)
apple1_LDADD = $(SDL3_LIBS)

check_PROGRAMS = test_cpu test_aci test_dualram test_bus test_tomharte
TESTS = $(check_PROGRAMS)

test_cpu_SOURCES = tests/test_cpu.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
test_cpu_CFLAGS = $(AM_CFLAGS)

test_aci_SOURCES = tests/test_aci.c tests/term_dummy.c cpu.c bus.c aci.c krusader.c disasm.c dbg.c
test_aci_CFLAGS = $(AM_CFLAGS)

test_dualram_SOURCES = tests/test_dualram.c tests/term_dummy.c bus.c io.c
test_dualram_CFLAGS = $(AM_CFLAGS)

test_bus_SOURCES = tests/test_bus.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
test_bus_CFLAGS = $(AM_CFLAGS)

test_tomharte_SOURCES = tests/test_tomharte.c tests/term_dummy.c cpu.c bus.c io.c aci.c krusader.c disasm.c dbg.c
test_tomharte_CFLAGS = $(AM_CFLAGS)

# Functional Test Suite Targets
DORMANN_URL = https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

6502_functional_test.bin:
	curl -L -o $@ $(DORMANN_URL)

.PHONY: test-official

test-official: apple1 6502_functional_test.bin
	./apple1 -H --flat-bus 6502_functional_test.bin

