/* SPDX-License-Identifier: GPL-2.0-only */
/* Inert device/time boundaries; the complete production core is included below. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "nt98513-mmc.h"
#include "nt98513-mmc-core.c"

static unsigned local_irq_depth;

struct fixture {
	struct nt_mmc c;
	uint32_t regs[128];
	uint64_t now;
	unsigned reads, writes, fifo_read, fifo_write, done, locked, irq_locked;
	unsigned mode, stage, copied, length, direction, fail_reset, fault_read, fault_reads;
	uint64_t ready_at;
	unsigned late_ready_reads;
	unsigned char bytes[512];
	struct nt_mmc_request next;
	int resubmit;
};

static unsigned rd(void *p, unsigned reg)
{
	struct fixture *f = p;
	f->reads++;
	if (f->fault_read == reg + 1) {
		f->fault_reads++;
		return ~0U;
	}
	if (reg == 0x40 && f->mode == 12 && f->now >= f->ready_at)
		f->late_ready_reads++;
	if (reg == 0x40 && f->mode == 13)
		f->now = f->ready_at;
	if (reg == 0x100) {
		unsigned n = f->fifo_read++ * 4;
		return (n & 255) | (((n + 1) & 255) << 8) |
		       (((n + 2) & 255) << 16) | (((n + 3) & 255) << 24);
	}
	return f->regs[reg / 4];
}
static void wr(void *p, unsigned reg, unsigned value)
{
	struct fixture *f = p;
	f->writes++;
	if (reg == 0x10c) assert(!(value & 2));
	if (reg == 0x28) {
		f->regs[reg / 4] &= ~value;
		return;
	}
	if (reg == 0x100) {
		unsigned n = f->fifo_write++ * 4, i;
		for (i = 0; i < 4; i++)
			assert(((value >> (i * 8)) & 255) ==
			       (n + i < f->length ? ((n + i) & 255) : 0));
		return;
	}
	if ((reg == 0 && (value & 0x400)) || (reg == 0x4c && (value & 1))) {
		f->regs[reg / 4] = f->fail_reset ? value : value & ~(reg ? 1U : 0x600U);
		return;
	}
	f->regs[reg / 4] = value;
	if (reg == 0 && (value & 0x200)) {
		f->regs[0] &= ~0x200U;
		f->stage = 1;
	}
}
static unsigned long long now_us(void *p) { return ((struct fixture *)p)->now; }
static void lock(void *p) { struct fixture *f = p; assert(!f->locked); f->locked = 1; }
static void unlock(void *p) { struct fixture *f = p; assert(f->locked); f->locked = 0; }
static unsigned long ilock(void *p) { struct fixture *f = p; unsigned flags = local_irq_depth; assert(!f->irq_locked); f->irq_locked = 1; local_irq_depth = 1; return flags; }
static void iunlock(void *p, unsigned long flags) { struct fixture *f = p; assert(f->irq_locked); f->irq_locked = 0; local_irq_depth = flags; }
static void sync_irq(void *p) { assert(!((struct fixture *)p)->irq_locked); }
static int copy(void *p, struct nt_mmc_request *r, unsigned char *bytes, int to_device)
{
	struct fixture *f = p;
	unsigned i;
	assert(f->locked);
	if (to_device)
		for (i = 0; i < r->length; i++) bytes[i] = i & 255;
	else
		memcpy(f->bytes, bytes, r->length);
	f->copied++;
	return r->length;
}
static void done(void *p, struct nt_mmc_request *r)
{
	struct fixture *f = p;
	assert(!f->locked && !f->irq_locked && f->c.active != r);
	f->done++;
	if (f->resubmit) {
		f->resubmit = 0;
		nt_mmc_submit(&f->c, &f->next);
	}
	/* Poison the completed request to catch reads after notification. */
	r->cookie = (void *)(uintptr_t)0xdead;
}
static void relax(void *p)
{
	struct fixture *f = p;
	unsigned status = 0;
	f->now += 100;
	if (f->mode == 12 && f->now >= f->ready_at)
		f->regs[0x40 / 4] = 1;
	if (!f->stage || f->mode == 1)
		return;
	if (f->mode == 2)
		status = 0xb2; /* Progress and data CRC error in the same sample. */
	else if (f->mode == 3)
		status = 0xc0; /* Data done plus0x40 must not mark command done. */
	else if (f->mode == 7)
		status = 1; /* R3 may finish with a CRC-fail indication. */
	else if (f->mode == 10)
		status = 0x80; /* Data completion cannot replace command/FIFO progress. */
	else if (f->mode == 4)
		status = f->stage == 1 ? 0x80 : 0x10;
	else
		status = f->length ? 0x90 : 0x10;
	f->regs[0x28 / 4] |= status;
	f->regs[0x108 / 4] = 0x300 | (((f->length + 3) / 4) & 31);
	if (f->mode == 6 && f->stage == 1)
		f->now += 11000000;
	if (f->mode == 8)
		f->regs[0x108 / 4] &= ~0x100U;
	if (f->mode == 9)
		f->regs[0x108 / 4] = 3;
	f->stage++;
	if (f->mode == 11)
		f->fault_read = 0x29;
	if (f->mode != 5)
		nt_mmc_irq(&f->c);
}
static const struct nt_mmc_ops ops = {
	rd, wr, now_us, relax, lock, unlock, ilock, iunlock, sync_irq, copy, done
};
static void setup(struct fixture *f)
{
	memset(f, 0, sizeof(*f));
	f->regs[0x40 / 4] = 1;
	nt_mmc_init(&f->c, &ops, f);
	assert(nt_mmc_admit(&f->c) == 0);
	assert(f->writes == 0);
	assert(nt_mmc_initialize(&f->c, 400000) == 0);
}
static struct nt_mmc_request request(unsigned op, unsigned response, unsigned len, unsigned write)
{
	struct nt_mmc_request r;
	memset(&r, 0, sizeof(r));
	r.opcode = op; r.response = response; r.length = len;
	r.blocks = len ? 1 : 0; r.write = write;
	r.timeout_ns = 100000000; r.cookie = (void *)(uintptr_t)1;
	return r;
}
static void transaction(unsigned length, unsigned write)
{
	struct fixture f;
	struct nt_mmc_request r = request(length ? (write ? 24 : 17) : 13, 21, length, write);
	unsigned i;
	setup(&f); f.length = length;
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(f.done == 1 && r.error == 0 && r.bytes == length);
	assert((write ? f.fifo_write : f.fifo_read) == (length + 3) / 4);
	if (!write)
		for (i = 0; i < length; i++) assert(f.bytes[i] == (i & 255));
	nt_mmc_irq(&f.c); nt_mmc_run(&f.c);
	assert(f.done == 1);
}
static void fault_tests(void)
{
	unsigned init_regs[] = { 0, 0x10c, 0x1c, 0x1b0, 0x4c, 0x54, 0x3c, 0x30 };
	unsigned request_regs[] = { 0, 0x10c, 0x1c, 0x30, 0x40, 0x108, 0x28 };
	unsigned i, before;
	struct fixture f;
	struct nt_mmc_request r;
	for (i = 0; i < sizeof(init_regs) / sizeof(init_regs[0]); i++) {
		memset(&f, 0, sizeof(f)); f.regs[0x40 / 4] = 1;
		nt_mmc_init(&f.c, &ops, &f); assert(!nt_mmc_admit(&f.c));
		f.fault_read = init_regs[i] + 1;
		assert(nt_mmc_initialize(&f.c, 400000) < 0 && f.c.fault);
		before = f.reads + f.writes; nt_mmc_irq(&f.c);
		assert(before == f.reads + f.writes);
	}
	for (i = 0; i < sizeof(request_regs) / sizeof(request_regs[0]); i++) {
		setup(&f); f.length = 8; f.fault_read = request_regs[i] + 1;
		r = request(51, 21, 8, 0); nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
		assert(r.error && !r.bytes && f.done == 1 && f.c.fault);
		before = f.reads + f.writes; nt_mmc_irq(&f.c);
		assert(before == f.reads + f.writes);
	}
}

