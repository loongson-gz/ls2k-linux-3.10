/*
 * Loongson2k SPI driver
 *
 * Copyright (C) 2017 Juxin Gao <gaojuxin@loongson.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/pci.h>
#ifdef CONFIG_CPU_LOONGSON2K
#include <ls2k.h>
#endif
#include <linux/of.h>
/*define spi register */
#define	SPCR	0x00
#define	SPSR	0x01
#define FIFO	0x02
#define	SPER	0x03
#define	PARA	0x04
#define	SFCS	0x05
#define	TIMI	0x06

extern unsigned long bus_clock;
struct ls_spi {
	struct work_struct	work;
	spinlock_t			lock;

	struct	list_head	msg_queue;
	struct	spi_master	*master;
	void	__iomem		*base;
	int cs_active;
	unsigned int hz;
	unsigned char spcr, sper;
	struct workqueue_struct	*wq;
};

static inline int set_cs(struct ls_spi *ls_spi, struct spi_device  *spi, int val);

static void ls_spi_write_reg(struct ls_spi *spi,
		unsigned char reg, unsigned char data)
{
	writeb(data, spi->base +reg);
}

static char ls_spi_read_reg(struct ls_spi *spi,
		unsigned char reg)
{
	return readb(spi->base + reg);
}

static int ls_spi_update_state(struct ls_spi *ls_spi,struct spi_device *spi,
		struct spi_transfer *t)
{
	unsigned int hz;
	unsigned int div, div_tmp;
	unsigned int bit;
	unsigned long clk;
	unsigned char val;
	const char rdiv[12]= {0,1,4,2,3,5,6,7,8,9,10,11}; 

	hz  = t ? t->speed_hz : spi->max_speed_hz;

	if (!hz)
		hz = spi->max_speed_hz;

	if (hz && ls_spi->hz != hz) {
		clk = 100000000;
		div = DIV_ROUND_UP(clk, hz);

		if (div < 2)
			div = 2;

		if (div > 4096)
			div = 4096;

		bit = fls(div) - 1;
		if((1<<bit) == div) bit--;
		div_tmp = rdiv[bit];
		
		dev_dbg(&spi->dev, "clk = %ld hz = %d div_tmp = %d bit = %d\n",
				clk, hz, div_tmp, bit);

		ls_spi->hz = hz;
		ls_spi->spcr = div_tmp & 3;
		ls_spi->sper = (div_tmp >> 2) & 3;

		val = ls_spi_read_reg(ls_spi, SPCR);
		ls_spi_write_reg(ls_spi, SPCR, (val & ~3) | ls_spi->spcr);
		val = ls_spi_read_reg(ls_spi, SPER);
		ls_spi_write_reg(ls_spi, SPER, (val & ~3) | ls_spi->sper);
	}

	return 0;
}



static int ls_spi_setup(struct spi_device *spi)
{
	struct ls_spi *ls_spi;

	ls_spi = spi_master_get_devdata(spi->master);
	if (spi->bits_per_word %8)
		return -EINVAL;

	if(spi->chip_select >= spi->master->num_chipselect)
		return -EINVAL;

	ls_spi_update_state(ls_spi, spi, NULL);

	set_cs(ls_spi, spi, 1);

	return 0;
}

static int ls_spi_write_read_8bit( struct spi_device *spi,
		const u8 **tx_buf, u8 **rx_buf, unsigned int num)
{
	struct ls_spi *ls_spi;
	ls_spi = spi_master_get_devdata(spi->master);

	if (tx_buf && *tx_buf){
		ls_spi_write_reg(ls_spi, FIFO, *((*tx_buf)++));
		while((ls_spi_read_reg(ls_spi, SPSR) & 0x1) == 1);
	}else{
		ls_spi_write_reg(ls_spi, FIFO, 0);
		while((ls_spi_read_reg(ls_spi, SPSR) & 0x1) == 1);
	}

	if (rx_buf && *rx_buf) {
		*(*rx_buf)++ = ls_spi_read_reg(ls_spi, FIFO);
	}else{
		ls_spi_read_reg(ls_spi, FIFO);
	}

	return 1;
}


