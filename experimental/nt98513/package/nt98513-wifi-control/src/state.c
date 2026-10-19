/* SPDX-License-Identifier: MIT */
#include "wifi.h"
#include <string.h>

/* Invalidate before stopping: even a deconfig helper run by TERM is stale. */
static int retire(struct wifi_state *s, const struct wifi_actions *a)
{
	s->generation++;
	s->expires = s->acquire_deadline = 0;
	if (s->dhcp) {
		s->dhcp = false;
		a->stop(a->context);
	}
	if (a->clear(a->context)) {
		s->failed = true;
		return -1;
	}
	s->online = false;
	return 0;
}
static int acquire(struct wifi_state *s, uint64_t now, const struct wifi_actions *a)
{
	if (s->failed || !s->completed || s->dhcp)
		return s->failed ? -1 : 0;
	s->generation++;
	if (a->start(a->context, s->generation)) {
		s->failed = true;
		return -1;
	}
	s->dhcp = true;
	s->acquire_deadline = now + 30000;
	s->retry_at = 0;
	return 0;
}
int wifi_state_disconnect(struct wifi_state *s, const struct wifi_actions *a)
{
	s->completed = false;
	s->retry_at = 0;
	s->failures = 0;
	return retire(s, a);
}
int wifi_state_child_exit(struct wifi_state *s, uint64_t now, const struct wifi_actions *a)
{
	/* Caller cleared/reaped the child; stop callback is safe and idempotent. */
	if (retire(s, a))
		return -1;
	s->failures++;
	s->retry_at = now + (s->failures == 1 ? 5000 : s->failures == 2 ? 10000 : 30000);
	return 0;
}
int wifi_state_tick(struct wifi_state *s, uint64_t now, const struct wifi_actions *a)
{
	if (s->failed)
		return -1;
	if (s->online && now >= s->expires) {
		if (retire(s, a))
			return -1;
		return acquire(s, now, a);
	}
	if (s->dhcp && !s->online && now >= s->acquire_deadline)
		return wifi_state_child_exit(s, now, a);
	if (s->completed && !s->dhcp && now >= s->retry_at)
		return acquire(s, now, a);
	return 0;
}
int wifi_state_status(struct wifi_state *s, bool completed, int id, const char *bssid, uint64_t now, const struct wifi_actions *a)
{
	if (!completed)
		return s->completed ? wifi_state_disconnect(s, a) : 0;
	if (id < 0 || !bssid || strlen(bssid) != 17)
		return -1;
	if (s->completed && (s->network_id != id || strcmp(s->bssid, bssid)))
		if (retire(s, a))
			return -1;
	s->completed = true;
	s->network_id = id;
	memcpy(s->bssid, bssid, 18);
	return wifi_state_tick(s, now, a);
}
int wifi_state_lease(struct wifi_state *s, uint64_t generation, const struct wifi_lease *l, uint64_t now, const struct wifi_actions *a)
{
	/* Expiry wins even if a renewal was queued before this iteration. */
	if (wifi_state_tick(s, now, a))
		return -1;
	if (!s->completed || !s->dhcp || generation != s->generation || !l->seconds)
		return -1;
	if (!s->online || memcmp(&s->lease, l, sizeof(*l)))
		if (a->apply(a->context, l)) {
			s->failed = true;
			return -1;
		}
	s->lease = *l;
	s->online = true;
	s->expires = now + (uint64_t) l->seconds * 1000;
	s->acquire_deadline = 0;
	s->failures = 0;
	return 0;
}