static void adapter_tests(void);
static void probe_tests(void);
static void resource_tests(void);
static void adapter_fault_tests(void);
static void irq_boundary_tests(void);
static void async_mmio_tests(void);

int main(void)
{
	unsigned sizes[] = { 0, 1, 3, 4, 8, 64, 128, 512 }, i;
	struct fixture f;
	struct nt_mmc_request r;
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		transaction(sizes[i], 0); transaction(sizes[i], 1);
	}
	for (i = 1; i <= 5; i++) {
		setup(&f); f.mode = i; f.length = 8;
		r = request(51, 21, 8, 0);
		nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
		assert(f.done == 1);
		if (i <= 3) assert(r.error && !r.bytes);
		else assert(!r.error && r.bytes == 8);
	}
	/* Readiness at the deadline, including time spent in the final read,
	 * cannot authorize a command launch. */
	for (i = 12; i <= 13; i++) {
		setup(&f); f.mode = i; f.length = 8;
		f.regs[0x40 / 4] = i == 13;
		r = request(51, 21, 8, 0);
		nt_mmc_submit(&f.c, &r); f.ready_at = r.submitted_us + NT_COMMAND_US;
		nt_mmc_run(&f.c);
		assert(r.error == -ETIMEDOUT && !f.stage && !r.bytes && !f.late_ready_reads);
	}
	/* Deadline cannot be refreshed or passed merely because success arrives late. */
	setup(&f); f.mode = 6; f.length = 8; r = request(51, 21, 8, 0);
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(r.error == -ETIMEDOUT && !r.bytes);
	for (i = 8; i <= 10; i++) {
		setup(&f); f.mode = i; f.length = 3;
		r = request(17, 21, 3, i == 8);
		nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
		assert(r.error == -ETIMEDOUT && !r.bytes && !f.fifo_read && !f.fifo_write);
	}
	setup(&f); f.mode = 7; r = request(41, 1, 0, 0);
	f.regs[8 / 4] = 0x80ff8000;
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(!r.error && r.resp[0] == 0x80ff8000);
	setup(&f); r = request(2, 7, 0, 0);
	for (i = 0; i < 4; i++) f.regs[2 + i] = 0x100 + i;
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(!r.error && r.resp[0] == 0x103 && r.resp[3] == 0x100);
	setup(&f); r = request(0, 0, 0, 0);
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c); assert(!r.error);
	setup(&f); r = request(13, 21, 0, 0); f.mode = 1; f.fail_reset = 1;
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(r.error && f.c.fault);
	i = f.writes; r = request(13, 21, 0, 0); nt_mmc_submit(&f.c, &r);
	assert(r.error && f.writes == i);
	setup(&f); r = request(13, 21, 0, 0); nt_mmc_submit(&f.c, &r);
	nt_mmc_stopping(&f.c); nt_mmc_run(&f.c);
	assert(!r.error && f.done == 1); assert(!nt_mmc_shutdown(&f.c));
	setup(&f); f.c.pending = 0x10; f.regs[0x28 / 4] = 0x10; f.mode = 1;
	r = request(13, 21, 0, 0); nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(r.error == -ETIMEDOUT); /* Neither software nor silicon stale status completes a new request. */
	setup(&f); r = request(13, 21, 0, 0); nt_mmc_submit(&f.c, &r);
	i = f.writes; f.now += 1000000; nt_mmc_run(&f.c);
	assert(r.error == -ETIMEDOUT && f.writes == i);
	setup(&f); f.fault_read = 0x29; r = request(13, 21, 0, 0);
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c); assert(r.error && f.c.fault);
	i = f.reads + f.writes; nt_mmc_irq(&f.c); assert(f.reads + f.writes == i);
	setup(&f); f.mode = 11; r = request(13, 21, 0, 0);
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(r.error && f.c.fault && f.fault_reads == 1);
	setup(&f); f.resubmit = 1;
	f.next = request(13, 21, 0, 0); r = request(12, 21, 0, 0);
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c);
	assert(f.done == 1 && f.c.active == &f.next);
	nt_mmc_run(&f.c); assert(f.done == 2);
	setup(&f); r = request(17, 21, 513, 0); i = f.writes;
	nt_mmc_submit(&f.c, &r); assert(f.done == 1 && r.error && i == f.writes);
	setup(&f); r = request(38, 29, 0, 0); r.busy_timeout_ms = 120000;
	nt_mmc_submit(&f.c, &r); nt_mmc_run(&f.c); assert(!r.error);
	setup(&f); nt_mmc_stopping(&f.c); r = request(13, 21, 0, 0); i = f.writes;
	nt_mmc_submit(&f.c, &r); assert(r.error && f.done == 1 && f.writes == i);
	fault_tests();
	async_mmio_tests();
	irq_boundary_tests();
	adapter_tests();
	adapter_fault_tests();
	resource_tests();
	probe_tests();
	puts("NT98513 actual request/IRQ/worker/lifetime and Linux adapter tests passed");
	return 0;
}

