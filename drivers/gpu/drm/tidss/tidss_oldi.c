// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 - Texas Instruments Incorporated
 *
 * Aradhya Bhatia <a-bhati1@ti.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/mfd/syscon.h>
#include <linux/media-bus-format.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>
#include <drm/drm_crtc.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "tidss_oldi.h"

struct tidss_oldi {
	struct device_node *node;

	struct drm_bridge	bridge;
	struct drm_connector	connector;
	struct drm_bridge	*next_bridge;

	struct drm_panel *panel;

	enum tidss_oldi_link_type link_type;

	struct clk *s_clk;
	struct regmap *io_ctrl;
};

static inline struct tidss_oldi *
drm_bridge_to_tidss_oldi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct tidss_oldi, bridge);
}

static inline struct tidss_oldi *
drm_connector_to_tidss_oldi(struct drm_connector *connector)
{
	return container_of(connector, struct tidss_oldi, connector);
}

static int tidss_oldi_get_modes(struct drm_connector *connector)
{
	struct tidss_oldi *oldi = drm_connector_to_tidss_oldi(connector);
	
	return drm_panel_get_modes(oldi->panel, connector);
}

static const struct drm_connector_helper_funcs tidss_oldi_con_helper_funcs = {
	.get_modes	= tidss_oldi_get_modes,
};

static const struct drm_connector_funcs tidss_oldi_con_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int tidss_oldi_bridge_attach(struct drm_bridge *bridge,
				    enum drm_bridge_attach_flags flags)
{
	struct tidss_oldi *oldi = drm_bridge_to_tidss_oldi(bridge);
	//u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int ret;

	pr_err("%s\n", __func__);

	if (!oldi->next_bridge) {
		pr_err("%s: Returning Error enodev\n", __func__);
		return -ENODEV;
	}

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return drm_bridge_attach(bridge->encoder, oldi->next_bridge,
					 bridge, flags);

	drm_connector_helper_add(&oldi->connector,&
				 tidss_oldi_con_helper_funcs);

	ret = drm_connector_init(bridge->dev, &oldi->connector,
				 &tidss_oldi_con_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		DRM_ERROR("oldi Failed to initialize connector\n");
		return ret;
	}

	//ret = drm_display_info_set_bus_formats(&oldi->connector.display_info,
	//				       &bus_format, 1);
	//if (ret)
	//	return ret;

	drm_connector_attach_encoder(&oldi->connector, bridge->encoder);
	return 0;
}

static void tidss_oldi_atomic_enable(struct drm_bridge *bridge,
				     struct drm_bridge_state *old_bridge_state)
{
	struct tidss_oldi *oldi = drm_bridge_to_tidss_oldi(bridge);
	pr_err("%s: %s\n", __func__, oldi->node->name);

	// enable oldi_cfg
	// enable clock
}

static void tidss_oldi_atomic_disable(struct drm_bridge *bridge,
				      struct drm_bridge_state *old_bridge_state)
{
	struct tidss_oldi *oldi = drm_bridge_to_tidss_oldi(bridge);
	pr_err("%s: %s\n", __func__, oldi->node->name);

	// disable clock
	// disable oldi_cfg signal

}
static u32 *tidss_oldi_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
						 struct drm_bridge_state *bridge_state,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state,
						 u32 output_fmt,
						 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(1, sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;

	*num_input_fmts = 1;

	return input_fmts;
}

