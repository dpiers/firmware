// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  This is the driver for the ST MAC 10/100/1000 on-chip Ethernet controllers.
  ST Ethernet IPs are built around a Synopsys IP Core.

	Copyright(C) 2007-2011 STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>

  Documentation available at:
	http://www.stlinux.com
  Support available at:
	https://bugzilla.stlinux.com/
*******************************************************************************/

/* Exact functions from accepted Linux 6.6.158 + patches1-6:
 * drivers/net/ethernet/stmicro/stmmac/stmmac_main.c
 */

/**
 * stmmac_hw_setup - setup mac in a usable state.
 *  @dev : pointer to the device structure.
 *  @ptp_register: register PTP if set
 *  Description:
 *  this is the main function to setup the HW in a usable state because the
 *  dma engine is reset, the core registers are configured (e.g. AXI,
 *  Checksum features, timers). The DMA is ready to start receiving and
 *  transmitting.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_hw_setup(struct net_device *dev, bool ptp_register)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	bool sph_en;
	u32 chan;
	int ret;

	/* DMA initialization and SW reset */
	ret = stmmac_init_dma_engine(priv);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: DMA engine initialization failed\n",
			   __func__);
		return ret;
	}

	/* Copy the MAC addr into the HW  */
	stmmac_set_umac_addr(priv, priv->hw, dev->dev_addr, 0);

	/* PS and related bits will be programmed according to the speed */
	if (priv->hw->pcs) {
		int speed = priv->plat->mac_port_sel_speed;

		if ((speed == SPEED_10) || (speed == SPEED_100) ||
		    (speed == SPEED_1000)) {
			priv->hw->ps = speed;
		} else {
			dev_warn(priv->device, "invalid port speed\n");
			priv->hw->ps = 0;
		}
	}

	/* Initialize the MAC Core */
	stmmac_core_init(priv, priv->hw, dev);

	/* Initialize MTL*/
	stmmac_mtl_configuration(priv);

	/* Apply the RX packet parser table */
	if (priv->tc_entries) {
		ret = stmmac_rxp_config(priv, priv->hw->pcsr, priv->tc_entries,
					priv->tc_entries_max);
		if (ret)
			return ret;
	}

	/* Initialize Safety Features */
	stmmac_safety_feat_configuration(priv);

	ret = stmmac_rx_ipc(priv, priv->hw);
	if (!ret) {
		netdev_warn(priv->dev, "RX IPC Checksum Offload disabled\n");
		priv->plat->rx_coe = STMMAC_RX_COE_NONE;
		priv->hw->rx_csum = 0;
	}

	/* Enable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, true);

	/* Set the HW DMA mode and the COE */
	stmmac_dma_operation_mode(priv);

	stmmac_mmc_setup(priv);

	if (ptp_register) {
		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0)
			netdev_warn(priv->dev,
				    "failed to enable PTP reference clock: %pe\n",
				    ERR_PTR(ret));
	}

	ret = stmmac_init_ptp(priv);
	if (ret == -EOPNOTSUPP)
		netdev_info(priv->dev, "PTP not supported by HW\n");
	else if (ret)
		netdev_warn(priv->dev, "PTP init failed\n");
	else if (ptp_register)
		stmmac_ptp_register(priv);

	priv->eee_tw_timer = STMMAC_DEFAULT_TWT_LS;

	/* Convert the timer from msec to usec */
	if (!priv->tx_lpi_timer)
		priv->tx_lpi_timer = eee_timer * 1000;

	if (priv->use_riwt) {
		u32 queue;

		for (queue = 0; queue < rx_cnt; queue++) {
			if (!priv->rx_riwt[queue])
				priv->rx_riwt[queue] = DEF_DMA_RIWT;

			stmmac_rx_watchdog(priv, priv->ioaddr,
					   priv->rx_riwt[queue], queue);
		}
	}

	if (priv->hw->pcs)
		stmmac_pcs_ctrl_ane(priv, priv->ioaddr, 1, priv->hw->ps, 0);

	/* set TX and RX rings length */
	stmmac_set_rings_length(priv);

	/* Enable TSO */
	if (priv->tso) {
		for (chan = 0; chan < tx_cnt; chan++) {
			struct stmmac_tx_queue *tx_q = &priv->dma_conf.tx_queue[chan];

			/* TSO and TBS cannot co-exist */
			if (tx_q->tbs & STMMAC_TBS_AVAIL)
				continue;

			stmmac_enable_tso(priv, priv->ioaddr, 1, chan);
		}
	}

	/* Enable Split Header */
	sph_en = (priv->hw->rx_csum > 0) && priv->sph;
	for (chan = 0; chan < rx_cnt; chan++)
		stmmac_enable_sph(priv, priv->ioaddr, sph_en, chan);


	/* VLAN Tag Insertion */
	if (priv->dma_cap.vlins)
		stmmac_enable_vlan(priv, priv->hw, STMMAC_VLAN_INSERT);

	/* TBS */
	for (chan = 0; chan < tx_cnt; chan++) {
		struct stmmac_tx_queue *tx_q = &priv->dma_conf.tx_queue[chan];
		int enable = tx_q->tbs & STMMAC_TBS_AVAIL;

		stmmac_enable_tbs(priv, priv->ioaddr, enable, chan);
	}

	/* Configure real RX and TX queues */
	netif_set_real_num_rx_queues(dev, priv->plat->rx_queues_to_use);
	netif_set_real_num_tx_queues(dev, priv->plat->tx_queues_to_use);

	/* Start the ball rolling... */
	stmmac_start_all_dma(priv);

	if (priv->dma_cap.fpesel) {
		stmmac_fpe_start_wq(priv);

		if (priv->plat->fpe_cfg->enable)
			stmmac_fpe_handshake(priv, true);
	}

	return 0;
}

