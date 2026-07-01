#!/bin/bash
# Build Apple-1 inside the FreeRTOS Posix_GCC simulator.
#
# Usage:
#   cd /path/to/FreeRTOS/FreeRTOS/Demo/Posix_GCC
#   export FREERTOS_DIR=/path/to/FreeRTOS/FreeRTOS
#   export APPLE1_DIR=/path/to/apple1-emu
#   bash /path/to/apple1-emu/freertos_demo_test.sh

FREERTOS_DIR="${FREERTOS_DIR:-../../../FreeRTOS/FreeRTOS}"
APPLE1_DIR="${APPLE1_DIR:-../../..}"

if [ ! -d "$FREERTOS_DIR" ]; then
	echo "ERROR: FreeRTOS not found at $FREERTOS_DIR"
	echo "Set FREERTOS_DIR environment variable"
	exit 1
fi

if [ ! -d "$APPLE1_DIR" ]; then
	echo "ERROR: Apple-1 emulator not found at $APPLE1_DIR"
	echo "Set APPLE1_DIR environment variable"
	exit 1
fi

gcc -Wall -Wextra -std=gnu89 -O2 \
	-I. \
	-I"${FREERTOS_DIR}/Source/include" \
	-I"${FREERTOS_DIR}/Source/portable/ThirdParty/GCC/Posix" \
	-I"${FREERTOS_DIR}/Source/portable/ThirdParty/GCC/Posix/utils" \
	-I"$APPLE1_DIR" \
	-I"${FREERTOS_DIR}/Demo/Posix_GCC/Common/include" \
	-DAPPLE1_PORT_FREERTOS -DAPPLE1_TERM_ANSI -DAPPLE1_OMIT_CHARMAP \
	-DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER \
	-DprojENABLE_TRACING=0 -DprojCOVERAGE_TEST=0 \
	-DFREERTOS_DEMO \
	"${FREERTOS_DIR}/Source/croutine.c" \
	"${FREERTOS_DIR}/Source/list.c" \
	"${FREERTOS_DIR}/Source/queue.c" \
	"${FREERTOS_DIR}/Source/tasks.c" \
	"${FREERTOS_DIR}/Source/timers.c" \
	"${FREERTOS_DIR}/Source/portable/ThirdParty/GCC/Posix/port.c" \
	"${FREERTOS_DIR}/Source/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c" \
	"${FREERTOS_DIR}/Source/portable/MemMang/heap_4.c" \
	console.c \
	"$APPLE1_DIR"/freertos_demo_main.c \
	"$APPLE1_DIR"/main.c \
	"$APPLE1_DIR"/cpu.c \
	"$APPLE1_DIR"/bus.c \
	"$APPLE1_DIR"/io.c \
	"$APPLE1_DIR"/cli_config.c \
	"$APPLE1_DIR"/term.c \
	"$APPLE1_DIR"/port.c \
	-o apple1_test \
	-lpthread -lrt

if [ $? -eq 0 ]; then
	echo "Build successful: ./apple1_test"
	echo "Run with: ./apple1_test"
else
	echo "Build failed"
	exit 1
fi
