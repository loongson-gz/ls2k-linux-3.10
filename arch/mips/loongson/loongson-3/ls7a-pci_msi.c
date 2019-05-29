#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#define IRQ_LS7A_MSI_0 0
#define LS7A_NUM_MSI_IRQS 64

/*msi use irq, other device not used*/
static DECLARE_BITMAP(msi_irq_in_use, LS7A_NUM_MSI_IRQS)={0xff0000000000ffffULL};

static DEFINE_SPINLOCK(lock);
extern int ls3a_msi_enabled;

int pch_create_dirq(unsigned int irq);
void pch_destroy_dirq(unsigned int irq);

#define irq2bit(irq) (irq - IRQ_LS7A_MSI_0)
#define bit2irq(bit) (IRQ_LS7A_MSI_0 + bit)

/* LS7A MSI target address only for devices with 64bit MSI */
#define LS7A_MSI_TARGET_ADDRESS_64_HI		0xFD
#define LS7A_MSI_TARGET_ADDRESS_64_LO		0xF8000000

/* LS7A MSI target address for devices with 64bit MSI or 32bit MSI */
#define LS7A_MSI_TARGET_ADDRESS_64_32_HI	0x0
#define LS7A_MSI_TARGET_ADDRESS_64_32_LO	0x2FF00000


/*
 * Dynamic irq allocate and deallocation
 */
static int ls7a_create_msi_irq(void)
{
	int irq, pos;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
again:
	pos = find_first_zero_bit(msi_irq_in_use, LS7A_NUM_MSI_IRQS);
	if(pos==LS7A_NUM_MSI_IRQS) {
		spin_unlock_irqrestore(&lock, flags);
		return -ENOSPC;
	}

	irq = pos + IRQ_LS7A_MSI_0;
	pch_create_dirq(irq);
	/* test_and_set_bit operates on 32-bits at a time */
	if (test_and_set_bit(pos, msi_irq_in_use))
		goto again;
	spin_unlock_irqrestore(&lock, flags);

	dynamic_irq_init(irq);

	return irq;
}

static void ls7a_destroy_irq(unsigned int irq)
{
	int pos = irq2bit(irq);

	pch_destroy_dirq(irq);
	dynamic_irq_cleanup(irq);

	clear_bit(pos, msi_irq_in_use);
}

void ls7a_teardown_msi_irq(unsigned int irq)
{
	ls7a_destroy_irq(irq);
}


static void ls3a_msi_nop(struct irq_data *data)
{
	return;
}


static struct irq_chip ls7a_msi_chip = {
	.name = "PCI-MSI",
	.irq_ack = ls3a_msi_nop,
	.irq_enable = unmask_msi_irq,
	.irq_disable = mask_msi_irq,
	.irq_mask = mask_msi_irq,
	.irq_unmask = unmask_msi_irq,
	.irq_set_affinity = plat_set_irq_affinity,
};

int ls7a_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	int irq = ls7a_create_msi_irq();
	struct msi_msg msg;

	if(!ls3a_msi_enabled)
		return -ENOSPC;

	if (irq < 0)
		return irq;

	irq_set_msi_desc(irq, desc);

	msg.address_hi = LS7A_MSI_TARGET_ADDRESS_64_32_HI;
	msg.address_lo = LS7A_MSI_TARGET_ADDRESS_64_32_LO;

	msg.data = irq;

	write_msi_msg(irq, &msg);
	irq_set_chip_and_handler(irq, &ls7a_msi_chip, handle_edge_irq);

	return 0;
}
