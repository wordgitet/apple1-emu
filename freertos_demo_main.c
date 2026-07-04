/*
 * FreeRTOS demo main for Apple-1 emulator
 * Defines all required FreeRTOS hooks and main entry point
 */

#include "FreeRTOS.h"
#include "console.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>

/* Forward declare the actual emulator main */
extern int
emulator_main(int argc, char *argv[]);

/* FreeRTOS hooks */
void
vApplicationIdleHook(void)
{
}

void
vApplicationTickHook(void)
{
}

void
vApplicationDaemonTaskStartupHook(void)
{
}

void
vAssertCalled(const char *const file, unsigned long line)
{
	printf("ASSERT: %s:%lu\n", file, line);
	abort();
}

void
vApplicationMallocFailedHook(void)
{
	printf("MALLOC FAILED\n");
	abort();
}

void
vApplicationGetIdleTaskMemory(StaticTask_t **ppxTCB,
    StackType_t **ppxStack,
    uint32_t *pulSize)
{
	static StaticTask_t xTCB;
	static StackType_t xStack[16384];

	*ppxTCB = &xTCB;
	*ppxStack = xStack;
	*pulSize = 16384;
}

void
vApplicationGetTimerTaskMemory(StaticTask_t **ppxTCB,
    StackType_t **ppxStack,
    uint32_t *pulSize)
{
	static StaticTask_t xTCB;
	static StackType_t xStack[16384];

	*ppxTCB = &xTCB;
	*ppxStack = xStack;
	*pulSize = 16384;
}

static void
apple1_task(void *pvParameters)
{
	char *argv[1];

	(void)pvParameters;
	argv[0] = "apple1";
	printf("Apple-1 FreeRTOS task started\n");
	emulator_main(1, argv);
	vTaskDelete(NULL);
}

int
main(void)
{
	printf("Starting Apple-1 FreeRTOS demo\n");
	console_init();
	xTaskCreate(apple1_task,
	    "Apple1",
	    8192,
	    NULL,
	    tskIDLE_PRIORITY + 2,
	    NULL);
	vTaskStartScheduler();
	for (;;)
		;
	return (0);
}
