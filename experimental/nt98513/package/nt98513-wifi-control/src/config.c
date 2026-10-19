/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wifi.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}
static int bytes(const char *s, unsigned char *out, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		int a = hex((unsigned char)s[i * 2]), b = hex((unsigned char)s[i * 2 + 1]);
		if (a < 0 || b < 0)
			return -1;
		out[i] = (unsigned char)(a * 16 + b);
	}
	return 0;
}
static int parse(char *text, struct wifi_config *cfg)
{
	char *save = NULL, *line;
	unsigned seen = 0;
	memset(cfg, 0, sizeof(*cfg));

	for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *v = strchr(line, '=');
		unsigned bit;
		if (!v)
			return -1;
		*v++ = 0;
		if (!strcmp(line, "interface")) {
			bit = 1;
			if (!*v || strlen(v) > 15)
				return -1;
			for (char *p = v; *p; p++)
				if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
					return -1;
			strcpy(cfg->interface, v);
		} else if (!strcmp(line, "country")) {
			bit = 2;
			if (strlen(v) != 2 || v[0] < 'A' || v[0] > 'Z' || v[1] < 'A' || v[1] > 'Z')
				return -1;
			strcpy(cfg->country, v);
		} else if (!strcmp(line, "ssid")) {
			bit = 4;
			size_t n = strlen(v);
			if (n < 2 || n > 64 || n % 2)
				return -1;
			cfg->ssid_len = n / 2;
			if (bytes(v, cfg->ssid, n / 2))
				return -1;
		} else if (!strcmp(line, "psk")) {
			bit = 8;
			if (strlen(v) != 64 || bytes(v, cfg->psk, 32))
				return -1;
		} else if (!strcmp(line, "resolver_owner")) {
			bit = 16;
			if (strcmp(v, "exclusive"))
				return -1;
		} else
			return -1;
		if (seen & bit)
			return -1;
		seen |= bit;
	}

	return seen == 31 ? 0 : -1;
}
void wifi_wipe(void *data, size_t size)
{
	volatile unsigned char *p = data;
	while (size--)
		*p++ = 0;
}
int wifi_config_parse(const char *input, size_t size, struct wifi_config *cfg)
{
	char text[WIFI_MAX_CONFIG + 1];
	if (!size || size > WIFI_MAX_CONFIG || memchr(input, 0, size))
		return -1;
	memcpy(text, input, size);
	text[size] = 0;
	int ret = parse(text, cfg);
	wifi_wipe(text, sizeof(text));
	if (ret)
		wifi_wipe(cfg, sizeof(*cfg));
	return ret;
}
/* Walk every component, holding directory fds; no symlink traversal. */
int wifi_safe_open(const char *path, int flags, mode_t mode, bool private_file)
{
	char copy[4096], *save = NULL, *part, *next;
	struct stat st;
	int dir = -1, fd = -1;
	if (!path || path[0] != '/' || strlen(path) >= sizeof(copy))
		return -1;
	strcpy(copy, path);
	dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dir < 0)
		return -1;
	part = strtok_r(copy, "/", &save);
	if (!part)
		goto out;
	while (part) {
		if (!strcmp(part, ".") || !strcmp(part, ".."))
			goto out;
		next = strtok_r(NULL, "/", &save);
		if (fstat(dir, &st) || st.st_uid != 0 || (st.st_mode & 0022))
			goto out;
		if (next) {
			int child = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (child < 0)
				goto out;
			close(dir);
			dir = child;
			part = next;
			continue;
		}
		fd = openat(dir, part, flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, mode);
		if (fd < 0)
			goto out;
		if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_nlink != 1 ||
		    (private_file ? (st.st_mode & 0777) != 0600 : (st.st_mode & 0022) != 0)) {
			close(fd);
			fd = -1;
		}
		break;
	}
out:	close(dir);
	return fd;
}
int wifi_config_read(const char *path, struct wifi_config *cfg)
{
	char input[WIFI_MAX_CONFIG + 1];
	struct stat before, after;
	int ret = -1;
	int fd = wifi_safe_open(path, O_RDONLY, 0, true);
	if (fd < 0)
		return -1;
	if (fstat(fd, &before) || before.st_size < 1 || before.st_size > WIFI_MAX_CONFIG)
		goto out;
	ssize_t n = read(fd, input, sizeof(input));
	if (n != before.st_size || fstat(fd, &after) || after.st_size != before.st_size ||
	    after.st_mtime != before.st_mtime || after.st_ctime != before.st_ctime)
		goto out;
	ret = wifi_config_parse(input, (size_t) n, cfg);
out:	wifi_wipe(input, sizeof(input));
	close(fd);
	return ret;
}
int wifi_config_render(int dirfd, const struct wifi_config *cfg, const char *ctrl)
{
	int fd = openat(dirfd, "supplicant.conf", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	FILE *f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		return -1;
	}
	int bad = fprintf(f, "update_config=0\nctrl_interface=%s\ncountry=%s\nnetwork={\n ssid=", ctrl, cfg->country) < 0;
	for (size_t i = 0; i < cfg->ssid_len; i++)
		bad |= fprintf(f, "%02x", cfg->ssid[i]) < 0;
	bad |= fprintf(f, "\n psk=") < 0;
	for (size_t i = 0; i < 32; i++)
		bad |= fprintf(f, "%02x", cfg->psk[i]) < 0;
	bad |= fprintf(f, "\n proto=RSN\n key_mgmt=WPA-PSK\n pairwise=CCMP\n group=CCMP\n}\n") < 0;
	bad |= fclose(f) != 0;
	return bad ? -1 : 0;
}
