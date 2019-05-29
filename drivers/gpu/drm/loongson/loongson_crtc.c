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

#include "loongson_drv.h"
#ifdef CONFIG_CPU_LOONGSON2K
#include "ls2k.h"
#else
#include <loongson-pch.h>
#endif
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>

static u32 output_mode;

DEFINE_SPINLOCK(loongson_crtc_lock);

/**
 * loongson_crtc_load_lut
 *
 * @ctrc: point to a drm_crtc srtucture
 *
 * Load a LUT
 */
static void loongson_crtc_load_lut(struct drm_crtc *crtc)
{

}



/*
   This is how the framebuffer base address is stored in g200 cards:
   * Assume @offset is the gpu_addr variable of the framebuffer object
   * Then addr is the number of _pixels_ (not bytes) from the start of
     VRAM to the first pixel we want to display. (divided by 2 for 32bit
     framebuffers)
   * addr is stored in the CRTCEXT0, CRTCC and CRTCD registers
   addr<20> -> CRTCEXT0<6>
   addr<19-16> -> CRTCEXT0<3-0>
   addr<15-8> -> CRTCC<7-0>
   addr<7-0> -> CRTCD<7-0>
   CRTCEXT0 has to be programmed last to trigger an update and make the
   new addr variable take effect.
 */
static void loongson_set_start_address(struct drm_crtc *crtc, unsigned offset)
{
	struct loongson_drm_device *ldev;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	unsigned int crtc_id;
	u32 addr;
	int count;
	unsigned long base;

	crtc_id = loongson_crtc->crtc_id;
	DRM_DEBUG("crtc_gpu_addr = 0x%x\n",offset);
	ldev = crtc->dev->dev_private;
	base = (unsigned long)(ldev->rmmio);
	DRM_DEBUG("base=0x%x\n",base);
	if(crtc_id == 0){
		ls_writel(offset,base +	LS_FB_ADDR0_DVO0_REG);
		ls_writel(offset,base +	LS_FB_ADDR1_DVO0_REG);
	}else{
		ls_writel(offset,base +	LS_FB_ADDR0_DVO1_REG);
		ls_writel(offset,base +	LS_FB_ADDR1_DVO1_REG);
	}
}


/**
 * loongson_crtc_do_set_base
 *
 * @crtc: point to a drm_crtc structure
 * @fb: point to a drm_framebuffer structure
 * @x: x position on screen
 * @y: y position on screen
 * @atomic: int variable
 *
 * Ast is different - we will force move buffers out of VRAM
 */
