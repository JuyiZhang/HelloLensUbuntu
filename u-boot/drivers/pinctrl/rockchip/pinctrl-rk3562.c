// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2022 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <dm/pinctrl.h>
#include <regmap.h>
#include <syscon.h>

#include "pinctrl-rockchip.h"

static int rk3562_set_mux(struct rockchip_pin_bank *bank, int pin, int mux)
{
	struct rockchip_pinctrl_priv *priv = bank->priv;
	int iomux_num = (pin / 8);
	struct regmap *regmap;
	int reg, ret, mask;
	u8 bit;
	u32 data;

	debug("setting mux of GPIO%d-%d to %d\n", bank->bank_num, pin, mux);

	regmap = priv->regmap_base;
	reg = bank->iomux[iomux_num].offset;
	if ((pin % 8) >= 4)
		reg += 0x4;
	bit = (pin % 4) * 4;
	mask = 0xf;

	data = (mask << (bit + 16));
	data |= (mux & mask) << bit;

	/* force jtag m1 */
	if (bank->bank_num == 1) {
		if ((pin == 13) || (pin == 14)) {
			if (mux == 1) {
				regmap_write(regmap, 0x504, 0x10001);
			} else {
				regmap_write(regmap, 0x504, 0x10000);
			}
		}
	}

	debug("iomux write reg = %x data = %x\n", reg, data);

	ret = regmap_write(regmap, reg, data);

	return ret;
}

#define RK3562_DRV_BITS_PER_PIN		8
#define RK3562_DRV_PINS_PER_REG		2
#define RK3562_DRV_GPIO0_OFFSET		0x20070
#define RK3562_DRV_GPIO1_OFFSET		0x200
#define RK3562_DRV_GPIO2_OFFSET		0x240
#define RK3562_DRV_GPIO3_OFFSET		0x10280
#define RK3562_DRV_GPIO4_OFFSET		0x102C0

static void rk3562_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
					int pin_num, struct regmap **regmap,
					int *reg, u8 *bit)
{
	struct rockchip_pinctrl_priv *priv = bank->priv;

	*regmap = priv->regmap_base;
	switch (bank->bank_num) {
	case 0:
		*reg = RK3562_DRV_GPIO0_OFFSET;
		break;

	case 1:
		*reg = RK3562_DRV_GPIO1_OFFSET;
		break;

	case 2:
		*reg = RK3562_DRV_GPIO2_OFFSET;
		break;

	case 3:
		*reg = RK3562_DRV_GPIO3_OFFSET;
		break;

	case 4:
		*reg = RK3562_DRV_GPIO4_OFFSET;
		break;

	default:
		*reg = 0;
		dev_err(priv->dev, "unsupported bank_num %d\n", bank->bank_num);
		break;
	}

	*reg += ((pin_num / RK3562_DRV_PINS_PER_REG) * 4);
	*bit = pin_num % RK3562_DRV_PINS_PER_REG;
	*bit *= RK3562_DRV_BITS_PER_PIN;
}

static int rk3562_set_drive(struct rockchip_pin_bank *bank,
			    int pin_num, int strength)
{
	struct regmap *regmap;
	int reg, ret;
	u32 data;
	u8 bit;
	int drv = (1 << (strength + 1)) - 1;

	rk3562_calc_drv_reg_and_bit(bank, pin_num, &regmap, &reg, &bit);

	/* enable the write to the equivalent lower bits */
	data = ((1 << RK3562_DRV_BITS_PER_PIN) - 1) << (bit + 16);
	data |= (drv << bit);
	ret = regmap_write(regmap, reg, data);

	return ret;
}

#define RK3562_PULL_BITS_PER_PIN		2
#define RK3562_PULL_PINS_PER_REG		8
#define RK3562_PULL_GPIO0_OFFSET		0x20020
#define RK3562_PULL_GPIO1_OFFSET		0x80
#define RK3562_PULL_GPIO2_OFFSET		0x90
#define RK3562_PULL_GPIO3_OFFSET		0x100A0
#define RK3562_PULL_GPIO4_OFFSET		0x100B0

static void rk3562_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl_priv *priv = bank->priv;

	*regmap = priv->regmap_base;
	switch (bank->bank_num) {
	case 0:
		*reg = RK3562_PULL_GPIO0_OFFSET;
		break;

	case 1:
		*reg = RK3562_PULL_GPIO1_OFFSET;
		break;

	case 2:
		*reg = RK3562_PULL_GPIO2_OFFSET;
		break;

	case 3:
		*reg = RK3562_PULL_GPIO3_OFFSET;
		break;

	case 4:
		*reg = RK3562_PULL_GPIO4_OFFSET;
		break;

	default:
		*reg = 0;
		dev_err(priv->dev, "unsupported bank_num %d\n", bank->bank_num);
		break;
	}

	*reg += ((pin_num / RK3562_PULL_PINS_PER_REG) * 4);
	*bit = pin_num % RK3562_PULL_PINS_PER_REG;
	*bit *= RK3562_PULL_BITS_PER_PIN;
}