/* Linux adapter is extracted unchanged; only kernel/service boundaries are fake. */
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define MMC_DATA_WRITE 256U
#define MMC_DATA_READ 512U
#define GFP_ATOMIC 0
#define IRQ_HANDLED 1
#define IRQ_NONE 0
#define mmc_resp_type(cmd) ((cmd)->flags & 31)
#define for_each_sg(list, entry, count, index) \
	for ((index) = 0, (entry) = (list); (index) < (count); (index)++, (entry)++)
#define dev_err(...) ((void)0)
static int test_read_once(int value);
#define READ_ONCE(x) test_read_once(x)
static void test_latch_boundary(void);
void test_local_restore(void);
#define local_irq_save(flags) do { (flags) = local_irq_depth; local_irq_depth = 1; } while (0)
#define local_irq_restore(flags) do { local_irq_depth = (flags); test_local_restore(); } while (0)
typedef struct { int value; } atomic_t;
static void atomic_set(atomic_t *a, int v) { a->value = v; }
static int atomic_cmpxchg(atomic_t *a, int old, int value)
{
	int previous = a->value;
	if (previous == old) { a->value = value; test_latch_boundary(); }
	return previous;
}
typedef int irqreturn_t;
struct mutex { int held; };
struct scatterlist { unsigned length; unsigned char *buffer; };
struct mmc_command { unsigned opcode, arg, flags, busy_timeout, resp[4]; int error; };
struct mmc_data { unsigned sg_len, blocks, blksz, flags, timeout_ns, timeout_clks, bytes_xfered; int error; struct scatterlist *sg; };
struct mmc_request { struct mmc_command *cmd, *sbc, *stop; struct mmc_data *data; };
struct mmc_ios { unsigned clock, bus_width, timing, signal_voltage, vdd; };
struct mmc_host {
	void *private;
	const void *ops;
	unsigned actual_clock, f_min, f_max, ocr_avail, caps, caps2;
	unsigned max_blk_count, max_blk_size, max_req_size, max_seg_size, max_segs;
};
struct work_struct { int pending; };
struct nt98513_mmc_clock { unsigned on, fault, starts, resumes, stops; };
struct nt_host {
	struct mmc_host *mmc;
	struct nt98513_mmc_clock *clock;
	struct mutex lock;
	int irq_lock;
	void *regs;
	bool irq_live, registered;
	struct work_struct work;
	struct nt_mmc core;
	int irq;
	atomic_t irq_disabled;
};
struct nt_request { struct nt_mmc_request core; struct mmc_request *mrq; };
static struct fixture af;
static unsigned allocations, notifications, disabled_irqs, in_hard_irq, sync_calls;
static struct mmc_request *resubmit_mrq;
static int probe_active, probe_fail, probe_admitted, probe_started;
static unsigned probe_freed, probe_irq_freed, probe_released;
static struct nt98513_mmc_clock probe_clock;
static const struct nt_mmc_ops adapter_ops;
static void nt_set_ios(struct mmc_host *, struct mmc_ios *);
static irqreturn_t nt_irq(int, void *);
static void nt_request(struct mmc_host *, struct mmc_request *);
static void *mmc_priv(struct mmc_host *m) { return m->private; }
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
static int nt98513_mmc_clock_fault(struct nt98513_mmc_clock *c) { return c->fault; }
static int nt98513_mmc_clock_stop(struct nt98513_mmc_clock *c) { if (c->fault) return c->fault; c->on = 0; c->stops++; return 0; }
static int nt98513_mmc_clock_rate(struct nt98513_mmc_clock *c, unsigned rate, unsigned *actual) { if (c->fault) return c->fault; c->on = 1; c->resumes++; *actual = rate; return 0; }
static int schedule_work(struct work_struct *w) { w->pending = 1; return 1; }
static void disable_irq_nosync(int irq) { (void)irq; assert(!af.irq_locked); disabled_irqs++; }
static void synchronize_irq(int irq) { (void)irq; assert(!af.irq_locked && !in_hard_irq && !local_irq_depth); sync_calls++; }
static void *kzalloc(size_t bytes, unsigned flags) { (void)flags; allocations++; return calloc(1, bytes); }
static void kfree(void *p) { assert(allocations); allocations--; free(p); }
static unsigned sg_copy_to_buffer(struct scatterlist *sg, unsigned count, void *buffer, unsigned size)
{
	unsigned i, offset = 0;
	for (i = 0; i < count; i++) {
		unsigned take = sg[i].length < size - offset ? sg[i].length : size - offset;
		memcpy((char *)buffer + offset, sg[i].buffer, take); offset += take;
	}
	return offset;
}
static unsigned sg_copy_from_buffer(struct scatterlist *sg, unsigned count, const void *buffer, unsigned size)
{
	unsigned i, offset = 0;
	for (i = 0; i < count; i++) {
		unsigned take = sg[i].length < size - offset ? sg[i].length : size - offset;
		memcpy(sg[i].buffer, (const char *)buffer + offset, take); offset += take;
	}
	return offset;
}
static void mmc_request_done(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct nt_host *h = mmc_priv(mmc);
	(void)mrq;
	assert(!h->lock.held);
	if (h->core.fault && h->irq_live) assert(disabled_irqs == 1);
	if (h->core.active)
		assert(container_of(h->core.active, struct nt_request, core)->mrq != mrq);
	notifications++;
	if (resubmit_mrq) {
		struct mmc_request *next = resubmit_mrq;
		resubmit_mrq = NULL;
		nt_request(mmc, next);
	}
}

