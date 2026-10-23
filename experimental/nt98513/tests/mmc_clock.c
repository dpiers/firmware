/* SPDX-License-Identifier: GPL-2.0-only */
/* Real production owner + MMC transactions + CC core, external services only. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EPROBE_DEFER 517
#define __iomem
#define __init
#define GFP_KERNEL 0
#define IORESOURCE_MEM 1
#define DL_FLAG_AUTOREMOVE_CONSUMER 1
#define MODULE_LICENSE(x)
#define subsys_initcall(x)
#define EXPORT_SYMBOL_GPL(x)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define smp_load_acquire(p) (*(p))
#define smp_store_release(p,v) (*(p)=(v))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define dev_err(...) ((void)0)
#define might_sleep() ((void)0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef uint32_t u32;
typedef uint64_t resource_size_t;
typedef _Bool bool;
#define true 1
#define false 0
static unsigned active_locks;
struct mutex { int held; };
#define DEFINE_MUTEX(n) struct mutex n
static void mutex_init(struct mutex *m) { m->held=0; }
static void mutex_lock(struct mutex *m) { assert(!m->held);m->held=1;active_locks++; }
static void mutex_unlock(struct mutex *m) { assert(m->held);m->held=0;active_locks--; }
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define lockdep_assert_held(m) assert((m)->held)
struct device;
struct clk_init_data;
struct clk_hw { const struct clk_init_data *init; };
struct clk_ops { int (*prepare)(struct clk_hw *);int (*enable)(struct clk_hw *);void (*disable)(struct clk_hw *);void (*unprepare)(struct clk_hw *);unsigned long (*recalc_rate)(struct clk_hw *,unsigned long); };
struct clk_init_data { const char *name;const struct clk_ops *ops; };
struct of_phandle_args { unsigned args_count,args[1]; };
struct device_node { const char *compat; struct device_node *supplier; int available, cells; };
struct device { struct device_node *of_node; };
struct resource { const char *name; resource_size_t start,end; };
struct platform_device { struct device dev; unsigned num_resources; struct resource resource[5]; void *data; };
struct of_device_id { const char *compatible; };
struct platform_driver { int (*probe)(struct platform_device *); struct {const char *name;const struct of_device_id *of_match_table;bool suppress_bind_attrs;} driver; };
static int platform_driver_register(struct platform_driver *p) { return 0; }
static struct resource *platform_get_resource_byname(struct platform_device *p,int t,const char *n) { unsigned i;for(i=0;i<p->num_resources;i++)if(!strcmp(n,p->resource[i].name))return &p->resource[i];return NULL; }
static struct resource *platform_get_resource(struct platform_device *p,int t,unsigned i) { return i<p->num_resources?&p->resource[i]:NULL; }
static resource_size_t resource_size(struct resource *r) { return r->end-r->start+1; }
static void *devm_kzalloc(struct device *d,size_t s,int g) { return calloc(1,s); }
static void platform_set_drvdata(struct platform_device *p,void *d) { p->data=d; }
static int of_property_count_u32_elems(struct device_node *n,const char *p) { return n->cells; }
static struct device_node *of_parse_phandle(struct device_node *n,const char *p,int i) { return n->supplier; }
static int of_device_is_available(struct device_node *n) { return n->available; }
static int of_device_is_compatible(struct device_node *n,const char *c) { return !strcmp(n->compat,c); }
static void of_node_put(struct device_node *n) { }
static void get_device(struct device *d) { }
static void put_device(void *d) { }
static int devm_add_action_or_reset(struct device *d,void (*fn)(void *),void *p) { return 0; }
static void devm_release_action(struct device *d,void (*fn)(void *),void *p) { fn(p); }
static void *device_link_add(struct device *a,struct device *b,int flags) { return a; }
#include "nt98513_cc_bank_core.h"
#include "cc-core.c"
static unsigned readl(void *p);
static void writel(unsigned v,void *p);
static void *devm_ioremap_resource(struct device *d,struct resource *r);
#define readl_poll_timeout(p,v,c,delay,timeout) ((v)=readl(p),(c)?0:-ETIMEDOUT)
#include "cc-bank.c"
#define fixture_bank (*nt_cc_singleton)
static unsigned regs[5],cc_regs[3], reads, writes, delays, entries, releases, fail_acquire, fail_release, fail_write, fail_read;
struct event { char kind;unsigned reg,value;int bank_held; } events[2048];
static unsigned nevents,drift_on_delay;
static void event(char kind,unsigned r,unsigned v) { assert(nevents<2048);events[nevents++]=(struct event){kind,r,v,nt_cc_singleton?fixture_bank.lock.held:0}; }
static unsigned readl(void *p) {
 if(p==cc_regs+2)return cc_regs[2];
 unsigned r=(unsigned)((unsigned *)p-regs);assert(r<5);reads++;event('r',r,regs[r]);return reads==fail_read?~0U:regs[r];
}
static void writel(unsigned v,void *p) {
 if(p==cc_regs){assert(fixture_bank.lock.held);entries++;event('A',0,v);if(entries!=fail_acquire)cc_regs[2]|=v;return;}
 if(p==cc_regs+1){assert(fixture_bank.lock.held);releases++;event('R',0,v);if(releases!=fail_release)cc_regs[2]&=~v;return;}
 unsigned r=(unsigned)((unsigned *)p-regs);assert(r<5);assert(fixture_bank.lock.held&&fixture_bank.core.acquired);writes++;event('w',r,v);if(writes!=fail_write)regs[r]=v;
}
static void udelay(unsigned n) { assert(n>=10);assert(active_locks==(fixture_bank.lock.held?2U:1U));delays++;event('d',0,n);if(drift_on_delay&&delays==1)regs[2]|=0x10; }
static void *devm_ioremap_resource(struct device *d,struct resource *r) { switch(r->start){case 0xc0090050:return cc_regs;case 0xc0020074:return regs;case 0xc0020084:return regs+1;case 0xc0020020:return regs+2;case 0xc002003c:return regs+3;case 0xc00200b0:return regs+4;default:abort();} }
#include "nt98513_clock_private.h"
static int clk_hw_register(struct device *d,struct clk_hw *hw) { return 0; }
static void clk_hw_unregister(struct clk_hw *hw) { }
static int of_clk_add_hw_provider(struct device_node *n,struct clk_hw *(*get)(struct of_phandle_args *,void *),void *data) { return 0; }
#include "nt98513_clock_txn.c"
#include "nt98513_clock_owner.c"
#include "nt98513_eth_clock.c"
#include "nt98513_mmc_clock.c"
static struct device_node node,consumer_node,bank_node;
static struct platform_device bank_platform;
static struct platform_device platform;
static struct device consumer;
static struct nt98513_clock_owner *owner;
static void reset(unsigned v3)
{
 unsigned i;static const char *names[]={"gate","prepare","mux","divider","phase"};
 static const unsigned addresses[]={0xc0020074,0xc0020084,0xc0020020,0xc002003c,0xc00200b0};
 memset(cc_regs,0,sizeof(cc_regs));memset(&platform,0,sizeof(platform));memset(&bank_platform,0,sizeof(bank_platform));
 bank_node=(struct device_node){"openipc,nt98513-cc-bank-v1",NULL,1,1};bank_platform.dev.of_node=&bank_node;
 bank_platform.num_resources=1;bank_platform.resource[0]=(struct resource){"bank",0xc0090050,0xc009005b};
 nt_cc_singleton=NULL;nt_cc_reservation=NULL;assert(nt_cc_probe(&bank_platform)==0);
 reads=writes=delays=entries=releases=nevents=fail_acquire=fail_release=fail_write=fail_read=drift_on_delay=0;
 regs[0]=0x8000000c;regs[1]=0x4000000c;regs[2]=0x12340001;regs[3]=0xa123b456;regs[4]=0x80600000;
 node=(struct device_node){v3?"openipc,nt98513-shared-clock-v3":"openipc,nt98513-shared-clock-v2",&bank_node,1,1};
 consumer_node=(struct device_node){"consumer",&node,1,1};consumer.of_node=&consumer_node;
 platform.dev.of_node=&node;platform.num_resources=v3?5:2;
 for(i=0;i<5;i++)platform.resource[i]=(struct resource){names[i],addresses[i],addresses[i]+3};
 nt_singleton=NULL;nt_reservation=NULL;
 assert(nt_owner_probe(&platform)==0);owner=platform.data;
 assert(reads==0&&writes==0);
}
static struct nt98513_mmc_clock *get(unsigned index) { struct nt98513_mmc_clock *c=devm_nt98513_mmc_clock_get(&consumer,index);assert(!IS_ERR(c));return c; }
static void started(struct nt98513_mmc_clock **c,unsigned index) { unsigned actual=0;reset(1);*c=get(index);assert(nt98513_mmc_clock_admit(*c)==0);assert(writes==0);assert(nt98513_mmc_clock_start(*c,400000,&actual)==0);assert(actual==400000); }
static void lifecycle(void)
{
 struct nt98513_mmc_clock *a,*b;unsigned actual,w,prep,phase,i,d=0;unsigned saved[5],write_index=0;
 static const unsigned expected_regs[]={0,3,4,1,1,0};
 static const unsigned expected_values[]={0x80000008,0xa123b1df,0x80700000,0x40000008,0x4000000c,0x8000000c};
 reset(1);memcpy(saved,regs,sizeof(regs));a=get(0);b=get(1);
 assert(nt98513_mmc_clock_admit(a)==0);assert(nt98513_mmc_clock_admit(b)==0);assert(writes==0);
 assert(nt98513_mmc_clock_start(a,400000,&actual)==0&&actual==400000);
 assert((regs[3]&2047)==479);assert(regs[4]==(saved[4]|(1u<<20)));
 assert((regs[0]&~4u)==(saved[0]&~4u));assert((regs[1]&~4u)==(saved[1]&~4u));assert((regs[3]&~2047u)==(saved[3]&~2047u));assert(regs[2]==saved[2]);
 for(i=0;i<nevents;i++)if(events[i].kind=='d'){assert(events[i].bank_held==(d?1:0));if(!d){assert(i>0&&events[i-1].kind=='R');assert(events[i+1].kind=='A');}else assert(events[i+1].kind=='R');d++;}
 assert(d==2);
 for(i=0;i<nevents;i++)if(events[i].kind=='w'){assert(write_index<6);assert(events[i].reg==expected_regs[write_index]);assert(events[i].value==expected_values[write_index]);write_index++;}
 assert(write_index==6);
 assert(nt98513_mmc_clock_start(b,12000000,&actual)==0&&actual==12000000);
 assert(((regs[3]>>16)&2047)==15);assert((regs[4]&(1u<<21))==(saved[4]&(1u<<21)));assert((regs[3]&2047)==479);
 w=writes;assert(nt98513_mmc_clock_rate(a,400000,&actual)==0&&writes==w);
 prep=regs[1];phase=regs[4];assert(nt98513_mmc_clock_stop(a)==0);assert(!(regs[0]&4));assert(regs[0]&8);
 w=writes;assert(nt98513_mmc_clock_stop(a)==0&&writes==w);
 assert(nt98513_mmc_clock_rate(a,700000,&actual)==0&&actual==698182);assert((regs[3]&2047)==274);assert(delays==4&&regs[1]==prep&&regs[4]==phase);
 assert(nt98513_mmc_clock_release(a)==-EBUSY);assert(nt98513_mmc_clock_stop(a)==0);assert(nt98513_mmc_clock_release(a)==0);
 a=get(0);w=writes;assert(nt98513_mmc_clock_admit(a)!=0&&writes==w);
 assert(nt98513_mmc_clock_fault(b)==0);assert(nt98513_mmc_clock_stop(b)==0);assert(nt98513_mmc_clock_release(b)==0);
}
static void validation(void)
{
 struct nt98513_mmc_clock *a,*b;unsigned actual=123,w,r,i;
 reset(1);assert(IS_ERR(devm_nt98513_mmc_clock_get(&consumer,2)));a=get(0);w=writes;r=reads;
 assert(nt98513_mmc_clock_start(a,400000,&actual)<0);assert(nt98513_mmc_clock_rate(a,400000,&actual)<0);assert(nt98513_mmc_clock_stop(a)<0);assert(reads==r&&writes==w);
 assert(nt98513_mmc_clock_admit(a)==0);b=get(0);assert(nt98513_mmc_clock_admit(b)==-EBUSY);
 r=reads;for(i=0;i<4;i++){unsigned bad[]={0,399999,12000001,~0u};assert(nt98513_mmc_clock_start(a,bad[i],&actual)==-EINVAL);}assert(nt98513_mmc_clock_start(a,400000,NULL)==-EINVAL);assert(reads==r&&writes==0&&actual==123);
 assert(nt98513_mmc_clock_release(a)==0&&writes==0);assert(nt98513_mmc_clock_admit(b)==0);
 for(i=0;i<5;i++){reset(1);a=get(0);regs[i]=~0u;assert(nt98513_mmc_clock_admit(a)<0&&writes==0);}
 reset(1);a=get(0);regs[2]|=0x10;assert(nt98513_mmc_clock_admit(a)<0&&writes==0);
 reset(1);a=get(0);regs[0]&=~4u;assert(nt98513_mmc_clock_admit(a)<0&&writes==0);
 reset(1);a=get(0);regs[1]&=~4u;assert(nt98513_mmc_clock_admit(a)<0&&writes==0);
}
static void failures(void)
{
 struct nt98513_mmc_clock *a,*b;unsigned actual=55,i,w,r,successful_entries,successful_writes;
 started(&a,0);successful_entries=entries;successful_writes=writes;
 for(i=1;i<=successful_entries;i++){
  reset(1);a=get(0);fail_acquire=i;
  if(nt98513_mmc_clock_admit(a)==0)assert(nt98513_mmc_clock_start(a,400000,&actual)<0);
  assert(fixture_bank.core.uncertain&&!fixture_bank.lock.held);assert(releases==i-1);
  w=writes;r=reads;assert(nt98513_mmc_clock_fault(a)<0);assert(nt98513_mmc_clock_start(a,400000,&actual)<0);assert(writes==w&&reads==r);
 }
 for(i=1;i<=successful_entries;i++){
  reset(1);a=get(0);fail_release=i;
  if(nt98513_mmc_clock_admit(a)==0)assert(nt98513_mmc_clock_start(a,400000,&actual)<0);
  assert(fixture_bank.core.uncertain&&!fixture_bank.lock.held);assert(nt98513_mmc_clock_fault(a)<0);
 }
 for(i=1;i<=successful_writes;i++){
  reset(1);a=get(0);assert(nt98513_mmc_clock_admit(a)==0);fail_write=i;
  assert(nt98513_mmc_clock_start(a,400000,&actual)<0);assert(nt98513_mmc_clock_fault(a)<0);assert(!fixture_bank.lock.held);w=writes;assert(nt98513_mmc_clock_start(a,400000,&actual)<0&&writes==w);assert(nt98513_mmc_clock_release(a)<0);
 }
 /* Selected drift fails before mutation, sibling updates remain admissible. */
 for(i=0;i<5;i++){
  static const unsigned corrupt[]={4,4,0x10,1,1u<<20};started(&a,0);regs[i]^=corrupt[i];w=writes;
  assert(nt98513_mmc_clock_rate(a,1000000,&actual)<0&&writes==w);assert(nt98513_mmc_clock_fault(a)<0);
 }
 started(&a,0);b=get(1);assert(nt98513_mmc_clock_admit(b)==0);regs[0]^=4;assert(nt98513_mmc_clock_stop(a)<0);assert(nt98513_mmc_clock_start(b,400000,&actual)==0);
 started(&a,0);fail_write=writes+1;assert(nt98513_mmc_clock_stop(a)<0);assert(nt98513_mmc_clock_release(a)<0);
 started(&a,0);assert(nt98513_mmc_clock_stop(a)==0);fail_write=writes+1;assert(nt98513_mmc_clock_rate(a,400000,&actual)<0);
 reset(1);a=get(0);assert(nt98513_mmc_clock_admit(a)==0);drift_on_delay=1;assert(nt98513_mmc_clock_start(a,400000,&actual)<0);assert(delays==1);assert(!(regs[1]&4));assert(!(regs[0]&4));
 reset(1);a=get(0);regs[4]=~(1u<<20);assert(nt98513_mmc_clock_admit(a)==0);assert(nt98513_mmc_clock_start(a,400000,&actual)<0);assert(regs[4]==~(1u<<20));
 reset(1);a=get(0);cc_regs[2]=4;assert(nt98513_mmc_clock_admit(a)<0&&writes==0&&releases==0);
}
static struct nt98513_mmc_clock *setup_transition(unsigned op)
{
 struct nt98513_mmc_clock *c;
 started(&c,0);
 if(op==2||op==4||op==5)assert(nt98513_mmc_clock_stop(c)==0);
 return c;
}
static int transition(struct nt98513_mmc_clock *c,unsigned op,unsigned *actual)
{
 switch(op) {
 case 0:return nt98513_mmc_clock_rate(c,1000000,actual);
 case 1:case 4:return nt98513_mmc_clock_stop(c);
 case 2:case 3:return nt98513_mmc_clock_rate(c,400000,actual);
 default:return nt98513_mmc_clock_release(c);
 }
}
static void transition_failures(void)
{
 struct nt98513_mmc_clock *c;unsigned op,i,nread,nentry,nwrite,bread,bentry,bwrite,actual;
 for(op=0;op<6;op++) {
  c=setup_transition(op);bread=reads;bentry=entries;bwrite=writes;
  assert(transition(c,op,&actual)==0);nread=reads-bread;nentry=entries-bentry;nwrite=writes-bwrite;
  for(i=1;i<=nread;i++) {
   c=setup_transition(op);fail_read=reads+i;actual=77;
   assert(transition(c,op,&actual)<0);assert(actual==77);assert(nt98513_mmc_clock_fault(c)<0);assert(!active_locks);
  }
  for(i=1;i<=nentry;i++) {
   c=setup_transition(op);fail_acquire=entries+i;actual=77;
   assert(transition(c,op,&actual)<0);assert(actual==77);assert(nt98513_mmc_clock_fault(c)<0);assert(!active_locks);
   c=setup_transition(op);fail_release=releases+i;actual=77;
   assert(transition(c,op,&actual)<0);assert(actual==77);assert(nt98513_mmc_clock_fault(c)<0);assert(!active_locks);
  }
  for(i=1;i<=nwrite;i++) {
   c=setup_transition(op);fail_write=writes+i;actual=77;
   assert(transition(c,op,&actual)<0);assert(actual==77);assert(nt98513_mmc_clock_fault(c)<0);assert(!active_locks);
  }
 }
 /* Every admission and first-start read must reject an all-ones sample. */
 started(&c,0);nread=reads;
 for(i=1;i<=nread;i++) {
  reset(1);c=get(0);fail_read=i;actual=77;
  if(nt98513_mmc_clock_admit(c)==0)assert(nt98513_mmc_clock_start(c,400000,&actual)<0);
  assert(actual==77);assert(nt98513_mmc_clock_fault(c)<0);assert(!active_locks);
 }
 /* Shared resource4 uncertainty is published by the actual bank and stops all
  * later clock consumers; there is no clock-private replacement semaphore. */
 reset(1);c=get(0);assert(nt_cc_enter(owner->bank,4)==0);fail_acquire=1;
 assert(nt_cc_acquire_locked(owner->bank,4)==-ETIMEDOUT);nt_cc_leave(owner->bank);
 assert(nt_cc_failure_phase(owner->bank,4)==NT_CC_FAIL_ACQUIRE);
 assert(nt_cc_cause(owner->bank,4)==-ETIMEDOUT);
 assert(nt98513_mmc_clock_admit(c)<0&&writes==0);assert(nt_eth_enable(&owner->hw)<0);
}
static void legacy_and_probe(void)
{
 unsigned v,i,w;struct nt98513_mmc_clock *a;
 for(v=0;v<2;v++){
  reset(v);assert(nt98513_clock_prepare_enable(owner,NT98513_CLOCK_WATCHDOG)==0);assert(nt98513_clock_prepare_enable(owner,NT98513_CLOCK_ETHERNET)==0);
  assert((regs[0]&0x20020000)==0x20020000);assert((regs[1]&0x20020000)==0x20020000);assert(nt_clock_enable_snapshot(owner)==0);assert(owner->snapshot==7);assert(nt_eth_rate(&owner->hw,123)==0);assert(nt_eth_enable(&owner->hw)==0);
  {struct of_phandle_args args={1,{1}};assert(nt_clock_get_hw(&args,owner)==&owner->hw);args.args[0]=2;assert(IS_ERR(nt_clock_get_hw(&args,owner)));}
  w=writes;assert(nt98513_clock_prepare_enable(owner,NT98513_CLOCK_WATCHDOG)==0&&writes==w);
  if(!v)assert(IS_ERR(devm_nt98513_mmc_clock_get(&consumer,0)));
  else {a=get(0);assert(nt98513_mmc_clock_admit(a)==0);assert(owner->snapshot==7);}
 }
 for(i=0;i<5;i++) {reset(1);nt_singleton=NULL;nt_reservation=NULL;platform.resource[i].start++;assert(nt_owner_probe(&platform)==-EINVAL);assert(!writes&&!reads);}
 reset(1);nt_singleton=NULL;nt_reservation=NULL;platform.num_resources=2;assert(nt_owner_probe(&platform)==-EINVAL);
 reset(0);nt_singleton=NULL;nt_reservation=NULL;platform.num_resources=5;assert(nt_owner_probe(&platform)==-EINVAL);
 reset(1);nt_singleton=NULL;assert(PTR_ERR(devm_nt98513_mmc_clock_get(&consumer,0))==-EPROBE_DEFER);
 reset(1);node.available=0;assert(PTR_ERR(devm_nt98513_mmc_clock_get(&consumer,0))==-EINVAL);
}
int main(void) { lifecycle();validation();failures();transition_failures();legacy_and_probe();puts("production MMC clock checks passed");return 0; }
