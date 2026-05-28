/*
 * net.c - argument parsing and lwIP/interface bring-up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/autoip.h"

#include "net.h"
#include "ping.h"
#include "qeneth_netif.h"

static struct netif s_netif;

void app_cfg_defaults( struct app_cfg * c )
{
    memset( c, 0, sizeof( *c ) );
    strcpy( c->localaddr, "127.0.0.1:20000" );
    strcpy( c->peeraddr, "127.0.0.1:20001" );
    /* No --ip default: when omitted, the drone claims a 169.254/16 address
     * via AutoIP (RFC 3927).  netmask/gw apply only when --ip is given. */
    strcpy( c->netmask, "255.255.255.0" );
    strcpy( c->gw, "10.0.0.1" );
    /* Locally administered unicast MAC (02:..).  --hostname stays empty so
     * app_cfg_finalize() can derive drone-XXYYZZ from the low MAC bytes. */
    c->mac[ 0 ] = 0x02; c->mac[ 1 ] = 0x00; c->mac[ 2 ] = 0x00;
    c->mac[ 3 ] = 0x00; c->mac[ 4 ] = 0x00; c->mac[ 5 ] = 0x02;
    strcpy( c->broker_ip, "10.0.0.1" );
    c->broker_port = 1883;
    c->ping_count = 4;
}

static int parse_mac( const char * s, uint8_t mac[ 6 ] )
{
    unsigned v[ 6 ];

    if( sscanf( s, "%x:%x:%x:%x:%x:%x",
                &v[ 0 ], &v[ 1 ], &v[ 2 ], &v[ 3 ], &v[ 4 ], &v[ 5 ] ) != 6 )
    {
        return -1;
    }

    for( int i = 0; i < 6; i++ )
    {
        mac[ i ] = ( uint8_t ) v[ i ];
    }

    return 0;
}

static void usage( const char * argv0 )
{
    fprintf( stderr,
             "usage: %s [options]\n"
             "  --localaddr HOST:PORT   bind the link UDP socket here "
             "(default 127.0.0.1:20000)\n"
             "  --udp HOST:PORT         send frames to this peer "
             "(default 127.0.0.1:20001)\n"
             "  --ip ADDR               static IPv4 address (omit -> AutoIP)\n"
             "  --netmask ADDR          (default 255.255.255.0, used with --ip)\n"
             "  --gw ADDR               default gateway (default 10.0.0.1, used with --ip)\n"
             "  --mac XX:XX:XX:XX:XX:XX  interface MAC (default 02:00:00:00:00:02)\n"
             "  --hostname NAME         device id / role (default: drone-XXYYZZ from MAC)\n"
             "  --broker ADDR[:PORT]    MQTT broker (default 10.0.0.1:1883)\n"
             "  --run-broker            act as the built-in test broker instead\n"
             "  --no-mqtt               skip the MQTT client (diagnostics only)\n"
             "  --ping ADDR [COUNT]     send ICMP echo requests after bring-up\n",
             argv0 );
}

int app_cfg_parse( struct app_cfg * c, int argc, char ** argv )
{
    for( int i = 1; i < argc; i++ )
    {
        const char * a = argv[ i ];
        #define NEED_ARG()  do { if( ++i >= argc ) { usage( argv[ 0 ] ); return -1; } } while( 0 )

        if( !strcmp( a, "--localaddr" ) )      { NEED_ARG(); snprintf( c->localaddr, sizeof c->localaddr, "%s", argv[ i ] ); }
        else if( !strcmp( a, "--udp" ) )       { NEED_ARG(); snprintf( c->peeraddr, sizeof c->peeraddr, "%s", argv[ i ] ); }
        else if( !strcmp( a, "--ip" ) )        { NEED_ARG(); snprintf( c->ip, sizeof c->ip, "%s", argv[ i ] ); }
        else if( !strcmp( a, "--netmask" ) )   { NEED_ARG(); snprintf( c->netmask, sizeof c->netmask, "%s", argv[ i ] ); }
        else if( !strcmp( a, "--gw" ) )        { NEED_ARG(); snprintf( c->gw, sizeof c->gw, "%s", argv[ i ] ); }
        else if( !strcmp( a, "--hostname" ) )  { NEED_ARG(); snprintf( c->hostname, sizeof c->hostname, "%s", argv[ i ] ); }
        else if( !strcmp( a, "--mac" ) )       { NEED_ARG(); if( parse_mac( argv[ i ], c->mac ) != 0 ) { fprintf( stderr, "bad --mac\n" ); return -1; } }
        else if( !strcmp( a, "--run-broker" ) ) { c->is_broker = 1; }
        else if( !strcmp( a, "--no-mqtt" ) )    { c->no_mqtt = 1; }
        else if( !strcmp( a, "--broker" ) )
        {
            char * colon;
            NEED_ARG();
            colon = strrchr( argv[ i ], ':' );
            if( colon != NULL ) { *colon = '\0'; c->broker_port = atoi( colon + 1 ); }
            snprintf( c->broker_ip, sizeof c->broker_ip, "%s", argv[ i ] );
        }
        else if( !strcmp( a, "--ping" ) )
        {
            NEED_ARG();
            snprintf( c->ping_target, sizeof c->ping_target, "%s", argv[ i ] );
            c->do_ping = 1;
            if( ( i + 1 < argc ) && ( argv[ i + 1 ][ 0 ] != '-' ) )
            {
                c->ping_count = atoi( argv[ ++i ] );
            }
        }
        else if( !strcmp( a, "-h" ) || !strcmp( a, "--help" ) ) { usage( argv[ 0 ] ); return -1; }
        else { fprintf( stderr, "unknown option: %s\n", a ); usage( argv[ 0 ] ); return -1; }

        #undef NEED_ARG
    }

    return 0;
}

