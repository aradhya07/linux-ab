// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 - Texas Instruments Incorporated
 *
 * Aradhya Bhatia <a-bhati1@ti.com>
 */

#ifndef __TIDSS_OLDI_H__
#define __TIDSS_OLDI_H__

#include <linux/media-bus-format.h>

#include "tidss_drv.h"

struct tidss_oldi;

/* OLDI Registers */
#define OLDI_REG_CONFIG		0x00
#define OLDI_REG_STATUS		0x04
#define OLDI_REG_LOOPBACK	0x08

/* OLDI Config Bits */
#define OLDI_ENABLE		BIT(0)
#define OLDI_MAP		BIT(1) | BIT(2) | BIT(3)
#define OLDI_SRC		BIT(4)
#define OLDI_MODE		BIT(5)
#define OLDI_MASTERSLAVE	BIT(6)
#define OLDI_DEPOL		BIT(7)
#define OLDI_MSB		BIT(8)
#define OLDI_LBEN		BIT(9)
#define OLDI_LBDATA		BIT(10)
#define OLDI_DUALMODESYNC	BIT(11)
#define OLDI_SOFTRST		BIT(12)
#define OLDI_TPATCFG		BIT(13)

enum tidss_oldi_link_type {
	OLDI_MODE_UNSUPPORTED,
	OLDI_MODE_SINGLE_LINK,
	OLDI_MODE_CLONE_SINGLE_LINK,
	OLDI_MODE_DUAL_LINK,
	OLDI_MODE_SECONDARY,
};

enum oldi_mode_reg_val { SPWG_18 = 0, JEIDA_24 = 1, SPWG_24 = 2 };

struct oldi_bus_format {
	u32 bus_fmt;
	u32 data_width;
	enum oldi_mode_reg_val oldi_mode_reg_val;
};

static const struct oldi_bus_format dispc_bus_formats[] = {
	{ MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,	18, SPWG_18 },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,	24, SPWG_24 },
	{ MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA,	24, JEIDA_24 },
};

int tidss_oldi_init(struct tidss_device *tidss, struct device_node *oldi_tx);

#endif /* __TIDSS_OLDI_H__ */
