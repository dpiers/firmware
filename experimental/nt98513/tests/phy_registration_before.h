/* SPDX-License-Identifier: GPL-2.0-only */
/* Verbatim successful-completion body from accepted Linux 6.6.158 stmmac_mdio.c.
 * Source SHA-256 38d88656d2014ea66c364f141ad9a8cd4e3861ffc52b8859c87e1ff71f876127
 * Wrapped with the new helper signature solely to demonstrate the absent reset
 * and absent callback-error cleanup in the baseline production completion path.
 */
static inline int stmmac_mdio_finish_registration(struct stmmac_priv *priv,
                                                struct mii_bus *new_bus)
{
	priv->mii = new_bus;

	return 0;

}