/* Probe fault boundaries; actual nt_probe and nt_retire remain unchanged. */
typedef unsigned u32;
struct device_node { unsigned index, width, frequency, children, assertions, irq_count, args_count, hwirq; int linux_irq, missing_mem, bad_parent, bad_domain, missing_data, missing_domain; };
struct device { struct device_node *of_node; };
struct resource { unsigned long long start, size; };
struct platform_device { struct device dev; void *data; unsigned num_resources; struct resource mem; };
struct of_phandle_args { struct device_node *np; unsigned args_count, args[2]; };
struct irq_domain { struct device_node *node; };
struct irq_data { unsigned hwirq; struct irq_domain *domain; };
static struct device_node device_node, parent_node, wrong_node;
static struct irq_domain irq_domain;
static struct irq_data irq_data;
#define IORESOURCE_MEM 1
static unsigned of_get_available_child_count(struct device_node *n) { return n->children; }
static int of_property_read_bool(struct device_node *n, const char *name) {
	if (!strcmp(name, "openipc,linux-exclusive-ownership")) return !!(n->assertions & 1);
	if (!strcmp(name, "openipc,qualified-inherited-board-state")) return !!(n->assertions & 2);
	if (!strcmp(name, "openipc,stable-192mhz-parent")) return !!(n->assertions & 4);
	abort();
}
static int of_property_read_u32(struct device_node *n, const char *name, u32 *value) {
	if (!strcmp(name, "openipc,controller-index")) *value = n->index;
	else if (!strcmp(name, "bus-width")) *value = n->width;
	else if (!strcmp(name, "max-frequency")) *value = n->frequency;
	else abort();
	return 0;
}
static struct resource *platform_get_resource_byname(struct platform_device *p, unsigned type, const char *name) {
	assert(type == IORESOURCE_MEM && !strcmp(name, "host"));
	return p->dev.of_node->missing_mem ? NULL : &p->mem;
}
static unsigned long long resource_size(struct resource *r) { return r->size; }
int of_irq_count(struct device_node *n) { return n->irq_count; }
static int of_irq_parse_one(struct device_node *n, unsigned i, struct of_phandle_args *args) {
	assert(i == 0); if (!n->irq_count) return -EINVAL;
	args->np = n->bad_parent ? &wrong_node : &parent_node;
	args->args_count = n->args_count; args->args[0] = n->hwirq; return 0;
}
static int of_device_is_compatible(struct device_node *n, const char *name) { return n == &parent_node && !strcmp(name, "openipc,nt98513-intc"); }
static void of_node_put(struct device_node *n) { assert(n == &parent_node || n == &wrong_node); }
static int platform_get_irq(struct platform_device *p, unsigned i) { assert(i == 0); return p->dev.of_node->linux_irq; }
static struct irq_data *irq_get_irq_data(int irq) {
	assert(irq == device_node.linux_irq);
	irq_domain.node = device_node.bad_domain ? &wrong_node : &parent_node;
	irq_data.domain = device_node.missing_domain ? NULL : &irq_domain;
	irq_data.hwirq = device_node.hwirq;
	return device_node.missing_data ? NULL : &irq_data;
}
static unsigned irqd_to_hwirq(struct irq_data *d) { return d->hwirq; }
struct device_node *irq_domain_get_of_node(struct irq_domain *d) { return d->node; }
static void valid_platform(struct platform_device *p, unsigned id) {
	memset(p, 0, sizeof(*p)); memset(&device_node, 0, sizeof(device_node));
	device_node.index = id; device_node.width = 1; device_node.frequency = 12000000;
	device_node.assertions = 7; device_node.irq_count = 1; device_node.args_count = 1;
	device_node.hwirq = 30 + id; device_node.linux_irq = 100 + id;
	p->dev.of_node = &device_node;
	/* Linux6.6 OF counts addresses only; IRQ is resolved separately. */
	p->num_resources = 1; p->mem.start = id ? 0xc0500000 : 0xc0420000; p->mem.size = 0x200;
}
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) > -4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define nt_ops adapter_ops
#define MMC_VDD_32_33 (1U << 20)
#define MMC_VDD_33_34 (1U << 21)
#define MMC_CAP_NEEDS_POLL 1
#define MMC_CAP2_NO_SDIO 2
#define MMC_CAP2_NO_MMC 4
static const int nt_host_ops;
static void mutex_init(struct mutex *m) { m->held = 0; }
static void spin_lock_init(int *s) { *s = 0; }
#define INIT_WORK(w, fn) do { (w)->pending = 0; } while (0)
static struct mmc_host *mmc_alloc_host(size_t size, struct device *d)
{
	struct mmc_host *m = calloc(1, sizeof(*m));
	(void)d; m->private = calloc(1, size); return m;
}
static void mmc_free_host(struct mmc_host *m) { probe_freed++; free(m->private); free(m); }
static struct nt98513_mmc_clock *devm_nt98513_mmc_clock_get(struct device *d, unsigned id)
{
	(void)d; assert(id == 0); return probe_fail == 1 ? (void *)(intptr_t)-EIO : &probe_clock;
}
static int nt98513_mmc_clock_admit(struct nt98513_mmc_clock *c)
{
	(void)c; if (probe_fail == 2) return -EIO; probe_admitted = 1; return 0;
}
static void *devm_platform_ioremap_resource_byname(struct platform_device *p, const char *name)
{
	(void)p; assert(probe_admitted && !strcmp(name, "host"));
	if (probe_fail == 3) return (void *)(intptr_t)-EIO;
	if (probe_fail == 4) af.fault_read = 1;
	return af.regs;
}
static int nt98513_mmc_clock_start(struct nt98513_mmc_clock *c, unsigned rate, unsigned *actual)
{
	assert(probe_admitted && af.reads >= 3 && af.writes == 1 && rate == 400000);
	if (probe_fail == 5) { c->fault = -EIO; return -EIO; }
	probe_started = 1; c->on = 1; c->starts++; *actual = rate;
	if (probe_fail == 6) af.fail_reset = 1;
	return 0;
}
static int nt98513_mmc_clock_release(struct nt98513_mmc_clock *c)
{
	if (c->fault || (probe_started && c->on)) return -EIO;
	probe_released++; return 0;
}
static void test_physical_access(unsigned offset);
static unsigned readl(void *address) {
	unsigned offset = (unsigned)((char *)address - (char *)af.regs);
	unsigned value = rd(&af, offset);
	test_physical_access(offset);
	return value;
}
static void writel(unsigned value, void *address) {
	unsigned offset = (unsigned)((char *)address - (char *)af.regs);
	wr(&af, offset, value); test_physical_access(offset);
}
static const char *dev_name(struct device *d) { (void)d; return "test"; }
static int request_irq(int irq, irqreturn_t (*fn)(int, void *), unsigned flags, const char *name, void *p)
{
	struct nt_host *h = p;
	(void)irq; (void)fn; (void)flags; (void)name;
	assert(h->core.initialized && probe_started);
	return probe_fail == 7 ? -EIO : 0;
}
static void platform_set_drvdata(struct platform_device *p, void *data) { p->data = data; }
static int mmc_add_host(struct mmc_host *m)
{
	struct nt_host *h = mmc_priv(m);
	assert(h->core.initialized && h->irq_live && h->clock->on);
	return probe_fail == 8 ? -EIO : 0;
}
static void mmc_remove_host(struct mmc_host *m)
{
	struct mmc_ios ios = { 0, 0, 0, 0, 0 };
	nt_set_ios(m, &ios);
}
static void flush_work(struct work_struct *w) { assert(!w->pending); }
static void free_irq(int irq, void *p)
{
	unsigned before = af.reads + af.writes;
	nt_irq(irq, p);
	assert(before == af.reads + af.writes);
	probe_irq_freed++;
}

