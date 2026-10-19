/* SPDX-License-Identifier: MIT */
#ifndef NT98513_WIFI_H
#define NT98513_WIFI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define WIFI_MAX_CONFIG 4096
#define WIFI_RESOLVER_MAX 65536
#define WIFI_TOKEN_HEX 32
struct wifi_config {
	char interface[16], country[3];
	unsigned char ssid[32], psk[32];
	size_t ssid_len;
};
struct wifi_lease {
	uint32_t address, mask, router, dns[3];	/* network byte order */
	uint32_t dns_count, seconds;
};
struct wifi_state {
	uint64_t generation, expires, acquire_deadline, retry_at;
	unsigned failures;
	bool completed, dhcp, online, failed;
	int network_id;
	char bssid[18];
	struct wifi_lease lease;
};
struct wifi_actions {
	void *context;
	int (*start) (void *, uint64_t);
	void (*stop) (void *);
	int (*apply) (void *, const struct wifi_lease *);
	int (*clear) (void *);
};
void wifi_wipe(void *, size_t);
int wifi_config_parse(const char *, size_t, struct wifi_config *);
int wifi_config_read(const char *, struct wifi_config *);
int wifi_config_render(int, const struct wifi_config *, const char *);
int wifi_safe_open(const char *, int, mode_t, bool);
int wifi_lease_parse(const char *, const char *, const char *, const char *, const char *, struct wifi_lease *);
int wifi_state_status(struct wifi_state *, bool, int, const char *, uint64_t, const struct wifi_actions *);
int wifi_state_lease(struct wifi_state *, uint64_t, const struct wifi_lease *, uint64_t, const struct wifi_actions *);
int wifi_state_tick(struct wifi_state *, uint64_t, const struct wifi_actions *);
int wifi_state_disconnect(struct wifi_state *, const struct wifi_actions *);
int wifi_state_child_exit(struct wifi_state *, uint64_t, const struct wifi_actions *);
uint64_t wifi_now(void);
int wifi_entropy(void *, size_t, unsigned);
int wifi_run(const struct wifi_config *);
int wifi_client(const char *, const char *);
int wifi_lease_entry(const char *);
struct wifi_net {
	unsigned ifindex;
	char interface[16];
	bool raised, address, route, dns_owned;
	struct wifi_lease lease;
	char *baseline, *expected;
	size_t baseline_len, expected_len;
	dev_t resolver_dev;
	ino_t resolver_ino;
	int resolver_dir;
	char resolver_name[64];
};
int wifi_net_admit(struct wifi_net *, const char *);
int wifi_net_apply(struct wifi_net *, const struct wifi_lease *);
int wifi_net_clear(struct wifi_net *);
int wifi_net_close(struct wifi_net *);
int wifi_resolver_open(struct wifi_net *, const char *);
int wifi_resolver_set(struct wifi_net *, const struct wifi_lease *);
int wifi_resolver_clear(struct wifi_net *);
#endif
