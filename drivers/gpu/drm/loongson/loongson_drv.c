/*
 * Copyright (c) 2018 Loongson Technology Co., Ltd.
 * Authors:
 *	Zhu Chen <zhuchen@loongson.cn>
 *	Fang Yaling <fangyaling@loongson.cn>
 *	Zhang Dandan <zhangdandan@loongson.cn>
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "loongson_drv.h"
#ifdef CONFIG_CPU_LOONGSON2K
#include "ls2k.h"
#else
#include <loongson-pch.h>
#endif
#define DEVICE_NAME "loongson-vga"

#include <asm/addrspace.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/vga_switcheroo.h>
/*#include "platform_driver.h"*/
#include "loongson_drv.h"



#define DRIVER_NAME	"loongson-vga-drm"
#define DRIVER_DESC	"Loongson VGA DRM"
#define DRIVER_DATE	"20180328"
#define DRIVER_MAJOR	0
#define DRIVER_MINOR	1
#define DRIVER_PATCHLEVEL	0

/**
 * loongson_user_framebuffer_destroy -- release framebuffer, clean up framebuffer resource
 *
 * @fb pointer to drm_framebuffer
 */
static void loongson_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct loongson_framebuffer *loongson_fb = to_loongson_framebuffer(fb);
	if (loongson_fb->obj)
		drm_gem_object_unreference_unlocked(loongson_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(fb);
}


/**
 * loongson_fb_funcs --framebuffer hooks sturcture
 */
static const struct drm_framebuffer_funcs loongson_fb_funcs = {
	.destroy = loongson_user_framebuffer_destroy,
};

/**
 *  loongson_framebuffer_init ---registers the framebuffer and
 *   makes it accessible to other threads
 *
 *  @dev      pointer to drm_device structure
 * @lfb      pointer to loongson_framebuffer structure
 * @mode_cmd drm mode framebuffer command
 * @obj      pointer to drm_gem_object structure
 *
 * RETURN
 *  init result
 */
int loongson_framebuffer_init(struct drm_device *dev,
			     struct loongson_framebuffer *lfb,
			     const struct drm_mode_fb_cmd2 *mode_cmd,
			     struct drm_gem_object *obj)
{
	int ret;

	drm_helper_mode_fill_fb_struct(&lfb->base, mode_cmd);
	lfb->obj = obj;
	ret = drm_framebuffer_init(dev, &lfb->base, &loongson_fb_funcs);
	if (ret) {
		DRM_ERROR("drm_framebuffer_init failed: %d\n", ret);
		return ret;
	}
	return 0;
}

/**
 * loongson_user_framebuffer_create
 *
 * @dev       the pointer to drm_device structure
 * @filp      the pointer to drm_file structure
 * @mode_cmd  drm mode framebuffer cmd structure
 *
 * RETURN
 * A new framebuffer with an initial reference count of 1 or a negative
 * error code encoded with ERR_PTR()
 */
static struct drm_framebuffer *
loongson_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *filp,
				const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct loongson_framebuffer *loongson_fb;
	int ret;

	obj = drm_gem_object_lookup(filp, mode_cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	loongson_fb = kzalloc(sizeof(*loongson_fb), GFP_KERNEL);
	if (!loongson_fb) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	ret = loongson_framebuffer_init(dev, loongson_fb, mode_cmd, obj);
	if (ret) {
		drm_gem_object_unreference_unlocked(obj);
		kfree(loongson_fb);
		return ERR_PTR(ret);
	}
	return &loongson_fb->base;
}

/**
 * loongson_mode_funcs---basic driver provided mode setting functions
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
static const struct drm_mode_config_funcs loongson_mode_funcs = {
	.fb_create = loongson_user_framebuffer_create,
};

/**
 * loongson_probe_vram
 *
 * @ldev  pointer to loongson_drm_device
 * @mem   vram memory pointer
 *
 * RETURN
 *  probe read/write vram result
 */
static int loongson_probe_vram(struct loongson_drm_device *ldev, void __iomem *mem)
{
	int offset;
	int orig;
	int test1, test2;
	int orig1, orig2;
	unsigned int vram_size;

	/* Probe */
	orig = ioread16(mem);
	iowrite16(0, mem);

	vram_size = ldev->mc.vram_window;

	/*vram read and write test*/

	for (offset = 0x100000; offset < vram_size; offset += 0x4000) {
		orig1 = ioread8(mem + offset);
		orig2 = ioread8(mem + offset + 0x100);

		iowrite16(0xaa55, mem + offset);
		iowrite16(0xaa55, mem + offset + 0x100);

		test1 = ioread16(mem + offset);
		test2 = ioread16(mem);

		iowrite16(orig1, mem + offset);
		iowrite16(orig2, mem + offset + 0x100);

		if (test1 != 0xaa55) {
			break;
		}

		if (test2) {
			break;
		}
	}

	iowrite16(orig, mem);

	DRM_INFO("loongson vram test success.\n");
	return offset - 65536;
}

