#include <linux/module.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <linux/dma-mapping.h>
#include <dma-coherence.h>
#include <asm/bitops.h>
#include <linux/bootmem.h>
#include <asm/r4kcache.h>

#if defined(CONFIG_CPU_LOONGSON2H) || defined(CONFIG_CPU_LOONGSON2K)
#define DEFAULT_CACHEABLE 4
#else
#define DEFAULT_CACHEABLE 2
#endif
static int cachelock=DEFAULT_CACHEABLE;
static int devinited;
static struct device cachelock_device;
static void *cache_virt;
static long cache_size;

int reinit_cachedev(struct device *dev);
#ifdef  CONFIG_LS3A2000_NEWCACHELOCK
#include <asm/r4kcache.h>
#define __locksflush_prologue {
#define __locksflush_epilogue }

__BUILD_BLAST_CACHE_RANGE(locks, scache, 15, )
#endif

static int __init cachelock_setup(char *str)
{
	get_option(&str, &cachelock);

	return 0;
}

early_param("cachelock", cachelock_setup);

static int param_set_cachelock(const char *val, const struct kernel_param *kp)
{
	int ret;
	int old = cachelock;
	ret = param_set_int(val, kp);
	if(ret!=0) return ret;

	if(old==cachelock) return 0;
	ret = reinit_cachedev(&cachelock_device);
        if(ret!=0) cachelock = old;
        return ret;
}

#ifdef CONFIG_CPU_LOONGSON2H
#define set_L2_win(mas_i, win_i, base, mask, mmap) \
	*(volatile long *)(0xffffffffbfd80000+mas_i*0x100+win_i*8) = base; \
	*(volatile long *)(0xffffffffbfd80000+mas_i*0x100+win_i*8+0x40) = mask; \
	*(volatile long *)(0xffffffffbfd80000+mas_i*0x100+win_i*8+0x80) = mmap;
#endif