/**
 *  __stmmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  @dma_conf :  structure to take the dma data
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int __stmmac_open(struct net_device *dev,
			 struct stmmac_dma_conf *dma_conf)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int mode = priv->plat->phy_interface;
	u32 chan;
	int ret;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	if (priv->hw->pcs != STMMAC_PCS_TBI &&
	    priv->hw->pcs != STMMAC_PCS_RTBI &&
	    (!priv->hw->xpcs ||
	     xpcs_get_an_mode(priv->hw->xpcs, mode) != DW_AN_C73) &&
	    !priv->hw->lynx_pcs) {
		ret = stmmac_init_phy(dev);
		if (ret) {
			netdev_err(priv->dev,
				   "%s: Cannot attach to PHY (error: %d)\n",
				   __func__, ret);
			goto init_phy_error;
		}
	}

	priv->rx_copybreak = STMMAC_RX_COPYBREAK;

	buf_sz = dma_conf->dma_buf_sz;
	for (int i = 0; i < MTL_MAX_TX_QUEUES; i++)
		if (priv->dma_conf.tx_queue[i].tbs & STMMAC_TBS_EN)
			dma_conf->tx_queue[i].tbs = priv->dma_conf.tx_queue[i].tbs;
	memcpy(&priv->dma_conf, dma_conf, sizeof(*dma_conf));

	stmmac_reset_queues_param(priv);

	if (!(priv->plat->flags & STMMAC_FLAG_SERDES_UP_AFTER_PHY_LINKUP) &&
	    priv->plat->serdes_powerup) {
		ret = priv->plat->serdes_powerup(dev, priv->plat->bsp_priv);
		if (ret < 0) {
			netdev_err(priv->dev, "%s: Serdes powerup failed\n",
				   __func__);
			goto init_error;
		}
	}

	ret = stmmac_hw_setup(dev, true);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		goto init_error;
	}

	stmmac_init_coalesce(priv);

	phylink_start(priv->phylink);
	/* We may have called phylink_speed_down before */
	phylink_speed_up(priv->phylink);

	ret = stmmac_request_irq(dev);
	if (ret)
		goto irq_error;

	stmmac_enable_all_queues(priv);
	netif_tx_start_all_queues(priv->dev);
	stmmac_enable_all_dma_irq(priv);

	return 0;

irq_error:
	phylink_stop(priv->phylink);

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		hrtimer_cancel(&priv->dma_conf.tx_queue[chan].txtimer);

	stmmac_hw_teardown(dev);
init_error:
	phylink_disconnect_phy(priv->phylink);
