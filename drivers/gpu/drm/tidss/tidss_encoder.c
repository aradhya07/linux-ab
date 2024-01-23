// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/export.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "tidss_crtc.h"
#include "tidss_dispc.h"
#include "tidss_drv.h"
#include "tidss_encoder.h"

struct tidss_encoder {
	struct drm_bridge bridge;
	struct drm_encoder encoder;
	struct drm_connector *connector;
	struct drm_bridge *next_bridge;
	struct tidss_device *tidss;
};

static inline struct tidss_encoder
*bridge_to_tidss_encoder(struct drm_bridge *b)
{
	return container_of(b, struct tidss_encoder, bridge);
}

static int tidss_bridge_attach(struct drm_bridge *bridge,
			       enum drm_bridge_attach_flags flags)
{
	struct tidss_encoder *t_enc = bridge_to_tidss_encoder(bridge);

	return drm_bridge_attach(bridge->encoder, t_enc->next_bridge,
				 bridge, flags);
}

static int tidss_bridge_atomic_check(struct drm_bridge *bridge,
				     struct drm_bridge_state *bridge_state,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct tidss_encoder *t_enc = bridge_to_tidss_encoder(bridge);
	struct tidss_device *tidss = t_enc->tidss;
	struct tidss_crtc_state *tcrtc_state = to_tidss_crtc_state(crtc_state);
	struct drm_display_info *di = &conn_state->connector->display_info;
	struct drm_bridge_state *next_bridge_state = NULL;

	if (t_enc->next_bridge)
		next_bridge_state = drm_atomic_get_new_bridge_state(crtc_state->state,
								    t_enc->next_bridge);

	if (next_bridge_state) {
		tcrtc_state->bus_flags = next_bridge_state->input_bus_cfg.flags;
		tcrtc_state->bus_format = next_bridge_state->input_bus_cfg.format;
	} else if (di->num_bus_formats) {
		tcrtc_state->bus_format = di->bus_formats[0];
		tcrtc_state->bus_flags = di->bus_flags;
	} else {
		dev_err(tidss->dev, "%s: No bus_formats in connected display\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}

static void tidss_bridge_atomic_enable(struct drm_bridge *bridge,
                                       struct drm_bridge_state *state)
{
	struct tidss_encoder *t_enc = bridge_to_tidss_encoder(bridge);
	struct tidss_device *tidss = t_enc->tidss;
	struct drm_crtc *crtc = bridge->encoder->crtc;
	struct tidss_crtc *tcrtc = to_tidss_crtc(crtc);
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	unsigned long flags;
	int r;

	dev_dbg(tidss->dev, "%s\n", __func__);

	r = dispc_vp_set_clk_rate(tidss->dispc, tcrtc->hw_videoport,
				  mode->clock * 1000);
	if (r != 0)
		return;

	r = dispc_vp_enable_clk(tidss->dispc, tcrtc->hw_videoport);
	if (r != 0)
		return;

	dispc_vp_enable_final(tidss->dispc, tcrtc->hw_videoport);

	spin_lock_irqsave(&tidss->ddev.event_lock, flags);

	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}

       spin_unlock_irqrestore(&tidss->ddev.event_lock, flags);
}

static void tidss_bridge_atomic_disable(struct drm_bridge *bridge,
					struct drm_bridge_state *state)
{
	struct tidss_encoder *t_enc = bridge_to_tidss_encoder(bridge);
	struct tidss_device *tidss = t_enc->tidss;
	struct drm_crtc *crtc = bridge->encoder->crtc;
	struct tidss_crtc *tcrtc = to_tidss_crtc(crtc);

	dev_dbg(tidss->dev, "%s\n", __func__);

	reinit_completion(&tcrtc->framedone_completion);

	dispc_vp_disable(tidss->dispc, tcrtc->hw_videoport);

	if (!wait_for_completion_timeout(&tcrtc->framedone_completion,
					 msecs_to_jiffies(500)))
		dev_err(tidss->dev, "Timeout waiting for framedone on crtc %d",
			tcrtc->hw_videoport);
}

static const struct drm_bridge_funcs tidss_bridge_funcs = {
	.attach				= tidss_bridge_attach,
	.atomic_check			= tidss_bridge_atomic_check,
	.atomic_enable			= tidss_bridge_atomic_enable,
	.atomic_disable			= tidss_bridge_atomic_disable,
	.atomic_reset			= drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state		= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state		= drm_atomic_helper_bridge_destroy_state,
};

int tidss_encoder_create(struct tidss_device *tidss,
			 struct drm_bridge *next_bridge,
			 u32 encoder_type, u32 possible_crtcs)
{
	struct tidss_encoder *t_enc;
	struct drm_encoder *enc;
	struct drm_connector *connector;
	int ret;

	t_enc = drmm_simple_encoder_alloc(&tidss->ddev, struct tidss_encoder,
					  encoder, encoder_type);
	if (IS_ERR(t_enc))
		return PTR_ERR(t_enc);

	t_enc->tidss = tidss;
	t_enc->next_bridge = next_bridge;
	t_enc->bridge.funcs = &tidss_bridge_funcs;

	enc = &t_enc->encoder;
	enc->possible_crtcs = possible_crtcs;

	/* Attaching first bridge to the encoder */
	ret = drm_bridge_attach(enc, &t_enc->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(tidss->dev, "bridge attach failed: %d\n", ret);
		return ret;
	}

	/* Initializing the connector at the end of bridge-chain */
	connector = drm_bridge_connector_init(&tidss->ddev, enc);
	if (IS_ERR(connector)) {
		dev_err(tidss->dev, "bridge_connector create failed\n");
		return PTR_ERR(connector);
	}

	ret = drm_connector_attach_encoder(connector, enc);
	if (ret) {
		dev_err(tidss->dev, "attaching encoder to connector failed\n");
		return ret;
	}

	t_enc->connector = connector;

	dev_dbg(tidss->dev, "Encoder create done\n");

	return ret;
}
