/*
 * FILE NAME gpio-rtk-mips.c
 *
 * BRIEF MODULE DESCRIPTION
 *  Driver for Realtek GPIO controller.
 *
 * Copyright (C) 2018 Realtek, Inc.
 *
 * Based on gpio-ca77xx.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>

#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <rtk/gpio.h>

/* Use the rtl86900 switch SDK GPIO HAL (rtk_gpio_*), matching vendor default */
#define CONFIG_LUNA_SOC_GPIO

/* GPIO Register Map */
/*
#define RTK_GPIO_CFG		0x00
#define RTK_GPIO_OUT		0x04
#define RTK_GPIO_IN		0x08
#define RTK_GPIO_LVL		0x0C
#define RTK_GPIO_EDGE	0x10
#define RTK_GPIO_BOTHEDGE	0x14
#define RTK_GPIO_IE		0x18
#define RTK_GPIO_INT		0x1C
#define RTK_GPIO_STAT	0x20
*/
#define RTK_GPIO_DIR        0x00
#define RTK_GPIO_DAT        0x04
#define RTK_GPIO_ISR        0x08
#define RTK_GPIO_IMR1       0x0C //used for AB, EF, JK
#define RTK_GPIO_IMR2       0x10 //used for CD, GH, MN

/* rtk_mips_ia_GPIO_CFG BIT */
//#define RTK_GPIO_CFG_OUT	0
//#define RTK_GPIO_CFG_IN	1
#define RTK_GPIO_CFG_OUT	1
#define RTK_GPIO_CFG_IN		0

#define GPIO_MAX_BANK_NUM	3
#define GPIO_BANK_SIZE		32
#define GPIO_BANK_REG_OFFSET	0x1C
#define GPIO_ENABLE_REG_OFFSET	0x4

/**
 * struct rtk_gpio_bank - GPIO bank private data
 * @chip:	Generic GPIO chip for GPIO bank
 * @domain:	IRQ domain for GPIO bank (may be NULL)
 * @reg:	Base of registers, offset for this GPIO bank
 * @irq:	IRQ number for GPIO bank
 * @label:	Debug GPIO bank label, used for storage of chip->label
 *
 * This is the main private data for a GPIO bank. It encapsulates a gpio_chip,
 * and the callbacks for the gpio_chip can access the private data with the
 * to_bank() macro below.
 */
struct rtk_gpio_bank {
	struct gpio_chip chip;
	void __iomem *reg;
	void __iomem *enable_reg;
#ifndef CONFIG_PINCTRL
	void __iomem *mux_reg;
#endif
	int irq;
	u32 imr;	/* for isr handler */
	u8 trig_type[32];
	char label[16];
	struct irq_domain *domain;
	spinlock_t lock; /* lock access to bank regs */
};

#define to_bank(c)	container_of(c, struct rtk_gpio_bank, chip)

/**
 * struct rtk_gpio_bank_info - Temporary registration info for GPIO bank
 * @priv:	Overall GPIO device private data
 * @node:	Device tree node specific to this GPIO bank
 * @index:	Index of bank in range 0 - (GPIO_MAX_BANK_NUM - 1)
 */
struct rtk_gpio_bank_info {
	struct rtk_gpio *priv;
	struct device_node *node;
	unsigned int index;
	struct rtk_gpio_bank *bank;
};

/**
 * struct rtk_gpio - Overall GPIO device private data
 * @dev:	Device (from platform device)
 * @reg:	Base of GPIO registers
 *
 * Represents the overall GPIO device. This structure is actually only
 * temporary, and used during init.
 */
struct rtk_gpio {
	struct device *dev;
	void __iomem *reg;
	void __iomem *enable_reg;
#ifndef CONFIG_PINCTRL
	void __iomem *mux_reg;
#endif
	struct rtk_gpio_bank_info *info[GPIO_MAX_BANK_NUM];
};

/* Convenience register accessors */
static inline void rtk_gpio_write(struct rtk_gpio_bank *bank,
				     unsigned int reg_offs, u32 data)
{
	iowrite32(data, bank->reg + reg_offs);
}

static inline u32 rtk_gpio_read(struct rtk_gpio_bank *bank,
				   unsigned int reg_offs)
{
	return ioread32(bank->reg + reg_offs);
}

static inline void _rtk_gpio_clear_bit(struct rtk_gpio_bank *bank,
					  unsigned int reg_offs,
					  unsigned int offset)
{
	u32 value;

	value = rtk_gpio_read(bank, reg_offs);
	value &= ~BIT(offset);
	rtk_gpio_write(bank, reg_offs, value);
}