/**
 * loongson_vram_init --Map the framebuffer from the card and configure the core
 *
 * @ldev pointer to loongson_drm_device
 *
 * RETURN
 *  vram init result
 */
static int loongson_vram_init(struct loongson_drm_device *ldev)
{
	void __iomem *mem;
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	struct platform_device *vram_pdev;
	struct resource *r;
#else
	struct pci_dev *vram_pdev;
#endif
	struct apertures_struct *aper = alloc_apertures(1);
	if (!aper)
		return -ENOMEM;
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	ldev->vram_pdev = ldev->dev->platformdev;
	/* BAR 0 is VRAM */
	r = platform_get_resource(ldev->dev->platformdev, IORESOURCE_MEM, 1);
	ldev->mc.vram_base  = r->start;
	ldev->mc.vram_window  = r->end - r->start + 1;

    	ldev->mc.vram_size = ldev->mc.vram_window;
#else
	vram_pdev = pci_get_device(PCI_VENDOR_ID_LOONGSON,PCI_DEVICE_ID_LOONGSON_GPU ,NULL);
	ldev->vram_pdev = vram_pdev;
	/* BAR 0 is VRAM */
	ldev->mc.vram_base = pci_resource_start(ldev->vram_pdev, 2);
	ldev->mc.vram_window = pci_resource_len(ldev->vram_pdev, 2);
#endif
	aper->ranges[0].base = ldev->mc.vram_base;
	aper->ranges[0].size = ldev->mc.vram_window;

	remove_conflicting_framebuffers(aper, "loongsonfb", true);
	kfree(aper);

	if (!devm_request_mem_region(ldev->dev->dev, ldev->mc.vram_base, ldev->mc.vram_window,
				"loongsondrmfb_vram")) {
		DRM_ERROR("can't reserve VRAM\n");
		return -ENXIO;
	}

#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
#else
	mem = pci_iomap(ldev->vram_pdev, 2, 0);
	ldev->mc.vram_size = loongson_probe_vram(ldev, mem);

	pci_iounmap(ldev->vram_pdev, mem);
#endif

	return 0;
}

/**
 *  loongson_drm_device_init  ----init drm device
 *
 * @dev   pointer to drm_device structure
 * @flags start up flag
 *
 * RETURN
 *   drm device init result
 */
static int loongson_drm_device_init(struct drm_device *dev,
					uint32_t flags)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	int ret;
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	struct resource *r;
#endif

	loongson_vbios_init(ldev);
	ldev->num_crtc = ldev->vbios->crtc_num;
	ldev->fb_vram_base = 0x0;
	/*BAR 0 contains registers */
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	r = platform_get_resource(ldev->dev->platformdev, IORESOURCE_MEM, 0);
	ldev->rmmio_base = r->start;
	ldev->rmmio_size = r->end - r->start + 1;
#else
	ldev->rmmio_base = pci_resource_start(ldev->dev->pdev, 0);
	ldev->rmmio_size = pci_resource_len(ldev->dev->pdev, 0);
#endif
	DRM_INFO("ldev->rmmio_base = 0x%x,ldev->rmmio_size = 0x%x\n",ldev->rmmio_base,ldev->rmmio_size);

	if (!devm_request_mem_region(ldev->dev->dev, ldev->rmmio_base, ldev->rmmio_size,
				"loongsonfb_mmio")) {
		DRM_ERROR("can't reserve mmio registers\n");
		return -ENOMEM;
	}
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	ldev->rmmio = (void *)TO_UNCAC(ldev->rmmio_base);
#else
	ldev->rmmio = pcim_iomap(dev->pdev, 0, 0);
#endif
	if (ldev->rmmio == NULL)
		return -ENOMEM;

	ret = loongson_vram_init(ldev);
	if (ret)
		return ret;
	return 0;

}

