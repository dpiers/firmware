/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wifi.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PACKET_SIZE 68
struct runtime {
	struct wifi_state state;
	struct wifi_actions actions;
	struct wifi_net net;
	const struct wifi_config *config;
	pid_t supplicant, dhcp;
	int dir, lock, resolver_lock, manage, lease, monitor, request, stop_client;
	char directory[64], ctrl[72], token[33];
	uint64_t next_status, association_deadline;
	bool stopping, dirty;
};
static volatile sig_atomic_t interrupted;
static void signal_stop(int sig)
{
	(void)sig;
	interrupted = 1;
}
uint64_t wifi_now(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t))
		return 0;
	return (uint64_t) t.tv_sec * 1000 + (uint64_t) t.tv_nsec / 1000000;
}
int wifi_entropy(void *out, size_t len, unsigned deadline_ms)
{
	uint64_t end = wifi_now() + deadline_ms;
	size_t done = 0;
	while (done < len) {
		ssize_t n = getrandom((char *)out + done, len - done, GRND_NONBLOCK);
		if (n > 0) {
			done += (size_t) n;
			continue;
		}
		if (n < 0 && errno != EAGAIN && errno != EINTR)
			return -1;
		if (interrupted || wifi_now() >= end)
			return -1;
		struct timespec pause = {0, 10000000};
		nanosleep(&pause, NULL);
	}
	return 0;
}
static int random_token(char token[33])
{
	unsigned char bytes[16];
	if (wifi_entropy(bytes, sizeof(bytes), 1000))
		return -1;
	for (unsigned i = 0; i < 16; i++)
		snprintf(token + i * 2, 3, "%02x", bytes[i]);
	wifi_wipe(bytes, sizeof(bytes));
	return 0;
}
static int private_dir(const char *path)
{
	char copy[128], *save = NULL, *part, *next;
	struct stat st;
	if (path[0] != '/' || strlen(path) >= sizeof(copy))
		return -1;
	strcpy(copy, path);
	int dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dir < 0)
		return -1;
	part = strtok_r(copy, "/", &save);
	while (part) {
		next = strtok_r(NULL, "/", &save);
		if (!strcmp(part, ".") || !strcmp(part, "..") ||
		    fstat(dir, &st) || st.st_uid != 0 || (st.st_mode & 0022))
			goto fail;
		if (!next && mkdirat(dir, part, 0700) && errno != EEXIST)
			goto fail;
		int child = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (child < 0)
			goto fail;
		close(dir);
		dir = child;
		part = next;
	}
	if (fstat(dir, &st) || st.st_uid != 0 || (st.st_mode & 0777) != 0700)
		goto fail;
	return dir;
