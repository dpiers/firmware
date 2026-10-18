/* SPDX-License-Identifier: GPL-2.0-only */
/* Host dependencies only: ordering/error branches come from production functions. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define MTL_MAX_TX_QUEUES 1
#define STMMAC_TBS_EN 1
#define STMMAC_TBS_AVAIL 2
#define STMMAC_PCS_TBI 1
#define STMMAC_PCS_RTBI 2
#define STMMAC_FLAG_SERDES_UP_AFTER_PHY_LINKUP 1
#define DW_AN_C73 3
#define SPEED_10 10
#define SPEED_100 100
#define SPEED_1000 1000
#define STMMAC_RX_COE_NONE 0
#define STMMAC_DEFAULT_TWT_LS 1
#define DEF_DMA_RIWT 1
#define STMMAC_VLAN_INSERT 1
#define STMMAC_RX_COPYBREAK 256
#define CLOCK_MONOTONIC 1
#define HRTIMER_MODE_REL 0
#define GFP_KERNEL 0
#define EXPORT_SYMBOL_GPL(x)
#define IS_ERR(x) false
#define PTR_ERR(x) (-ENOMEM)
#define max(a,b) ((a)>(b)?(a):(b))
struct timer { void *function; };
struct stmmac_tx_queue { int tbs, dma_tx_phy, tx_tail_addr; struct timer txtimer; };
struct stmmac_rx_queue { int dma_rx_phy, rx_tail_addr, buf_alloc_num, queue_index; void *xsk_pool; };
struct stmmac_dma_conf { int dma_buf_sz; struct stmmac_tx_queue tx_queue[1]; struct stmmac_rx_queue rx_queue[1]; };
struct dma_desc { int unused; };
struct net_device;
struct plat { bool dma_start_after_irq; u32 rx_queues_to_use,tx_queues_to_use; int phy_interface,mac_port_sel_speed,rx_coe,flags,pmt; void *clk_ptp_ref,*bsp_priv,*dma_cfg; void (*serdes_powerdown)(struct net_device *,void *); int (*serdes_powerup)(struct net_device *,void *); struct { int enable; } *fpe_cfg; };
struct hw { int pcs,ps,rx_csum; void *xpcs,*lynx_pcs,*pcsr; };
struct stmmac_priv { struct plat *plat; struct hw *hw; struct net_device *dev; void *device,*ioaddr,*phylink,*tc_entries,*mii; int eee_enabled,tx_path_in_lpi_mode,eee_ctrl_timer; int tc_entries_max,eee_tw_timer,tx_lpi_timer,use_riwt,rx_riwt[1],tso,sph,rx_copybreak,irq_wake,lock; struct { int vlins,fpesel; } dma_cap; struct stmmac_dma_conf dma_conf; };
struct net_device { struct stmmac_priv *priv; int mtu; unsigned char dev_addr[6]; };
static struct plat plat;
static struct hw hw;
static struct stmmac_priv priv;
static struct net_device dev;
static struct stmmac_dma_conf conf;
static int buf_sz,eee_timer;
static void *stmmac_tx_timer;
struct device;
static int setup_error,irq_error,starts,requests,frees,enables,phy_stops,teardowns,stops,irq_frees;
static char events[128];
static void event(char c) { size_t n=strlen(events); events[n]=c; events[n+1]=0; }
#define netdev_priv(d) ((d)->priv)
#define dev_get_drvdata(d) (priv.dev)
#define netdev_err(...) ((void)0)
#define netdev_warn(...) ((void)0)
#define netdev_info(...) ((void)0)
#define dev_warn(...) ((void)0)
#define xpcs_get_an_mode(...) 0
#define pm_runtime_resume_and_get(...) 0
#define stmmac_init_phy(...) 0
#define stmmac_rx_ipc(...) 1
#define clk_prepare_enable(...) 0
#define stmmac_init_ptp(...) (-EOPNOTSUPP)
#define stmmac_rxp_config(...) 0
#define xsk_pool_get_rx_frame_size(...) 2048
#define alloc_dma_desc_resources(...) 0
#define init_dma_desc_rings(...) setup_error
#define netif_running(...) true
#define device_may_wakeup(...) false
#define stmmac_init_dma_engine(...) (event('H'),setup_error)
#define stmmac_request_irq(...) (event('I'),requests++,irq_error)
#define stmmac_start_all_dma(...) (event('D'),starts++)
#define stmmac_setup_dma_desc(...) (&conf)
#define free_dma_desc_resources(...) (event('F'),frees++)
#define stmmac_enable_all_queues(...) (event('Q'),enables++)
#define stmmac_enable_all_dma_irq(...) event('E')
#define stmmac_hw_teardown(...) (teardowns++)
#define phylink_stop(...) (phy_stops++)
#define kfree(...) ((void)0)
#define hrtimer_cancel(...) ((void)0)
#define hrtimer_init(...) ((void)0)
#define mutex_lock(...) ((void)0)
#define mutex_unlock(...) ((void)0)
#define netif_carrier_on(...) ((void)0)
#define netif_device_attach(...) ((void)0)
#define netif_set_real_num_rx_queues(...) ((void)0)
#define netif_set_real_num_tx_queues(...) ((void)0)
#define netif_tx_start_all_queues(...) ((void)0)
#define phylink_disconnect_phy(...) ((void)0)
#define phylink_resume(...) ((void)0)
#define phylink_speed_up(...) ((void)0)
#define phylink_start(...) ((void)0)
#define pinctrl_pm_select_default_state(...) ((void)0)
#define pm_runtime_put(...) ((void)0)
#define rtnl_lock(...) ((void)0)
#define rtnl_unlock(...) ((void)0)
#define stmmac_clear_descriptors(...) ((void)0)
#define stmmac_core_init(...) ((void)0)
#define stmmac_disable_dma_irq(...) ((void)0)
#define stmmac_dma_operation_mode(...) ((void)0)
#define stmmac_enable_sph(...) ((void)0)
#define stmmac_enable_tbs(...) ((void)0)
#define stmmac_enable_tso(...) ((void)0)
#define stmmac_enable_vlan(...) ((void)0)
#define stmmac_fpe_handshake(...) ((void)0)
#define stmmac_fpe_start_wq(...) ((void)0)
#define stmmac_free_tx_skbufs(...) ((void)0)
#define stmmac_init_chan(...) ((void)0)
#define stmmac_init_coalesce(...) ((void)0)
#define stmmac_init_rx_chan(...) ((void)0)
#define stmmac_init_tx_chan(...) ((void)0)
#define stmmac_mac_set(...) ((void)0)
#define stmmac_mdio_reset(...) ((void)0)
#define stmmac_mmc_setup(...) ((void)0)
#define stmmac_mtl_configuration(...) ((void)0)
#define stmmac_pcs_ctrl_ane(...) ((void)0)
#define stmmac_pmt(...) ((void)0)
#define stmmac_ptp_register(...) ((void)0)
#define stmmac_reset_queues_param(...) ((void)0)
#define stmmac_restore_hw_vlan_rx_fltr(...) ((void)0)
#define stmmac_rx_watchdog(...) ((void)0)
#define stmmac_safety_feat_configuration(...) ((void)0)
#define stmmac_set_dma_bfsize(...) ((void)0)
#define stmmac_set_rings_length(...) ((void)0)
#define stmmac_set_rx_mode(...) ((void)0)
#define stmmac_set_rx_tail_ptr(...) ((void)0)
#define stmmac_set_tx_tail_ptr(...) ((void)0)
#define stmmac_set_umac_addr(...) ((void)0)

#define del_timer_sync(...) ((void)0)
#define netif_carrier_off(...) ((void)0)
#define netif_trans_update(...) ((void)0)
#define netif_tx_disable(...) ((void)0)
#define phylink_speed_down(...) ((void)0)
#define stmmac_disable_all_queues(...) ((void)0)
#define stmmac_fpe_stop_wq(...) ((void)0)
#define stmmac_free_irq(...) (event('R'),irq_frees++)
#define stmmac_release_ptp(...) ((void)0)
#define stmmac_stop_all_dma(...) (event('S'),stops++)

#include "dma_irq_functions.c"

static void reset(bool selected)
{
 memset(&plat,0,sizeof(plat)); memset(&hw,0,sizeof(hw));
 memset(&priv,0,sizeof(priv)); memset(&dev,0,sizeof(dev));
 memset(&conf,0,sizeof(conf));
 plat.dma_start_after_irq=selected; plat.rx_queues_to_use=plat.tx_queues_to_use=1;
 priv.plat=&plat; priv.hw=&hw; priv.dev=&dev; dev.priv=&priv;
 setup_error=irq_error=starts=requests=frees=enables=phy_stops=teardowns=0;
 stops=irq_frees=0; events[0]=0;
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s; events=%s starts=%d requests=%d frees=%d\n",__LINE__,#c,events,starts,requests,frees); return 1; } } while (0)
static int run(bool xdp, bool selected, int setup, int irq)
{
 int ret;
 reset(selected); setup_error=setup; irq_error=irq;
 ret=xdp ? stmmac_xdp_open(&dev) : stmmac_open(&dev);
 CHECK(ret==(setup ? setup : irq));
 CHECK(requests==(setup ? 0 : 1));
 CHECK(starts==((setup || (selected && irq)) ? 0 : 1));
 CHECK(frees==((setup || irq) ? 1 : 0));
 CHECK(enables==((setup || irq) ? 0 : 1));
 if (!setup && selected && !irq) CHECK(strchr(events,'I')<strchr(events,'D') && strchr(events,'D')<strchr(events,'Q'));
 if (!setup && !selected) CHECK(strchr(events,'D')<strchr(events,'I'));
 if (!setup && irq) { CHECK(teardowns==1); if (!xdp) CHECK(phy_stops==1); }
 if (!setup && !irq) {
  /* Exercise the real release/reopen path; stop completion is mocked. */
  if (xdp) stmmac_xdp_release(&dev); else CHECK(stmmac_release(&dev)==0);
  CHECK(stops==1 && irq_frees==1 && frees==1);
  ret=xdp ? stmmac_xdp_open(&dev) : stmmac_open(&dev);
  CHECK(ret==0 && starts==2 && requests==2 && enables==2);
 }
 if (selected && (setup || irq)) {
  /* A later open after failure must not inherit deferred-start state. */
  setup_error=irq_error=0;
  ret=xdp ? stmmac_xdp_open(&dev) : stmmac_open(&dev);
  CHECK(ret==0 && starts==1 && requests==(setup ? 1 : 2) && frees==1);
 }
 return 0;
}
int main(int argc,char **argv)
{
 if (argc>1 && !strcmp(argv[1],"irq-failure")) return run(false,true,0,-EBUSY);
 for (int xdp=0;xdp<2;xdp++) for (int selected=0;selected<2;selected++) {
  if (run(xdp,selected,0,0) || run(xdp,selected,-EIO,0) || run(xdp,selected,0,-EBUSY)) return 1;
 }
 /* Resume retains registered IRQs and the original DMA start timing. */
 for (int selected=0;selected<2;selected++) {
  reset(selected); CHECK(stmmac_resume(NULL)==0); CHECK(starts==1 && requests==0);
  reset(selected); setup_error=-EIO; CHECK(stmmac_resume(NULL)==-EIO); CHECK(starts==0 && requests==0);
 }
 puts("all DMA/IRQ production-path assertions passed"); return 0;
}