/**
 * loongson_gem_create  -- allocate GEM object
 *
 * @dev: pointer to drm_device structure
 * @size: Requested size of buffer object.
 * @iskernel
 * @obj: address of pointer to drm_gem_object
 *
 * RETURN
 *   the result of alloc gem object
 */
int loongson_gem_create(struct drm_device *dev,
		   u32 size, bool iskernel,
		   struct drm_gem_object **obj)
{
	struct loongson_bo *astbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	ret = loongson_bo_create(dev, size, 0, 0, &astbo);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		return ret;
	}
	*obj = &astbo->gem;
	return 0;
}

/**
 * loongson_modeset_init --- init kernel mode setting
 *
 * @ldev: pointer to loongson_drm_device structure
 *
 * RETURN
 *  return init result
 */
int loongson_modeset_init(struct loongson_drm_device *ldev)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret,i;


	ldev->mode_info[0].mode_config_initialized = true;
	ldev->mode_info[1].mode_config_initialized = true;

	ldev->dev->mode_config.max_width = LOONGSON_MAX_FB_WIDTH;
	ldev->dev->mode_config.max_height = LOONGSON_MAX_FB_HEIGHT;

	ldev->dev->mode_config.cursor_width = 32;
	ldev->dev->mode_config.cursor_height = 32;

	ldev->dev->mode_config.fb_base = ldev->mc.vram_base;

	loongson_crtc_init(ldev);
	ldev->num_crtc = ldev->vbios->crtc_num;

	for(i=0;i<ldev->num_crtc;i++){
		DRM_DEBUG("loongson drm encoder init\n");
		encoder = loongson_encoder_init(ldev->dev,i);
		if (!encoder) {
			DRM_ERROR("loongson_encoder_init failed\n");
			return -1;
		}

		DRM_DEBUG("loongson drm i2c init\n");
		connector = loongson_vga_init(ldev->dev,i);
		if (!connector) {
			DRM_ERROR("loongson_vga_init failed\n");
			return -1;
		}

		ldev->mode_info[i].connector = connector;
		drm_mode_connector_attach_encoder(connector, encoder);
		connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
	}
	DRM_DEBUG("loongson drm fbdev init\n");
	ret = loongson_fbdev_init(ldev);
	if (ret) {
		DRM_ERROR("loongson_fbdev_init failed\n");
		return ret;
	}
	return 0;
}

/**
 * loongson_modeset_fini --- deinit kernel mode setting
 *
 * @ldev: pointer to loongson_drm_device structure
 *
 * RETURN
 */
void loongson_modeset_fini(struct loongson_drm_device *ldev)
{
}

/*
 * Userspace get information ioctl
 */
/**
 *ioctl_get_fb_vram_base - answer a device specific request.
 *
 * @rdev: drm device pointer
 * @data: request object
 * @filp: drm filp
 *
 * This function is used to pass device specific parameters to the userspace drivers.
 * Returns 0 on success, -EINVAL on failure.
 */
static int ioctl_get_fb_vram_base(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	struct drm_loongson_param *args = data;


	args->value = ldev->fb_vram_base;

	return 0;
}

/**
 *ioctl_get_bo_vram_base - answer a device specific request.
 *
 * @rdev: drm device pointer
 * @data: request object
 * @filp: drm filp
 *
 * This function is used to pass device specific parameters to the userspace drivers.
 * Returns 0 on success, -EINVAL on failure.
 */
static int ioctl_get_bo_vram_base(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct loongson_drm_device *ldev = dev->dev_private;
	struct drm_loongson_param *args = data;
	struct drm_gem_object *obj;
	struct loongson_bo *bo;
	int ret;
	unsigned long gpu_addr;

	obj = drm_gem_object_lookup(file_priv, args->value);
	if (obj == NULL)
		return -ENOENT;
	bo = gem_to_loongson_bo(obj);
        ret = loongson_bo_reserve(bo, false);
        if (ret)
                return ret;

        ret = loongson_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
        if (ret) {
                loongson_bo_unreserve(bo);
                return ret;
        }
        ldev->fb_vram_base = gpu_addr;
        args->value = gpu_addr;
        DRM_DEBUG("loongson_get_bo_vram_base bo=%x, fb_vram_base=%x\n", gpu_addr);
        loongson_bo_unpin(bo);
        loongson_bo_unreserve(bo);
	return 0;
}


