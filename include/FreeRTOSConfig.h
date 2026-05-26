/*
 * FreeRTOS configuration for the POSIX/Linux simulator port.
 *
 * This build runs the FreeRTOS kernel as an ordinary x86_64 Linux process
 * (each task is a pthread).  It is the functional-test target for the qeneth
 * lab; the eventual silicon target (Cortex-M7 / NXP S32K3) will get its own
 * FreeRTOSConfig.h.  See README.md.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* The POSIX port has no real clock; this value is unused by the tick source
 * (SIGALRM at configTICK_RATE_HZ) but some code references it. */
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 1000000000 )

/* 1 kHz tick gives 1 ms resolution, comfortable for networking timeouts. */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )

/* The POSIX port defines TickType_t as unsigned long (64-bit here); this macro
 * only needs to agree for FreeRTOS.h's validation and helper macros. */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ( 256 )      /* words */
#define configMAX_TASK_NAME_LEN                 16
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               16
#define configUSE_NEWLIB_REENTRANT              0
/* The bundled lwIP FreeRTOS port (contrib) uses the legacy portTICK_RATE_MS
 * name, which only exists when backward compatibility is enabled. */
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configSTACK_DEPTH_TYPE                  size_t

/* Memory allocation.  heap_4 with a generous pool (this is a hosted process);
 * the kernel heap only backs task stacks/queues/etc - lwIP keeps its own
 * pools.  Using heap_4 keeps behaviour close to what the MCU build will use. */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 256 * 1024 ) )
#define configHEAP_CLEAR_MEMORY_ON_FREE         1

/* Software timers. */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )
#define configTIMER_QUEUE_LENGTH                16

/* Hooks. */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configUSE_MALLOC_FAILED_HOOK            1
/* The POSIX port runs tasks on pthread stacks, not the depth passed to
 * xTaskCreate, so kernel stack-overflow checking is not meaningful here. */
#define configCHECK_FOR_STACK_OVERFLOW          0

/* Run-time stats / trace - off for now. */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Co-routines - unused. */
#define configUSE_CO_ROUTINES                   0

/* API inclusion. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xSemaphoreGetMutexHolder        1

/* configASSERT() routes to a printing handler (defined in main.c). */
extern void vAssertCalled( const char * pcFile, int iLine );
#define configASSERT( x )    do { if( ( x ) == 0 ) { vAssertCalled( __FILE__, __LINE__ ); } } while( 0 )

#endif /* FREERTOS_CONFIG_H */