#include "nt-adapter.inc"

static unsigned ar(void *p, unsigned reg) { (void)p; assert(!probe_active || probe_admitted); return nt_host_read(p, reg); }
static void aw(void *p, unsigned reg, unsigned value) { nt_host_write(p, reg, value); }
static unsigned long long an(void *p) { (void)p; return now_us(&af); }
static void ax(void *p) { (void)p; relax(&af); }
static void al(void *p) { mutex_lock(&((struct nt_host *)p)->lock); }
static void au(void *p) { mutex_unlock(&((struct nt_host *)p)->lock); }
static unsigned long ai(void *p) { (void)p; return ilock(&af); }
static void test_async_unlock(void *p);
static void test_unlock_boundary(void *p);
static void aj(void *p, unsigned long flags) { iunlock(&af, flags); test_unlock_boundary(p); test_async_unlock(p); }
static void az(void *p) { nt_host_sync_irq(p); }
static int ac(void *p, struct nt_mmc_request *r, unsigned char *b, int d) { return nt_host_copy(p, r, b, d); }
static const struct nt_mmc_ops adapter_ops = { ar, aw, an, ax, al, au, ai, aj, az, ac, nt_host_done };
static void run_queued(struct nt_host *h)
{
	unsigned count = 0;
	while (h->work.pending) {
		assert(count++ < 10);
		h->work.pending = 0;
		nt_work(&h->work);
	}
}
static void adapter_tests(void)
{
	struct nt_host h;
	struct mmc_host mmc;
	struct nt98513_mmc_clock clock = { 1, 0, 1, 0, 0 };
	struct mmc_ios ios = { 0, 0, 0, 0, 0 };
	struct mmc_command command = { .opcode = 51, .flags = 21 };
	struct mmc_command recovery = { .opcode = 12, .flags = 21 };
	unsigned char a[3] = { 0 }, b[5] = { 0 };
	struct scatterlist sg[2] = { { 3, a }, { 5, b } };
	struct mmc_data data = { .sg_len = 2, .blocks = 1, .blksz = 8, .flags = MMC_DATA_READ, .sg = sg };
	struct mmc_request mrq = { .cmd = &command, .data = &data };
	struct mmc_request next = { .cmd = &recovery };
	unsigned before, i;
	notifications = 0;
	memset(&h, 0, sizeof(h)); memset(&af, 0, sizeof(af));
	mmc.private = &h; h.mmc = &mmc; h.clock = &clock; h.regs = af.regs; h.irq_live = true;
	disabled_irqs = 0;
	af.regs[0x40 / 4] = 1; af.mode = 5;
	nt_mmc_init(&h.core, &adapter_ops, &h);
	assert(!nt_mmc_admit(&h.core)); assert(!nt_mmc_initialize(&h.core, 400000));
	/* Actual set_ios OFF -> UP(clock0) -> ON must remain usable. */
	nt_set_ios(&mmc, &ios); assert(!h.core.fault && !clock.on);
	before = af.reads + af.writes;
	nt_irq(30, &h); assert(af.reads + af.writes == before);
	nt_set_ios(&mmc, &ios); assert(!h.core.fault);
	ios.vdd = 21; ios.clock = 400000;
	nt_set_ios(&mmc, &ios); assert(!h.core.fault && clock.on && clock.starts == 1);
	/* SG copy actually crosses the3/5 split through production adapter code. */
	af.length = 8; resubmit_mrq = &next;
	nt_request(&mmc, &mrq); run_queued(&h);
	assert(notifications == 2 && !command.error && data.bytes_xfered == 8);
	for (i = 0; i < 3; i++) assert(a[i] == i);
	for (i = 0; i < 5; i++) assert(b[i] == i + 3);
	assert(!recovery.error && !allocations);
	/* Rejecting an overlapping request must leave the existing request owned. */
	{
		struct mmc_command first_cmd = { .opcode = 13, .flags = 21 };
		struct mmc_command second_cmd = { .opcode = 12, .flags = 21 };
		struct mmc_request first = { .cmd = &first_cmd };
		struct mmc_request second = { .cmd = &second_cmd };
		nt_request(&mmc, &first); nt_request(&mmc, &second);
		assert(second_cmd.error == -EBUSY && h.core.active && allocations == 1);
		run_queued(&h); assert(!first_cmd.error && !allocations);
	}
	/* Invalid scatterlist length is rejected without launch. */
	sg[1].length = 4; before = af.writes;
	nt_request(&mmc, &mrq); assert(command.error && af.writes == before);
	sg[1].length = 5;
	/* A clock readback fault closes IRQ admission before any further MMIO. */
	clock.fault = -EIO; h.core.irq_access = 1; before = af.reads + af.writes;
	nt_set_ios(&mmc, &ios); nt_irq(30, &h);
	assert(h.core.fault && !h.core.irq_access && af.reads + af.writes == before && disabled_irqs == 1);
	nt_request(&mmc, &next); run_queued(&h);
	assert(recovery.error && af.reads + af.writes == before && disabled_irqs == 1);
}