static int loongson_crtc_do_set_base(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				int x, int y, int atomic)
{
	struct loongson_drm_device *ldev = crtc->dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	struct drm_gem_object *obj;
	struct loongson_framebuffer *loongson_fb;
	struct loongson_bo *bo;
	struct drm_crtc *crtci;
	struct drm_device *dev = crtc->dev;
	int ret;
	unsigned int depth;
	u64 gpu_addr;
	unsigned long flags,base;
	unsigned int crtc_id;
	unsigned int crtc_address;
	unsigned int width,pitch;
	unsigned int crtc_count;

	crtc_id = loongson_crtc->crtc_id;
	base = (unsigned long)(ldev->rmmio);
	ldev = crtc->dev->dev_private;
	width = crtc->primary->fb->width;
	depth = crtc->primary->fb->bits_per_pixel;

	pitch = crtc->primary->fb->pitches[0];
	/* push the previous fb to system ram */
	if (!atomic && fb) {
		loongson_fb = to_loongson_framebuffer(fb);
		obj = loongson_fb->obj;
		bo = gem_to_loongson_bo(obj);
		ret = loongson_bo_reserve(bo, false);
		if (ret)
			return ret;
		loongson_bo_unpin(bo);
		loongson_bo_unreserve(bo);
	}

	DRM_DEBUG ("crtc width = %d,height = %d\n",width,crtc->primary->fb->height);
	DRM_DEBUG ("crtc pitches[0]=%d\n",crtc->primary->fb->pitches[0]);
	loongson_fb = to_loongson_framebuffer(crtc->primary->fb);

	crtc_count = 0;
	list_for_each_entry(crtci, &dev->mode_config.crtc_list, head)
		if (crtci->enabled) {
			crtc_count++;
		}

	if(ldev->num_crtc < 2 || crtc_count < 2) {
		DRM_DEBUG("not use clone mode\n");
		ldev->clone_mode = false;
        }else if(ldev->mode_info[0].connector->base.status == connector_status_connected
		&& ldev->mode_info[1].connector->base.status == connector_status_connected
		&& loongson_fb->base.width == crtc->mode.hdisplay && loongson_fb->base.height == crtc->mode.vdisplay && x == 0 && y == 0){
		DRM_DEBUG("use clone mode\n");
		ldev->clone_mode = true;
	}else{
		DRM_DEBUG("not use clone mode\n");
		ldev->clone_mode = false;
	}

	obj = loongson_fb->obj;
	bo = gem_to_loongson_bo(obj);

	ret = loongson_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = loongson_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	if (ret) {
		loongson_bo_unreserve(bo);
		return ret;
	}

	DRM_DEBUG("gpu_addr = 0x%x\n",gpu_addr);
	ldev-> fb_vram_base = gpu_addr;
	if (&ldev->lfbdev->lfb == loongson_fb) {
		/* if pushing console in kmap it */
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret)
			DRM_ERROR("failed to kmap fbcon\n");

	}
	loongson_bo_unreserve(bo);
	if(crtc_id == 0){
		switch (depth) {
		case 32:
		case 24:
			ls_writel(0x00100104,base + LS_FB_CFG_DVO0_REG);
			ls_writel(pitch,base + LS_FB_STRI_DVO0_REG);
			crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 4;
			break;
		case 16:
			ls_writel(0x00100103,base + LS_FB_CFG_DVO0_REG);
			ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO0_REG);
			crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
			break;
		case 15:
			ls_writel(0x00100102,base + LS_FB_CFG_DVO0_REG);
			ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO0_REG);
			crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
			break;
		case 12:
			ls_writel(0x00100101,base + LS_FB_CFG_DVO0_REG);
			ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO0_REG);
			crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
			break;
		default:
			ls_writel(0x00100104,base + LS_FB_CFG_DVO0_REG);
			ls_writel(crtc->primary->fb->pitches[0],base + LS_FB_STRI_DVO0_REG);
			crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 4;
			break;
		}

	}else{
		if(ldev->clone_mode == false){
			switch (depth) {
			case 32:
			case 24:
				ls_writel(0x00100104,base + LS_FB_CFG_DVO1_REG);
				ls_writel(crtc->primary->fb->pitches[0],base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 4;
				break;
			case 16:
				ls_writel(0x00100103,base + LS_FB_CFG_DVO1_REG);
				ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
				break;
			case 15:
				ls_writel(0x00100102,base + LS_FB_CFG_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
				ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO1_REG);
				break;
			case 12:
				ls_writel(0x00100101,base + LS_FB_CFG_DVO1_REG);
				ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
				break;
			default:
				ls_writel(0x00100104,base + LS_FB_CFG_DVO1_REG);
				ls_writel(crtc->primary->fb->pitches[0],base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 4;
				break;
			}
		}else{
			switch (depth) {
			case 32:
			case 24:
				ls_writel(0x00100304,base + LS_FB_CFG_DVO1_REG);
				ls_writel(crtc->primary->fb->pitches[0],base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 4;
				break;
			case 16:
				ls_writel(0x00100303,base + LS_FB_CFG_DVO1_REG);
				ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
				break;
			case 15:
				ls_writel(0x00100302,base + LS_FB_CFG_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
				ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO1_REG);
				break;
			case 12:
				ls_writel(0x00100301,base + LS_FB_CFG_DVO1_REG);
				ls_writel((width * 2 + 255) & ~255,base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 2;
				break;
			default:
				ls_writel(0x00100304,base + LS_FB_CFG_DVO1_REG);
				ls_writel(crtc->primary->fb->pitches[0],base + LS_FB_STRI_DVO1_REG);
				crtc_address = (u32)gpu_addr + y * pitch + ALIGN(x,64) * 4;
				break;
			}

		}
	}
	loongson_set_start_address(crtc, (u32)crtc_address);
	ldev->cursor_crtc_id = ldev->num_crtc;
	ldev->cursor_showed = false;

	return 0;
}


/**
 * loongson_crtc_mode_set_base
 *
 * @crtc: point to a drm_crtc structure
 * @old_fb: point to a drm_crtc structure
 *
 * Transfer the function which is loongson_crtc_do_set_base,and used by
 * the legacy CRTC helpers to set a new framebuffer and scanout position
 */
static int loongson_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				  struct drm_framebuffer *old_fb)
{
	return loongson_crtc_do_set_base(crtc, old_fb, x, y, 0);
}


/**
 * config_pll
 *
 * @pll_base: represent a long type
 * @pll_cfg: point to the pix_pll srtucture
 *
 * Config pll apply to 7a
 */
static void config_pll(unsigned long pll_base, struct pix_pll *pll_cfg)
{
	unsigned long val;

#ifdef CONFIG_CPU_LOONGSON2K
        /* set sel_pll_out0 0 */
	val = ls_readq(pll_base);
	val &= ~(1UL << 0);
	ls_writeq(val, pll_base);

	/* pll_pd 1 */
	val = ls_readq(pll_base);
	val |= (1UL << 19);
	ls_writeq(val, pll_base);

	/* set_pll_param 0 */
	val = ls_readq(pll_base);
	val &= ~(1UL << 2);
	ls_writeq(val, pll_base);

	/* set new div ref, loopc, div out */
	/* clear old value first*/
	val = (1 << 7) | (1L << 42) | (3 << 10) |
		((unsigned long)(pll_cfg->l1_loopc) << 32) |
		((unsigned long)(pll_cfg->l1_frefc) << 26);
	ls_writeq(val, pll_base);
	ls_writeq(pll_cfg->l2_div, pll_base + 8);

	/* set_pll_param 1 */
	val = ls_readq(pll_base);
	val |= (1UL << 2);
	ls_writeq(val, pll_base);

	/* pll_pd 0 */
	val = ls_readq(pll_base);
	val &= ~(1UL << 19);
	ls_writeq(val, pll_base);

	/* wait pll lock */
	while(!(ls_readl(pll_base) & 0x10000));
	/* set sel_pll_out0 1 */
	val = ls_readq(pll_base);
	val |= (1UL << 0);
	ls_writeq(val, pll_base);
#else
	/* set sel_pll_out0 0 */
	val = ls_readq(pll_base + LO_OFF);
	val &= ~(1UL << 40);
	ls_writeq(val, pll_base + LO_OFF);
	/* pll_pd 1 */
	val = ls_readq(pll_base + LO_OFF);
	val |= (1UL << 45);
	ls_writeq(val, pll_base + LO_OFF);
	/* set_pll_param 0 */
	val = ls_readq(pll_base + LO_OFF);
	val &= ~(1UL << 43);
	ls_writeq(val, pll_base + LO_OFF);
	/* div ref, loopc, div out */
	val = ls_readq(pll_base + LO_OFF);

	/* clear old value */
	val &= ~(0x7fUL << 32);
	val &= ~(0x1ffUL << 21);
	val &= ~(0x7fUL);

	/* config new value */
	val |= ((unsigned long)(pll_cfg->l1_frefc) << 32) | ((unsigned long)(pll_cfg->l1_loopc) << 21) |
		((unsigned long)(pll_cfg->l2_div) << 0);
	ls_writeq(val, pll_base + LO_OFF);
	/* set_pll_param 1 */
	val = ls_readq(pll_base + LO_OFF);
	val |= (1UL << 43);
	ls_writeq(val, pll_base + LO_OFF);
	/* pll_pd 0 */
	val = ls_readq(pll_base + LO_OFF);
	val &= ~(1UL << 45);
	ls_writeq(val, pll_base + LO_OFF);
	/* set sel_pll_out0 1 */
	val = ls_readq(pll_base + LO_OFF);
	val |= (1UL << 40);
	ls_writeq(val, pll_base + LO_OFF);
#endif
}


/**
 * cal_freq
 *
 * @pixclock_khz: unsigned int
 * @pll_config: point to the pix_pll structure
 *
 * Calculate frequency
 */
static unsigned int cal_freq(unsigned int pixclock_khz, struct pix_pll * pll_config)
{
	unsigned int pstdiv, loopc, frefc;
	unsigned long a, b, c;
	unsigned long min = 1000;

	for (pstdiv = 1; pstdiv < 64; pstdiv++) {
		a = (unsigned long)pixclock_khz * pstdiv;
		for (frefc = 3; frefc < 6; frefc++) {
			for (loopc = 24; loopc < 161; loopc++) {

				if ((loopc < 12 * frefc) ||
						(loopc > 32 * frefc))
					continue;

				b = 100000L * loopc / frefc;
				c = (a > b) ? (a - b) : (b - a);
				if (c < min) {
					pll_config->l2_div = pstdiv;
					pll_config->l1_loopc = loopc;
					pll_config->l1_frefc = frefc;

					return 1;
				}
			}
		}
	}
	return 0;
}


/**
 * loongson_crtc_mode_set
 *
 * @crtc: point to the drm_crtc structure
 * @mode: represent a display mode
 * @adjusted_mode: point to the drm_display_mode structure
 * @old_fb: point to the drm_framebuffer structure
 *
 * Used by the legacy CRTC helpers to set a new mode
 */
static int loongson_crtc_mode_set(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode,
				int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct loongson_drm_device *ldev = dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	unsigned int pix_freq;
	unsigned int depth;
	unsigned int hr, hss, hse, hfl;
	unsigned int vr, vss, vse, vfl;
	int ret;
	struct pix_pll pll_cfg;
	unsigned long flags,base;
	unsigned int crtc_id;


	crtc_id = loongson_crtc->crtc_id;
	base = (unsigned long)(ldev->rmmio);

	hr	= mode->hdisplay;
	hss	= mode->hsync_start;
	hse	= mode->hsync_end;
	hfl	= mode->htotal;

	vr	= mode->vdisplay;
	vss	= mode->vsync_start;
	vse	= mode->vsync_end;
	vfl	= mode->vtotal;
	depth = crtc->primary->fb->bits_per_pixel;
	pix_freq = mode->clock;
	DRM_DEBUG("crtc_id = %d,hr = %d,hss = %d,hse = %d,hfl = %d,vr = %d,vss = %d,vse = %d,vfl = %d,depth = %d,pix_freq = %d,x = %d,y = %d\n",crtc_id,hr,hss,hse,hfl,vr,vss,vse,vfl,depth,pix_freq,x,y);
	DRM_DEBUG("fb width = %d,height = %d\n",crtc->primary->fb->width,crtc->primary->fb->height);

	loongson_crtc->width = hr;
	loongson_crtc->height = vr;
	ret = cal_freq(pix_freq, &pll_cfg);

	if(crtc_id == 0){
		if (ret) {
			DRM_DEBUG("cal_freq OK!\n");
			config_pll(LS_PIX0_PLL, &pll_cfg);
		}

		loongson_crtc_do_set_base(crtc, old_fb, x, y, 0);

	/* these 4 lines cause out of range, because
	 * the hfl hss vfl vss are different with PMON vgamode cfg.
	 * So the refresh freq in kernel and refresh freq in PMON are different.
	 * */
		ls_writel(0,base + LS_FB_DITCFG_DVO0_REG);
		ls_writel(0,base + LS_FB_DITTAB_LO_DVO0_REG);
		ls_writel(0,base + LS_FB_DITTAB_HI_DVO0_REG);
		ls_writel(0x80001311,base + LS_FB_PANCFG_DVO0_REG);
		ls_writel(0x00000000,base + LS_FB_PANTIM_DVO0_REG);


		ls_writel((hfl << 16) | hr,base + LS_FB_HDISPLAY_DVO0_REG);
		ls_writel(0x40000000 | (hse << 16) | hss,base + LS_FB_HSYNC_DVO0_REG);
		ls_writel((vfl << 16) | vr,base + LS_FB_VDISPLAY_DVO0_REG);
		ls_writel(0x40000000 | (vse << 16) | vss,base + LS_FB_VSYNC_DVO0_REG);

	}else{
		if (ret) {
			DRM_DEBUG("cal_freq OK!\n");
			config_pll(LS_PIX1_PLL, &pll_cfg);
		}

		loongson_crtc_do_set_base(crtc, old_fb, x, y, 0);

	/* these 4 lines cause out of range, because
	 * the hfl hss vfl vss are different with PMON vgamode cfg.
	 * So the refresh freq in kernel and refresh freq in PMON are different.
	 * */
		ls_writel(0,base + LS_FB_DITCFG_DVO1_REG);
		ls_writel(0,base + LS_FB_DITTAB_LO_DVO1_REG);
		ls_writel(0,base + LS_FB_DITTAB_HI_DVO1_REG);
		ls_writel(0x80001311,base + LS_FB_PANCFG_DVO1_REG);
		ls_writel(0x00000000,base + LS_FB_PANTIM_DVO1_REG);


		ls_writel((hfl << 16) | hr,base + LS_FB_HDISPLAY_DVO1_REG);
		ls_writel(0x40000000 | (hse << 16) | hss,base + LS_FB_HSYNC_DVO1_REG);
		ls_writel((vfl << 16) | vr,base + LS_FB_VDISPLAY_DVO1_REG);
		ls_writel(0x40000000 | (vse << 16) | vss,base + LS_FB_VSYNC_DVO1_REG);

	}
	ldev->cursor_crtc_id = ldev->num_crtc;
	ldev->cursor_showed = false;
	return 0;
}


/**
 * loongson_crtc_dpms
 *
 * @crtc: point to the drm_crtc structure
 * @mode: represent mode
 *
 * According to mode,represent the power levels on the CRTC
 */
static void loongson_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct loongson_drm_device *ldev = dev->dev_private;
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);
	unsigned long base;
	unsigned int crtc_id,val;

	crtc_id = loongson_crtc->crtc_id;
	if (ldev->inited == false || ldev->clone_mode == true) {
		return ;
	}
	base = (unsigned long)(ldev->rmmio);
	switch(mode) {
	case DRM_MODE_DPMS_ON:
		if (crtc_id) {
			val = ls_readq(base + LS_FB_CFG_DVO1_REG);
			val |=(1 << 8);
			ls_writeq(val, base + LS_FB_CFG_DVO1_REG);
		} else {
			val = ls_readq(base + LS_FB_CFG_DVO0_REG);
			val |= (1 << 8);
			ls_writeq(val, base + LS_FB_CFG_DVO0_REG);
		}
		loongson_crtc->enabled = true;
		break;
	case DRM_MODE_DPMS_OFF:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		if (crtc_id) {
			val = ls_readq(base + LS_FB_CFG_DVO1_REG);
			val &=~(1 << 8);
			ls_writeq(val, base + LS_FB_CFG_DVO1_REG);
		} else {
			val = ls_readq(base + LS_FB_CFG_DVO0_REG);
			val &=~(1 << 8);
			ls_writeq(val, base + LS_FB_CFG_DVO0_REG);
		}
		loongson_crtc->enabled = false;
		break;
	}
}


