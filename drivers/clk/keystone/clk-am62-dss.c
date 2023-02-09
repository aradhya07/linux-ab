// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 */
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

struct ti_am62_dss_clk {
	struct clk_hw	hw;
	unsigned int	div;
};

#define to_ti_am62_dss_clk(_hw) \
	container_of(_hw, struct ti_am62_dss_clk, hw)

static unsigned long ti_am62_dss_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct ti_am62_dss_clk *priv = to_ti_am62_dss_clk(hw);
	unsigned long rate;

	rate = parent_rate;
	do_div(rate, priv->div);
	return (unsigned long)rate;
}

static long ti_am62_dss_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *prate)
{
	struct ti_am62_dss_clk *priv = to_ti_am62_dss_clk(hw);

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		unsigned long best_parent;

		best_parent = rate * priv->div;
		*prate = clk_hw_round_rate(clk_hw_get_parent(hw), best_parent);
	}

	return (*prate / priv->div);
}

static int ti_am62_dss_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * ti_am62_dss_clk_round_rate returns values that ensure this call is a
	 * nop.
	 */

	return 0;
}

static const struct clk_ops ti_am62_dss_clk_ops = {
	.round_rate = ti_am62_dss_clk_round_rate,
	.set_rate = ti_am62_dss_clk_set_rate,
	.recalc_rate = ti_am62_dss_clk_recalc_rate,
};

static struct clk_hw *
clk_hw_register_am62_dss_clk(struct device *dev, const char *name,
			     unsigned long flags, unsigned int div)
{
	struct ti_am62_dss_clk *priv;
	struct clk_init_data init = { };
	struct clk_parent_data pdata = { .index = 0 };
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	/* struct ti_am62_dss_clk assignments */
	priv->div = div;
	priv->hw.init = &init;

	init.name = name;
	init.ops = &ti_am62_dss_clk_ops;
	init.flags = flags;
	init.parent_names = NULL;
	init.parent_data = &pdata;
	init.num_parents = 1;

	ret = devm_clk_hw_register(dev, &priv->hw);
	if (ret)
		return ERR_PTR(ret);

	return &priv->hw;
}

static void clk_hw_unregister_am62_dss_clk(struct clk_hw *hw)
{
	struct ti_am62_dss_clk *priv;

	priv = to_ti_am62_dss_clk(hw);

	clk_hw_unregister(hw);
	kfree(priv);
}

static int ti_am62_dss_clk_remove(struct platform_device *pdev)
{
	struct clk_hw *hw = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);
	clk_hw_unregister_am62_dss_clk(hw);

	return 0;
}

static int ti_am62_dss_clk_probe(struct platform_device *pdev)
{
	struct clk_hw *hw;
	const char *clk_name = pdev->name;
	struct device *dev = &pdev->dev;
	unsigned long flags = 0;
	u32 div;
	int ret;

	if (of_property_read_u32(dev->of_node, "clock-div", &div)) {
		dev_err(dev, "%s: TI AM62 DSS clock must have a clock-div property.\n",
			__func__);
		return -EIO;
	}

	flags |= CLK_SET_RATE_PARENT;

	hw = clk_hw_register_am62_dss_clk(dev, clk_name, flags, div);
	if (IS_ERR(hw)) {
		dev_err(dev, "%s: failed to register %s\n", __func__, clk_name);
		return PTR_ERR(hw);
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
	if (ret) {
		clk_hw_unregister_am62_dss_clk(hw);
		return ret;
	}

	platform_set_drvdata(pdev, hw);

	return 0;
}

static const struct of_device_id ti_am62_dss_clk_ids[] = {
	{ .compatible = "ti,am62-dss-vp0-div-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_am62_dss_clk_ids);

static struct platform_driver ti_am62_dss_clk_driver = {
	.driver = {
		.name = "ti_am62_dss_clk",
		.of_match_table = ti_am62_dss_clk_ids,
	},
	.probe = ti_am62_dss_clk_probe,
	.remove = ti_am62_dss_clk_remove,
};
module_platform_driver(ti_am62_dss_clk_driver);

MODULE_AUTHOR("Aradhya Bhatia <a-bhatia1@ti.com>");
MODULE_DESCRIPTION("TI AM62 DSS - OLDI Fixed Clock Divider driver");
MODULE_LICENSE("GPL");