fail:
	close(dir);
	return -1;
}
static int private_file(int dir, const char *name, int flags)
{
	struct stat st;
	int fd = openat(dir, name, flags | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600) {
		close(fd);
		return -1;
	}
	return fd;
}
static int listener(const char *path)
{
	struct sockaddr_un a = {.sun_family = AF_UNIX};
	if (strlen(path) >= sizeof(a.sun_path))
		return -1;
	strcpy(a.sun_path, path);
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -1;
	if (bind(fd, (void *)&a, sizeof(a)) || listen(fd, 4)) {
		close(fd);
		return -1;
	}
	return fd;
}
static int connected(const char *path, unsigned timeout)
{
	struct sockaddr_un a = {.sun_family = AF_UNIX};
	struct timeval limit = {(time_t)timeout, 0};
	if (strlen(path) >= sizeof(a.sun_path))
		return -1;
	strcpy(a.sun_path, path);
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)) ||
	    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)) || connect(fd, (void *)&a, sizeof(a))) {
		close(fd);
		return -1;
	}
	return fd;
}
static int peer_root(int fd)
{
	struct ucred c;
	socklen_t size = sizeof(c);
	return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &size) || size != sizeof(c) || c.uid != 0 ? -1 : 0;
}
static int token_decode(const char *token, unsigned char out[16])
{
	if (!token || strlen(token) != 32)
		return -1;
	for (unsigned i = 0; i < 16; i++) {
		unsigned v = 0;
		for (unsigned j = 0; j < 2; j++) {
			char c = token[i * 2 + j];
			unsigned n;
			if (c >= '0' && c <= '9')
				n = (unsigned)(c - '0');
			else if (c >= 'a' && c <= 'f')
				n = (unsigned)(c - 'a' + 10);
			else
				return -1;
			v = v * 16 + n;
		}
		out[i] = (unsigned char)v;
	}
	return 0;
}
static void put32(unsigned char *p, uint32_t value)
{
	value = htonl(value);
	memcpy(p, &value, 4);
}
static uint32_t get32(const unsigned char *p)
{
	uint32_t n;
	memcpy(&n, p, 4);
	return ntohl(n);
}
int wifi_lease_entry(const char *action)
{
	pid_t parent = getppid();
	if (parent == 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
		return 1;
	const char *endpoint = getenv("NTW_ENDPOINT"), *generation = getenv("NTW_GENERATION");
	const char *iface = getenv("NTW_INTERFACE"), *actual = getenv("interface");
	unsigned char packet[PACKET_SIZE] = {0};
	struct wifi_lease l = {0};
	char *end;
	unsigned kind;
	if (getuid() != 0 || !endpoint || !generation || !iface || !actual || strcmp(iface, actual))
		return 1;
	if (!strcmp(action, "bound") || !strcmp(action, "renew"))
		kind = 1;
	else if (!strcmp(action, "deconfig"))
		kind = 2;
	else if (!strcmp(action, "nak") || !strcmp(action, "leasefail"))
		kind = 3;
	else
		return 1;
	for (const char *p = generation; *p; p++)
		if (*p < '0' || *p > '9')
			return 1;
	errno = 0;
	uint64_t gen = strtoull(generation, &end, 10);
	if (errno || *end || !gen)
		return 1;
	memcpy(packet, "NTW1", 4);
	put32(packet + 4, (uint32_t) (gen >> 32));
	put32(packet + 8, (uint32_t) gen);
	if (token_decode(getenv("NTW_TOKEN"), packet + 12))
		return 1;
	put32(packet + 28, kind);
	if (kind == 1 && wifi_lease_parse(getenv("ip"), getenv("subnet"), getenv("router"), getenv("dns"), getenv("lease"), &l))
		return 1;
	memcpy(packet + 32, &l.address, 4);
	memcpy(packet + 36, &l.mask, 4);
	memcpy(packet + 40, &l.router, 4);
	memcpy(packet + 44, l.dns, 12);
	put32(packet + 56, l.dns_count);
	put32(packet + 60, l.seconds);
	put32(packet + 64, if_nametoindex(iface));
	int fd = connected(endpoint, 60);
	if (fd < 0)
		return 1;
	char reply[3];
	int ret = peer_root(fd) || send(fd, packet, sizeof(packet), MSG_NOSIGNAL) != (ssize_t) sizeof(packet) || recv(fd, reply, sizeof(reply), 0) != 2 || memcmp(reply, "OK", 2);
	close(fd);
	return ret ? 1 : 0;
}
/* Parent retains waitable PID ownership until this function reaps it. */
static int reap(pid_t * pid, bool terminate)
{
	if (!*pid)
		return 0;
	pid_t target = *pid;
	int status;
	pid_t result;
	do {
		result = waitpid(target, &status, WNOHANG);
	} while (result < 0 && errno == EINTR);
	if (result == target) {
		*pid = 0;
		return 1;
	}
	if (result < 0) {
		*pid = 0;
		return -1;
	}
	if (!terminate)
		return 0;
	if (kill(target, SIGTERM) && errno != ESRCH)
		return -1;
	uint64_t deadline = wifi_now() + 2000;
	do {
		result = waitpid(target, &status, WNOHANG);
		if (result == target) {
			*pid = 0;
			return 1;
		}
		if (result < 0 && errno != EINTR) {
			*pid = 0;
			return -1;
		}
		struct timespec pause = {0, 10000000};
		nanosleep(&pause, NULL);
	} while (wifi_now() < deadline);
	if (kill(target, SIGKILL) && errno != ESRCH)
		return -1;
	do {
		result = waitpid(target, &status, 0);
	} while (result < 0 && errno == EINTR);
	*pid = 0;
	return result == target ? 1 : -1;
}
static pid_t child(struct runtime *r, bool dhcp)
{
	int errors[2];
	if (pipe2(errors, O_CLOEXEC))
		return -1;
	pid_t parent = getpid(), pid = fork();
	if (pid < 0) {
		close(errors[0]);
		close(errors[1]);
		return -1;
	}
	if (!pid) {
		close(errors[0]);
		int error = 0;
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
			goto fail;
		int log = private_file(r->dir, "diagnostic.log", O_WRONLY | O_APPEND);
		int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (log < 0 || null < 0 || dup2(null, 0) < 0 || dup2(log, 1) < 0 || dup2(log, 2) < 0)
			goto fail;
		struct rlimit cap = {65536, 65536}, core = {0, 0};
		if (setrlimit(RLIMIT_FSIZE, &cap) || setrlimit(RLIMIT_CORE, &core))
			goto fail;
		clearenv();
		setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
		setenv("LC_ALL", "C", 1);
		if (dhcp) {
			char gen[32], endpoint[128];
			snprintf(gen, sizeof(gen), "%" PRIu64, r->state.generation);
			snprintf(endpoint, sizeof(endpoint), "%s/lease", r->directory);
			setenv("NTW_GENERATION", gen, 1);
			setenv("NTW_TOKEN", r->token, 1);
			setenv("NTW_INTERFACE", r->config->interface, 1);
			setenv("NTW_ENDPOINT", endpoint, 1);
			execl("/bin/busybox", "busybox", "udhcpc", "-f", "-n", "-t", "5", "-T", "3", "-i", r->config->interface, "-s", "/usr/libexec/nt98513-wifi-lease", (char *)NULL);
		} else {
			char config[128];
			snprintf(config, sizeof(config), "%s/supplicant.conf", r->directory);
			execl("/usr/sbin/wpa_supplicant", "wpa_supplicant", "-Dnl80211", "-i", r->config->interface, "-c", config, (char *)NULL);
		}
fail:		error = errno ? errno : EIO;
		(void)write(errors[1], &error, sizeof(error));
		_exit(127);
	}
	close(errors[1]);
	int error = 0;
	ssize_t n;
	struct pollfd ready = {errors[0], POLLIN | POLLHUP, 0};
	do {
		int available = poll(&ready, 1, 3000);
		n = available > 0 ? read(errors[0], &error, sizeof(error)) : -1;
	} while (n < 0 && errno == EINTR);
	close(errors[0]);
	if (n != 0) {
		reap(&pid, true);
		return -1;
	}
	return pid;
}
static int start_dhcp(void *context, uint64_t generation)
{
	struct runtime *r = context;
	(void)generation;
	if (r->dhcp || random_token(r->token))
		return -1;
	r->dhcp = child(r, true);
	if (r->dhcp < 0) {
		r->dhcp = 0;
		return -1;
	}
	return 0;
}
static void stop_dhcp(void *context)
{
	struct runtime *r = context;
	memset(r->token, 0, sizeof(r->token));
	if (reap(&r->dhcp, true) < 0)
		r->dirty = true;
}
static int apply_lease(void *context, const struct wifi_lease *lease)
{
	struct runtime *r = context;
	return wifi_net_apply(&r->net, lease);
}
static int clear_lease(void *context)
{
	struct runtime *r = context;
	int ret = wifi_net_clear(&r->net);
	if (ret)
		r->dirty = true;
	return ret;
}
static int control_open(struct runtime *r, const char *name)
{
	struct sockaddr_un local = {.sun_family = AF_UNIX}, remote = {.sun_family = AF_UNIX};
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -1;
	snprintf(local.sun_path, sizeof(local.sun_path), "%s/%s", r->directory, name);
	snprintf(remote.sun_path, sizeof(remote.sun_path), "%s/%s", r->ctrl, r->config->interface);
	if (bind(fd, (void *)&local, sizeof(local)) || connect(fd, (void *)&remote, sizeof(remote))) {
		close(fd);
		return -1;
	}
	return fd;
}
static int request(int fd, const char *command, char *answer, size_t capacity)
{
	if (send(fd, command, strlen(command), MSG_NOSIGNAL) != (ssize_t) strlen(command))
		return -1;
	struct pollfd p = {fd, POLLIN, 0};
	int ready;
	do {
		ready = poll(&p, 1, 1000);
	} while (ready < 0 && errno == EINTR && !interrupted);
	if (ready != 1 || !(p.revents & POLLIN))
		return -1;
	ssize_t n = recv(fd, answer, capacity, MSG_TRUNC);
	if (n < 0 || (size_t) n >= capacity)
		return -1;
	answer[n] = 0;
	return 0;
}
static int reconcile(struct runtime *r)
{
	char status[4096], bssid[18] = "", *save = NULL, *line;
	bool completed = false;
	int id = -1;
	if (request(r->request, "STATUS", status, sizeof(status)))
		return -1;
	for (line = strtok_r(status, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		if (!strcmp(line, "wpa_state=COMPLETED"))
			completed = true;
		if (!strncmp(line, "id=", 3)) {
			char *end;
			long parsed = strtol(line + 3, &end, 10);
			if (*end || parsed < 0 || parsed > 2147483647)
				return -1;
			id = (int)parsed;
		}
		if (!strncmp(line, "bssid=", 6)) {
			if (strlen(line + 6) != 17)
				return -1;
			for (unsigned i = 0; i < 17; i++)
				if (i % 3 == 2 ? line[i + 6] != ':' : !((line[i + 6] >= '0' && line[i + 6] <= '9') || (line[i + 6] >= 'a' && line[i + 6] <= 'f')))
					return -1;
			strcpy(bssid, line + 6);
		}
	}
	uint64_t now = wifi_now();
	r->next_status = now + 2000;
	if (completed)
		r->association_deadline = 0;
	else if (!r->association_deadline)
		r->association_deadline = now + 60000;
	return wifi_state_status(&r->state, completed, id, bssid, now, &r->actions);
}
static int decode_lease(const unsigned char *p, struct wifi_lease *l)
{
	char ip[16], mask[16], router[16] = "", dns[64] = "", duration[16], item[16];
	if (get32(p + 56) > 3)
		return -1;
	if (!inet_ntop(AF_INET, p + 32, ip, sizeof(ip)) || !inet_ntop(AF_INET, p + 36, mask, sizeof(mask)))
		return -1;
	uint32_t gateway;
	memcpy(&gateway, p + 40, 4);
	if (gateway && !inet_ntop(AF_INET, p + 40, router, sizeof(router)))
		return -1;
	for (unsigned i = 0; i < get32(p + 56); i++) {
		if (!inet_ntop(AF_INET, p + 44 + i * 4, item, sizeof(item)))
			return -1;
		if (i)
			strcat(dns, " ");
		strcat(dns, item);
	}
	snprintf(duration, sizeof(duration), "%u", get32(p + 60));
	return wifi_lease_parse(ip, mask, router, dns, duration, l);
}
static void lease_event(struct runtime *r)
{
	int fd = accept4(r->lease, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (fd < 0)
		return;
	unsigned char p[PACKET_SIZE], token[16];
	struct pollfd wait = {fd, POLLIN, 0};
	bool good = false;
	if (peer_root(fd) || poll(&wait, 1, 1000) != 1)
		goto out;
	if (recv(fd, p, sizeof(p), MSG_TRUNC) != (ssize_t) sizeof(p) || memcmp(p, "NTW1", 4))
		goto out;
	uint64_t gen = ((uint64_t) get32(p + 4) << 32) | get32(p + 8);
	if (token_decode(r->token, token) || memcmp(token, p + 12, 16) || gen != r->state.generation || !r->dhcp ||
	get32(p + 64) != r->net.ifindex || if_nametoindex(r->config->interface) != r->net.ifindex)
		goto out;
	if (reconcile(r) || gen != r->state.generation)
		goto out;
	unsigned kind = get32(p + 28);
	struct wifi_lease l;
	if (kind == 1) {
		if (!decode_lease(p, &l) && !wifi_state_lease(&r->state, gen, &l, wifi_now(), &r->actions))
			good = true;
	} else if (kind == 2 || kind == 3) {
		/* Initial deconfig is normal; it must not kill the requesting DHCP child. */
		if (r->state.online) {
			if (clear_lease(r))
				r->state.failed = true;
			r->state.online = false;
			r->state.expires = 0;
		}
		if (!r->state.acquire_deadline)
			r->state.acquire_deadline = wifi_now() + 30000;
		good = true;
	}
out:	(void)send(fd, good ? "OK" : "NO", 2, MSG_NOSIGNAL);
	close(fd);
}
static void management(struct runtime *r)
{
	int fd = accept4(r->manage, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (fd < 0)
		return;
	char command[16];
	struct pollfd wait = {fd, POLLIN, 0};
	if (!peer_root(fd) && poll(&wait, 1, 1000) == 1) {
		ssize_t n = recv(fd, command, sizeof(command), MSG_TRUNC);
		if (n == 4 && !memcmp(command, "STOP", 4)) {
			r->stopping = true;
			r->stop_client = fd;
			return;
		} else if (n == 6 && !memcmp(command, "STATUS", 6)) {
			const char *state = r->state.failed ? "FAILED\n" : r->state.online ? "ONLINE\n" : r->state.completed ? "DHCP_WAIT\n" : "ASSOCIATING\n";
			(void)send(fd, state, strlen(state), MSG_NOSIGNAL);
		}
	}
	close(fd);
}
int wifi_client(const char *interface, const char *command)
{
	char path[128];
	if (!*interface || strlen(interface) > 15)
		return 1;
	for (const char *p = interface; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
			return 1;
	snprintf(path, sizeof(path), "/run/nt98513-wifi/%s/control", interface);
	int fd = connected(path, 3);
	if (fd < 0 || peer_root(fd)) {
		if (fd >= 0)
			close(fd);
		return 1;
	}
	char answer[64];
	ssize_t n;
	if (send(fd, command, strlen(command), MSG_NOSIGNAL) != (ssize_t) strlen(command)) {
		close(fd);
		return 1;
	}
	struct pollfd wait = {fd, POLLIN, 0};
	/* Two bounded child shutdowns plus bounded network transactions. */
	int ready;
	do {
		ready = poll(&wait, 1, !strcmp(command, "STOP") ? 30000 : 3000);
	} while (ready < 0 && errno == EINTR);
	n = ready == 1 ? recv(fd, answer, sizeof(answer) - 1, MSG_TRUNC) : -1;
	close(fd);
	if (n < 1 || (size_t)n >= sizeof(answer))
		return 1;
	answer[n] = 0;
	fputs(answer, stdout);
	if (!strcmp(command, "STOP"))
		return strcmp(answer, "STOPPED\n") ? 1 : 0;
	return 0;
}
int wifi_run(const struct wifi_config *cfg)
{
	struct runtime r = {.config = cfg,.dir = -1,.lock = -1,.resolver_lock = -1,.manage = -1,.lease = -1,.monitor = -1,.request = -1,.stop_client = -1};
	int base = -1, ret = 1;
	char path[128], reply[4096];
	unsigned char seed[16];
	bool admitted = false, marked = false;
	r.net.resolver_dir = -1;
	r.actions = (struct wifi_actions){
		&r, start_dhcp, stop_dhcp, apply_lease, clear_lease
	};
	struct sigaction sa = {.sa_handler = signal_stop};
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	if (wifi_entropy(seed, sizeof(seed), 30000))
		goto out;
	wifi_wipe(seed, sizeof(seed));
	base = private_dir("/run/nt98513-wifi");
	if (base < 0)
		goto out;
	r.resolver_lock = private_file(base, "resolver.lock", O_RDWR);
	if (r.resolver_lock < 0 || flock(r.resolver_lock, LOCK_EX | LOCK_NB))
		goto out;
	snprintf(r.directory, sizeof(r.directory), "/run/nt98513-wifi/%s", cfg->interface);
	r.dir = private_dir(r.directory);
	if (r.dir < 0)
		goto out;
	r.lock = private_file(r.dir, "session.lock", O_RDWR);
	if (r.lock < 0 || flock(r.lock, LOCK_EX | LOCK_NB))
		goto out;
	int marker = openat(r.dir, "active", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (marker < 0)
		goto out;
	close(marker);
	marked = true;
	snprintf(r.ctrl, sizeof(r.ctrl), "%s/wpa", r.directory);
	int ctrl = private_dir(r.ctrl);
	if (ctrl < 0)
		goto out;
	close(ctrl);
	if (wifi_config_render(r.dir, cfg, r.ctrl))
		goto out;
	snprintf(path, sizeof(path), "%s/control", r.directory);
	r.manage = listener(path);
	snprintf(path, sizeof(path), "%s/lease", r.directory);
	r.lease = listener(path);
	if (r.manage < 0 || r.lease < 0)
		goto out;
	if (wifi_net_admit(&r.net, cfg->interface))
		goto out;
	admitted = true;
	r.supplicant = child(&r, false);
	if (r.supplicant < 0) {
		r.supplicant = 0;
		goto out;
	}
	uint64_t deadline = wifi_now() + 10000;
	do {
		r.monitor = control_open(&r, "monitor");
		if (r.monitor >= 0)
			break;
		unlinkat(r.dir, "monitor", 0);
		if (reap(&r.supplicant, false) != 0 || interrupted)
			goto out;
		struct timespec pause = {0, 100000000};
		nanosleep(&pause, NULL);
	} while (wifi_now() < deadline);
	if (r.monitor < 0 || request(r.monitor, "ATTACH", reply, sizeof(reply)) || strcmp(reply, "OK\n"))
		goto out;
	r.request = control_open(&r, "request");
	if (r.request < 0)
		goto out;
	r.association_deadline = wifi_now() + 60000;
	if (reconcile(&r))
		goto out;
	while (!interrupted && !r.stopping && !r.state.failed) {
		uint64_t now = wifi_now();
		if (if_nametoindex(cfg->interface) != r.net.ifindex || reap(&r.supplicant, false) != 0)
			goto out;
		int dead = reap(&r.dhcp, false);
		if (dead < 0)
			goto out;
		if (dead && wifi_state_child_exit(&r.state, now, &r.actions))
			goto out;
		if (wifi_state_tick(&r.state, now, &r.actions))
			goto out;
		if (r.association_deadline && now >= r.association_deadline)
			goto out;
		if (now >= r.next_status && reconcile(&r))
			goto out;
		struct pollfd fds[3] = {{r.monitor, POLLIN, 0}, {r.manage, POLLIN, 0}, {r.lease, POLLIN, 0}};
		int ready = poll(fds, 3, 100);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		}
		if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			goto out;
		if (fds[0].revents & POLLIN) {
			ssize_t n;
			do {
				n = recv(r.monitor, reply, sizeof(reply) - 1, MSG_TRUNC);
				if (n == 0 || n >= (ssize_t)sizeof(reply))
					goto out;
				if (n > 0) {
					reply[n] = 0;
					char *event = reply;
					if (*event == '<') {
						char *end = strchr(event, '>');
						if (!end || end - event > 4)
							goto out;
						event = end + 1;
					}
					if (!strncmp(event, "CTRL-EVENT-DISCONNECTED", 23) &&
					    (event[23] == ' ' || event[23] == '\n' || !event[23])) {
						if (wifi_state_disconnect(&r.state, &r.actions))
							goto out;
					}
				}
			} while (n > 0);
			if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				goto out;
			if (reconcile(&r))
				goto out;
		}
		if (fds[1].revents & POLLIN)
			management(&r);
		if (!r.stopping && !interrupted && (fds[2].revents & POLLIN))
			lease_event(&r);
	}
	ret = r.state.failed ? 1 : 0;
out:
	if (admitted) {
		if (wifi_state_disconnect(&r.state, &r.actions))
			ret = 1;
	} else
		stop_dhcp(&r);
	if (reap(&r.supplicant, true) < 0) {
		r.dirty = true;
		ret = 1;
	}
	if (admitted) {
		if (wifi_net_close(&r.net)) {
			r.dirty = true;
			ret = 1;
		}
	}
	else {
		if (r.net.resolver_dir >= 0)
			close(r.net.resolver_dir);
		free(r.net.baseline);
		free(r.net.expected);
	}
	int *fds[] = {&r.monitor, &r.request, &r.manage, &r.lease};
	for (unsigned i = 0; i < 4; i++)
		if (*fds[i] >= 0)
			close(*fds[i]);
	if (marked) {
		const char *names[] = {"control", "lease", "monitor", "request", "supplicant.conf"};
		for (unsigned i = 0; i < 5; i++)
			unlinkat(r.dir, names[i], 0);
		snprintf(path, sizeof(path), "wpa/%s", cfg->interface);
		unlinkat(r.dir, path, 0);
		if (!r.dirty)
			unlinkat(r.dir, "active", 0);
	}
	if (r.lock >= 0)
		close(r.lock);
	if (r.dir >= 0)
		close(r.dir);
	if (r.resolver_lock >= 0)
		close(r.resolver_lock);
	if (base >= 0)
		close(base);
	if (r.stop_client >= 0) {
		const char *result = ret || r.dirty ? "FAILED\n" : "STOPPED\n";
		(void)send(r.stop_client, result, strlen(result), MSG_NOSIGNAL);
		close(r.stop_client);
	}
	return ret || r.dirty ? 1 : 0;
}