static unsigned int ls_spi_write_read(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct ls_spi *ls_spi;
	unsigned int count;
	const u8 *tx = xfer->tx_buf;
	u8 *rx = xfer->rx_buf;

	ls_spi = spi_master_get_devdata(spi->master);
	count = xfer->len;

	do {
		if (ls_spi_write_read_8bit(spi, &tx, &rx, count) < 0)
			goto out;
		count--;
	} while (count);

out:
	return xfer->len - count;

}

static inline int set_cs(struct ls_spi *ls_spi, struct spi_device  *spi, int val)
{
	int cs = ls_spi_read_reg(ls_spi, SFCS) & ~(0x11 << spi->chip_select);
	ls_spi_write_reg(ls_spi, SFCS, ( val ? (0x11 << spi->chip_select):(0x1 << spi->chip_select)) | cs);
	return 0;
}

static void ls_spi_work(struct work_struct *work)
{
	struct ls_spi *ls_spi =
		container_of(work, struct ls_spi, work);
	int param;

	spin_lock(&ls_spi->lock);
	param = ls_spi_read_reg(ls_spi, PARA);
	ls_spi_write_reg(ls_spi, PARA, param&~1);
	while (!list_empty(&ls_spi->msg_queue)) {

		struct spi_message *m;
		struct spi_device  *spi;
		struct spi_transfer *t = NULL;

		m = container_of(ls_spi->msg_queue.next, struct spi_message, queue);

		list_del_init(&m->queue);
		spin_unlock(&ls_spi->lock);

		spi = m->spi;

		/*setup spi clock*/
		ls_spi_update_state(ls_spi, spi, NULL);

		/*in here set cs*/
		set_cs(ls_spi, spi, 0);

		list_for_each_entry(t, &m->transfers, transfer_list) {
			if (t->len)
				m->actual_length +=
					ls_spi_write_read(spi, t);
		}

		set_cs(ls_spi, spi, 1);
		m->complete(m->context);


		spin_lock(&ls_spi->lock);
	}

	ls_spi_write_reg(ls_spi, PARA, param);
	spin_unlock(&ls_spi->lock);
}



static int ls_spi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct ls_spi	*ls_spi;
	struct spi_transfer *t = NULL;

	m->actual_length = 0;
	m->status		 = 0;
	if (list_empty(&m->transfers) || !m->complete)
		return -EINVAL;

	ls_spi = spi_master_get_devdata(spi->master);

	list_for_each_entry(t, &m->transfers, transfer_list) {

		if (t->tx_buf == NULL && t->rx_buf == NULL && t->len) {
			dev_err(&spi->dev,
					"message rejected : "
					"invalid transfer data buffers\n");
			goto msg_rejected;
		}

		/*other things not check*/

	}

	spin_lock(&ls_spi->lock);
	list_add_tail(&m->queue, &ls_spi->msg_queue);
	queue_work(ls_spi->wq, &ls_spi->work);
	spin_unlock(&ls_spi->lock);

	return 0;
msg_rejected:

	m->status = -EINVAL;
	if (m->complete)
		m->complete(m->context);
	return -EINVAL;
}

static int ls_spi_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct ls_spi		*spi;
	struct resource		*res;
	int ret;
	master = spi_alloc_master(&pdev->dev, sizeof(struct ls_spi));

	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return-ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num	= pdev->id;

	master->setup = ls_spi_setup;
	master->transfer = ls_spi_transfer;
	master->num_chipselect = 4;
#ifdef CONFIG_OF
	master->dev.of_node = of_node_get(pdev->dev.of_node);
