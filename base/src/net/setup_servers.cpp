#include "net/setup_servers.h"

#include <cstring>

#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/time.h"

namespace wxb {
namespace setup_servers {
namespace {

constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDhcpClientPort = 68;
constexpr uint16_t kDnsPort = 53;
constexpr int kLeases = 8;
constexpr uint8_t kFirstHost = 16;
constexpr uint32_t kLeaseSeconds = 2 * 3600;

udp_pcb* g_dhcp = nullptr;
udp_pcb* g_dns = nullptr;
netif* g_netif = nullptr;

struct Lease {
  uint8_t mac[6];
  uint32_t expires_ms;
  bool used;
};
Lease g_leases[kLeases];

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

uint32_t server_ip() { return ip4_addr_get_u32(netif_ip4_addr(g_netif)); }  // network order

// ---------------------------------------------------------------- DHCP

// BOOTP header is 236 bytes, then the magic cookie, then options.
constexpr size_t kBootpLen = 236;
constexpr size_t kMaxMsg = 548;

int lease_for(const uint8_t* mac) {
  int free_slot = -1, oldest = 0;
  for (int i = 0; i < kLeases; ++i) {
    if (g_leases[i].used && std::memcmp(g_leases[i].mac, mac, 6) == 0) return i;
    if (!g_leases[i].used && free_slot < 0) free_slot = i;
    if (static_cast<int32_t>(g_leases[i].expires_ms - g_leases[oldest].expires_ms) < 0) oldest = i;
  }
  int i = free_slot >= 0 ? free_slot : oldest;  // full: reuse the oldest lease
  std::memcpy(g_leases[i].mac, mac, 6);
  g_leases[i].used = true;
  return i;
}

const uint8_t* find_opt(const uint8_t* opts, size_t len, uint8_t code) {
  size_t i = 0;
  while (i < len) {
    const uint8_t c = opts[i];
    if (c == 255) break;
    if (c == 0) {
      ++i;
      continue;
    }
    if (i + 1 >= len) break;
    const uint8_t l = opts[i + 1];
    if (i + 2 + l > len) break;
    if (c == code) return opts + i;
    i += 2u + l;
  }
  return nullptr;
}

void put_opt(uint8_t*& p, uint8_t code, const void* data, uint8_t len) {
  *p++ = code;
  *p++ = len;
  std::memcpy(p, data, len);
  p += len;
}

void dhcp_recv(void*, udp_pcb* pcb, pbuf* p, const ip_addr_t*, u16_t) {
  uint8_t msg[kMaxMsg];
  const u16_t n = pbuf_copy_partial(p, msg, sizeof(msg), 0);
  pbuf_free(p);
  if (n < kBootpLen + 4 || msg[0] != 1) return;  // BOOTREQUEST only
  if (msg[236] != 99 || msg[237] != 130 || msg[238] != 83 || msg[239] != 99) return;

  const uint8_t* opts = msg + 240;
  const size_t opts_len = n - 240u;
  const uint8_t* type = find_opt(opts, opts_len, 53);
  if (type == nullptr || type[1] != 1) return;
  uint8_t reply_type;
  if (type[2] == 1) {
    reply_type = 2;  // DISCOVER → OFFER
  } else if (type[2] == 3) {
    reply_type = 5;  // REQUEST → ACK
  } else {
    return;  // RELEASE/DECLINE/INFORM: nothing to do for a setup network
  }

  const int li = lease_for(msg + 28);
  g_leases[li].expires_ms = now_ms() + kLeaseSeconds * 1000u;

  const uint32_t sip = server_ip();
  uint32_t yip = sip;
  reinterpret_cast<uint8_t*>(&yip)[3] = static_cast<uint8_t>(kFirstHost + li);

  // Reply reuses the request buffer: same xid, flags, chaddr.
  msg[0] = 2;  // BOOTREPLY
  std::memset(msg + 12, 0, 4);  // ciaddr
  std::memcpy(msg + 16, &yip, 4);  // yiaddr
  std::memcpy(msg + 20, &sip, 4);  // siaddr
  uint8_t* o = msg + 240;
  put_opt(o, 53, &reply_type, 1);
  put_opt(o, 54, &sip, 4);  // server id
  const uint32_t mask = PP_HTONL(0xFFFFFF00u);
  put_opt(o, 1, &mask, 4);
  put_opt(o, 3, &sip, 4);   // router
  put_opt(o, 6, &sip, 4);   // DNS
  const uint32_t lease = PP_HTONL(kLeaseSeconds);
  put_opt(o, 51, &lease, 4);
  *o++ = 255;
  const size_t used = static_cast<size_t>(o - msg);
  const size_t out_len = used < 300 ? 300 : used;  // BOOTP minimum size
  if (out_len > used) std::memset(o, 0, out_len - used);

  pbuf* out = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(out_len), PBUF_RAM);
  if (out == nullptr) return;
  pbuf_take(out, msg, static_cast<u16_t>(out_len));
  udp_sendto_if(pcb, out, IP_ADDR_BROADCAST, kDhcpClientPort, g_netif);
  pbuf_free(out);
}

// ---------------------------------------------------------------- DNS

void dns_recv(void*, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr, u16_t port) {
  uint8_t q[512];
  const u16_t n = pbuf_copy_partial(p, q, sizeof(q) - 16, 0);
  pbuf_free(p);
  if (n < 12) return;
  if (q[2] & 0x80) return;                // a response, not a query
  const uint16_t qdcount = static_cast<uint16_t>((q[4] << 8) | q[5]);
  if (qdcount != 1) return;

  // Walk the question name.
  size_t i = 12;
  while (i < n && q[i] != 0) {
    if ((q[i] & 0xC0) != 0) return;  // no compression in questions
    i += 1u + q[i];
  }
  if (i + 5 > n) return;
  const size_t qend = i + 5;  // zero byte + type + class
  const uint16_t qtype = static_cast<uint16_t>((q[i + 1] << 8) | q[i + 2]);

  q[2] = 0x84;  // response, authoritative
  q[3] = 0x00;  // no error
  q[6] = 0;
  q[7] = (qtype == 1) ? 1 : 0;  // one answer for A, none for AAAA etc.
  std::memset(q + 8, 0, 4);
  size_t len = qend;
  if (qtype == 1) {
    const uint8_t answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 60, 0, 4};
    std::memcpy(q + len, answer, sizeof(answer));
    len += sizeof(answer);
    const uint32_t ip = server_ip();
    std::memcpy(q + len, &ip, 4);
    len += 4;
  }
  pbuf* out = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(len), PBUF_RAM);
  if (out == nullptr) return;
  pbuf_take(out, q, static_cast<u16_t>(len));
  udp_sendto_if(pcb, out, addr, port, g_netif);
  pbuf_free(out);
}

}  // namespace

bool start(netif* ap) {
  if (running()) return true;
  g_netif = ap;
  std::memset(g_leases, 0, sizeof(g_leases));
  g_dhcp = udp_new();
  g_dns = udp_new();
  if (g_dhcp == nullptr || g_dns == nullptr) {
    stop();
    return false;
  }
  udp_bind_netif(g_dhcp, ap);
  udp_bind_netif(g_dns, ap);
  if (udp_bind(g_dhcp, IP_ANY_TYPE, kDhcpServerPort) != ERR_OK ||
      udp_bind(g_dns, IP_ANY_TYPE, kDnsPort) != ERR_OK) {
    stop();
    return false;
  }
  udp_recv(g_dhcp, dhcp_recv, nullptr);
  udp_recv(g_dns, dns_recv, nullptr);
  return true;
}

void stop() {
  if (g_dhcp) udp_remove(g_dhcp);
  if (g_dns) udp_remove(g_dns);
  g_dhcp = nullptr;
  g_dns = nullptr;
}

bool running() { return g_dhcp != nullptr; }

}  // namespace setup_servers
}  // namespace wxb