static struct drm_ioctl_desc ioctls[DRM_COMMAND_END - DRM_COMMAND_BASE] = {
	DRM_IOCTL_DEF_DRV(LOONGSON_GET_FB_VRAM_BASE, ioctl_get_fb_vram_base, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(LOONGSON_GET_BO_VRAM_BASE, ioctl_get_bo_vram_base, DRM_UNLOCKED|DRM_AUTH),
};

/**
 * loongson_vga_load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
static int loongson_vga_load(struct drm_device *dev, unsigned long flags)
{
	struct loongson_drm_device *ldev;
	int ret,r;

	ldev = devm_kzalloc(dev->dev, sizeof(struct loongson_drm_device), GFP_KERNEL);
	if (ldev == NULL)
		return -ENOMEM;
	dev->dev_private = (void *)ldev;
	ldev->dev = dev;

	ret = loongson_drm_device_init(dev, flags);
	DRM_DEBUG("end loongson drm device init.\n");
	loongson_ttm_init(ldev);

	drm_mode_config_init(dev);
	dev->mode_config.funcs = (void *)&loongson_mode_funcs;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	r = loongson_modeset_init(ldev);
	if (r) {
		dev_err(&dev->pdev->dev, "Fatal error during modeset init: %d\n", r);
	}

	ldev->inited = true;

	/* Make small buffers to store a hardware cursor (double buffered icon updates) */
	loongson_bo_create(dev, roundup(32*32*4, PAGE_SIZE), 0, 0,
					  &ldev->cursor.pixels);
	drm_kms_helper_poll_init(dev);
	return 0;
}

/**
 * loongson_vga_unload--release drm resource
 *
 * @dev: pointer to drm_device
 *
 * RETURN
 *  release success or fail
 */
static int loongson_vga_unload(struct drm_device *dev)
{
        struct loongson_drm_device *ldev = dev->dev_private;

        if (ldev == NULL)
                return 0;

        loongson_modeset_fini(ldev);
        loongson_fbdev_fini(ldev);
        drm_mode_config_cleanup(dev);
        loongson_ttm_fini(ldev);
        dev->dev_private = NULL;
	ldev->inited = false;
	return 0;
}

/**
 *  loongson_dumb_create --dump alloc support
 *
 * @file: pointer to drm_file structure,DRM file private date
 * @dev:  pointer to drm_device structure
 * @args: a dumb scanout buffer param
 *
 * RETURN
 *  dum alloc result
 */
int loongson_dumb_create(struct drm_file *file,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	int ret;
	struct drm_gem_object *gobj;
	u32 handle;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	ret = loongson_gem_create(dev, args->size, false,
			     &gobj);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file, gobj, &handle);
	drm_gem_object_unreference_unlocked(gobj);
	if (ret)
		return ret;

	args->handle = handle;
	return 0;
}

/**
 * loongson_bo_unref -- reduce ttm object buffer refer
 *
 * @bo: the pointer to loongson ttm buffer object
 */
static void loongson_bo_unref(struct loongson_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;

	tbo = &((*bo)->bo);
	ttm_bo_unref(&tbo);
	*bo = NULL;
}

/**
 * loongson_gem_free_object --free gem object
 *
 * @obj: the pointer to drm_gem_object
 */
void loongson_gem_free_object(struct drm_gem_object *obj)
{
	struct loongson_bo *loongson_bo = gem_to_loongson_bo(obj);

	loongson_bo_unref(&loongson_bo);
}

/**
 * loongson_bo_mmap_offset -- Return sanitized offset for user-space mmaps
 *
 * @bo: the pointer to loongson ttm buffer object
 *
 * RETURN
 *  Offset of @node for byte-based addressing
 * 0 if the node does not have an object allocatedS
 */
static inline u64 loongson_bo_mmap_offset(struct loongson_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->bo.vma_node);
}

/**
 * loongson_dumb_mmap_offset --return sanitized offset for userspace mmaps
 *
 * @file: the pointer to drm_file structure,DRM file private date
 * @dev: the pointer to drm_device structrure
 * @handle: user space handle
 * @offset: return value pointer
 *
 * RETRUN
 *  return sainitized offset
 */
int
loongson_dumb_mmap_offset(struct drm_file *file,
		     struct drm_device *dev,
		     uint32_t handle,
		     uint64_t *offset)
{
	struct drm_gem_object *obj;
	struct loongson_bo *bo;

	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return -ENOENT;

	bo = gem_to_loongson_bo(obj);
	*offset = loongson_bo_mmap_offset(bo);

	drm_gem_object_unreference_unlocked(obj);
	return 0;
}