/**
 * loongson_crtc_prepare
 *
 * @crtc: point to a drm_crtc structure
 *
 * This is called before a mode is programmed. A typical use might be to
 * enable DPMS during the programming to avoid seeing intermediate stages
 */
static void loongson_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc *crtci;
	/*
	 * The hardware wedges sometimes if you reconfigure one CRTC
	 * whilst another is running
	 */
	DRM_DEBUG("loongson_crtc_prepare\n");
	list_for_each_entry(crtci, &dev->mode_config.crtc_list, head)
		if (crtci->enabled) {
			loongson_crtc_dpms(crtci, DRM_MODE_DPMS_ON);
		} else {
			loongson_crtc_dpms(crtci, DRM_MODE_DPMS_OFF);
		}
}



/**
 * loongson_crtc_commit
 *
 * @crtc: point to the drm_crtc structure
 *
 * Commit the new mode on the CRTC after a modeset.This is called after
 * a mode is programmed. It should reverse anything done by the prepare function
 */
static void loongson_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_crtc *crtci;

	DRM_DEBUG("loongson_crtc_commit\n");
	list_for_each_entry(crtci, &dev->mode_config.crtc_list, head) {
		if (crtci->enabled)
			loongson_crtc_dpms(crtci, DRM_MODE_DPMS_ON);
        }
}