static void probe_tests(void)
{
	struct platform_device p;
	unsigned mode;
	for (mode = 0; mode <= 8; mode++) {
		valid_platform(&p, 0); memset(&af, 0, sizeof(af));
		memset(&probe_clock, 0, sizeof(probe_clock));
		probe_active = 1; probe_fail = mode; probe_admitted = probe_started = 0;
		probe_freed = probe_irq_freed = probe_released = 0;
		af.regs[0x40 / 4] = 1; af.mode = 5;
		if (!mode) {
			struct nt_host *h;
			assert(!nt_probe(&p)); h = p.data;
			assert(h->registered && h->irq_live);
			nt_retire(h); assert(!h->registered && !h->irq_live);
			assert(!probe_clock.on && probe_released == 1 && probe_irq_freed == 1);
			mmc_free_host(h->mmc);
		} else {
			assert(nt_probe(&p) < 0);
			if (mode < 4) assert(!af.reads && !af.writes);
			if (mode == 4) assert(!af.writes);
			if (mode == 8) assert(probe_irq_freed == 1);
		}
		assert(probe_freed == 1 && !allocations);
	}
	probe_active = 0;
}

static void resource_tests(void)
{
	struct platform_device p;
	unsigned id, out, before, mode;
	int irq;
	for (id = 0; id < 2; id++) {
		valid_platform(&p, id);
		assert(!nt_resources(&p, &out, &irq));
		assert(out == id && irq == (int)(100 + id));
	}
	for (mode = 0; mode < 16; mode++) {
		valid_platform(&p, 0);
		switch (mode) {
		case 0: p.num_resources = 2; break;
		case 1: p.num_resources = 0; break;
		case 2: p.mem.size = 0x400; break;
		case 3: p.mem.start = 0xc0500000; break;
		case 4: device_node.irq_count = 0; break;
		case 5: device_node.irq_count = 2; break;
		case 6: device_node.args_count = 2; break;
		case 7: device_node.hwirq = 31; break;
		case 8: device_node.bad_parent = 1; break;
		case 9: device_node.bad_domain = 1; break;
		case 10: device_node.linux_irq = -517; break;
		case 11: device_node.linux_irq = 0; break;
		case 12: device_node.missing_mem = 1; break;
		case 13: device_node.missing_data = 1; break;
		case 14: device_node.missing_domain = 1; break;
		case 15: device_node.assertions = 3; break;
		}
		before = af.reads + af.writes;
		assert(nt_resources(&p, &out, &irq) < 0);
		assert(before == af.reads + af.writes);
	}
}

