/*
 * frtos-dev - FreeRTOS + lwIP MQTT end-device for the qeneth test lab.
 *
 * The POSIX-simulator build runs as a native x86_64 process and attaches to a
 * qeneth topology over a UDP-socket link (see port/lwip/qeneth_netif.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "net.h"
#include "mqtt_app.h"
#include "test_broker.h"

static struct app_cfg g_cfg;

static void app_task( void * arg )
{
    const struct app_cfg * cfg = arg;

    net_start( cfg );

    if( cfg->is_broker )
    {
        test_broker_start( cfg );
    }
    else
    {
        mqtt_app_start( cfg );
    }

    vTaskDelete( NULL );
}

int main( int argc, char ** argv )
{
    /* Unbuffered so output shows up promptly when piped through qeneth. */
    setvbuf( stdout, NULL, _IONBF, 0 );
    srand( ( unsigned ) ( time( NULL ) ^ getpid() ) );

    app_cfg_defaults( &g_cfg );
    if( app_cfg_parse( &g_cfg, argc, argv ) != 0 )
    {
        return EXIT_FAILURE;
    }

    printf( "frtos-dev: FreeRTOS %s + lwIP, POSIX simulator\n",
            tskKERNEL_VERSION_NUMBER );

    if( xTaskCreate( app_task, "app", configMINIMAL_STACK_SIZE * 4, &g_cfg,
                     tskIDLE_PRIORITY + 2, NULL ) != pdPASS )
    {
        fprintf( stderr, "frtos-dev: failed to create app task\n" );
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
