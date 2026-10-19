/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wifi.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int current(struct wifi_net *n, char **data, size_t * size, struct stat *st)
{
	int fd = openat(n->resolver_dir, n->resolver_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (fstat(fd, st) || !S_ISREG(st->st_mode) || st->st_uid != 0 || st->st_nlink != 1 ||
	    (st->st_mode & 0022) || st->st_size < 0 || st->st_size > WIFI_RESOLVER_MAX) {
		close(fd);
		return -1;
	}
	*size = (size_t) st->st_size;
	*data = malloc(*size + 1);
	if (!*data) {
		close(fd);
		return -1;
	}
	ssize_t r = read(fd, *data, *size + 1);
	close(fd);
	if (r != (ssize_t) * size) {
		free(*data);
		*data = NULL;
		return -1;
	}
	return 0;
}
static int matches(struct wifi_net *n)
{
	struct stat st;
	char *data = NULL;
	size_t size = 0;
	if (current(n, &data, &size, &st))
		return -1;
	int ok = st.st_dev == n->resolver_dev && st.st_ino == n->resolver_ino && size == n->expected_len &&
	!memcmp(data, n->expected, size);
	free(data);
	return ok ? 0 : -1;
}
int wifi_resolver_open(struct wifi_net *n, const char *path)
{
	char copy[4096];
	struct stat st;
	int fd;
	if (strlen(path) >= sizeof(copy))
		return -1;
	strcpy(copy, path);
	char *slash = strrchr(copy, '/');
	if (!slash || !slash[1] || strlen(slash + 1) >= sizeof(n->resolver_name))
		return -1;
	strcpy(n->resolver_name, slash + 1);
	*slash = 0;
	/* Validate the complete path and every parent before using its directory. */
	fd = wifi_safe_open(path, O_RDONLY, 0, false);
	if (fd < 0 && errno == ENOENT)
		fd = wifi_safe_open(path, O_WRONLY | O_CREAT | O_EXCL, 0644, false);
	if (fd < 0)
		return -1;
	close(fd);
	n->resolver_dir = open(*copy ? copy : "/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (n->resolver_dir < 0)
		return -1;
	if (current(n, &n->baseline, &n->baseline_len, &st))
		goto fail;
	n->expected = malloc(n->baseline_len + 1);
	if (!n->expected)
		goto fail;
	memcpy(n->expected, n->baseline, n->baseline_len);
	n->expected_len = n->baseline_len;
	n->resolver_dev = st.st_dev;
	n->resolver_ino = st.st_ino;
	return 0;
fail:	close(n->resolver_dir);
	n->resolver_dir = -1;
	free(n->baseline);
	n->baseline = NULL;
	return -1;
}
static int replace(struct wifi_net *n, const char *data, size_t size)
{
	const char *tmp = ".nt98513-wifi-resolv.tmp";
	struct stat st;
	int fd;
	if (matches(n))
		return -1;
	if (size == n->expected_len && !memcmp(data, n->expected, size))
		return 0;
	char *expected = malloc(size + 1);
	if (!expected)
		return -1;
	memcpy(expected, data, size);
	fd = openat(n->resolver_dir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) {
		free(expected);
		return -1;
	}
	size_t done = 0;
	int ret = -1;
	while (done < size) {
		ssize_t w = write(fd, data + done, size - done);
		if (w <= 0)
			goto out;
		done += (size_t) w;
	}
	if (fsync(fd) || fstat(fd, &st) || matches(n))
		goto out;
	if (renameat(n->resolver_dir, tmp, n->resolver_dir, n->resolver_name))
		goto out;
	n->resolver_dev = st.st_dev;
	n->resolver_ino = st.st_ino;
	free(n->expected);
	n->expected = expected;
	expected = NULL;
	n->expected_len = size;
	ret = matches(n);
out:	free(expected);
	close(fd);
	unlinkat(n->resolver_dir, tmp, 0);
	return ret;
}
int wifi_resolver_set(struct wifi_net *n, const struct wifi_lease *l)
{
	char block[256], address[INET_ADDRSTRLEN];
	size_t used = 0;
	int r = snprintf(block, sizeof(block), "# nt98513-wifi %s begin\n", n->interface);
	if (r < 0 || (size_t) r >= sizeof(block))
		return -1;
	used = (size_t) r;
	for (unsigned i = 0; i < l->dns_count; i++) {
		if (!inet_ntop(AF_INET, &l->dns[i], address, sizeof(address)))
			return -1;
		r = snprintf(block + used, sizeof(block) - used, "nameserver %s\n", address);
		if (r < 0 || (size_t) r >= sizeof(block) - used)
			return -1;
		used += (size_t) r;
	}
	r = snprintf(block + used, sizeof(block) - used, "# nt98513-wifi %s end\n", n->interface);
	if (r < 0 || (size_t) r >= sizeof(block) - used)
		return -1;
	used += (size_t) r;
	if (n->baseline_len > WIFI_RESOLVER_MAX - used)
		return -1;
	char *result = malloc(used + n->baseline_len);
	if (!result)
		return -1;
	memcpy(result, block, used);
	memcpy(result + used, n->baseline, n->baseline_len);
	int ret = replace(n, result, used + n->baseline_len);
	free(result);
	if (!ret)
		n->dns_owned = true;
	return ret;
}
int wifi_resolver_clear(struct wifi_net *n)
{
	if (!n->dns_owned)
		return 0;
	if (replace(n, n->baseline, n->baseline_len))
		return -1;
	n->dns_owned = false;
	return 0;
}