/* Every terminal-fault entry executes the real adapter and portable engine. */
static void adapter_fault_tests(void)
{
	unsigned mode;
	for (mode = 0; mode < 8; mode++) {
		struct nt_host h;
		struct mmc_host mmc = { 0 };
		struct nt98513_mmc_clock clock = { 1, 0, 1, 0, 0 };
		struct mmc_command cmd = { .opcode = 13, .flags = 21 };
		struct mmc_request mrq = { .cmd = &cmd };
		unsigned before, synced;
		memset(&h, 0, sizeof(h)); memset(&af, 0, sizeof(af));
		mmc.private = &h; h.mmc = &mmc; h.clock = &clock; h.regs = af.regs; h.irq_live = true;
		disabled_irqs = sync_calls = 0;
		af.regs[0x40 / 4] = 1; af.mode = 5;
		nt_mmc_init(&h.core, &adapter_ops, &h);
		assert(!nt_mmc_admit(&h.core)); assert(!nt_mmc_initialize(&h.core, 400000));
		if (mode == 0) clock.fault = -EIO; /* request admission */
		nt_request(&mmc, &mrq);
		if (mode == 1) clock.fault = -EIO; /* worker precheck */
		if (mode == 2) af.fault_read = 0x29; /* status poll */
		if (mode == 3) af.fault_read = 0x31; /* early drain/notify */
		if (mode == 4) { af.mode = 1; af.fail_reset = 1; } /* late reset failure */
		if (mode == 5) af.fault_read = 0x1d; /* late halt failure, command-only */
		if (mode == 6) {
			h.core.irq_access = 1; af.fault_read = 0x29;
			in_hard_irq = 1; synced = sync_calls;
			assert(nt_irq(h.irq, &h) == IRQ_HANDLED);
			assert(disabled_irqs == 1 && sync_calls == synced);
			assert(nt_irq(h.irq, &h) == IRQ_HANDLED && disabled_irqs == 1);
			in_hard_irq = 0;
		}
		if (mode == 7) {
			/* Normal completion, then retirement's halt fails. */
			run_queued(&h); assert(!cmd.error && !disabled_irqs);
			af.fault_read = 0x1d;
			nt_retire(&h);
		} else {
			run_queued(&h);
			assert(cmd.error && !allocations);
		}
		assert(h.core.fault && disabled_irqs == 1);
		before = af.reads + af.writes;
		nt_irq(h.irq, &h); nt_mmc_irq_off(&h.core); nt_disable_irq(&h);
		assert(disabled_irqs == 1 && before == af.reads + af.writes);
	}
}

static struct nt_host *boundary_host;
static unsigned boundary_mode, boundary_hits, latch_attempts, latch_deferred;

static void boundary_irq(struct nt_host *h)
{
	unsigned previous = in_hard_irq, before = af.reads + af.writes;
	assert(!local_irq_depth && !af.irq_locked);
	in_hard_irq = 1; local_irq_depth = 1;
	nt_irq(h->irq, h);
	local_irq_depth = 0; in_hard_irq = previous;
	assert(before == af.reads + af.writes);
}

/* Inject immediately after the real core drops its IRQ-excluded lock. */
static void test_unlock_boundary(void *p)
{
	struct nt_host *h = p;
	unsigned before;
	if (h != boundary_host || boundary_mode < 1 || boundary_mode > 3)
		return;
	boundary_hits++;
	if (boundary_mode == 1)
		assert(!(af.regs[0x30 / 4] & af.regs[0x28 / 4]));
	else
		assert(h->core.fault && !h->core.irq_access);
	boundary_mode = 0; /* Do not recursively inject at the IRQ's own unlock. */
	before = af.reads + af.writes;
	boundary_irq(h);
	assert(before == af.reads + af.writes);
	if (h->core.fault) assert(disabled_irqs == 1);
}

/* An IRQ is requested exactly after cmpxchg has set the disable latch. */
static void test_latch_boundary(void)
{
	if (!boundary_host || boundary_mode != 4) return;
	latch_attempts++;
	boundary_mode = 0;
	if (local_irq_depth) {
		latch_deferred = 1;
		return;
	}
	boundary_irq(boundary_host);
	assert(disabled_irqs == 1); /* v2 exposes a set latch before actual mask. */
}

static void test_async_restore(void);
void test_local_restore(void)
{
	test_async_restore();
	if (!latch_deferred || local_irq_depth) return;
	latch_deferred = 0;
	assert(disabled_irqs == 1);
	boundary_irq(boundary_host);
	assert(disabled_irqs == 1);
}

static void irq_boundary_tests(void)
{
	unsigned mode;
	const char *selected = getenv("NT_MMC_BOUNDARY_CASE");
	for (mode = 1; mode <= 4; mode++) {
		struct nt_host h;
		struct mmc_host mmc = { 0 };
		struct nt98513_mmc_clock clock = { 1, 0, 1, 0, 0 };
		if (selected && (unsigned)atoi(selected) != mode) continue;
		memset(&h, 0, sizeof(h)); memset(&af, 0, sizeof(af));
		mmc.private = &h; h.mmc = &mmc; h.clock = &clock; h.regs = af.regs; h.irq_live = true;
		disabled_irqs = sync_calls = 0;
		af.regs[0x40 / 4] = 1; af.mode = 5;
		nt_mmc_init(&h.core, &adapter_ops, &h);
		assert(!nt_mmc_admit(&h.core)); assert(!nt_mmc_initialize(&h.core, 400000));
		h.core.irq_access = 1; af.regs[0x30 / 4] = 0xff; af.regs[0x28 / 4] = 0x10;
		boundary_host = &h; boundary_mode = mode;
		boundary_hits = latch_attempts = latch_deferred = 0;
		if (mode == 1) {
			assert(!nt_drain(&h.core));
			assert(boundary_hits == 1 && !disabled_irqs && sync_calls);
		} else if (mode == 2) {
			af.fault_read = 0x29;
			assert(nt_events(&h.core) & NT_READ_FAULT);
			assert(boundary_hits == 1 && af.fault_reads == 1 && disabled_irqs == 1);
		} else if (mode == 3) {
			h.core.fault = -EIO;
			nt_mmc_irq_off(&h.core);
			assert(boundary_hits == 1 && disabled_irqs == 1);
		} else {
			h.core.fault = -EIO; h.core.irq_access = 0;
			nt_disable_irq(&h);
			assert(latch_attempts == 1 && !latch_deferred && disabled_irqs == 1);
		}
		assert(!local_irq_depth && !af.irq_locked);
		boundary_host = NULL; boundary_mode = 0;
	}
}

