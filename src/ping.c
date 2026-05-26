/*
 * ping.c - raw-API ICMP echo originator (see ping.h).
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/ip.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/tcpip.h"
#include "lwip/prot/ip4.h"

#include "ping.h"

#define PING_DATA_SIZE      32
#define PING_TIMEOUT_MS     1000
#define PING_INTERVAL_MS    1000

static struct raw_pcb *  s_pcb;
static SemaphoreHandle_t s_sem;
static u16_t             s_id;
static volatile u16_t    s_reply_seq;

struct ping_args
{
    ip4_addr_t target;
    int        count;
};

/* Runs in the tcpip thread.  Return 1 to consume the pbuf, 0 to pass it on. */
static u8_t ping_recv( void * arg,
                       struct raw_pcb * pcb,
                       struct pbuf * p,
                       const ip_addr_t * addr )
{
    struct ip_hdr iphdr;
    struct icmp_echo_hdr iecho;
    u16_t hlen;

    LWIP_UNUSED_ARG( arg );
    LWIP_UNUSED_ARG( pcb );
    LWIP_UNUSED_ARG( addr );

    if( p->tot_len < sizeof( iphdr ) )
    {
        return 0;
    }

    pbuf_copy_partial( p, &iphdr, sizeof( iphdr ), 0 );
    hlen = ( u16_t ) ( IPH_HL( &iphdr ) * 4 );

    if( ( p->tot_len < hlen + sizeof( iecho ) ) ||
        ( pbuf_copy_partial( p, &iecho, sizeof( iecho ), hlen ) != sizeof( iecho ) ) )
    {
        return 0;
    }

    if( ( ICMPH_TYPE( &iecho ) == ICMP_ER ) && ( iecho.id == lwip_htons( s_id ) ) )
    {
        s_reply_seq = lwip_ntohs( iecho.seqno );
        xSemaphoreGive( s_sem );
        pbuf_free( p );
        return 1; /* consumed our reply */
    }

    return 0; /* let the core handle e.g. incoming echo requests */
}

static err_t ping_send( const ip_addr_t * dst,
                        u16_t seq )
{
    struct pbuf * p;
    struct icmp_echo_hdr * iecho;
    size_t len = sizeof( struct icmp_echo_hdr ) + PING_DATA_SIZE;
    err_t err;

    p = pbuf_alloc( PBUF_IP, ( u16_t ) len, PBUF_RAM );
    if( p == NULL )
    {
        return ERR_MEM;
    }

    iecho = ( struct icmp_echo_hdr * ) p->payload;
    ICMPH_TYPE_SET( iecho, ICMP_ECHO );
    ICMPH_CODE_SET( iecho, 0 );
    iecho->chksum = 0;
    iecho->id = lwip_htons( s_id );
    iecho->seqno = lwip_htons( seq );

    for( size_t i = 0; i < PING_DATA_SIZE; i++ )
    {
        ( ( char * ) iecho )[ sizeof( struct icmp_echo_hdr ) + i ] =
            ( char ) ( 'a' + ( i % 26 ) );
    }

    iecho->chksum = inet_chksum( iecho, ( u16_t ) len );

    LOCK_TCPIP_CORE();
    err = raw_sendto( s_pcb, p, dst );
    UNLOCK_TCPIP_CORE();

    pbuf_free( p );
    return err;
}

static void ping_task( void * arg )
{
    struct ping_args * pa = arg;
    ip_addr_t dst;
    int sent = 0, recvd = 0;

    ip_addr_copy_from_ip4( dst, pa->target );

    s_sem = xSemaphoreCreateBinary();
    s_id = ( u16_t ) ( xTaskGetTickCount() ^ 0x4f4bu );

    LOCK_TCPIP_CORE();
    s_pcb = raw_new( IP_PROTO_ICMP );
    if( s_pcb != NULL )
    {
        raw_recv( s_pcb, ping_recv, NULL );
        raw_bind( s_pcb, IP_ADDR_ANY );
    }
    UNLOCK_TCPIP_CORE();

    if( ( s_pcb == NULL ) || ( s_sem == NULL ) )
    {
        printf( "ping: setup failed\n" );
        vTaskDelete( NULL );
        return;
    }

    printf( "ping %s: %d request(s)\n", ip4addr_ntoa( &pa->target ), pa->count );

    for( int seq = 1; seq <= pa->count; seq++ )
    {
        TickType_t t0 = xTaskGetTickCount();

        if( ping_send( &dst, ( u16_t ) seq ) != ERR_OK )
        {
            printf( "ping: send failed (seq=%d)\n", seq );
        }
        else
        {
            sent++;

            if( xSemaphoreTake( s_sem, pdMS_TO_TICKS( PING_TIMEOUT_MS ) ) == pdTRUE )
            {
                TickType_t rtt = xTaskGetTickCount() - t0;
                recvd++;
                printf( "ping: reply from %s: seq=%u time=%lu ms\n",
                        ip4addr_ntoa( &pa->target ),
                        ( unsigned ) s_reply_seq,
                        ( unsigned long ) ( rtt * portTICK_PERIOD_MS ) );
            }
            else
            {
                printf( "ping: timeout (seq=%d)\n", seq );
            }
        }

        vTaskDelay( pdMS_TO_TICKS( PING_INTERVAL_MS ) );
    }

    printf( "ping: %d sent, %d received, %d%% loss\n",
            sent, recvd, sent ? ( 100 * ( sent - recvd ) / sent ) : 0 );

    LOCK_TCPIP_CORE();
    raw_remove( s_pcb );
    s_pcb = NULL;
    UNLOCK_TCPIP_CORE();

    vSemaphoreDelete( s_sem );
    vPortFree( pa );
    vTaskDelete( NULL );
}

void ping_start( const ip4_addr_t * target,
                 int count )
{
    struct ping_args * pa = pvPortMalloc( sizeof( *pa ) );

    if( pa == NULL )
    {
        return;
    }

    ip4_addr_copy( pa->target, *target );
    pa->count = ( count > 0 ) ? count : 4;

    xTaskCreate( ping_task, "ping", configMINIMAL_STACK_SIZE * 4, pa,
                 tskIDLE_PRIORITY + 3, NULL );
}
