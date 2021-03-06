#ifndef __ARM_MMU_H
#define __ARM_MMU_H

#ifdef CONFIG_MMU

typedef struct {
#ifdef CONFIG_CPU_HAS_ASID
#if defined(CONFIG_BCM_KF_ARM_BCM963XX) && defined(CONFIG_BCM_KF_ARM_ERRATA_798181)
	atomic64_t	id;
#else
	unsigned int id;
#endif
	raw_spinlock_t id_lock;
#endif
	unsigned int kvm_seq;
} mm_context_t;

#ifdef CONFIG_CPU_HAS_ASID
#if defined(CONFIG_BCM_KF_ARM_BCM963XX) && defined(CONFIG_BCM_KF_ARM_ERRATA_798181)
#define ASID_BITS	8
#define ASID_MASK	((~0ULL) << ASID_BITS)
#define ASID(mm)	((mm)->context.id.counter & ~ASID_MASK)
#else
#define ASID(mm)	((mm)->context.id & 255)
#endif

/* init_mm.context.id_lock should be initialized. */
#define INIT_MM_CONTEXT(name)                                                 \
	.context.id_lock    = __RAW_SPIN_LOCK_UNLOCKED(name.context.id_lock),
#else
#define ASID(mm)	(0)
#endif

#else

/*
 * From nommu.h:
 *  Copyright (C) 2002, David McCullough <davidm@snapgear.com>
 *  modified for 2.6 by Hyok S. Choi <hyok.choi@samsung.com>
 */
typedef struct {
	unsigned long		end_brk;
} mm_context_t;

#endif

#if !defined(CONFIG_BCM_KF_ANDROID) || !defined(CONFIG_BCM_ANDROID)
/*
 * switch_mm() may do a full cache flush over the context switch,
 * so enable interrupts over the context switch to avoid high
 * latency.
 */
#define __ARCH_WANT_INTERRUPTS_ON_CTXSW

#endif
#endif