/**
 * loongson_crtc_destroy
 *
 * @crtc: pointer to a drm_crtc struct
 *
 * Destory the CRTC when not needed anymore,and transfer the drm_crtc_cleanup
 * function,the function drm_crtc_cleanup() cleans up @crtc and removes it
 * from the DRM mode setting core.Note that the function drm_crtc_cleanup()
 * does not free the structure itself.
 */
static void loongson_crtc_destroy(struct drm_crtc *crtc)
{
	struct loongson_crtc *loongson_crtc = to_loongson_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(loongson_crtc);
}


/**
 * loongosn_crtc_disable
 *
 * @crtc: DRM CRTC
 *
 * Used to shut down CRTC
 */
static void loongson_crtc_disable(struct drm_crtc *crtc)
{
	int ret;
	DRM_DEBUG_KMS("\n");
	loongson_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	if (crtc->primary->fb) {
		struct loongson_framebuffer *mga_fb = to_loongson_framebuffer(crtc->primary->fb);
		struct drm_gem_object *obj = mga_fb->obj;
		struct loongson_bo *bo = gem_to_loongson_bo(obj);
		ret = loongson_bo_reserve(bo, false);
		if (ret)
			return;
		loongson_bo_unpin(bo);
		loongson_bo_unreserve(bo);
	}
	crtc->primary->fb = NULL;
}