/**
 * loongson_vga_open -Driver callback when a new struct drm_file is opened.
 * Useful for setting up driver-private data structures like buffer allocators,
 *  execution contexts or similar things.
 *
 * @dev DRM device
 * @file DRM file private date
 *
 * RETURN
 * 0 on success, a negative error code on failure, which will be promoted to
 *  userspace as the result of the open() system call.
 */
static int loongson_vga_open(struct drm_device *dev, struct drm_file *file)
{
	file->driver_priv = NULL;

	DRM_DEBUG("open: dev=%p, file=%p", dev, file);

	return 0;
}


static int loongson_vga_firstopen(struct drm_device *dev)
{
	DRM_DEBUG("firstopen: dev=%p", dev);
	return 0;
}

/**
 * lastclose - clean up after all DRM clients have exited
 * @dev: DRM device
 *
 * Take care of cleaning up after all DRM clients have exited.  In the
 * mode setting case, we want to restore the kernel's initial mode (just
 * in case the last client left us in a bad state).
 */
static void loongson_vga_lastclose(struct drm_device *dev)
{
        int i;

        /* we don't support vga-switcheroo.. so just make sure the fbdev
         * mode is active
         */
        struct loongson_drm_device *ldev = dev->dev_private;
        int ret;

        DRM_DEBUG("lastclose: dev=%p", ldev);
}



static void loongson_vga_preclose(struct drm_device *dev, struct drm_file *file)
{
       DRM_DEBUG("loongson_vga_precolse : dev=%p, file=%p", dev, file);
}

/**
 * loongson_vga_postclose
 *
 * @dev: DRM device
 * @file: DRM device private data
 *
 * One of the driver callbacks when a new struct drm_file is closed. Useful for tearing down
 * driver-private data structures allocated in open like buffer allocators,
 * execution contexts or similar things.
 */
static void loongson_vga_postclose(struct drm_device *dev, struct drm_file *file)
{
	DRM_DEBUG("loongson_vga_posecolse : dev=%p, file=%p", dev, file);
}

/**
 * loongson_drm_driver_fops - file operations structure
 *
 * .open: file open
 * .release : file close
 * .unlocked_ioctl:
 * .mmap: map device memory to process address space
 * .poll: device status: POLLIN POLLOUT POLLPRI
 * .compat_ioctl: used in 64 bit
 * .read: sync and read data from device
 */
static const struct file_operations loongson_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = loongson_drm_mmap,
	.poll = drm_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.read = drm_read,
};


/**
 * loongson_vga_drm_driver - DRM device structure
 *
 * .load: driver callback to complete initialization steps after the driver is registered
 * .unload:Reverse the effects of the driver load callback
 * .open:Driver callback when a new struct drm_file is opened
 * .fops:File operations for the DRM device node.
 * .gem_free_object:deconstructor for drm_gem_objects
 * .dumb_create:This creates a new dumb buffer in the driver’s backing storage manager
 *  (GEM, TTM or something else entirely) and returns the resulting buffer handle.
 *  This handle can then be wrapped up into a framebuffer modeset object
 * .dumb_map_offset:Allocate an offset in the drm device node’s address space
 *  to be able to memory map a dumb buffer
 * .dump_destory:This destroys the userspace handle for the given dumb backing storage buffer
 */
static struct drm_driver loongson_vga_drm_driver = {
		.driver_features =
				DRIVER_MODESET | DRIVER_GEM,
		.load = loongson_vga_load,
		.unload = loongson_vga_unload,
		.open = loongson_vga_open,
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
		.set_busid = drm_platform_set_busid,
#else
		.set_busid = drm_pci_set_busid,
#endif
		.fops = &loongson_drm_driver_fops,
		.gem_free_object = loongson_gem_free_object,
		.dumb_create = loongson_dumb_create,
		.dumb_map_offset = loongson_dumb_mmap_offset,
		.dumb_destroy = drm_gem_dumb_destroy,
		.ioctls = ioctls,
		.num_ioctls = DRM_LOONGSON_VGA_NUM_IOCTLS,
		.name = DRIVER_NAME,
		.desc = DRIVER_DESC,
		.date = DRIVER_DATE,
		.major = DRIVER_MAJOR,
		.minor = DRIVER_MINOR,
		.patchlevel = DRIVER_PATCHLEVEL,
};

#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
static int loongson_vga_platform_probe(struct platform_device *pdev)
{
	printk(KERN_ERR "loongson_vga_platform_probe\n");
	return drm_platform_init(&loongson_vga_drm_driver, pdev);

}


