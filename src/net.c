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

#include "net.h"
#include "ping.h"
#include "qeneth_netif.h"

static struct netif s_netif;

void app_cfg_defaults( struct app_cfg * c )
{
    memset( c, 0, sizeof( *c ) );
    strcpy( c->localaddr, "127.0.0.1:20000" );
    strcpy( c->peeraddr, "127.0.0.1:20001" );
    strcpy( c->ip, "10.0.0.2" );
    strcpy( c->netmask, "255.255.255.0" );
    strcpy( c->gw, "10.0.0.1" );
    /* Locally administered unicast MAC (02:..). */
    c->mac[ 0 ] = 0x02; c->mac[ 1 ] = 0x00; c->mac[ 2 ] = 0x00;
    c->mac[ 3 ] = 0x00; c->mac[ 4 ] = 0x00; c->mac[ 5 ] = 0x02;
    strcpy( c->hostname, "frtos-dev" );
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
             "  --ip ADDR               static IPv4 address (default 10.0.0.2)\n"
             "  --netmask ADDR          (default 255.255.255.0)\n"
             "  --gw ADDR               default gateway (default 10.0.0.1)\n"
             "  --mac XX:XX:XX:XX:XX:XX  interface MAC (default 02:00:00:00:00:02)\n"
             "  --hostname NAME         device hostname/id (default frtos-dev)\n"
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
    sa->sin_addr.s_addr = inet_addr( host[ 0 ] ? host : "127.0.0.1" );
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

void net_start( const struct app_cfg * c )
{
    ip4_addr_t ip, mask, gw;
    struct sockaddr_in peer;
    int fd;

    if( !ip4addr_aton( c->ip, &ip ) ||
        !ip4addr_aton( c->netmask, &mask ) ||
        !ip4addr_aton( c->gw, &gw ) )
    {
        fprintf( stderr, "frtos-dev: bad IP configuration\n" );
        return;
    }

    if( parse_hostport( c->peeraddr, &peer ) != 0 )
    {
        fprintf( stderr, "frtos-dev: bad --udp '%s'\n", c->peeraddr );
        return;
    }

    fd = open_link_socket( c->localaddr );
    if( fd < 0 )
    {
        return;
    }

    printf( "frtos-dev: link bind=%s peer=%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
            c->localaddr, c->peeraddr,
            c->mac[ 0 ], c->mac[ 1 ], c->mac[ 2 ],
            c->mac[ 3 ], c->mac[ 4 ], c->mac[ 5 ] );

    /* Bring up lwIP.  tcpip_init() creates the core-lock mutex synchronously
     * and spawns the tcpip thread. */
    tcpip_init( NULL, NULL );

    if( qeneth_netif_add( &s_netif, &ip, &mask, &gw, c->mac,
                          fd, peer.sin_addr.s_addr, peer.sin_port ) != ERR_OK )
    {
        fprintf( stderr, "frtos-dev: failed to add interface\n" );
        return;
    }

    printf( "frtos-dev: interface up, ip=%s/%s gw=%s\n",
            c->ip, c->netmask, c->gw );

    if( c->do_ping )
    {
        ip4_addr_t target;

        if( ip4addr_aton( c->ping_target, &target ) )
        {
            ping_start( &target, c->ping_count );
        }
        else
        {
            fprintf( stderr, "frtos-dev: bad --ping target '%s'\n", c->ping_target );
        }
    }
}
