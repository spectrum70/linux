// SPDX-License-Identifier: GPL-2.0
/*
 * Simple driver for mcPWM (Motor Control Pulse Width Modulation)
 * controller, available on ColdFire SoCs mcf54415/6/7/8.
 *
 *
 *
 *
 * Copyright (C) 2026 BayLibre, SAS.
 */

#define DEBUG 1

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define MCF_PWM_SUBMODS			0x04

#define MCF_PWM_SUB_OFFS(n)		(n * 0x50)
#define MCF_PWM_SM_CNT(n)		(0x00 + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_INIT(n)		(0x02 + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_CR2(n)		(0x04 + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_CR1(n)		(0x06 + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_VAL0(n)		(0x08 + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_VAL1(n)		(0x0A + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_VAL2(n)		(0x0C + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_VAL3(n)		(0x0E + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_VAL4(n)		(0x10 + MCF_PWM_SUB_OFFS(n))
#define MCF_PWM_SM_VAL5(n)		(0x12 + MCF_PWM_SUB_OFFS(n))

#define MCF_PWM_OUTEN			0x140
#define MCF_PWM_OUTEN_PWMX_EN		GENMASK(3, 0)
#define MCF_PWM_MCR			0x148
#define MCF_PWM_MCR_RUN			GENMASK(11, 8)

struct pwm_mcf_chan {
};

struct pwm_mcf {
	struct regmap *map;
	struct clk *clk;
	struct pwm_mcf_chan ch[MCF_PWM_SUBMODS];
};

static const struct regmap_config pwm_mcf_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = MCFPWM_SIZE, /* R.M. Table 34-4, PWM Memory Map */
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
};

static inline struct pwm_mcf *to_pwm_mcf(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int pwm_mcf_config(struct pwm_chip *chip,
			  struct pwm_device *pwm, u64 duty_ns, u64 period_ns)
{
	struct pwm_mcf *priv = to_pwm_mcf(chip);
	unsigned long rate;
	u64 ns_min, ns_max;
	int val, ch, ret;
	

	/* 
	 * The MCF PWM module allows to set up and down edges separately,
	 * for each submodule (channel) and for each output line (A and B).
	 * Each channel has its own timer, registers SMnVAL2 and 3 are used
	 * to define up and down edges of output A, SMnVAL4 and 5 are used
	 * to define up and down edges of output B.
	 *
	 * Timer:     
	 *
	 *  VAL1 0x0100             /.            /.            /.
	 *    VAL3                /. .          /  .          /  .
	 *    VAL5              /. . .        /    .        /    .
	 *  VAL0 0x0000       /  . . .      /      .      /      .
	 *    VAL4          /    . . .    /        .    /        .    /
	 *    VAL2        / .    . . .  /          .  /          .  /
	 *  INIT 0xff00 / . .    . . ./            ./            ./
	 *                .________.    .________.    .________,    .__
         *  A           __| .____. |____| .____. |____| .____. |____|
	 *  B           ____|   |_________|   |_________|    |________|
	 *
	 */
	 
	ch = pwm->hwpwm;

	/* Enable outputs. */
	val = FIELD_PREP(MCF_PWM_OUTEN_PWMX_EN, BIT(ch));
	ret = regmap_update_bits(priv->map, MCF_PWM_OUTEN,
				 MCF_PWM_OUTEN_PWMX_EN, val);
	if (ret)
		return ret;

	/* 
	 * What we can do in nsecs:
	 * - each value is in PWM clock cicles
	 * - setting init as 0, max is 65535
	 * so, min is 1 / clk, max is min * 65535 
	 */

	rate = clk_get_rate(priv->clk);
	if (rate == 0)
		return -EINVAL;

	ns_min = div_u64(1000000000ULL, rate);
	ns_max = ns_min * 65535UL; 

	/* Set timer limits, -0x100 to 0x100. */
	ret = regmap_write(priv->map, MCF_PWM_SM_INIT(ch), 0xff00);
	if (ret)
		return ret;
	ret = regmap_write(priv->map, MCF_PWM_SM_VAL0(ch), 0x0);
	if (ret)
		return ret;
	ret = regmap_write(priv->map, MCF_PWM_SM_VAL1(ch), 0x0100);
	if (ret)
		return ret;





	//max = readl(imx->mmio_base + MX1_PWMP);
	//p = mul_u64_u64_div_u64(max, duty_ns, period_ns);

	//writel(max - p, imx->mmio_base + MX1_PWMS);

	return 0;
}

static int pwm_mcf_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_mcf *priv = to_pwm_mcf(chip);
	unsigned int val;
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret < 0)
		return ret;

	/* RUN specific submodule. */
	val = FIELD_PREP(MCF_PWM_OUTEN_PWMX_EN, BIT(pwm->hwpwm));
	return regmap_update_bits(priv->map, MCF_PWM_MCR, MCF_PWM_MCR_RUN, val);
}