/* Physical MMIO counters are below the extracted production callbacks. */
static struct nt_host *async_host;
static unsigned async_mode, async_armed, async_pending, async_hits, after_fault;

static void inject_async_fault(void)
{
	unsigned previous = in_hard_irq;
	assert(async_host && !af.irq_locked && !local_irq_depth);
	async_hits++; async_armed = async_pending = 0;
	af.fault_read = 0x29;
	in_hard_irq = 1; local_irq_depth = 1;
	assert(nt_irq(async_host->irq, async_host) == IRQ_HANDLED);
	local_irq_depth = 0; in_hard_irq = previous;
	assert(async_host->core.fault && disabled_irqs == 1);
	after_fault = af.reads + af.writes;
}

static void test_async_unlock(void *p)
{
	struct nt_host *h = p;
	if (h == async_host && async_armed && async_mode == 1 &&
	    af.stage >= 2 && h->core.active && !h->core.pending)
		inject_async_fault();
}

static int test_read_once(int value)
{
	if (async_host && async_armed && (async_mode == 4 || async_mode == 5)) {
		async_armed = 0;
		if (local_irq_depth) async_pending = 1;
		else inject_async_fault();
	}
	return value;
}

static void test_physical_access(unsigned offset)
{
	if (!async_host || !async_armed) return;
	if (((async_mode == 2 || async_mode == 3) && offset == 0x100) ||
	    (async_mode == 6 && offset == 0x40)) {
		async_armed = 0;
		if (local_irq_depth) async_pending = 1;
		else inject_async_fault();
	}
}

static void test_async_restore(void)
{
	if (async_pending && !local_irq_depth) {
		unsigned before = af.reads + af.writes;
		if (async_mode == 6) assert(async_host->core.fault);
		inject_async_fault();
		if (async_mode == 6) assert(before == af.reads + af.writes);
	}
}

static void async_mmio_tests(void)
{
	unsigned mode, i;
	const char *selected = getenv("NT_MMC_ASYNC_CASE");
	for (mode = 1; mode <= 7; mode++) {
		struct nt_host h;
		struct mmc_host mmc = { 0 };
		struct nt98513_mmc_clock clock = { 1, 0, 1, 0, 0 };
		struct mmc_command command = { .opcode = 51, .flags = 21 };
		unsigned char bytes[8];
		struct scatterlist sg = { 8, bytes };
		struct mmc_data data = { .sg_len = 1, .blocks = 1, .blksz = 8,
			.flags = MMC_DATA_READ, .sg = &sg };
		struct mmc_request mrq = { .cmd = &command, .data = &data };
		if (selected && (unsigned)atoi(selected) != mode) continue;
		memset(&h, 0, sizeof(h)); memset(&af, 0, sizeof(af));
		mmc.private = &h; h.mmc = &mmc; h.clock = &clock; h.regs = af.regs; h.irq_live = true;
		disabled_irqs = sync_calls = 0; af.regs[0x40 / 4] = 1; af.mode = 5;
		nt_mmc_init(&h.core, &adapter_ops, &h);
		assert(!nt_mmc_admit(&h.core)); assert(!nt_mmc_initialize(&h.core, 400000));
		for (i = 0; i < 8; i++) bytes[i] = i;
		af.length = 8; async_host = &h; async_mode = mode; async_armed = 1;
		async_pending = async_hits = after_fault = 0;
		if (mode <= 3) {
			if (mode == 3) { data.flags = MMC_DATA_WRITE; command.opcode = 24; }
			nt_request(&mmc, &mrq); run_queued(&h);
			assert(command.error == -EIO && !data.bytes_xfered && !allocations);
		} else if (mode <= 6) {
			h.core.irq_access = 1;
			if (mode == 5) nt_host_write(&h, 4, 123);
			else {
				if (mode == 6) af.fault_read = 0x41;
				(void)nt_host_read(&h, 0x40);
			}
		} else {
			/* Valid response and FIFO payloads can be all ones. */
			unsigned offsets[] = { 8, 12, 16, 20, 0x100 };
			async_armed = 0;
			for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
				af.fault_read = offsets[i] + 1;
				assert(nt_host_read(&h, offsets[i]) == ~0U && !h.core.fault);
			}
			/* Also complete actual transfers with all-ones FIFO/response data. */
			af.fault_read = 0x101;
			nt_request(&mmc, &mrq); run_queued(&h);
			assert(!command.error && data.bytes_xfered == 8 && !h.core.fault);
			for (i = 0; i < 8; i++) assert(bytes[i] == 255);
			af.fault_read = 9;
			nt_request(&mmc, &mrq); run_queued(&h);
			assert(!command.error && command.resp[0] == ~0U && !h.core.fault);
		}
		if (mode < 7) {
			assert(async_hits == 1 && h.core.fault);
			assert(af.reads + af.writes == after_fault);
			nt_host_write(&h, 4, 456); (void)nt_host_read(&h, 8);
			assert(af.reads + af.writes == after_fault);
		}
		async_host = NULL; async_armed = async_pending = 0;
	}
}
