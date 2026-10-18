/* SPDX-License-Identifier: GPL-2.0-only */
/* Host kernel-API fixtures; control flow comes from production patch headers. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PHY_MAX_ADDR 32
#define MII_BMCR 0
#define BMCR_RESET 0x8000
struct mii_bus;
struct device { void *driver; bool locked; };
struct phy_device {
	struct { struct device dev; struct mii_bus *bus; int addr; } mdio;
	void *drv, *attached_dev;
	bool is_c45, has_fixups;
};
struct plat_stmmacenet_data {
	int phy_addr;
	int (*mdio_phy_setup)(struct phy_device *);
};
struct stmmac_priv { struct plat_stmmacenet_data *plat; struct mii_bus *mii; };
struct mii_bus { struct phy_device *phy[PHY_MAX_ADDR]; int unregistered, freed; };
static struct mii_bus bus;
static struct phy_device selected, other;
static struct plat_stmmacenet_data plat;
static struct stmmac_priv priv;
static int reads, writes, sleeps, settles, locks, unlocks, lookups;
static int write_error, written, sequence[32], sequence_len, selected_address;
static int callback_calls;
static unsigned long long now_us;
typedef unsigned long long u64;
typedef unsigned long long ktime_t;
static ktime_t ktime_get(void) { return now_us; }
static ktime_t ktime_add_us(ktime_t t, u64 us) { return t + us; }
static int ktime_compare(ktime_t a, ktime_t b) { return (a > b) - (a < b); }
#define might_sleep_if(c) ((void)(c))
#define cpu_relax() ((void)0)
#define phydev_err(p, ...) ((void)(p))
static void usleep_range(unsigned long low, unsigned long high)
{
	assert(selected.mdio.dev.locked && low <= high && high == 50000);
	now_us += high;
	sleeps++;
}
static void msleep(unsigned int ms)
{
	assert(selected.mdio.dev.locked && ms == 1);
	now_us += ms * 1000;
	settles++;
}
static void device_lock(struct device *d)
{
	assert(!d->locked); d->locked = true; locks++;
}
static void device_unlock(struct device *d)
{
	assert(d->locked); d->locked = false; unlocks++;
}
static int phy_read(struct phy_device *phy, int reg)
{
	assert(phy == &selected && phy->mdio.dev.locked && reg == MII_BMCR);
	assert(!priv.mii);
	assert(phy->mdio.addr == selected_address && reads < 64);
	int i = reads++;
	return sequence[i < sequence_len ? i : sequence_len - 1];
}
static int phy_write(struct phy_device *phy, int reg, int value)
{
	assert(phy == &selected && phy->mdio.dev.locked && reg == MII_BMCR);
	assert(!priv.mii);
	assert(phy->mdio.addr == selected_address);
	writes++; written = value;
	return write_error;
}
static struct phy_device *mdiobus_get_phy(struct mii_bus *b, int addr)
{
	assert(b == &bus && addr >= 0 && addr < PHY_MAX_ADDR);
	lookups++;
	return b->phy[addr];
}
static void mdiobus_unregister(struct mii_bus *b)
{
	assert(b == &bus && !b->unregistered && !b->freed);
	assert(!selected.mdio.dev.locked);
	b->unregistered++;
}
static void mdiobus_free(struct mii_bus *b)
{
	assert(b == &bus && b->unregistered && !b->freed);
	b->freed++;
}
#include "phy_poll_fixture.h"
#include "dwmac-nt98513-phy.h"
#include "stmmac-mdio-setup.h"

static int reset_callback(struct phy_device *phy)
{
	callback_calls++;
	return nt98513_phy_reset(phy);
}
static void setup(void)
{
	memset(&bus, 0, sizeof(bus)); memset(&selected, 0, sizeof(selected));
	memset(&other, 0, sizeof(other)); memset(&plat, 0, sizeof(plat));
	memset(&priv, 0, sizeof(priv)); memset(sequence, 0, sizeof(sequence));
	reads=writes=sleeps=settles=locks=unlocks=lookups=callback_calls=0;
	write_error=written=0; now_us=0;
	selected_address=5;
	selected.mdio.bus=&bus; selected.mdio.addr=5;
	other.mdio.bus=&bus; other.mdio.addr=7;
	bus.phy[5]=&selected; bus.phy[7]=&other;
	plat.phy_addr=5; plat.mdio_phy_setup=reset_callback; priv.plat=&plat;
	sequence[0]=0x6d25; sequence[1]=0xed25; sequence[2]=0x6d25; sequence_len=3;
}
static void failed(int error)
{
	assert(stmmac_mdio_finish_registration(&priv, &bus) == error);
	assert(!priv.mii && bus.unregistered==1 && bus.freed==1);
	assert(!selected.mdio.dev.locked && locks==unlocks);
}
static void success(void)
{
	assert(stmmac_mdio_finish_registration(&priv, &bus)==0);
	assert(priv.mii==&bus && !bus.unregistered && !bus.freed);
	assert(!selected.mdio.dev.locked && locks==unlocks);
}
int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "reset-failure")) {
		setup(); sequence[1] = -EIO;
		int ret = stmmac_mdio_finish_registration(&priv, &bus);
		if (ret != -EIO || priv.mii || bus.unregistered != 1 || bus.freed != 1) {
			fprintf(stderr, "missing reset-error propagation: ret=%d published=%d unregister=%d free=%d\n",
				ret, priv.mii != NULL, bus.unregistered, bus.freed);
			return 1;
		}
		return 0;
	}
	setup(); plat.mdio_phy_setup=NULL; plat.phy_addr=-1; success();
	assert(!lookups && !callback_calls && !reads && !locks);
	setup(); bus.phy[5]=NULL; failed(-ENODEV); assert(!callback_calls && !reads);
	setup(); plat.phy_addr=-1; failed(-ENODEV); assert(!lookups && !reads);
	setup(); plat.phy_addr=32; failed(-ENODEV); assert(!lookups && !reads);
	setup(); selected.mdio.dev.driver=&selected; selected.has_fixups=true;
	selected.is_c45=true; success(); assert(!reads && locks==1 && !settles);
	setup(); selected.drv=&selected; success(); assert(!reads && locks==1);
	setup(); selected.has_fixups=true; failed(-EOPNOTSUPP); assert(!reads && !writes && locks==1);
	setup(); selected.is_c45=true; failed(-EOPNOTSUPP); assert(!reads && !writes && locks==1);
	setup(); selected.attached_dev=&selected; failed(-EBUSY); assert(!reads && !writes && locks==1);
	setup(); success();
	assert(callback_calls==1 && lookups==1 && reads==3 && writes==1);
	assert(written==0xed25 && settles==1 && sleeps==2 && now_us==101000);
	setup(); sequence[0]=-EIO; failed(-EIO);
	assert(reads==1 && !writes && !sleeps && !settles);
	setup(); write_error=-EACCES; failed(-EACCES);
	assert(reads==1 && writes==1 && !sleeps && !settles);
	setup(); sequence[1]=-ENXIO; failed(-ENXIO);
	assert(reads==2 && writes==1 && sleeps==1 && !settles);
	setup(); sequence[0]=0x1234; sequence[1]=0x9234; sequence_len=2;
	failed(-ETIMEDOUT);
	assert(reads==15 && writes==1 && sleeps==13 && now_us==650000 && !settles);
	/* Prove every BMCR bit is retained; only RESET may be newly set. */
	for (int bit=0; bit<16; bit++) {
		setup(); sequence[0]=1u<<bit; sequence[1]=0; sequence_len=2;
		success(); assert((unsigned int)written==((1u<<bit)|BMCR_RESET) && settles==1);
	}
	for (int addr=0; addr<PHY_MAX_ADDR; addr++) {
		setup(); bus.phy[5]=NULL; bus.phy[addr]=&selected;
		selected.mdio.addr=addr; selected_address=addr; plat.phy_addr=addr;
		success(); assert(lookups==1 && callback_calls==1 && writes==1);
	}
	puts("selected-PHY production paths: all assertions passed");
	return 0;
}