static int rk3562_set_pull(struct rockchip_pin_bank *bank,
			   int pin_num, int pull)
{
	struct regmap *regmap;
	int reg, ret;
	u8 bit, type;
	u32 data;

	if (pull == PIN_CONFIG_BIAS_PULL_PIN_DEFAULT)
		return -ENOTSUPP;

	rk3562_calc_pull_reg_and_bit(bank, pin_num, &regmap, &reg, &bit);
	type = bank->pull_type[pin_num / 8];
	ret = rockchip_translate_pull_value(type, pull);
	if (ret < 0) {
		debug("unsupported pull setting %d\n", pull);
		return ret;
	}

	/* enable the write to the equivalent lower bits */
	data = ((1 << RK3562_PULL_BITS_PER_PIN) - 1) << (bit + 16);

	data |= (ret << bit);
	ret = regmap_write(regmap, reg, data);

	return ret;
}

#define RK3562_SMT_BITS_PER_PIN		2
#define RK3562_SMT_PINS_PER_REG		8
#define RK3562_SMT_GPIO0_OFFSET		0x20030
#define RK3562_SMT_GPIO1_OFFSET		0xC0
#define RK3562_SMT_GPIO2_OFFSET		0xD0
#define RK3562_SMT_GPIO3_OFFSET		0x100E0
#define RK3562_SMT_GPIO4_OFFSET		0x100F0

static int rk3562_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl_priv *priv = bank->priv;

	*regmap = priv->regmap_base;
	switch (bank->bank_num) {
	case 0:
		*reg = RK3562_SMT_GPIO0_OFFSET;
		break;

	case 1:
		*reg = RK3562_SMT_GPIO1_OFFSET;
		break;

	case 2:
		*reg = RK3562_SMT_GPIO2_OFFSET;
		break;

	case 3:
		*reg = RK3562_SMT_GPIO3_OFFSET;
		break;

	case 4:
		*reg = RK3562_SMT_GPIO4_OFFSET;
		break;

	default:
		*reg = 0;
		dev_err(priv->dev, "unsupported bank_num %d\n", bank->bank_num);
		break;
	}

	*reg += ((pin_num / RK3562_SMT_PINS_PER_REG) * 4);
	*bit = pin_num % RK3562_SMT_PINS_PER_REG;
	*bit *= RK3562_SMT_BITS_PER_PIN;

	return 0;
}

static int rk3562_set_schmitt(struct rockchip_pin_bank *bank,
			      int pin_num, int enable)
{
	struct regmap *regmap;
	int reg, ret;
	u32 data;
	u8 bit;

	rk3562_calc_schmitt_reg_and_bit(bank, pin_num, &regmap, &reg, &bit);

	/* enable the write to the equivalent lower bits */
	data = ((1 << RK3562_SMT_BITS_PER_PIN) - 1) << (bit + 16);
	data |= (enable << bit);
	ret = regmap_write(regmap, reg, data);

	return ret;
}

static struct rockchip_pin_bank rk3562_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS_OFFSET(0, 32, "gpio0",
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    0x20000, 0x20008, 0x20010, 0x20018),
	PIN_BANK_IOMUX_FLAGS_OFFSET(1, 32, "gpio1",
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    0, 0x08, 0x10, 0x18),
	PIN_BANK_IOMUX_FLAGS_OFFSET(2, 32, "gpio2",
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    0x20, 0, 0, 0),
	PIN_BANK_IOMUX_FLAGS_OFFSET(3, 32, "gpio3",
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    0x10040, 0x10048, 0x10050, 0x10058),
	PIN_BANK_IOMUX_FLAGS_OFFSET(4, 16, "gpio4",
				    IOMUX_WIDTH_4BIT,
				    IOMUX_WIDTH_4BIT,
				    0,
				    0,
				    0x10060, 0x10068, 0, 0),
};

static const struct rockchip_pin_ctrl rk3562_pin_ctrl = {
	.pin_banks		= rk3562_pin_banks,
	.nr_banks		= ARRAY_SIZE(rk3562_pin_banks),
	.nr_pins		= 144,
	.grf_mux_offset		= 0x0,
	.set_mux		= rk3562_set_mux,
	.set_pull		= rk3562_set_pull,
	.set_drive		= rk3562_set_drive,
	.set_schmitt		= rk3562_set_schmitt,
};

static const struct udevice_id rk3562_pinctrl_ids[] = {
	{
		.compatible = "rockchip,rk3562-pinctrl",
		.data = (ulong)&rk3562_pin_ctrl
	},
	{ }
};

U_BOOT_DRIVER(pinctrl_rk3562) = {
	.name		= "rockchip_rk3562_pinctrl",
	.id		= UCLASS_PINCTRL,
	.of_match	= rk3562_pinctrl_ids,
	.priv_auto_alloc_size = sizeof(struct rockchip_pinctrl_priv),
	.ops		= &rockchip_pinctrl_ops,
#if !CONFIG_IS_ENABLED(OF_PLATDATA)
	.bind		= dm_scan_fdt_dev,
#endif
	.probe		= rockchip_pinctrl_probe,
};
