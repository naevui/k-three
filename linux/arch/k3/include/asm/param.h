/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/param.h
 *
 * Architecture-specific HZ, EXEC_PAGESIZE and NOGROUP constants for K-Three.
 */

#ifndef _ASM_K3_PARAM_H
#define _ASM_K3_PARAM_H

#define HZ              100
#define USER_HZ         100
#define CLOCKS_PER_SEC  USER_HZ

#define EXEC_PAGESIZE   729     /* 3^6 ternary words per page */

#ifndef NGROUPS
#define NGROUPS         32
#endif

#ifndef NOGROUP
#define NOGROUP         (-1)
#endif

#define MAXHOSTNAMELEN  64

#endif /* _ASM_K3_PARAM_H */
