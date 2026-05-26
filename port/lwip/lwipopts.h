/*
 * lwIP options for the drone simulator build.
 *
 * Model: NO_SYS=0 with a tcpip thread, IPv4 only, raw/UDP/TCP enabled.
 * App code (pinger, MQTT) uses the raw API bracketed by LOCK_TCPIP_CORE()/
 * UNLOCK_TCPIP_CORE(); the netif RX task hands frames in via tcpip_input().
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* lwIP sources don't include FreeRTOSConfig.h; pull it in so thread priority
 * and stack macros below can be expressed in terms of FreeRTOS config. */
#include "FreeRTOSConfig.h"

/* === OS / threading =================================================== */
#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_COMPAT_MUTEX               0

/* sys_thread_new() stack sizes are in BYTES (FreeRTOS port default), prios
 * are FreeRTOS priorities.  Real stacks are pthread-sized; these mostly
 * size the kernel-heap bookkeeping allocation. */
#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          8192
#define TCPIP_THREAD_PRIO               ( configMAX_PRIORITIES - 2 )
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_THREAD_STACKSIZE        4096
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       8
#define DEFAULT_ACCEPTMBOX_SIZE         8

/* === Memory =========================================================== */
#define MEM_LIBC_MALLOC                 0
#define MEMP_MEM_MALLOC                 0
#define MEM_ALIGNMENT                   8        /* 64-bit host */
#define MEM_SIZE                        ( 128 * 1024 )
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_RAW_PCB                8
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_SYS_TIMEOUT            16
#define PBUF_POOL_SIZE                  32
#define PBUF_POOL_BUFSIZE               1536

/* === Protocols ======================================================== */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1      /* auto echo reply */
#define LWIP_RAW                        1      /* used by the pinger */
#define LWIP_UDP                        1
#define LWIP_TCP                        1      /* used by MQTT */
#define LWIP_ALTCP                      1      /* apps/mqtt uses the altcp API */
#define LWIP_ALTCP_TLS                  0
#define LWIP_DHCP                       0
#define LWIP_DNS                        0
#define LWIP_IGMP                       0
#define LWIP_NETIF_HOSTNAME             1

/* === TCP sizing (for MQTT) ============================================ */
#define TCP_MSS                         1460
#define TCP_WND                         ( 8 * TCP_MSS )
#define TCP_SND_BUF                     ( 8 * TCP_MSS )

/* === Netif ============================================================ */
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_SINGLE_NETIF               1

/* === Checksums in software (the qeneth UDP link offers no offload) ==== */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_CHECK_ICMP             1

/* === Diagnostics ====================================================== */
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

#endif /* LWIPOPTS_H */
