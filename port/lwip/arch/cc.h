/*
 * lwIP compiler/platform abstraction for the hosted (gcc) simulator build.
 * Most type definitions come from lwip/arch.h via <stdint.h>; this file only
 * supplies the few hooks lwIP expects the port to provide.
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>

/* Pull errno from the C library (we do not use lwIP sockets). */
#define LWIP_ERRNO_STDINCLUDE   1

/* PRNG for lwIP (TCP ISN, MQTT client-id entropy, ...); seeded in main(). */
#define LWIP_RAND()             ( ( u32_t ) rand() )

/* Formatted diagnostics and assertions, routed to stdout for the simulator. */
#define LWIP_PLATFORM_DIAG( x )    do { printf x; } while( 0 )
#define LWIP_PLATFORM_ASSERT( x )                                          \
    do {                                                                   \
        printf( "lwIP assert \"%s\" failed at %s:%d\n",                    \
                ( x ), __FILE__, __LINE__ );                               \
        fflush( NULL );                                                    \
        abort();                                                           \
    } while( 0 )

#endif /* LWIP_ARCH_CC_H */
