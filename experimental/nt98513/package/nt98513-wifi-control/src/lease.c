/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "wifi.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int unicast(const char *s, uint32_t * out)
{
	struct in_addr a;
	if (!s || inet_pton(AF_INET, s, &a) != 1)
		return -1;
	uint32_t h = ntohl(a.s_addr);
	if (!(h >> 24) || h == 0xffffffffu || (h >> 24) == 127 || (h >> 28) >= 14)
		return -1;
	*out = a.s_addr;
	return 0;
}
int wifi_lease_parse(const char *ip, const char *mask, const char *router, const char *dns, const char *seconds, struct wifi_lease *l)
{
	struct in_addr m;
	char copy[128], *save = NULL, *part, *end;
	unsigned long duration;
	memset(l, 0, sizeof(*l));
	if (unicast(ip, &l->address) || !mask || inet_pton(AF_INET, mask, &m) != 1)
		return -1;
	uint32_t bits = ntohl(m.s_addr), host = ~bits, addr = ntohl(l->address);
	if (!bits || host < 3 || (host & (host + 1)) || (addr & host) == 0 || (addr & host) == host)
		return -1;
	l->mask = m.s_addr;
	if (router && *router) {
		if (unicast(router, &l->router) || (l->router & l->mask) != (l->address & l->mask) || l->router == l->address ||
		    !(ntohl(l->router) & host) || (ntohl(l->router) & host) == host)
			return -1;
	}
	if (!dns || strlen(dns) >= sizeof(copy) || !seconds || !*seconds)
		return -1;
	strcpy(copy, dns);
	for (part = strtok_r(copy, " ", &save); part; part = strtok_r(NULL, " ", &save)) {
		if (l->dns_count == 3 || unicast(part, &l->dns[l->dns_count]))
			return -1;
		l->dns_count++;
	}
	if (!l->dns_count)
		return -1;
	for (const char *p = seconds; *p; p++)
		if (*p < '0' || *p > '9')
			return -1;
	errno = 0;
	duration = strtoul(seconds, &end, 10);
	if (errno || *end || duration < 1 || duration > 604800)
		return -1;
	l->seconds = (uint32_t) duration;
	return 0;
}
