/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wifi.h"
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
int main(int argc, char **argv)
{
	if (getuid() != 0 || geteuid() != 0)
		return 1;
	umask(077);
	if (prctl(PR_SET_DUMPABLE, 0))
		return 1;
	/* All descriptors opened by the program itself use CLOEXEC. */
	if (syscall(SYS_close_range, 3u, ~0u, 0u))
		return 1;
	if (strstr(argv[0], "nt98513-wifi-lease"))
		return argc == 2 ? wifi_lease_entry(argv[1]) : 1;
	if (argc != 4) {
		fputs("usage: nt98513-wifi start --config /absolute/file | status|stop --interface name\n", stderr);
		return 1;
	}
	if (!strcmp(argv[1], "start") && !strcmp(argv[2], "--config")) {
		struct wifi_config cfg;
		if (wifi_config_read(argv[3], &cfg)) {
			fputs("Wi-Fi: owner configuration rejected\n", stderr);
			return 1;
		}
		int ret = wifi_run(&cfg);
		wifi_wipe(&cfg, sizeof(cfg));
		if (ret)
			fputs("Wi-Fi: session failed or cleanup conflict; inspect private runtime state\n", stderr);
		return ret;
	}
	if (!strcmp(argv[2], "--interface")) {
		if (!strcmp(argv[1], "status"))
			return wifi_client(argv[3], "STATUS");
		if (!strcmp(argv[1], "stop"))
			return wifi_client(argv[3], "STOP");
	}
	return 1;
}
