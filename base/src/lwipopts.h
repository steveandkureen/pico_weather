// lwIP settings for the base station. NO_SYS poll mode, with lwIP's own fixed
// memory pools (MEM_LIBC_MALLOC 0), so networking never touches the C heap.
#ifndef WX_LWIPOPTS_H
#define WX_LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE 16000
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10
#define MEMP_NUM_TCP_PCB 8          // 4 web clients + forecast + spares (TIME_WAIT)
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_UDP_PCB 10         // DHCP client, DNS client, SNTP, mDNS, setup DHCP + DNS servers
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)
#define PBUF_POOL_SIZE 24

#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_DHCP 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_IGMP 1                 // mDNS uses multicast
#define LWIP_NUM_NETIF_CLIENT_DATA 1  // mDNS stores per-interface state

#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define LWIP_CHKSUM_ALGORITHM 3

// mDNS: weather.local
#define LWIP_MDNS_RESPONDER 1
#define MDNS_MAX_SERVICES 1

// SNTP: time from pool.ntp.org every 3 h, handed to the app.
#ifdef __cplusplus
extern "C" {
#endif
void wx_sntp_set_time(unsigned int sec);
#ifdef __cplusplus
}
#endif
#define SNTP_SERVER_DNS 1
#define SNTP_SET_SYSTEM_TIME(sec) wx_sntp_set_time(sec)
#define SNTP_UPDATE_DELAY (3 * 60 * 60 * 1000)
#define SNTP_STARTUP_DELAY 0

#define MEM_STATS 0
#define SYS_STATS 0
#define MEMP_STATS 0
#define LINK_STATS 0

#endif