static int init_cachedev(struct device *dev)
{
	dma_addr_t  dma;
	void *virt;
	long phys;
	int retval = 0;
	long size;
#if defined(CONFIG_CPU_LOONGSON3)
	if(read_c0_prid()>=0x146300 && read_c0_prid() <0x14630d && cachelock == 1)
	{
            printk("loongson3a 2000/3000 can only use uncached mode force to uncached.\n");
	    cachelock = 2;
	}
#elif defined(CONFIG_CPU_LOONGSON2H) || defined(CONFIG_CPU_LOONGSON2K)
	if(cachelock == 2)
	{
            printk("loongson2h, loongson2k can only use cachelock mode force to cachelocked.\n");
	    cachelock = 1;
	}
#endif

        if(cachelock != 2)
	 dev->init_name = "cachemem";
	else
         dev->init_name = "uncachemem";
        
	virt = cache_virt;
	size = cache_size;
	phys = virt_to_phys(virt);
	dma = plat_map_dma_mem(dev, PAGE_OFFSET+phys, size);

	printk("cachlock=%d\n", cachelock);
	/*cachelock mem*/
	if(cachelock == 1)
	{
		blast_scache_range(virt, virt+size);
#ifdef CONFIG_CPU_LOONGSON2H
		/*pcie dma cross bar*/
		set_L2_win(4, 0, 0x0000000000000000, 0xfffffffffffff000, 0x000000001fd00081) 
		set_L2_win(4, 1, 0x0000000000000000, 0xfffffffff0000000, 0x00000000400000f3) 
		set_L2_win(4, 2, 0x0000000000000000, 0xffffffffc0000000, 0x00000001400000f3) 
		set_L2_win(4, 3, 0x0000000040000000, 0xffffffffc0000000, 0x00000002400000f3) 
		set_L2_win(4, 4, 0x0000000080000000, 0xffffffffc0000000, 0x00000003400000f3) 
		set_L2_win(4, 5, 0x00000000c0000000, 0xffffffffc0000000, 0x00000004400000f3) 

		/*axi dma cross bar*/
		set_L2_win(1, 0, 0x0000000000000000, 0xfffffffff0000000, 0x00000000400000f3)
		set_L2_win(1, 1, 0x0000000000000000, 0xffffffffc0000000, 0x00000001400000f3)
		set_L2_win(1, 2, 0x0000000040000000, 0xffffffffc0000000, 0x00000002400000f3)
		set_L2_win(1, 3, 0x0000000080000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(1, 4, 0x00000000c0000000, 0xffffffffc0000000, 0x00000004400000f3)
		set_L2_win(1, 5, 0xffffffff80000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(1, 6, 0xffffffffc0000000, 0xffffffffc0000000, 0x00000004400000f3)

                /*lock cache*/
		*(volatile long *)0xffffffffbfd84250 = ~(size-1);
		*(volatile long *)0xffffffffbfd84210 = 0x8000000000000000UL|phys;
#elif defined(CONFIG_CPU_LOONGSON2K)
                /*lock cache*/
		*(volatile long *)0xffffffffbfe10200 = ~(size-1);
		*(volatile long *)0xffffffffbfe10240 = 0x8000000000000000UL|phys;
#elif defined(CONFIG_CPU_LOONGSON3)
                /*disable ucache window*/
		*(volatile int *)0x90000efdfb0000f0 = 0x00000000;
#ifndef  CONFIG_LS3A2000_NEWCACHELOCK
                /*lock cache*/
		*(volatile long *)0x900000003ff00240 = ~(size-1);
		*(volatile long *)0x900000003ff00200 = 0x8000000000000000UL|phys;
#else
		blast_lockscache_range(virt, virt+size);
#endif
#endif
		__sync();
		memset(virt, 0, size);
		wmb();
		memset(UNCAC_ADDR(virt), 0xcc, size);
		wmb();
	}
	else if(cachelock == 2)
	{
#ifdef CONFIG_CPU_LOONGSON2H
                /*dma noncoherent for needed dma memory region, others dma coherent, disable cachelock*/
		/*pcie dma cross bar*/
		set_L2_win(4, 0, 0x0000000000000000, 0xfffffffffffff000, 0x000000001fd00081)
		set_L2_win(4, 6, 0x0000000000000000, 0xfffffffff0000000, 0x00000000400000f3)
		set_L2_win(4, 1, dma, ~(size-1), dma | 0xf0)
		set_L2_win(4, 2, 0x0000000000000000, 0xffffffffc0000000, 0x00000001400000f3)
		set_L2_win(4, 3, 0x0000000040000000, 0xffffffffc0000000, 0x00000002400000f3)
		set_L2_win(4, 4, 0x0000000080000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(4, 5, 0x00000000c0000000, 0xffffffffc0000000, 0x00000004400000f3)

		/*axi dma cross bar*/
		set_L2_win(1, 0, dma, ~(size-1), dma | 0xf0)
		set_L2_win(1, 1, 0x0000000000000000, 0xfffffffff0000000, 0x00000000400000f3)
		set_L2_win(1, 2, 0x0000000000000000, 0xffffffffc0000000, 0x00000001400000f3)
		set_L2_win(1, 3, 0x0000000040000000, 0xffffffffc0000000, 0x00000002400000f3)
		set_L2_win(1, 4, 0x0000000080000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(1, 5, 0x00000000c0000000, 0xffffffffc0000000, 0x00000004400000f3)
		set_L2_win(1, 6, 0xffffffff80000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(1, 7, 0xffffffffc0000000, 0xffffffffc0000000, 0x00000004400000f3)

                /*disable lock cache*/
		*(volatile long *)0xffffffffbfd84210 = 0x0000000000000000UL;
#elif defined(CONFIG_CPU_LOONGSON3)
	 dma = plat_map_dma_mem(dev, PAGE_OFFSET, size);
	 virt = PAGE_OFFSET + 0x100000;
	 size = 0x100000;
         *(volatile int *)0x90000efdfb0000f0 = 0xc0000000;
         *(volatile int *)0x90000efdfb0000f4 = 0x0000ffff|(dma>>8);
#endif
	}
        else if(cachelock == 0)
        {
                /*all memroy dma coherent and disable cache lock*/
#ifdef CONFIG_CPU_LOONGSON2H
                /*disable lock cache*/
		*(volatile long *)0xffffffffbfd84210 = 0x0000000000000000UL;
		/*pcie dma cross bar*/
		set_L2_win(4, 0, 0x0000000000000000, 0xfffffffffffff000, 0x000000001fd00081)
		set_L2_win(4, 1, 0x0000000000000000, 0xfffffffff0000000, 0x00000000400000f3)
		set_L2_win(4, 2, 0x0000000000000000, 0xffffffffc0000000, 0x00000001400000f3)
		set_L2_win(4, 3, 0x0000000040000000, 0xffffffffc0000000, 0x00000002400000f3)
		set_L2_win(4, 4, 0x0000000080000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(4, 5, 0x00000000c0000000, 0xffffffffc0000000, 0x00000004400000f3)

		/*axi dma cross bar*/
		set_L2_win(1, 0, 0x0000000000000000, 0xfffffffff0000000, 0x00000000400000f3)
		set_L2_win(1, 1, 0x0000000000000000, 0xffffffffc0000000, 0x00000001400000f3)
		set_L2_win(1, 2, 0x0000000040000000, 0xffffffffc0000000, 0x00000002400000f3)
		set_L2_win(1, 3, 0x0000000080000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(1, 4, 0x00000000c0000000, 0xffffffffc0000000, 0x00000004400000f3)
		set_L2_win(1, 5, 0xffffffff80000000, 0xffffffffc0000000, 0x00000003400000f3)
		set_L2_win(1, 6, 0xffffffffc0000000, 0xffffffffc0000000, 0x00000004400000f3)
#elif defined(CONFIG_CPU_LOONGSON3)
                /*disable lock cache*/
		*(volatile long *)0x900000003ff00200 = 0x0000000000000000UL;
		*(volatile int *)0x90000efdfb0000f0 = 0;
#elif defined(CONFIG_CPU_LOONGSON2K)
                /*lock cache*/
		*(volatile long *)0xffffffffbfe10240 = 0x0000000000000000UL;
		*(volatile long *)0xffffffffbfe10200 = 0;
#endif
	return 0;
        }

	phys = virt_to_phys(virt);
	dma = plat_map_dma_mem(dev, virt, size);


	if (!dma_declare_coherent_memory(dev, phys,
				dma,
				size,
				DMA_MEMORY_MAP)) {
		dev_err(dev, "cannot declare coherent memory\n");
		retval = -ENXIO;
	}

	printk("dma=0x%lx virt=0x%lx\n", (long)dma, (long)virt);
	devinited = 1;
        return retval;

}

int reinit_cachedev(struct device *dev)
{
        dma_addr_t dma_handle;
	void *ret;
	if(!devinited) return 0;


	if(dev->dma_mem)
	{
		if (!dma_alloc_from_coherent(dev, cache_size, &dma_handle, &ret))
			return -EBUSY;

		dma_release_declared_memory(dev);
	}
           
        return init_cachedev(dev);
}

static struct kernel_param_ops param_cachelock_ops = {
	.set = param_set_cachelock,
	.get = param_get_int,
};

//__module_param_call("", cachelock, &param_cachelock_ops, &cachelock, 0664, -1);
module_param_call(cachelock, param_set_cachelock, param_get_int, &cachelock, 0664);

struct device *get_cachelock_device(void *dev)
{
	return  cachelock?&cachelock_device:dev;
}

EXPORT_SYMBOL(get_cachelock_device);


static int cachelockmem_init(void)
{
	struct device *dev = &cachelock_device;

	cache_size = 1<<(fls(CONFIG_LOONGSON_CACHEMEM_SIZE*1024)-1);
	cache_virt = phys_to_virt(0x100000);

	device_initialize(dev);
	devinited = 1;
	if(!cachelock) return 0;

	init_cachedev(dev);

	return 0;
}


arch_initcall(cachelockmem_init);
