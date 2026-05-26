/*
 * frtos-dev - FreeRTOS MQTT end-device for the qeneth test lab.
 *
 * Milestone 1: prove the POSIX-simulator build runs the scheduler with a
 * couple of tasks.  Networking (lwIP) and MQTT arrive in later milestones.
 */
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#define HELLO_TASK_PRIORITY     ( tskIDLE_PRIORITY + 1 )
#define WORKER_TASK_PRIORITY    ( tskIDLE_PRIORITY + 1 )

/* Written by the worker task, read by the hello task - kept on one task for
 * printing because the POSIX port requires stdio to come from a single task. */
static volatile uint32_t g_worker_ticks;

static void worker_task( void * arg )
{
    ( void ) arg;

    for( ; ; )
    {
        g_worker_ticks++;
        vTaskDelay( pdMS_TO_TICKS( 250 ) );
    }
}

static void hello_task( void * arg )
{
    uint32_t n = 0;

    ( void ) arg;

    for( ; ; )
    {
        printf( "[frtos-dev] hello #%u  (worker=%u, uptime=%lu ms)\n",
                ( unsigned ) n++,
                ( unsigned ) g_worker_ticks,
                ( unsigned long ) ( xTaskGetTickCount() * portTICK_PERIOD_MS ) );
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

int main( void )
{
    /* Unbuffered so output shows up promptly when piped through qeneth. */
    setvbuf( stdout, NULL, _IONBF, 0 );

    printf( "frtos-dev: FreeRTOS kernel %s, POSIX simulator\n",
            tskKERNEL_VERSION_NUMBER );

    if( xTaskCreate( hello_task, "hello", configMINIMAL_STACK_SIZE * 2,
                     NULL, HELLO_TASK_PRIORITY, NULL ) != pdPASS ||
        xTaskCreate( worker_task, "worker", configMINIMAL_STACK_SIZE * 2,
                     NULL, WORKER_TASK_PRIORITY, NULL ) != pdPASS )
    {
        fprintf( stderr, "frtos-dev: failed to create tasks\n" );
        return EXIT_FAILURE;
    }

    vTaskStartScheduler();

    /* vTaskStartScheduler() only returns on insufficient heap. */
    fprintf( stderr, "frtos-dev: scheduler returned - out of heap?\n" );
    return EXIT_FAILURE;
}

/*-----------------------------------------------------------*/
/* FreeRTOS application hooks. */

void vApplicationMallocFailedHook( void )
{
    fprintf( stderr, "frtos-dev: pvPortMalloc() failed - heap exhausted\n" );
    abort();
}

void vAssertCalled( const char * pcFile, int iLine )
{
    fprintf( stderr, "frtos-dev: assertion failed at %s:%d\n", pcFile, iLine );
    fflush( stderr );
    abort();
}
