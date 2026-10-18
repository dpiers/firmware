/* SPDX-License-Identifier: GPL-2.0-only
 * Harness for newly authored production NT lifecycle functions extracted from
 * patch8. USB-core stage faults and allocator boundaries are substitutes;
 * the actual wrapper/setup/stop/fail-stop functions execute unchanged.
 * FOTG210-derived interfaces retain upstream attribution in the Linux patch:
 * Copyright (c) 2013 Faraday Technology Corporation; Yuan-Hsin Chen,
 * Feng-Hsin Chiang, Po-Yu Chuang; Linux EHCI contributors. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include <stdio.h>
#include <errno.h>
#include "nt98513-policy.h"
#define __iomem
#define CMD_RUN 1
#define CMD_PSE 16
#define CMD_ASE 32
#define CMD_IAAD 64
#define FOTG210_RH_HALTED 0
#define HCD_USB2 0x20
#define STS_FATAL 0x10
struct device { void *data; };
struct platform_device { struct device dev; };
struct resource { unsigned long start,size; };
struct nt98513_usb { struct nt_usb_io io;struct nt_usb_state state;bool core_ready,death_notified; };
struct fotg210 { void *base;struct resource *res;struct nt98513_usb *nt; };
struct fotg210_hcd {
 struct fotg210 *fotg;void *caps,*regs;
 unsigned int command,rh_state,enabled_hrtimer_events,hcs_params,sbrn;
 int lock,hrtimer;bool shutdown;
};
struct usb_hcd { int irq,has_tt,died_work;void *regs;unsigned long rsrc_start,rsrc_len;struct fotg210_hcd priv; };
static struct usb_hcd host;
static int stage,allocations,frees,put_count,resets,runs,halt_stuck,halt_reads,irq_sync,irq_disabled;
static int root_valid,notifications,cancellations;
static jmp_buf terminal;
static const int nt98513_hc_driver;
static void spin_lock_init(int *p){*p=0;}
static void spin_lock_irq(int *p){(void)p;}
static void spin_unlock_irq(int *p){(void)p;}
static struct fotg210_hcd *hcd_to_fotg210(struct usb_hcd *h){return &h->priv;}
static struct usb_hcd *fotg210_to_hcd(struct fotg210_hcd *f){assert(f==&host.priv);return &host;}
static void usb_hc_died(struct usb_hcd *h){assert(root_valid);h->died_work=1;notifications++;}
static void cancel_work_sync(int *work){assert(root_valid || !*work);*work=0;cancellations++;}
static void panic(const char *message){(void)message;longjmp(terminal,1);}
static void synchronize_irq(int irq){assert(irq==93);irq_sync++;}
static void hrtimer_cancel(int *timer){(void)timer;}
static void remove_sysfs_files(struct fotg210_hcd *f){(void)f;}
static void remove_debug_files(struct fotg210_hcd *f){(void)f;}
static void fotg210_mem_cleanup(struct fotg210_hcd *f){(void)f;assert(!halt_stuck);frees++;}
static int fixture_initialize(const struct nt_usb_io *io,struct nt_usb_state *s)
{(void)io;resets++;s->hcs=1;return stage==1 ? NT_USB_TIMEOUT : NT_USB_OK;}
#define nt_usb_initialize fixture_initialize
static int hcd_fotg210_init(struct usb_hcd *h)
{(void)h;allocations++;if(stage==2){frees++;return -ENOMEM;}return 0;}
static int platform_get_irq(struct platform_device *p,int n){(void)p;assert(n==0);return stage==6 ? -EINVAL : 93;}
static const char *dev_name(struct device *d){(void)d;return "fixture";}
static struct usb_hcd *usb_create_hcd(const void *driver,struct device *dev,const char *name)
{(void)driver;(void)dev;(void)name;memset(&host,0,sizeof(host));return stage==7 ? NULL : &host;}
static unsigned long resource_size(struct resource *r){return r->size;}
static void platform_set_drvdata(struct platform_device *p,void *data){p->dev.data=data;}
static void usb_put_hcd(struct usb_hcd *h){assert(h==&host);put_count++;}
static void device_wakeup_disable(struct device *d){(void)d;}
static void disable_irq(int n){assert(n==93);irq_disabled++;}
static void nt98513_notify_death(struct fotg210_hcd *);
static int nt98513_require_halt(struct fotg210_hcd *);
static void early_fatal(void)
{
 assert(!nt98513_require_halt(&host.priv));
 host.priv.shutdown=true;
 nt98513_notify_death(&host.priv);
}
static void enable_irq(int n)
{
 assert(n==93);irq_disabled--;
 if(stage==8 || stage==9 || stage==10)early_fatal();
}
static int fotg210_run(struct usb_hcd *h)
{runs++;assert(irq_disabled==1);h->priv.fotg->nt->state.published=1;return stage==4 || stage==8 ? -EIO : 0;}
static int usb_add_hcd(struct usb_hcd *,int,unsigned long);
static void fotg210_stop(struct usb_hcd *);
#include "nt-lifecycle.inc"
static void fotg210_stop(struct usb_hcd *h){nt98513_hcd_stop(h);}
static int usb_add_hcd(struct usb_hcd *h,int irq,unsigned long flags)
{
 int ret;assert(flags==0);root_valid=1;ret=nt98513_hcd_setup(h);if(ret){root_valid=0;return ret;}
 if(stage==3){root_valid=0;return -EBUSY;} /* IRQ failure: USB core skips stop */
 h->irq=irq;ret=nt98513_hcd_run(h);if(ret){assert(!h->died_work);root_valid=0;return ret;} /* start failure skips stop */
 if(stage==5 || stage==9){fotg210_stop(h);assert(!h->died_work);root_valid=0;return -EIO;} /* registration failure invokes stop */
 return 0;
}
static unsigned int rd(void *p,unsigned int b,unsigned int o)
{(void)p;assert(b==0);if(o==0x10)return 1;if(o==0x14){halt_reads++;return halt_stuck ? 0 : 0x1000;}return 0;}
static void wr(void *p,unsigned int b,unsigned int o,unsigned int v)
{(void)p;assert(b==0);assert(o==0x10||o==0x18);assert((o==0x18&&v==0)||(o==0x10&&!(v&0x71)));}
static void delay(void *p,unsigned int us){(void)p;assert(us==1);}
int main(void)
{
 struct resource res={0xc0600000,0x400};struct nt98513_usb nt;
 struct fotg210 fotg;struct platform_device pdev={0};int n,ret;
 for(n=0;n<=10;n++) {
  memset(&nt,0,sizeof(nt));nt.io=(struct nt_usb_io){0,rd,wr,delay};fotg=(struct fotg210){(void *)0x1000,&res,&nt};
  root_valid=notifications=cancellations=0;
  stage=n;allocations=frees=put_count=resets=runs=halt_reads=irq_sync=irq_disabled=0;
  ret=fotg210_nt98513_probe(&pdev,&fotg);
  assert((ret==0)==(n==0 || n==10));assert(!irq_disabled);
  assert(resets==(n==6||n==7?0:1));assert(allocations==(n==1||n==6||n==7?0:1));
  assert(notifications==(n==10?1:0));
  if(n==0 || n==10){assert(nt.state.allocated&&nt.state.published);fotg210_stop(&host);fotg210_stop(&host);assert(frees==1&&irq_sync==1&&!host.died_work);}
  else {assert(frees==allocations);assert(put_count==(n==6||n==7?0:1));assert(!nt.state.allocated);}
 }
 memset(&nt,0,sizeof(nt));nt.io=(struct nt_usb_io){0,rd,wr,delay};fotg=(struct fotg210){(void *)0x1000,&res,&nt};
 stage=0;allocations=frees=put_count=resets=runs=halt_reads=irq_sync=irq_disabled=0;
 assert(fotg210_nt98513_probe(&pdev,&fotg)==0);halt_stuck=1;
 if(!setjmp(terminal)){nt98513_hcd_stop(&host);assert(!"halt failure must panic");}
 assert(halt_reads==2000 && frees==0 && nt.state.allocated);
 puts("NT98513 USB actual lifecycle wrappers: all stage/fail-stop assertions passed");
 return 0;
}