static int pwm_mcf_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_mcf *priv = to_pwm_mcf(chip);
	unsigned int val;
	int ret;

	/* STOP specific submodule. */
	val = FIELD_PREP(MCF_PWM_MCR_RUN, BIT(pwm->hwpwm));
	ret = regmap_update_bits(priv->map, MCF_PWM_MCR, MCF_PWM_MCR_RUN, ~val);
	if (ret)
		return ret;

	clk_disable_unprepare(priv->clk);

	dev_dbg(pwmchip_parent(chip), "%s() disabled ok\n", __func__);

	return 0;
}

static int pwm_mcf_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			const struct pwm_state *state)
{
	int ret;

	dev_dbg(pwmchip_parent(chip), "%s() apply, ch %d\n", __func__,
			       pwm->hwpwm);

	if (!state->enabled && pwm->state.enabled)
		return pwm_mcf_disable(chip, pwm);

	ret = pwm_mcf_config(chip, pwm, state->duty_cycle, state->period);
	if (ret)
		return ret;

	if (!pwm->state.enabled)
		return pwm_mcf_enable(chip, pwm);

	return 0;
}

static int pwm_mcf_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct pwm_mcf *priv = to_pwm_mcf(chip);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->map, MCF_PWM_MCR, &val);
	if (ret)
		return ret;

	state->enabled = FIELD_GET(MCF_PWM_MCR_RUN, val) & BIT(pwm->hwpwm);
	
	/* TO DO */
	state->period = 0;
	state->duty_cycle = 0;

	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops pwm_mcf_ops = {
	.apply = pwm_mcf_apply,
	.get_state = pwm_mcf_get_state,
};

static int pwm_mcf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct pwm_mcf *priv;
	void __iomem *regs;
	int ret;

	dev_dbg(dev, "%s() entered\n", __func__);

	chip = devm_pwmchip_alloc(dev, MCF_PWM_SUBMODS, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = to_pwm_mcf(chip);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "failed to get io regs\n");

	priv->map = devm_regmap_init_mmio(dev, regs, &pwm_mcf_regmap_config);
	if (IS_ERR(priv->map))
		return PTR_ERR(priv->map);

	priv->clk = devm_clk_get_enabled(dev, "pwmmcf");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "failed getting clock\n");

	chip->npwm = MCF_PWM_SUBMODS;
	chip->ops = &pwm_mcf_ops;

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
                return dev_err_probe(dev, ret,
                                     "Failed to register pwmchip\n");

        platform_set_drvdata(pdev, chip);

	dev_dbg(dev, "%s() mcPWM enabled\n", __func__);

	return 0;
}

static const struct platform_device_id pwm_mcf_ids[] = {
	{ .name = "mcfpwm" },
	{ }
};

static struct platform_driver pwm_mcf_driver = {
	.driver = {
		.name = "pwm-mcf",
	},
	.probe = pwm_mcf_probe,
	.id_table = pwm_mcf_ids,
};
module_platform_driver(pwm_mcf_driver);

MODULE_DESCRIPTION("mcf54415 Motor Control Pulse Width Modulation driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Angelo Dureghello <adureghello@baylibre.com>");
