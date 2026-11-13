// Servers that make the setup network work like hotel WiFi:
//  * DHCP hands phones an address (192.168.4.16+) with the Pico as router and DNS.
//  * DNS answers every name with the Pico's address, so the phone's
//    "is there internet?" check lands on our web server, which redirects it
//    to /config. iOS/Android then pop the setup page up automatically.
#pragma once

#include <cstdint>

struct netif;

namespace wxb {
namespace setup_servers {

bool start(netif* ap_netif);
void stop();
bool running();

}  // namespace setup_servers
}  // namespace wxb
