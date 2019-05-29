/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Yann Le Du <ledu@kymasys.com>
 */

#include <linux/export.h>
#include <linux/kvm_host.h>
#include <asm/fpu.h>
#include <asm/tlbflush.h>
#include <asm/time.h>
#include <asm/cacheflush.h>
#include <asm/pgtable-64.h>
#include <asm/tlbex.h>

struct kvm_mips_callbacks *kvm_mips_callbacks;
EXPORT_SYMBOL_GPL(kvm_mips_callbacks);
EXPORT_SYMBOL(_save_fp);
extern void flush_tlb_all(void);
EXPORT_SYMBOL(flush_tlb_all);
extern unsigned int mips_hpt_frequency;
EXPORT_SYMBOL(mips_hpt_frequency);
extern unsigned long ebase;
EXPORT_SYMBOL(ebase);
extern void (*flush_icache_range)(unsigned long start, unsigned long end);
extern void (*local_flush_icache_range)(unsigned long start, unsigned long end);
EXPORT_SYMBOL(local_flush_icache_range);
#ifndef __PAGETABLE_PMD_FOLDED
extern pmd_t invalid_pmd_table[PTRS_PER_PMD];
EXPORT_SYMBOL(invalid_pmd_table);
EXPORT_SYMBOL(pmd_init);
#endif
EXPORT_SYMBOL(pgd_reg);
EXPORT_SYMBOL(build_tlb_write_entry);
EXPORT_SYMBOL(build_get_pmde64);
EXPORT_SYMBOL(build_get_ptep);
EXPORT_SYMBOL(build_update_entries);
 extern u32 tlbmiss_handler_setup_pgd[];
EXPORT_SYMBOL(tlbmiss_handler_setup_pgd);
extern void __kvm_save_fpu(struct kvm_vcpu_arch *vcpu);
extern void __kvm_restore_fpu(struct kvm_vcpu_arch *vcpu);
extern void __kvm_restore_fcsr(struct kvm_vcpu_arch *vcpu);
extern void __kvm_save_fcsr(struct kvm_vcpu_arch *vcpu);
EXPORT_SYMBOL(__kvm_save_fpu);
EXPORT_SYMBOL(__kvm_restore_fpu);
EXPORT_SYMBOL(__kvm_restore_fcsr);
EXPORT_SYMBOL(__kvm_save_fcsr);