static inline void _rtk_gpio_set_bit(struct rtk_gpio_bank *bank,
					unsigned int reg_offs,
					unsigned int offset)
{
	u32 value;

	value = rtk_gpio_read(bank, reg_offs);
	value |= BIT(offset);
	rtk_gpio_write(bank, reg_offs, value);
}

static inline u32 _rtk_gpio_get_bit(struct rtk_gpio_bank *bank,
				       unsigned int reg_offs,
				       unsigned int offset)
{
	u32 value;

	value =	rtk_gpio_read(bank, reg_offs) & BIT(offset);

	return value ? 1 :  0;
}

static int rtk_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rtk_gpio_bank *bank = to_bank(chip);
	unsigned long flags;

#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;

	spin_lock_irqsave(&bank->lock, flags);

	ret = rtk_gpio_state_set(chip->base + offset, 1);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);

	spin_unlock_irqrestore(&bank->lock, flags);

#else
	u32 enable_value;

	enable_value = ioread32(bank->enable_reg);
	enable_value |= BIT(offset);
	iowrite32(enable_value, bank->enable_reg);

	spin_lock_irqsave(&bank->lock, flags);

	_rtk_gpio_set_bit(bank, RTK_GPIO_DAT, offset);

	spin_unlock_irqrestore(&bank->lock, flags);
#endif

	return 0;
}

static void rtk_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct rtk_gpio_bank *bank = to_bank(chip);
	unsigned long flags;
#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;

	spin_lock_irqsave(&bank->lock, flags);

	ret =  rtk_gpio_state_set(chip->base + offset, 0);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);

	spin_unlock_irqrestore(&bank->lock, flags);
#else	
	u32 enable_value;

	spin_lock_irqsave(&bank->lock, flags);

	_rtk_gpio_set_bit(bank, RTK_GPIO_DAT, offset);

	spin_unlock_irqrestore(&bank->lock, flags);

	enable_value = ioread32(bank->enable_reg);
	enable_value &= ~BIT(offset);
	iowrite32(enable_value, bank->enable_reg);
#endif
}

static int rtk_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct rtk_gpio_bank *bank __maybe_unused = to_bank(chip);
	u32 val;
#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;

	ret = rtk_gpio_mode_get(chip->base + offset, &val);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
#else
	val = _rtk_gpio_get_bit(bank, RTK_GPIO_DIR, offset);
#endif

	return (val == RTK_GPIO_CFG_IN) ? GPIO_LINE_DIRECTION_IN : GPIO_LINE_DIRECTION_OUT;
}

static int rtk_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct rtk_gpio_bank *bank = to_bank(chip);
	unsigned long flags;
#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;
#endif

	spin_lock_irqsave(&bank->lock, flags);

#ifdef CONFIG_LUNA_SOC_GPIO
	ret = rtk_gpio_state_set(chip->base + offset, 1); /* enable gpio */
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);

	ret = rtk_gpio_mode_set(chip->base + offset, GPIO_INPUT);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
#else
	_rtk_gpio_clear_bit(bank, RTK_GPIO_DIR, offset);
#endif

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int rtk_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int output_value)
{
	struct rtk_gpio_bank *bank = to_bank(chip);
	unsigned long flags;
#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;
#endif

	spin_lock_irqsave(&bank->lock, flags);

#ifdef CONFIG_LUNA_SOC_GPIO
	ret = rtk_gpio_state_set(chip->base + offset, 1); /* enable gpio */
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
	ret = rtk_gpio_mode_set(chip->base + offset, GPIO_OUTPUT);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
	ret = rtk_gpio_databit_set(chip->base + offset, output_value);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
#else
	if (output_value)
		_rtk_gpio_set_bit(bank, RTK_GPIO_DAT, offset);
	else
		_rtk_gpio_clear_bit(bank, RTK_GPIO_DAT, offset);
	_rtk_gpio_set_bit(bank, RTK_GPIO_DIR, offset); /* change dir last */
#endif

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

/*
 * Return GPIO level
 */
static int rtk_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rtk_gpio_bank *bank __maybe_unused = to_bank(chip);
	u32 value;
#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;

	ret = rtk_gpio_databit_get(chip->base + offset, &value);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
#else
	value = _rtk_gpio_get_bit(bank, RTK_GPIO_DAT, offset);
#endif

	return value;
}

/*
 * Set output GPIO level
 */
