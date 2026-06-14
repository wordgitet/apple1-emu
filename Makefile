CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600

# Common core objects (excluding terminal backends and main.o)
CORE_OBJS = cpu.o bus.o io.o aci.o krusader.o disasm.o dbg.o

# Binaries
GUI_TARGET = apple1

TEST_OBJS = cpu.o bus.o io.o tests/term_dummy.o aci.o krusader.o disasm.o dbg.o tests/test_cpu.o
TEST_TARGET = tests/test_cpu

ACI_TEST_OBJS = cpu.o bus.o tests/term_dummy.o aci.o krusader.o disasm.o dbg.o tests/test_aci.o
ACI_TEST_TARGET = tests/test_aci

DUALRAM_TEST_OBJS = bus.o io.o tests/term_dummy.o tests/test_dualram.o
DUALRAM_TEST_TARGET = tests/test_dualram

BUS_TEST_OBJS = cpu.o bus.o io.o aci.o krusader.o disasm.o dbg.o tests/term_dummy.o tests/test_bus.o
BUS_TEST_TARGET = tests/test_bus

DORMANN_URL = https://github.com/Klaus2m5/6502_65C02_functional_tests/raw/master/bin_files/6502_functional_test.bin

.PHONY: all clean tests test-official test-illegal

all: $(GUI_TARGET)

$(GUI_TARGET): $(CORE_OBJS) term_sdl3.o main.o
	$(CC) $(CFLAGS) -o $@ $^ $(shell pkg-config --libs sdl3)

term_sdl3.o: term_sdl3.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl3) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/term_dummy.o: tests/term_dummy.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_cpu.o: tests/test_cpu.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_aci.o: tests/test_aci.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_dualram.o: tests/test_dualram.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_bus.o: tests/test_bus.c
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(TEST_TARGET) $(ACI_TEST_TARGET) $(DUALRAM_TEST_TARGET) $(BUS_TEST_TARGET)
	./$(TEST_TARGET)
	./$(ACI_TEST_TARGET)
	./$(DUALRAM_TEST_TARGET)
	./$(BUS_TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS)

$(ACI_TEST_TARGET): $(ACI_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(ACI_TEST_OBJS)

$(DUALRAM_TEST_TARGET): $(DUALRAM_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(DUALRAM_TEST_OBJS)

$(BUS_TEST_TARGET): $(BUS_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(BUS_TEST_OBJS)

test-official: $(GUI_TARGET) 6502_functional_test.bin
	./$(GUI_TARGET) -H --flat-bus 6502_functional_test.bin

test-illegal: $(GUI_TARGET)
	@if [ ! -f 6502_illegal_test.bin ]; then \
		echo "Error: 6502_illegal_test.bin not found."; \
		echo "Please place the Wolfgang Lorenz illegal test binary as '6502_illegal_test.bin' in this directory."; \
		exit 1; \
	fi
	./$(GUI_TARGET) -H --flat-bus 6502_illegal_test.bin

6502_functional_test.bin:
	curl -L -o $@ $(DORMANN_URL)

clean:
	rm -f $(CORE_OBJS) term_sdl3.o main.o $(GUI_TARGET) $(TEST_OBJS) $(TEST_TARGET) $(ACI_TEST_OBJS) $(ACI_TEST_TARGET) $(DUALRAM_TEST_OBJS) $(DUALRAM_TEST_TARGET) $(BUS_TEST_OBJS) $(BUS_TEST_TARGET) 6502_functional_test.bin