void app_cfg_finalize( struct app_cfg * c )
{
    /* When --hostname wasn't given, derive a unique-by-MAC default so two
     * unconfigured drones don't collide on the same name. */
    if( c->hostname[ 0 ] == '\0' )
    {
        snprintf( c->hostname, sizeof c->hostname, "drone-%02x%02x%02x",
                  c->mac[ 3 ], c->mac[ 4 ], c->mac[ 5 ] );
    }
}

/* Split "host:port" (host optional, defaults to 127.0.0.1) into a sockaddr. */
static int parse_hostport( const char * s, struct sockaddr_in * sa )
{
    char host[ 64 ];
    const char * colon = strrchr( s, ':' );
    int port;

    if( colon == NULL )
    {
        return -1;
    }

    port = atoi( colon + 1 );
    if( ( colon - s ) >= ( long ) sizeof( host ) )
    {
        return -1;
    }
    memcpy( host, s, colon - s );
    host[ colon - s ] = '\0';

    memset( sa, 0, sizeof( *sa ) );
    sa->sin_family = AF_INET;
    sa->sin_port = htons( ( uint16_t ) port );

    /* qeneth links use "localhost"; accept it (and an empty host) as loopback.
     * inet_addr() only understands dotted-decimal, so map the name here. */
    if( ( host[ 0 ] == '\0' ) || ( strcmp( host, "localhost" ) == 0 ) )
    {
        sa->sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    }
    else
    {
        sa->sin_addr.s_addr = inet_addr( host );
        if( sa->sin_addr.s_addr == INADDR_NONE )
        {
            return -1;
        }
    }
    return 0;
}

static int open_link_socket( const char * localaddr )
{
    struct sockaddr_in local;
    int fd, on = 1;

    if( parse_hostport( localaddr, &local ) != 0 )
    {
        fprintf( stderr, "bad --localaddr '%s'\n", localaddr );
        return -1;
    }

    fd = socket( AF_INET, SOCK_DGRAM, 0 );
    if( fd < 0 )
    {
        perror( "socket" );
        return -1;
    }

    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof( on ) );

    if( bind( fd, ( struct sockaddr * ) &local, sizeof( local ) ) != 0 )
    {
        fprintf( stderr, "bind %s: %s\n", localaddr, strerror( errno ) );
        close( fd );
        return -1;
    }

    return fd;
}

/* lwIP fires this callback when the netif's IPv4 address becomes valid;
 * with AutoIP, that's after the probe-and-claim cycle completes. */
NETIF_DECLARE_EXT_CALLBACK( s_ext_cb )
static void on_netif_ext( struct netif * nif, netif_nsc_reason_t reason,
                          const netif_ext_callback_args_t * args )
{
    LWIP_UNUSED_ARG( args );
    if( reason & ( LWIP_NSC_IPV4_ADDR_VALID | LWIP_NSC_IPV4_ADDRESS_CHANGED ) )
    {
        const ip4_addr_t * a = netif_ip4_addr( nif );
        if( !ip4_addr_isany( a ) )
        {
            printf( "drone: ipv4 address: %s\n", ip4addr_ntoa( a ) );
        }
    }
}

void net_start( const struct app_cfg * c )
{
    ip4_addr_t ip, mask, gw;
    struct sockaddr_in peer;
    int autoip = ( c->ip[ 0 ] == '\0' );
    int fd;

    if( autoip )
    {
        ip4_addr_set_zero( &ip );
        ip4_addr_set_zero( &mask );
        ip4_addr_set_zero( &gw );
    }
    else if( !ip4addr_aton( c->ip, &ip ) ||
             !ip4addr_aton( c->netmask, &mask ) ||
             !ip4addr_aton( c->gw, &gw ) )
    {
        fprintf( stderr, "drone: bad IP configuration\n" );
        return;
    }

    if( parse_hostport( c->peeraddr, &peer ) != 0 )
    {
        fprintf( stderr, "drone: bad --udp '%s'\n", c->peeraddr );
        return;
    }

    fd = open_link_socket( c->localaddr );
    if( fd < 0 )
    {
        return;
    }

    printf( "drone: link bind=%s peer=%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
            c->localaddr, c->peeraddr,
            c->mac[ 0 ], c->mac[ 1 ], c->mac[ 2 ],
            c->mac[ 3 ], c->mac[ 4 ], c->mac[ 5 ] );

    /* Bring up lwIP.  tcpip_init() creates the core-lock mutex synchronously
     * and spawns the tcpip thread. */
    tcpip_init( NULL, NULL );

    LOCK_TCPIP_CORE();
    netif_add_ext_callback( &s_ext_cb, on_netif_ext );
    UNLOCK_TCPIP_CORE();

    if( qeneth_netif_add( &s_netif, &ip, &mask, &gw, c->mac,
                          fd, peer.sin_addr.s_addr, peer.sin_port ) != ERR_OK )
    {
        fprintf( stderr, "drone: failed to add interface\n" );
        return;
    }

    if( autoip )
    {
        LOCK_TCPIP_CORE();
        autoip_start( &s_netif );
        UNLOCK_TCPIP_CORE();
        printf( "drone: interface up (AutoIP), awaiting claim...\n" );
    }
    else
    {
        printf( "drone: interface up, ip=%s/%s gw=%s\n",
                c->ip, c->netmask, c->gw );
    }

    if( c->do_ping )
    {
        ip4_addr_t target;

        if( ip4addr_aton( c->ping_target, &target ) )
        {
            ping_start( &target, c->ping_count );
        }
        else
        {
            fprintf( stderr, "drone: bad --ping target '%s'\n", c->ping_target );
        }
    }
}
