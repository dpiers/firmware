/* SPDX-License-Identifier: GPL-2.0-only */
/* Verbatim Linux 6.6.158 polling macros, with host API implementations
 * supplied by phy_reset.c. This is upstream code, not a polling model.
 * read_poll_timeout: Copyright (c) 2012-2014 The Linux Foundation.
 * phy.h: Copyright (c) 2004 Freescale Semiconductor, Inc.; Andy Fleming.
 * phy.h is GPL-2.0-or-later; this combined fixture uses GPL version2.
 */

/* include/linux/iopoll.h SHA-256 ae14ea5396f486ab5be2c364fe9ce10226c7088dbc1d798c6a6d8b88854b1558 */
#define read_poll_timeout(op, val, cond, sleep_us, timeout_us, \
				sleep_before_read, args...) \
({ \
	u64 __timeout_us = (timeout_us); \
	unsigned long __sleep_us = (sleep_us); \
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us); \
	might_sleep_if((__sleep_us) != 0); \
	if (sleep_before_read && __sleep_us) \
		usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
	for (;;) { \
		(val) = op(args); \
		if (cond) \
			break; \
		if (__timeout_us && \
		    ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = op(args); \
			break; \
		} \
		if (__sleep_us) \
			usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
		cpu_relax(); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

/* include/linux/phy.h SHA-256 1db0882764f5a1dc7bd3eea3bda5a677740a29a3f015b54bd047efe9e7ee667a */
#define phy_read_poll_timeout(phydev, regnum, val, cond, sleep_us, \
				timeout_us, sleep_before_read) \
({ \
	int __ret, __val; \
	__ret = read_poll_timeout(__val = phy_read, val, \
				  __val < 0 || (cond), \
		sleep_us, timeout_us, sleep_before_read, phydev, regnum); \
	if (__val < 0) \
		__ret = __val; \
	if (__ret) \
		phydev_err(phydev, "%s failed: %d\n", __func__, __ret); \
	__ret; \
})