static int loongson_vga_platform_remove(struct platform_device *pdev)
{
	return 0;
}
#else
/**
 * loongson_vga_pci_devices  -- pci device id info
 *
 * __u32 vendor, device            Vendor and device ID or PCI_ANY_ID
 * __u32 subvendor, subdevice     Subsystem ID's or PCI_ANY_ID
 * __u32 class, class_mask        (class,subclass,prog-if) triplet
 * kernel_ulong_t driver_data     Data private to the driver
 */
static struct pci_device_id loongson_vga_pci_devices[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC)},
	{0, 0, 0, 0, 0, 0, 0}
};

/**
 * loongson_vga_pci_register -- add pci device
 *
 * @pdev PCI device
 * @ent pci device id
 */
static int loongson_vga_pci_register(struct pci_dev *pdev,
				 const struct pci_device_id *ent)

{
	printk(KERN_ERR "loongson_vga_pci_register\n");
	return drm_get_pci_dev(pdev, ent, &loongson_vga_drm_driver);
}

/**
 * loongson_vga_pci_unregister -- release drm device
 *
 * @pdev PCI device
 */
static void loongson_vga_pci_unregister(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	drm_put_dev(dev);
}
#endif
/*
 * Suspend & resume.
 */
/*
 * loongson_drm_drm_suspend - initiate device suspend
 *
 * @pdev: drm dev pointer
 * @state: suspend state
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int loongson_drm_drm_suspend(struct drm_device *dev, bool suspend,
                                   bool fbcon, bool freeze)
{
        struct loongson_drm_device *ldev;
        struct drm_crtc *crtc;
        struct drm_connector *connector;
        int i, r;

	return 0;
        if (dev == NULL || dev->dev_private == NULL) {
                return -ENODEV;
        }

        ldev = dev->dev_private;

        if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
                return 0;

        drm_kms_helper_poll_disable(dev);

        drm_modeset_lock_all(dev);
        /* turn off display hw */
        list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
                drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
        }
        drm_modeset_unlock_all(dev);
        /* unpin the front buffers and cursors */
        list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
                struct loongson_framebuffer *lfb = to_loongson_framebuffer(crtc->primary->fb);
                struct loongson_bo *lobj;
                /*
                if (lcursor->pixels_current) {
                        struct loongson_bo *lobj = gem_to_loongson_bo(lcursor->pixels_current);
                        r = loongson_bo_reserve(lobj, false);
                        if (r == 0) {
                                loongson_bo_unpin(robj);
                                loongson_bo_unreserve(robj);
                        }
                }
                */
                if (lfb == NULL || lfb->obj == NULL) {
                        continue;
                }
                lobj = gem_to_loongson_bo(lfb->obj);
                /* don't unpin kernel fb objects */
                if (!loongson_fbdev_lobj_is_fb(ldev, lobj)) {
                        r = loongson_bo_reserve(lobj, false);
                        if (r == 0) {
                                loongson_bo_unpin(lobj);
                                loongson_bo_unreserve(lobj);
                        }
                }
        }
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
        pci_save_state(dev->pdev);
        if (freeze) {
                pci_restore_state(dev->pdev);
        } else if (suspend) {
                /* Shut down the device */
                pci_disable_device(dev->pdev);
                pci_set_power_state(dev->pdev, PCI_D3hot);
        }
#endif
        if (fbcon) {
                console_lock();
                loongson_fbdev_set_suspend(ldev, 1);
                console_unlock();
        }
        return 0;
}

/*
 *  * loongson_drm_drm_resume - initiate device suspend
 *
 * @pdev: drm dev pointer
 * @state: suspend state
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */

int loongson_drm_drm_resume(struct drm_device *dev, bool resume, bool fbcon)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM
	struct platform_device *pdev = dev->pdev;
#else
	struct pci_dev *pdev = dev->pdev;
#endif
	struct loongson_drm_device *ldev = dev->dev_private;
	struct drm_crtc *crtc;
        struct drm_connector *connector;
        int r;

	return 0;
        if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
                return 0;

        if (fbcon) {
                console_lock();
        }
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
        if (resume) {
                pci_set_power_state(dev->pdev, PCI_D0);
                pci_restore_state(dev->pdev);
                if (pci_enable_device(dev->pdev)) {
                        if (fbcon)
                                console_unlock();
                        return -1;
                }
        }