#endif
	dev_set_drvdata(&pdev->dev, master);

	spi = spi_master_get_devdata(master);

	spi->wq	= create_singlethread_workqueue(pdev->name);

	spi->master = master;


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
		ret = -ENOENT;
		goto free_master;
	}

	spi->base = ioremap(res->start, (res->end - res->start)+1);
	if (spi->base == NULL) {
		dev_err(&pdev->dev, "Cannot map IO\n");
		ret = -ENXIO;
		goto unmap_io;
	}

	ls_spi_write_reg(spi, SPCR, 0x51);
	ls_spi_write_reg(spi, SPER, 0x00);
	ls_spi_write_reg(spi, TIMI, 0x01);
	ls_spi_write_reg(spi, PARA, 0x40);
	INIT_WORK(&spi->work, ls_spi_work);

	spin_lock_init(&spi->lock);
	INIT_LIST_HEAD(&spi->msg_queue);

	ret = spi_register_master(master);
	if (ret < 0)
		goto unmap_io;

	return ret;

unmap_io:
	iounmap(spi->base);
free_master:
	kfree(master);
	spi_master_put(master);
	return ret;

}
#ifdef CONFIG_OF
static struct of_device_id ls_spi_id_table[] = {
	{ .compatible = "loongson,ls-spi", },
	{ },
};
#endif
static struct platform_driver ls_spi_driver = {
	.probe = ls_spi_probe,
	.driver	= {
		.name	= "ls-spi",
		.owner	= THIS_MODULE,
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_spi_id_table),
#endif
	},
};
static struct resource ls_spi_resources[] = {
    [0] = {
        .flags  = IORESOURCE_MEM,
    },
    [1] = {
        .flags  = IORESOURCE_IRQ,
    },
};

static struct platform_device ls_spi_device = {
    .name           = "ls-spi",
    .id             = 0,
    .num_resources  = ARRAY_SIZE(ls_spi_resources),
    .resource   = ls_spi_resources,
};


static int ls_spi_pci_register(struct pci_dev *pdev,
                 const struct pci_device_id *ent)
{
    int ret;
    unsigned char v8;

    pr_debug("ls_spi_pci_register BEGIN\n");
    /* Enable device in PCI config */
    ret = pci_enable_device(pdev);
    if (ret < 0) {
        printk(KERN_ERR "ls-pci (%s): Cannot enable PCI device\n",
               pci_name(pdev));
        goto err_out;
    }

    /* request the mem regions */
    ret = pci_request_region(pdev, 0, "ls-spi io");
    if (ret < 0) {
        printk( KERN_ERR "ls-spi (%s): cannot request region 0.\n",
            pci_name(pdev));
        goto err_out;
    }

    ls_spi_resources[0].start = pci_resource_start (pdev, 0);
    ls_spi_resources[0].end = pci_resource_end(pdev, 0);
    /* need api from pci irq */
    ret = pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &v8);

    if (ret == PCIBIOS_SUCCESSFUL) {

        ls_spi_resources[1].start = v8;
        ls_spi_resources[1].end = v8;
        platform_device_register(&ls_spi_device);
    }

err_out:
    return ret;
}

static void ls_spi_pci_unregister(struct pci_dev *pdev)
{
    pci_release_region(pdev, 0);
}

static struct pci_device_id ls_spi_devices[] = {
    {PCI_DEVICE(0x14, 0x7a0b)},
    {0, 0, 0, 0, 0, 0, 0}
};

static struct pci_driver ls_spi_pci_driver = {
    .name       = "ls-spi-pci",
    .id_table   = ls_spi_devices,
    .probe      = ls_spi_pci_register,
    .remove     = ls_spi_pci_unregister,
};


static int __init ls_spi_init(void)
{
	int ret;

	ret =  platform_driver_register(&ls_spi_driver);
	if(!ret)
		ret = pci_register_driver(&ls_spi_pci_driver);
	return ret;
}

static void __exit ls_spi_exit(void)
{
	platform_driver_unregister(&ls_spi_driver);
	pci_unregister_driver(&ls_spi_pci_driver);
}

subsys_initcall(ls_spi_init);
module_exit(ls_spi_exit);

MODULE_AUTHOR("Juxin Gao <gaojuxin@loongson.cn>");
MODULE_DESCRIPTION("Loongson2k SPI driver");
MODULE_LICENSE("GPL");
