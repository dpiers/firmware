/* SPDX-License-Identifier: MIT */
#include "wifi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int starts, stops, applies, clears;
static int start(void *ctx, uint64_t generation) { (void)ctx; assert(generation); starts++; return 0; }
static void stop(void *ctx) { (void)ctx; stops++; }
static int apply(void *ctx, const struct wifi_lease *l) { (void)ctx; assert(l->seconds); applies++; return 0; }
static int clear(void *ctx) { (void)ctx; clears++; return 0; }
int main(void)
{
 struct wifi_state s = {0};
 struct wifi_actions a = {NULL, start, stop, apply, clear};
 struct wifi_lease l;
 struct wifi_config c;
 const char *good = "interface=wlan0\ncountry=US\nssid=74657374\npsk=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nresolver_owner=exclusive\n";
 assert(!wifi_config_parse(good, strlen(good), &c));
 assert(c.ssid_len==4 && !strcmp(c.interface,"wlan0"));
 assert(wifi_config_parse("interface=wlan0\ninterface=wlan1\n",32,&c));
 assert(wifi_lease_parse("192.168.3.5", "255.255.255.0", "192.168.3.1", "1.1.1.1 8.8.8.8", "10", &l)==0);
 assert(wifi_lease_parse("192.168.3.5", "255.0.255.0", "192.168.3.1", "1.1.1.1", "10", &l));
 assert(wifi_lease_parse("192.168.3.5", "255.255.255.0", "10.1.1.1", "1.1.1.1", "10", &l));
 assert(wifi_lease_parse("0.1.2.3", "255.255.255.0", "", "1.1.1.1", "10", &l));
 assert(wifi_lease_parse("192.168.3.5", "255.255.255.0", "192.168.3.0", "1.1.1.1", "10", &l));
 assert(wifi_lease_parse("192.168.3.5", "255.255.255.0", "192.168.3.255", "1.1.1.1", "10", &l));
 assert(wifi_lease_parse("192.168.3.5", "255.255.255.0", "192.168.3.1", "0.1.1.1", "10", &l));
 assert(!wifi_lease_parse("192.168.3.5", "255.255.255.0", "192.168.3.1", "1.1.1.1", "10", &l));
 assert(!wifi_state_status(&s, true, 0, "00:11:22:33:44:55", 1000, &a));
 uint64_t first=s.generation;
 assert(starts==1 && s.dhcp);
 assert(!wifi_state_status(&s, true, 0, "00:11:22:33:44:55", 1100, &a));
 assert(starts==1);
 assert(!wifi_state_lease(&s,first,&l,1200,&a));
 assert(s.expires==11200 && applies==1);
 /* Silent DHCP renewal loss must retire configuration despite COMPLETED. */
 assert(!wifi_state_tick(&s,11200,&a));
 assert(stops==1 && clears==1 && !s.online && starts==2);
 assert(wifi_state_lease(&s,first,&l,11201,&a));
 uint64_t second=s.generation;
 assert(!wifi_state_lease(&s,second,&l,12000,&a));
 /* Exact-deadline renew loses to expiry and cannot resurrect its generation. */
 assert(wifi_state_lease(&s,second,&l,22000,&a));
 assert(!s.online && stops==2 && starts==3);
 uint64_t third=s.generation;
 assert(!wifi_state_lease(&s,third,&l,23000,&a));
 assert(!wifi_state_status(&s,true,0,"00:11:22:33:44:66",24000,&a));
 assert(stops==3 && starts==4 && s.generation!=third && !s.online);
 uint64_t fourth=s.generation;
 assert(!wifi_state_status(&s,true,1,"00:11:22:33:44:66",25000,&a));
 assert(s.generation!=fourth && starts==5);
 wifi_state_disconnect(&s,&a);
 assert(!s.completed && !s.dhcp && !s.online);
 assert(wifi_state_lease(&s,fourth,&l,26000,&a));
 puts("Wi-Fi actual state/input tests passed");
 return 0;
}
