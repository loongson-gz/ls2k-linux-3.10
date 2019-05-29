/*
 *  linux/arch/arm/plat-ls2h/gpio.c
 *
 * Support functions for ls2h GPIO
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <loongson-pch.h>

#include <asm/irq.h>

#define LS2H_NR_GPIOS		16
#define GPIO_REG_CFG		0x0
#define GPIO_REG_DIR		0x4
#define GPIO_REG_IN		0x8
#define GPIO_REG_OUT		0xc
#define GPIO_OUTPUT     0
#define GPIO_INPUT      1
struct ls2h_gpio {
	void *pbase;
	u32 virtual_irq_start;
	spinlock_t lock;
	struct gpio_chip gc;
};


static void _set_gpio_direction(struct ls2h_gpio *chip, int offset, int is_input)
{
	void __iomem *reg = chip->pbase+GPIO_REG_DIR;
	u32 l;

	l = __raw_readl(reg);
	if (is_input)
		l |= (1<<offset);
	else
		l &= ~(1<<offset);
	__raw_writel(l, reg);
}

static void _set_gpio_dataout(struct ls2h_gpio *chip, int offset, int enable)
{
	void __iomem *reg = chip->pbase+GPIO_REG_OUT;
	u32 l = 0;

	l = __raw_readl(reg);
	if(enable)
		l |= (1<<offset);
	else
		l &= ~(1<<offset);
	__raw_writel(l, reg);
}

static int _get_gpio_datain(struct ls2h_gpio *chip, int offset)
{
	void __iomem *reg;

	reg = chip->pbase + GPIO_REG_IN;
	return !!(__raw_readl(reg) & (1<<offset));
}


static int _get_gpio_dataout(struct ls2h_gpio *chip, int offset)
{
	void __iomem *reg;

	reg = chip->pbase + GPIO_REG_OUT;
	return !!(__raw_readl(reg) & (1<<offset));
}



static void _reset_gpio(struct ls2h_gpio *chip, int gpio)
{
	_set_gpio_direction(chip, gpio, GPIO_INPUT);
}


static int ls2h_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags;
	int val;
	struct ls2h_gpio *chip = container_of(gc, struct ls2h_gpio, gc);

	spin_lock_irqsave(&chip->lock, flags);
	val = __raw_readl(chip->pbase+GPIO_REG_CFG);
	val &= ~(1<<offset);
	__raw_writel(val,chip->pbase+GPIO_REG_CFG);
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static void ls2h_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct ls2h_gpio *chip = container_of(gc, struct ls2h_gpio, gc);
	unsigned int tmp, flags;

	spin_lock_irqsave(&chip->lock, flags);

	tmp = __raw_readl(chip->pbase + GPIO_REG_CFG);
	__raw_writel(tmp |(1<<offset), chip->pbase + GPIO_REG_CFG);
	_reset_gpio(chip, chip->gc.base + offset);
	spin_unlock_irqrestore(&chip->lock, flags);
}


static int gpio_input(struct gpio_chip *gc, unsigned offset)
{
	struct ls2h_gpio *chip;
	unsigned long flags;

	chip = container_of(gc, struct ls2h_gpio, gc);
	spin_lock_irqsave(&chip->lock, flags);
	_set_gpio_direction(chip, offset, GPIO_INPUT);
	spin_unlock_irqrestore(&chip->lock, flags);
	return 0;
}

static int gpio_is_input(struct ls2h_gpio *chip, int offset)
{
	void __iomem *reg  = chip->pbase+GPIO_REG_DIR;
	if(__raw_readl(reg) & offset)  /*ls2h gpio: 0 is output; 1 is input*/
		return GPIO_INPUT;
	else 
		return GPIO_OUTPUT;
}

static int gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct ls2h_gpio *chip;
	chip = container_of(gc, struct ls2h_gpio, gc);

	if (gpio_is_input(chip, offset))
		return _get_gpio_datain(chip, offset);
	else
		return _get_gpio_dataout(chip, offset);
}

static int gpio_output(struct gpio_chip *gc, unsigned offset, int value)
{
	struct ls2h_gpio *chip;
	unsigned long flags;

	chip = container_of(gc, struct ls2h_gpio, gc);
	spin_lock_irqsave(&chip->lock, flags);
	_set_gpio_dataout(chip, offset, value);
	_set_gpio_direction(chip, offset, GPIO_OUTPUT);
	spin_unlock_irqrestore(&chip->lock, flags);
	return 0;
}

static void gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct ls2h_gpio *chip;
	unsigned long flags;

	chip = container_of(gc, struct ls2h_gpio, gc);
	spin_lock_irqsave(&chip->lock, flags);
	_set_gpio_dataout(chip, offset, value);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int gpio_2irq(struct gpio_chip *gc, unsigned offset)
{
	struct ls2h_gpio *chip;

	chip = container_of(gc, struct ls2h_gpio, gc);
	return chip->virtual_irq_start + offset;
}


static int ls2h_gpio_probe(struct platform_device *pdev)
{
	struct ls2h_gpio *chip;
	static int initialized = 0;

	if (initialized) return -EBUSY;
	initialized = 1;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->pbase = (void *)CKSEG1ADDR(LS2H_GPIO_CFG_REG);

	spin_lock_init(&chip->lock);
	/* REVISIT eventually switch from ls2h-specific gpio structs
	 * over to the generic ones
	 */
	chip->gc.request = ls2h_gpio_request;
	chip->gc.free = ls2h_gpio_free;
	chip->gc.direction_input = gpio_input;
	chip->gc.get = gpio_get;
	chip->gc.direction_output = gpio_output;
	chip->gc.set = gpio_set;
	chip->gc.to_irq = gpio_2irq;

	chip->gc.label = "gpio";
	chip->gc.base = 0;

	chip->gc.ngpio = LS2H_NR_GPIOS;

	if(gpiochip_add(&chip->gc))
	{
		chip->gc.base = -1;
		gpiochip_add(&chip->gc);
	}
	return 0;
}


static struct platform_driver ls2h_gpio_driver = {
	.driver 	= {
		.name	= "gpio-ls2h",
		.owner	= THIS_MODULE,
	},
	.probe		= ls2h_gpio_probe,
};

static int __init gpio_ls2h_init(void)
{
	return platform_driver_register(&ls2h_gpio_driver);
}

postcore_initcall(gpio_ls2h_init);