/**
 * These provide the minimum set of functions required to handle a CRTC
 * Each driver is responsible for filling out this structure at startup time
 *
 * The drm_crtc_funcs structure is the central CRTC management structure
 * in the DRM. Each CRTC controls one or more connectors
 */
static const struct drm_crtc_funcs loongson_crtc_funcs = {
	.cursor_set2 = loongson_crtc_cursor_set2,
	.cursor_move = loongson_crtc_cursor_move,
	.set_config = drm_crtc_helper_set_config,
	.destroy = loongson_crtc_destroy,
};


/**
 * These provide the minimum set of functions required to handle a CRTC
 *
 * The drm_crtc_helper_funcs is a helper operations for CRTC
 */
static const struct drm_crtc_helper_funcs loongson_helper_funcs = {
	.disable = loongson_crtc_disable,
	.dpms = loongson_crtc_dpms,
	.mode_set = loongson_crtc_mode_set,
	.mode_set_base = loongson_crtc_mode_set_base,
	.prepare = loongson_crtc_prepare,
	.commit = loongson_crtc_commit,
	.load_lut = loongson_crtc_load_lut,
};


/**
 * loongosn_crtc_init
 *
 * @ldev: point to the loongson_drm_device structure
 *
 * Init CRTC
 */
void loongson_crtc_init(struct loongson_drm_device *ldev)
{
	struct loongson_crtc *ls_crtc;
	int i;

	for(i=0;i<ldev->vbios->crtc_num;i++){
		ls_crtc = kzalloc(sizeof(struct loongson_crtc) +
				      (1 * sizeof(struct drm_connector *)),
				      GFP_KERNEL);

		if (ls_crtc == NULL)
			return;
		ls_crtc->crtc_id = ldev->crtc_vbios[i]->crtc_id;
		drm_crtc_init(ldev->dev, &ls_crtc->base, &loongson_crtc_funcs);

		ldev->mode_info[i].crtc = ls_crtc;

		drm_crtc_helper_add(&ls_crtc->base, &loongson_helper_funcs);
	}
}