#endif
        /* pin cursors */
        list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
                struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);

               /* if (radeon_crtc->cursor_bo) {
                        struct radeon_bo *robj = gem_to_radeon_bo(radeon_crtc->cursor_bo);
                        r = radeon_bo_reserve(robj, false);
                        if (r == 0) {
                                r = radeon_bo_pin_restricted(robj,
                                                             RADEON_GEM_DOMAIN_VRAM,
                                                             ASIC_IS_AVIVO(rdev) ?
                                                             0 : 1 << 27,
                                                             &radeon_crtc->cursor_addr);
                                if (r != 0)
                                        DRM_ERROR("Failed to pin cursor BO (%d)\n", r);
                                radeon_bo_unreserve(robj);
                        }
                }*/
        }

        /* blat the mode back in */
        if (fbcon) {
                drm_helper_resume_force_mode(dev);
                /* turn on display hw */
                drm_modeset_lock_all(dev);
                list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
                    drm_helper_connector_dpms(connector, DRM_MODE_DPMS_ON);
                }
                drm_modeset_unlock_all(dev);
        }

        drm_kms_helper_poll_enable(dev);

        if (fbcon) {
                loongson_fbdev_set_suspend(ldev, 0);
                console_unlock();
        }
	return 0;
}

/**
 * loongson_drm_pm_suspend
 *
 * @dev   pointer to the device
 *
 * Executed before putting the system into a sleep state in which the
 * contents of main memory are preserved.
 */
static int loongson_drm_pm_suspend(struct device *dev)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
        struct pci_dev *pdev = to_pci_dev(dev);
        struct drm_device *drm_dev = pci_get_drvdata(pdev);
	DRM_DEBUG("loongson_drm_pm_suspend");
        return loongson_drm_drm_suspend(drm_dev, true, true, false);
#endif
}

/**
 * loongson_drm_pm_resume
 *
 * @dev pointer to the device
 *
 * Executed after waking the system up from a sleep state in which the
 * contents of main memory were preserved.
 */
static int loongson_drm_pm_resume(struct device *dev)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
        struct pci_dev *pdev = to_pci_dev(dev);
        struct drm_device *drm_dev = pci_get_drvdata(pdev);
	DRM_DEBUG("loongson_drm_pm_resume");
        return loongson_drm_drm_resume(drm_dev, true, true);
#endif
}

/**
 *  loongson_drm_pm_freeze
 *
 *  @dev pointer to device
 *
 *  Hibernation-specific, executed before creating a hibernation image.
 *  Analogous to @suspend(), but it should not enable the device to signal
 *  wakeup events or change its power state.
 */
static int loongson_drm_pm_freeze(struct device *dev)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	DRM_DEBUG("loongson_drm_pm_freeze");
	return loongson_drm_drm_suspend(drm_dev, false, true, true);
#endif
}

/**
 * loongson_drm_pm_draw
 *
 * @dev pointer to device
 *
 * Hibernation-specific, executed after creating a hibernation image OR
 * if the creation of an image has failed.  Also executed after a failing
 * attempt to restore the contents of main memory from such an image.
 * Undo the changes made by the preceding @freeze(), so the device can be
 * operated in the same way as immediately before the call to @freeze().
 */
static int loongson_drm_pm_thaw(struct device *dev)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
        struct pci_dev *pdev = to_pci_dev(dev);
        struct drm_device *drm_dev = pci_get_drvdata(pdev);
	DRM_DEBUG("loongson_drm_pm_thaw");
        return loongson_drm_drm_resume(drm_dev, false, true);
#endif
}

/**
 *  loongson_drm_pm_restore
 *
 * @dev   pointer to device
 *
 *  Hibernation-specific, executed after restoring the contents of main
 *  memory from a hibernation image, analogous to @resume()
 */
static int loongson_drm_pm_restore(struct device *dev)
{
#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#else
        struct pci_dev *pdev = to_pci_dev(dev);
        struct drm_device *drm_dev = pci_get_drvdata(pdev);
	DRM_DEBUG("loongson_drm_pm_restore");
        return loongson_drm_drm_resume(drm_dev, true, true);
#endif
}

#ifdef CONFIG_DRM_LOONGSON_VGA_PLATFORM

