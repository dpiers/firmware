/* Host-only tests. Production callbacks supply actual kernel operations. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "dwmac-nt98513-policy.h"
#include "stmmac-csr-policy.h"
struct fake { int enabled, fault, step; unsigned top, pads, version; };
static int enable(void *p) { struct fake *f=p; assert(f->step++ == 0); return f->enabled; }
static int fault(void *p) { struct fake *f=p; assert(f->step++ == 1); return f->fault; }
static unsigned top(void *p) { struct fake *f=p; assert(f->step++ == 2); return f->top; }
static unsigned pads(void *p) { struct fake *f=p; assert(f->step++ == 3); return f->pads; }
static unsigned version(void *p) { struct fake *f=p; assert(f->step++ == 4); return f->version; }
static const struct nt98513_eth_ops ops={enable,fault,top,pads,version};
int main(void)
{
    struct nt98513_eth_input in = {1,1,1,4,0,1,1};
    struct fake f = {0,0,0,0,0,0x40};
    unsigned tick=123, i;
    assert(nt98513_eth_validate(&in)==0);
    in.exclusive=0; assert(nt98513_eth_validate(&in)==-EPERM); in.exclusive=1;
    in.mode_ok=0; assert(nt98513_eth_validate(&in)==-EINVAL); in.mode_ok=1;
    in.irqs=2; assert(nt98513_eth_validate(&in)==-EINVAL); in.irqs=1;
    in.resources=3; assert(nt98513_eth_validate(&in)==-EINVAL); in.resources=4;
    in.unsupported=1; assert(nt98513_eth_validate(&in)==-EINVAL); in.unsupported=0;
    in.clock_ok=0; assert(nt98513_eth_validate(&in)==-EINVAL); in.clock_ok=1;
    in.raw_irq_ok=0; assert(nt98513_eth_validate(&in)==-EINVAL);
    assert(nt98513_eth_resource(0,0xc02b0000,0x2010));
    assert(nt98513_eth_resource(1,0xc02b3008,4));
    assert(nt98513_eth_resource(2,0xc0011000,4));
    assert(nt98513_eth_resource(3,0xc00100d0,4));
    assert(!nt98513_eth_resource(0,0xc02b0000,0x300c));
    assert(!nt98513_eth_resource(1,0xc02b1008,4));
    assert(!nt98513_eth_resource(4,0,4));
    assert(nt98513_eth_activate(&ops,&f)==0 && f.step==5);
    f.step=0; f.version=0x1241; assert(nt98513_eth_activate(&ops,&f)==0);
    f.step=0; f.enabled=-EIO; assert(nt98513_eth_activate(&ops,&f)==-EIO && f.step==1);
    f.step=0; f.enabled=-517; assert(nt98513_eth_activate(&ops,&f)==-517 && f.step==1);
    f.step=0; f.enabled=0; f.fault=-EIO; assert(nt98513_eth_activate(&ops,&f)==-EIO && f.step==2);
    f.step=0; f.fault=0; f.top=1u<<18; assert(nt98513_eth_activate(&ops,&f)==-EBUSY && f.step==3);
    f.top=0; f.pads=1; f.step=0; assert(nt98513_eth_activate(&ops,&f)==-EBUSY && f.step==4);
    f.pads=2; f.step=0; assert(nt98513_eth_activate(&ops,&f)==-EBUSY && f.step==4);
    f.pads=0;
    for(i=0;i<256;i++) {
        f.step=0; f.version=i;
        assert(nt98513_eth_activate(&ops,&f)==((i==0x40||i==0x41)?0:-ENODEV));
    }
    assert(!nt98513_eth_irq_pending(0));
    assert(nt98513_eth_irq_pending(1));
    assert(nt98513_eth_irq_pending(0x80000000u));
    assert(!stmmac_csr_tick(0,1,&tick) && tick==123);
    assert(!stmmac_csr_tick(50000000,1,&tick) && tick==123);
    assert(!stmmac_csr_tick(0,0,&tick) && tick==123);
    assert(stmmac_csr_tick(50000000,0,&tick) && tick==49);
    puts("Ethernet production policy: all assertions passed");
    return 0;
}
