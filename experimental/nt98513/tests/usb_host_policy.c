/* Inert register model exercises the actual production state/sequence. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nt98513-policy.h"
struct model {
 unsigned int reg[3][256], writes, reads, delay, bad, reset_stuck, halt_stuck;
 unsigned int reset_seen, start_seen, wrong_readback;
 unsigned int fault_on, fault_bank, fault_off;
};
static unsigned int rd(void *p, unsigned int bank, unsigned int off)
{
 struct model *m=p; m->reads++;
 assert(bank<3 && off<1024 && !(off&3));
 if(m->fault_on && bank==m->fault_bank && off==m->fault_off)return ~0u;
 assert(!(bank==0 && (off==0x84 || off==0xc4))); /* no FOTG OTGISR/GMIR */
 if (bank==0 && off==0x10 && (m->reg[0][4]&2) && !m->reset_stuck) {
  m->reg[0][4]&=~2u; m->reset_seen++;
  m->reg[1][0x14/4]=0xa5000000; m->reg[1][0x18/4]=0x5a000000;
  m->reg[0][0x108/4]=0;
 }
 if (m->wrong_readback && bank==1 && off==0x148) return 0;
 return m->reg[bank][off/4];
}
static void wr(void *p, unsigned int bank, unsigned int off, unsigned int value)
{
 struct model *m=p; m->writes++;
 assert(bank<2 && off<1024 && !(off&3));
 assert(!(bank==0 && (off==0x84 || off==0xc4)));
 m->reg[bank][off/4]=value;
 if (bank==0 && off==0x10 && !(value&1) && !m->halt_stuck) m->reg[0][0x14/4]|=0x1000;
 if (bank==0 && off==0x10 && (value&1)) m->start_seen++;
}
static void delay(void *p,unsigned int us) { ((struct model *)p)->delay+=us; }
static struct nt_usb_io io(struct model *m) { struct nt_usb_io x={m,rd,wr,delay}; return x; }
static void init(struct model *m)
{
 memset(m,0,sizeof(*m)); m->reg[0][0]=0x01000010;
 m->reg[0][1]=0x11; m->reg[0][2]=0;
 m->reg[0][0x108/4]=1; m->reg[1][0x14/4]=0xabcdef14;
 m->reg[1][0x18/4]=0xfedcba0a; m->reg[2][0]=0x100;
}
int main(void)
{
 struct model m; struct nt_usb_io x; struct nt_usb_state s={0}; unsigned int v;
 assert(nt_usb_admit(1,1,1,1,0xc0600000,0x400,0xc0601000,0x200,27));
 for(v=0;v<4;v++) assert(!nt_usb_admit(v!=0,v!=1,v!=2,v!=3,0xc0600000,0x400,0xc0601000,0x200,27));
 assert(!nt_usb_admit(1,1,1,1,0xc0600000,0x400,0xc0601000,0x200,26));
 assert(!nt_usb_admit(1,1,1,1,0xc0600000,0x400,0xc0601000,0x201,27));
 for(v=0;v<256;v++) assert(nt_usb_caps(0x01000000|v,0x11,0)==(v==16));
 assert(!nt_usb_caps(0x01000010,2,0)); assert(!nt_usb_caps(0x01000010,0x101,0));
 assert(!nt_usb_caps(0x01000010,1,1)); assert(!nt_usb_caps(0xffffffff,1,0));
 for(v=0;v<4;v++) {
  unsigned int wanted[]={0,3,0,1};
  init(&m); m.reg[2][0]=v<<8; x=io(&m); memset(&s,0,sizeof(s));
  assert(nt_usb_initialize(&x,&s)==NT_USB_OK);
  assert(s.mux==(v<<8) && (m.reg[0][0x304/4]&3)==wanted[v]);
  assert((m.reg[1][0x14/4]&0x1c)==0x14 && (m.reg[1][0x18/4]&0xe)==0xa);
  assert((m.reg[1][0x14/4]&~0x1cu)==0xa5000000);
  assert((m.reg[1][0x18/4]&~0xeu)==0x5a000000);
  assert(m.reg[0][0x108/4]&1); assert(m.reg[1][0x148/4]==0x6d);
  assert(m.reset_seen==1 && m.start_seen==0 && nt_usb_live(&x,&s));
  m.reg[2][0]^=0x100; assert(!nt_usb_live(&x,&s));
 }
 init(&m); x=io(&m); m.reg[0][0]=0; assert(nt_usb_initialize(&x,&s)==NT_USB_INVALID); assert(!m.writes);
 init(&m); x=io(&m); m.reg[0][0x108/4]=0; assert(nt_usb_initialize(&x,&s)==NT_USB_INVALID); assert(!m.writes);
 init(&m); x=io(&m); m.reg[0][0x80/4]=1u<<20; assert(nt_usb_initialize(&x,&s)==NT_USB_INVALID); assert(!m.writes);
 init(&m); x=io(&m); m.halt_stuck=1; assert(nt_usb_initialize(&x,&s)==NT_USB_TIMEOUT); assert(m.delay==2000 && !m.reset_seen);
 init(&m); x=io(&m); m.reset_stuck=1; assert(nt_usb_initialize(&x,&s)==NT_USB_TIMEOUT); assert(m.delay==250000 && !m.reset_seen);
 init(&m); x=io(&m); m.wrong_readback=1; assert(nt_usb_initialize(&x,&s)==NT_USB_INVALID);
 init(&m); x=io(&m);m.reg[1][0x14/4]=0x1c;m.reg[1][0x18/4]=0xe;
 assert(nt_usb_initialize(&x,&s)==NT_USB_OK);
 { unsigned int banks[]={2,0,0,1,1,0}, offsets[]={0,0x80,0x304,0x14,0x18,0x108};
  for(v=0;v<6;v++) {m.fault_on=1;m.fault_bank=banks[v];m.fault_off=offsets[v];assert(!nt_usb_live(&x,&s));}
 }
 assert(nt_usb_port_value(0xffffffff,0x1000,0)==(0xffffffffu&~0x2au));
 assert(!(nt_usb_port_value(0xffffffff,0,0x1000)&(0x1000|0x2a)));
 for(v=0;v<4;v++) assert(nt_usb_transfer_supported(v)==(v==2||v==3)); /* Linux PIPE_CONTROL/BULK */
 memset(&s,0,sizeof(s)); assert(nt_usb_allocate_once(&s)); assert(!nt_usb_allocate_once(&s));
 s.published=1; assert(!nt_usb_release(&s,0) && s.allocated);
 assert(nt_usb_release(&s,1) && !s.allocated); assert(!nt_usb_release(&s,1));
 puts("NT98513 USB production policy: all assertions passed"); return 0;
}