#ifdef CONFIG_OF
static struct of_device_id ls_fb_id_table[] = {
	{ .compatible = "loongson,ls-fb", },
};
#endif
static struct platform_driver loongson_vga_platform_driver = {
	.probe	= loongson_vga_platform_probe,
	.remove = loongson_vga_platform_remove,
#if 0
#ifdef	CONFIG_SUSPEND
	.suspend = ls_fb_suspend,
	.resume	 = ls_fb_resume,
#endif
#endif
	.driver = {
		.owner  = THIS_MODULE,
		.name	= "ls-fb",
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_fb_id_table),
#endif
	},
};




/**
 * loongson_vga_pci_init()  -- kernel module init function
 */
static int __init loongson_vga_platform_init(void)
{
	int ret;
	struct pci_dev *pdev = NULL;
	/*if PCIE Graphics card exist,use it as default*/
	pdev = pci_get_device(PCI_VENDOR_ID_ATI, PCI_ANY_ID, NULL);
	if(pdev)
		return 0;
	ret = platform_driver_register(&loongson_vga_platform_driver);
	return ret;
}

/**
 * loongson_vga_pci_exit()  -- kernel module exit function
 */
static void __exit loongson_vga_platform_exit(void)
{
	platform_driver_unregister(&loongson_vga_platform_driver);
}

module_init(loongson_vga_platform_init);
module_exit(loongson_vga_platform_exit);
#else
/*
 * * struct dev_pm_ops - device PM callbacks
 *
 *@suspend:  Executed before putting the system into a sleep state in which the
 *           contents of main memory are preserved.
 *@resume:   Executed after waking the system up from a sleep state in which the
 *           contents of main memory were preserved.
 *@freeze:   Hibernation-specific, executed before creating a hibernation image.
 *           Analogous to @suspend(), but it should not enable the device to signal
 *           wakeup events or change its power state.  The majority of subsystems
 *           (with the notable exception of the PCI bus type) expect the driver-level
 *           @freeze() to save the device settings in memory to be used by @restore()
 *           during the subsequent resume from hibernation.
 *@thaw:     Hibernation-specific, executed after creating a hibernation image OR
 *           if the creation of an image has failed.  Also executed after a failing
 *           attempt to restore the contents of main memory from such an image.
 *           Undo the changes made by the preceding @freeze(), so the device can be
 *           operated in the same way as immediately before the call to @freeze().
 *@poweroff: Hibernation-specific, executed after saving a hibernation image.
 
*           Analogous to @suspend(), but it need not save the device's settings in
 *           memory.
 *@restore:  Hibernation-specific, executed after restoring the contents of main
 *           memory from a hibernation image, analogous to @resume().
 */
static const struct dev_pm_ops loongson_drm_pm_ops = {
	.suspend = loongson_drm_pm_suspend,
	.resume = loongson_drm_pm_resume,
	.freeze = loongson_drm_pm_freeze,
	.thaw = loongson_drm_pm_thaw,
	.poweroff = loongson_drm_pm_freeze,
	.restore = loongson_drm_pm_restore,
};


/**
 * loongson_vga_pci_driver -- pci driver structure
 *
 * .id_table : must be non-NULL for probe to be called
 * .probe: New device inserted
 * .remove: Device removed
 * .resume: Device suspended
 * .suspend: Device woken up
 */
static struct pci_driver loongson_vga_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= loongson_vga_pci_devices,
	.probe		= loongson_vga_pci_register,
	.remove		= loongson_vga_pci_unregister,
	.driver.pm	= &loongson_drm_pm_ops,
};

/**
 * loongson_vga_pci_init()  -- kernel module init function
 */
static int __init loongson_vga_pci_init(void)
{
	int ret;
	struct pci_dev *pdev = NULL;
	/*if PCIE Graphics card exist,use it as default*/
	pdev = pci_get_device(PCI_VENDOR_ID_ATI, PCI_ANY_ID, NULL);
	if(pdev)
			return 0;
	/*if PCIE Graphics card of aspeed ast2400 exist, use it as default*/
	pdev = pci_get_device(0x1a03, PCI_ANY_ID, NULL);
	if(pdev)
			return 0;
	ret = pci_register_driver (&loongson_vga_pci_driver);
	return ret;
}

/**
 * loongson_vga_pci_exit()  -- kernel module exit function
 */
static void __exit loongson_vga_pci_exit(void)
{
	pci_unregister_driver (&loongson_vga_pci_driver);
}

module_init(loongson_vga_pci_init);
module_exit(loongson_vga_pci_exit);
#endif
MODULE_AUTHOR("Zhu Chen <zhuchen@loongson.cn>");
MODULE_DESCRIPTION("Loongson VGA DRM Driver");
MODULE_LICENSE("GPL");