static int rtk_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int output_value)
{
	struct rtk_gpio_bank *bank = to_bank(chip);
	unsigned long flags;
#ifdef CONFIG_LUNA_SOC_GPIO
	int ret;
#endif

	/* this function only applies to output pin */
	if (rtk_gpio_get_direction(chip, offset) == GPIO_LINE_DIRECTION_IN)
		return 0;

	spin_lock_irqsave(&bank->lock, flags);

#ifdef CONFIG_LUNA_SOC_GPIO
	ret = rtk_gpio_databit_set(chip->base + offset, output_value);
	if (ret)
		pr_err("%s: failed errno(%d)\n", __func__, ret);
#else
	if (output_value)
		_rtk_gpio_set_bit(bank, RTK_GPIO_DAT, offset);
	else
		_rtk_gpio_clear_bit(bank, RTK_GPIO_DAT, offset);
#endif

	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

/* IRQ chip handlers */

/* Get rtk_mips_ia GPIO chip from irq data provided to generic IRQ callbacks */
static inline struct rtk_gpio_bank *irqd_to_gpio_bank(struct irq_data *data)
{
	return (struct rtk_gpio_bank *)data->domain->host_data;
}

#ifndef CONFIG_LUNA_SOC_GPIO
static void __rtk_gpio_imr_set(struct rtk_gpio_bank *bank, int hwirq, int data) {
	int offset;
	u32 val, shift;
	if (hwirq>16) {
		offset = RTK_GPIO_IMR2;
	} else {
		offset = RTK_GPIO_IMR1;
	}
	shift = (hwirq&0xf) * 2;

	val = rtk_gpio_read(bank, offset) & ~(0x3 << shift);
	rtk_gpio_write(bank, offset, val | (data << shift));
}
#endif

static int rtk_gpio_set_irq_type(struct irq_data *data, unsigned int type)
{
	struct rtk_gpio_bank *bank = irqd_to_gpio_bank(data);
	unsigned int trig_type;

	switch (type) {
#ifdef CONFIG_LUNA_SOC_GPIO
	case IRQ_TYPE_NONE:
		trig_type = 0;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trig_type = 1;
		break;
	case IRQ_TYPE_EDGE_RISING:
		trig_type = 2;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trig_type = 3;
		break;
#else
	case IRQ_TYPE_NONE:
		trig_type = GPIO_INTR_DISABLE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trig_type = GPIO_INTR_ENABLE_FALLING_EDGE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		trig_type = GPIO_INTR_ENABLE_RISING_EDGE;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trig_type = GPIO_INTR_ENABLE_BOTH_EDGE;
		break;
#endif
	default:
		printk("Unsupported irq type %d\n", type);
		return -EINVAL;
	}

	bank->trig_type[data->hwirq] = trig_type;

	return 0;
}

static void rtk_gpio_ack_irq(struct irq_data *d) {
	struct rtk_gpio_bank *bank = irqd_to_gpio_bank(d);
	rtk_gpio_write(bank, RTK_GPIO_ISR, (1<<d->hwirq));
}

static void rtk_gpio_mask_irq(struct irq_data *d) {
	unsigned long flags;
	struct rtk_gpio_bank *bank = irqd_to_gpio_bank(d);

	spin_lock_irqsave(&bank->lock, flags);
#ifdef CONFIG_LUNA_SOC_GPIO
	rtk_gpio_intr_set(bank->chip.base+d->hwirq, GPIO_INTR_DISABLE);
#else
	__rtk_gpio_imr_set(bank, d->hwirq, 0);
#endif
	bank->imr &= ~(1 << d->hwirq);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void rtk_gpio_unmask_irq(struct irq_data *d) {
	unsigned long flags;
	struct rtk_gpio_bank *bank = irqd_to_gpio_bank(d);

	spin_lock_irqsave(&bank->lock, flags);
#ifdef CONFIG_LUNA_SOC_GPIO
	rtk_gpio_intr_set(bank->chip.base+d->hwirq, bank->trig_type[d->hwirq]);
#else
	__rtk_gpio_imr_set(bank, d->hwirq, bank->trig_type[d->hwirq]);
#endif
	bank->imr |= (1 << d->hwirq);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static unsigned int rtk_gpio_startup_irq(struct irq_data *data)
{
	/*
	 * This warning indicates that the type of the irq hasn't been set
	 * before enabling the irq. This would normally be done by passing some
	 * trigger flags to request_irq().
	 */
	struct rtk_gpio_bank *bank = irqd_to_gpio_bank(data);

	WARN(irqd_get_trigger_type(data) == IRQ_TYPE_NONE,
	     "irq type not set before enabling gpio irq %d", data->irq);

	rtk_gpio_direction_input(&bank->chip, data->hwirq);
	rtk_gpio_unmask_irq(data);

	return 0;
}

#ifdef CONFIG_SUSPEND
static int rtk_gpio_set_irq_wake(struct irq_data *data, unsigned int on)
{
	struct rtk_gpio_bank *bank = irqd_to_gpio_bank(data);

#ifdef CONFIG_PM_DEBUG
	pr_info("irq_wake irq%d state:%d\n", data->irq, on);
#endif

	/* wake on gpio block interrupt */
	return irq_set_irq_wake(bank->irq, on);
}
#else
#define rtk_gpio_set_irq_wake NULL
#endif

static int rtk_gpio_to_irq(struct gpio_chip *chip, unsigned int offset) {
	struct rtk_gpio_bank *bank = to_bank(chip);

	return irq_create_mapping(bank->domain, offset);
}

static irqreturn_t rtk_gpio_irq_handler(int irq, void *data) {
	struct rtk_gpio_bank *bank = (struct rtk_gpio_bank *)data;

	u32 isr, bit;
	isr = rtk_gpio_read(bank, RTK_GPIO_ISR);
	isr &= bank->imr;

	if (!isr)
		goto done;

	while (isr) {
		bit = __ffs(isr);
		isr &= ~(BIT(bit));

		generic_handle_irq(irq_find_mapping(bank->domain, bit));
	}

done:
	return IRQ_HANDLED;
}

static int __setup_bank_irq(struct rtk_gpio_bank_info *info, struct rtk_gpio_bank *bank) {
	struct device *dev = info->priv->dev;
	int ret;
	struct irq_chip_generic *gc;

	bank->chip.to_irq = rtk_gpio_to_irq;
	ret = devm_request_irq(dev, bank->irq, rtk_gpio_irq_handler, 0, bank->label, bank);
	if (ret != 0)
		return ret;

	bank->domain = irq_domain_add_linear(info->node, bank->chip.ngpio, &irq_generic_chip_ops, bank);
	if (!bank->domain) {
		dev_err(dev, "%s, Unable to allocate domain\n", bank->label);
		ret = -ENOMEM;
		goto ERR;
	}

	ret = irq_alloc_domain_generic_chips(bank->domain,bank->chip.ngpio,1,bank->label,handle_edge_irq,0, 	0, IRQ_GC_INIT_NESTED_LOCK);
	if (ret) {
		dev_err(dev, "%s, Unable to allocate chips\n", bank->label);
		ret = -ENOMEM;
		goto ERR;
	}

	gc = bank->domain->gc->gc[0];
	gc->chip_types[0].type               = IRQ_TYPE_EDGE_BOTH;
	gc->chip_types[0].chip.irq_startup   = rtk_gpio_startup_irq;
	gc->chip_types[0].chip.irq_ack       = rtk_gpio_ack_irq;
	gc->chip_types[0].chip.irq_mask      = rtk_gpio_mask_irq;
	gc->chip_types[0].chip.irq_unmask    = rtk_gpio_unmask_irq;
	gc->chip_types[0].chip.irq_set_type	= rtk_gpio_set_irq_type;
	gc->chip_types[0].chip.irq_set_wake	= rtk_gpio_set_irq_wake;
ERR:
	return ret;
}

static int rtk_gpio_bank_probe(struct rtk_gpio_bank_info *info)
{
	struct device_node *np = info->node;
	struct device *dev = info->priv->dev;
	struct rtk_gpio_bank *bank;
	unsigned int ngpios;
	int err = 0;

	bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
	if (!bank)
		return -ENOMEM;

	spin_lock_init(&bank->lock);

	/* Offset the main registers to the first register in this bank */
	if(info->index == 2)
		bank->reg = info->priv->reg + info->index * GPIO_BANK_REG_OFFSET + 0x8; //JKMN has more shift
	else
		bank->reg = info->priv->reg + info->index * GPIO_BANK_REG_OFFSET;

	bank->enable_reg = info->priv->enable_reg + info->index * GPIO_ENABLE_REG_OFFSET;
#if 0
#ifndef CONFIG_PINCTRL
	bank->mux_reg = info->priv->mux_reg + info->index * 4;
#endif
#endif

	/* Set up GPIO chip */
	snprintf(bank->label, sizeof(bank->label), "rtk-gpio-%u",
		 info->index);
	bank->chip.label		= bank->label;
	bank->chip.parent		= dev;
	bank->chip.request		= rtk_gpio_request;
	bank->chip.free			= rtk_gpio_free;
	bank->chip.get_direction	= rtk_gpio_get_direction;
	bank->chip.direction_input	= rtk_gpio_direction_input;
	bank->chip.direction_output	= rtk_gpio_direction_output;
	bank->chip.get			= rtk_gpio_get;
	bank->chip.set			= rtk_gpio_set;
	bank->chip.fwnode		= of_fwnode_handle(np);

	/* GPIO numbering from 0 */
	bank->chip.base			= info->index * GPIO_BANK_SIZE;
	if (of_property_read_u32(np, "ngpios", &ngpios)) {
		dev_warn(dev, "Missing ngpios OF property\n");
		ngpios = GPIO_BANK_SIZE;
	}
	if (ngpios > GPIO_BANK_SIZE) {
		dev_err(dev, "ngpios property is not valid\n");
		ngpios = GPIO_BANK_SIZE;
	}
	bank->chip.ngpio = ngpios;
	info->bank = bank;

	/* Add the GPIO bank */
	err = gpiochip_add_data(&bank->chip, NULL);
	if (err < 0) {
		dev_err(dev, "Cannot add gpiochip-%d\n",info->index);
		goto ERR;
	}

	if (of_find_property(np, "interrupt-controller", NULL)) {
		/* Get the GPIO bank IRQ if provided */
		bank->irq = irq_of_parse_and_map(np, 0);

		/* The interrupt is optional (it may be used by another core on chip) */
		if (!bank->irq) {
			dev_info(dev, "IRQ not provided for bank %u, IRQs disabled\n",
				info->index);
			return 0;
		}
		dev_info(dev, "Setting up IRQs for GPIO bank %u\n",
			info->index);

		err = __setup_bank_irq(info, bank);
	}
ERR:
	return err;
}

static void rtk_gpio_register_banks(struct rtk_gpio *priv)
{
	struct device_node *np = priv->dev->of_node;
	struct device_node *node;
	int i;

	for (i = 0; i < GPIO_MAX_BANK_NUM; i++)
		priv->info[i] = NULL;

	for_each_available_child_of_node(np, node) {
		struct rtk_gpio_bank_info *info;
		u32 addr;
		int ret;

		ret = of_property_read_u32(node, "reg", &addr);
		if (ret) {
			dev_err(priv->dev, "invalid reg on %s\n",
				node->full_name);
			continue;
		}
		if (addr >= GPIO_MAX_BANK_NUM) {
			dev_err(priv->dev, "index %u in %s out of range\n",
				addr, node->full_name);
			continue;
		}

		info = devm_kzalloc(priv->dev, sizeof(*info), GFP_KERNEL);
		if (!info)
			return;

		info->index = addr;
		info->node = of_node_get(node);
		info->priv = priv;
		info->bank = NULL;

		ret = rtk_gpio_bank_probe(info);
		if (ret) {
			dev_err(priv->dev, "failure registering %s\n",
				node->full_name);
			of_node_put(node);
			continue;
		}

		priv->info[info->index] = info;
	}
}

static int rtk_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res_regs;
	struct rtk_gpio *priv;

	if (!np) {
		dev_err(dev, "must be instantiated via devicetree\n");
		return -ENOENT;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->enable_reg = devm_ioremap_resource(dev, res_regs);
	if (!priv->enable_reg) {
		dev_err(dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}
	dev_info(dev, "resource - %pr mapped at 0x%pK\n", res_regs, priv->enable_reg);

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->reg = devm_ioremap_resource(dev, res_regs);
	if (!priv->reg) {
		dev_err(dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}
	dev_info(dev, "resource - %pr mapped at 0x%pK\n", res_regs, priv->reg);
#if 0
#ifndef CONFIG_PINCTRL
	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	priv->mux_reg = devm_ioremap_resource(dev, res_regs);
	if (!priv->mux_reg) {
		dev_err(dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}
	dev_info(dev, "resource - %pr mapped at 0x%pK\n", res_regs,
		 priv->mux_reg);
#endif
#endif
	rtk_gpio_register_banks(priv);

	return 0;
}

static void rtk_gpio_remove(struct platform_device *pdev)
{
	struct rtk_gpio *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < GPIO_MAX_BANK_NUM; i++) {
		if (priv->info[i])
			gpiochip_remove(&priv->info[i]->bank->chip);
	}
}

const struct of_device_id rtk_gpio_of_match[] = {
	{ .compatible = "rtk,gpio" },
	{ },
};

static struct platform_driver rtk_gpio_driver = {
	.driver = {
		.name		= "rtk-mips-ia-gpio",
		.of_match_table	= rtk_gpio_of_match,
	},
	.probe		= rtk_gpio_probe,
	.remove		= rtk_gpio_remove,
};

module_platform_driver(rtk_gpio_driver);

MODULE_DESCRIPTION("RTK GPIO Driver");
MODULE_LICENSE("GPL v2");