static u32 *tidss_oldi_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					   struct drm_bridge_state *bridge_state,
					   struct drm_crtc_state *crtc_state,
					   struct drm_connector_state *conn_state,
					   unsigned int *num_output_fmts)
{
	u32 *output_fmts;

	*num_output_fmts = 0;

	output_fmts = kcalloc(1, sizeof(*output_fmts), GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	output_fmts[0] = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	*num_output_fmts = 1;
	return output_fmts;
}

static const struct drm_bridge_funcs tidss_oldi_bridge_funcs = {
	.attach		= tidss_oldi_bridge_attach,
	.atomic_enable	= tidss_oldi_atomic_enable,
	.atomic_disable	= tidss_oldi_atomic_disable,
	.atomic_get_input_bus_fmts = tidss_oldi_atomic_get_input_bus_fmts,
	.atomic_get_output_bus_fmts = tidss_oldi_atomic_get_output_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int tidss_oldi_parse_dt(struct tidss_oldi *oldi)
{
	struct device_node *companion;
	struct device_node *port0, *port1;
	int pixel_order;
	int ret = 0;

	/* Locate the companion OLDI Transmitter for dual-link operation, if any. */
	companion = of_parse_phandle(oldi->node, "ti,companion-oldi", 0);
	if (!companion) {
		if (of_property_read_bool(oldi->node, "ti,secondary-oldi")) {
			pr_debug("oldi Secondary OLDI. Returning 0.\n");
			pr_err("oldi Secondary OLDI. Returning 0.\n");
			oldi->link_type = OLDI_MODE_SECONDARY;
			return 0;
		}

		oldi->link_type = OLDI_MODE_SINGLE_LINK;
		pr_err("oldi Single-link configuration detected\n");
		return 0;
	}

	/*
	 * We need to work out if the sink is expecting us to function in
	 * dual-link mode. We do this by looking at the DT port nodes we are
	 * connected to, if they are marked as expecting even pixels and
	 * odd pixels than we need to enable vertical stripe output.
	 */
	port0 = of_graph_get_port_by_id(oldi->node, 1);
	port1 = of_graph_get_port_by_id(companion, 1);
	pixel_order = drm_of_lvds_get_dual_link_pixel_order(port0, port1);
	of_node_put(port0);
	of_node_put(port1);

	switch (pixel_order) {
	case -EINVAL:
		/*
		 * The dual link properties were not found in at least
		 * one of the sink nodes. Since 2 OLDI ports are present
		 * in the DT, it can be safely assumed that the required
		 * configuration is Clone Mode.
		 */
		oldi->link_type = OLDI_MODE_CLONE_SINGLE_LINK;
		break;

	case DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS:
		oldi->link_type = OLDI_MODE_DUAL_LINK;
		break;

	case DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS:
		oldi->link_type = OLDI_MODE_UNSUPPORTED;
		break;

	default:
		/*
		 * Early dual-link bridge specific implementations populate the
		 * timings field of drm_bridge. If the flag is set, we assume
		 * that the OLDI transmitters are expected to work in dual-link
		 * mode.
		 */
		if (oldi->next_bridge && oldi->next_bridge->timings &&
		    oldi->next_bridge->timings->dual_link)
			oldi->link_type = OLDI_MODE_DUAL_LINK;
		else
			oldi->link_type = OLDI_MODE_UNSUPPORTED;

		break;
	}

	if (oldi->link_type == OLDI_MODE_UNSUPPORTED) {
		pr_err("oldi Detected OLDI mode is not supported\n");
		goto done;
	} else if (oldi->link_type == OLDI_MODE_CLONE_SINGLE_LINK) {
		pr_debug("Single-link Clone configuration detected\n");
		pr_err("oldi Single-link Clone configuration detected\n");
	} else {
		pr_debug("oldi Dual-link configuration detected (companion OLDI TX %pOF)\n",
			 companion);

		pr_err("oldi Dual-link configuration detected (companion OLDI TX %pOF)\n",
		       companion);
	}

done:
	of_node_put(companion);

	return ret;
}

int tidss_oldi_init(struct tidss_device *tidss, struct device_node *oldi_tx)
{
	struct tidss_oldi *oldi;
	struct device *dev = tidss->dev;
	int ret;
	unsigned long new_rate;

	oldi = devm_kzalloc(dev, sizeof(*oldi), GFP_KERNEL);
	if (!oldi)
		return -ENOMEM;
		//return NULL;

	oldi->node = oldi_tx;

	pr_err("%s: node name = %s\n", __func__, oldi->node->name);

	ret = tidss_oldi_parse_dt(oldi);
	if (ret < 0)
		return ret;
		//return NULL;

	ret = drm_of_find_panel_or_bridge(oldi->node, 1, 0,
					  &oldi->panel, &oldi->next_bridge);

	pr_err("%s: OLDI drm_of_find_panel_or_bridge Ret =  %d\n", __func__, ret);

	/* from this point on, probe defer is not possible */
	if (!(oldi->next_bridge || oldi->panel)) {
		pr_debug("oldi Next bridge/panel not found, deferring probe\n");
		pr_err("oldi Next bridge/panel not found, deferring probe\n");
		return -EPROBE_DEFER;
		//return NULL;
	} else if (oldi->panel && !oldi->next_bridge) {
		oldi->next_bridge = devm_drm_panel_bridge_add(dev,
							      oldi->panel);
		if (IS_ERR_OR_NULL(oldi->next_bridge)) {
			return -EINVAL;
		}
	}

 	/* Register the bridge. */
	oldi->bridge.driver_private = oldi;
	oldi->bridge.funcs = &tidss_oldi_bridge_funcs;
	oldi->bridge.of_node = oldi->node;
	
	if (oldi->link_type != OLDI_MODE_SECONDARY) {
		oldi->io_ctrl =
			syscon_regmap_lookup_by_phandle(oldi->node,
							"ti,oldi-io-ctrl");
		if (IS_ERR(oldi->io_ctrl)) {
			pr_err("%s: oldi syscon_regmap_lookup_by_phandle failed %ld\n",
			       __func__, PTR_ERR(oldi->io_ctrl));
			return PTR_ERR(oldi->io_ctrl);
			//return NULL;
		}

		regmap_update_bits(oldi->io_ctrl, 0x100, BIT(0) | BIT(1) | BIT(8), 0x00);

		oldi->s_clk = of_clk_get_by_name(oldi->node, "s_clk");
		if (IS_ERR(oldi->s_clk)) {
			dev_err(dev, "%s: oldi Failed to get s_clk: %ld\n",
				__func__, PTR_ERR(oldi->s_clk));
			return PTR_ERR(oldi->s_clk);
			//return NULL;
		}

		ret = clk_set_rate(oldi->s_clk, 1057000000);
		if (ret) {
			pr_err("clk set rate error %d\n", ret);	
		}

		new_rate = clk_get_rate(oldi->s_clk);

		pr_err("oldi clk get rate = %lu\n", new_rate);

	} else {
		oldi->io_ctrl = NULL;
		oldi->s_clk = NULL;
	}

	pr_err("Adding drm bridge for oldi \n");
	drm_bridge_add(&oldi->bridge);

	return ret;
}

MODULE_AUTHOR("Aradhya Bhatia <a-bhati1@ti.com>");
MODULE_DESCRIPTION("TIDSS DPI -> OLDI Transmitter");
MODULE_LICENSE("GPL v2");