init_phy_error:
	pm_runtime_put(priv->device);
	return ret;
}

static int stmmac_open(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct stmmac_dma_conf *dma_conf;
	int ret;

	dma_conf = stmmac_setup_dma_desc(priv, dev->mtu);
	if (IS_ERR(dma_conf))
		return PTR_ERR(dma_conf);

	ret = __stmmac_open(dev, dma_conf);
	if (ret)
		free_dma_desc_resources(priv, dma_conf);

	kfree(dma_conf);
	return ret;
}

int stmmac_xdp_open(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	u32 dma_csr_ch = max(rx_cnt, tx_cnt);
	struct stmmac_rx_queue *rx_q;
	struct stmmac_tx_queue *tx_q;
	u32 buf_size;
	bool sph_en;
	u32 chan;
	int ret;

	ret = alloc_dma_desc_resources(priv, &priv->dma_conf);
	if (ret < 0) {
		netdev_err(dev, "%s: DMA descriptors allocation failed\n",
			   __func__);
		goto dma_desc_error;
	}

	ret = init_dma_desc_rings(dev, &priv->dma_conf, GFP_KERNEL);
	if (ret < 0) {
		netdev_err(dev, "%s: DMA descriptors initialization failed\n",
			   __func__);
		goto init_error;
	}

	stmmac_reset_queues_param(priv);

	/* DMA CSR Channel configuration */
	for (chan = 0; chan < dma_csr_ch; chan++) {
		stmmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);
		stmmac_disable_dma_irq(priv, priv->ioaddr, chan, 1, 1);
	}

	/* Adjust Split header */
	sph_en = (priv->hw->rx_csum > 0) && priv->sph;

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_cnt; chan++) {
		rx_q = &priv->dma_conf.rx_queue[chan];

		stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    rx_q->dma_rx_phy, chan);

		rx_q->rx_tail_addr = rx_q->dma_rx_phy +
				     (rx_q->buf_alloc_num *
				      sizeof(struct dma_desc));
		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       rx_q->rx_tail_addr, chan);

		if (rx_q->xsk_pool && rx_q->buf_alloc_num) {
			buf_size = xsk_pool_get_rx_frame_size(rx_q->xsk_pool);
			stmmac_set_dma_bfsize(priv, priv->ioaddr,
					      buf_size,
					      rx_q->queue_index);
		} else {
			stmmac_set_dma_bfsize(priv, priv->ioaddr,
					      priv->dma_conf.dma_buf_sz,
					      rx_q->queue_index);
		}

		stmmac_enable_sph(priv, priv->ioaddr, sph_en, chan);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_cnt; chan++) {
		tx_q = &priv->dma_conf.tx_queue[chan];

		stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    tx_q->dma_tx_phy, chan);

		tx_q->tx_tail_addr = tx_q->dma_tx_phy;
		stmmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       tx_q->tx_tail_addr, chan);

		hrtimer_init(&tx_q->txtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		tx_q->txtimer.function = stmmac_tx_timer;
	}

	/* Enable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, true);

	/* Start Rx & Tx DMA Channels */
	stmmac_start_all_dma(priv);

	ret = stmmac_request_irq(dev);
	if (ret)
		goto irq_error;

	/* Enable NAPI process*/
	stmmac_enable_all_queues(priv);
	netif_carrier_on(dev);
	netif_tx_start_all_queues(dev);
	stmmac_enable_all_dma_irq(priv);

	return 0;

irq_error:
	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		hrtimer_cancel(&priv->dma_conf.tx_queue[chan].txtimer);

	stmmac_hw_teardown(dev);
init_error:
	free_dma_desc_resources(priv, &priv->dma_conf);
dma_desc_error:
	return ret;
}

/**
 * stmmac_resume - resume callback
 * @dev: device pointer
 * Description: when resume this function is invoked to setup the DMA and CORE
 * in a usable state.
 */
int stmmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;

	if (!netif_running(ndev))
		return 0;

	/* Power Down bit, into the PM register, is cleared
	 * automatically as soon as a magic packet or a Wake-up frame
	 * is received. Anyway, it's better to manually clear
	 * this bit because it can generate problems while resuming
	 * from another devices (e.g. serial console).
	 */
	if (device_may_wakeup(priv->device) && priv->plat->pmt) {
		mutex_lock(&priv->lock);
		stmmac_pmt(priv, priv->hw, 0);
		mutex_unlock(&priv->lock);
		priv->irq_wake = 0;
	} else {
		pinctrl_pm_select_default_state(priv->device);
		/* reset the phy so that it's ready */
		if (priv->mii)
			stmmac_mdio_reset(priv->mii);
	}

	if (!(priv->plat->flags & STMMAC_FLAG_SERDES_UP_AFTER_PHY_LINKUP) &&
	    priv->plat->serdes_powerup) {
		ret = priv->plat->serdes_powerup(ndev,
						 priv->plat->bsp_priv);

		if (ret < 0)
			return ret;
	}

	rtnl_lock();
	if (device_may_wakeup(priv->device) && priv->plat->pmt) {
		phylink_resume(priv->phylink);
	} else {
		phylink_resume(priv->phylink);
		if (device_may_wakeup(priv->device))
			phylink_speed_up(priv->phylink);
	}
	rtnl_unlock();

	rtnl_lock();
	mutex_lock(&priv->lock);

	stmmac_reset_queues_param(priv);

	stmmac_free_tx_skbufs(priv);
	stmmac_clear_descriptors(priv, &priv->dma_conf);

	ret = stmmac_hw_setup(ndev, false);
	if (ret < 0) {
		netdev_err(priv->dev, "%s: Hw setup failed\n", __func__);
		mutex_unlock(&priv->lock);
		rtnl_unlock();
		return ret;
	}

	stmmac_init_coalesce(priv);
	stmmac_set_rx_mode(ndev);

	stmmac_restore_hw_vlan_rx_fltr(priv, ndev, priv->hw);

	stmmac_enable_all_queues(priv);
	stmmac_enable_all_dma_irq(priv);

	mutex_unlock(&priv->lock);
	rtnl_unlock();

	netif_device_attach(ndev);

	return 0;
}

/**
 *  stmmac_release - close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver.
 */
static int stmmac_release(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 chan;

	if (device_may_wakeup(priv->device))
		phylink_speed_down(priv->phylink, false);
	/* Stop and disconnect the PHY */
	phylink_stop(priv->phylink);
	phylink_disconnect_phy(priv->phylink);

	stmmac_disable_all_queues(priv);

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		hrtimer_cancel(&priv->dma_conf.tx_queue[chan].txtimer);

	netif_tx_disable(dev);

	/* Free the IRQ lines */
	stmmac_free_irq(dev, REQ_IRQ_ERR_ALL, 0);

	if (priv->eee_enabled) {
		priv->tx_path_in_lpi_mode = false;
		del_timer_sync(&priv->eee_ctrl_timer);
	}

	/* Stop TX/RX DMA and clear the descriptors */
	stmmac_stop_all_dma(priv);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv, &priv->dma_conf);

	/* Disable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, false);

	/* Powerdown Serdes if there is */
	if (priv->plat->serdes_powerdown)
		priv->plat->serdes_powerdown(dev, priv->plat->bsp_priv);

	netif_carrier_off(dev);

	stmmac_release_ptp(priv);

	pm_runtime_put(priv->device);

	if (priv->dma_cap.fpesel)
		stmmac_fpe_stop_wq(priv);

	return 0;
}

void stmmac_xdp_release(struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	u32 chan;

	/* Ensure tx function is not running */
	netif_tx_disable(dev);

	/* Disable NAPI process */
	stmmac_disable_all_queues(priv);

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++)
		hrtimer_cancel(&priv->dma_conf.tx_queue[chan].txtimer);

	/* Free the IRQ lines */
	stmmac_free_irq(dev, REQ_IRQ_ERR_ALL, 0);

	/* Stop TX/RX DMA channels */
	stmmac_stop_all_dma(priv);

	/* Release and free the Rx/Tx resources */
	free_dma_desc_resources(priv, &priv->dma_conf);

	/* Disable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, false);

	/* set trans_start so we don't get spurious
	 * watchdogs during reset
	 */
	netif_trans_update(dev);
	netif_carrier_off(dev);
}
