/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/checksum.h
 *
 * IP / TCP / UDP packet checksum calculation routines for K-Three.
 */

#ifndef _ASM_K3_CHECKSUM_H
#define _ASM_K3_CHECKSUM_H

#include <linux/types.h>

static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
    const unsigned short *w = iph;
    unsigned int sum = 0;

    while (ihl--) {
        sum += *w++;
        sum += *w++;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (__sum16)(~sum);
}

#endif /* _ASM_K3_CHECKSUM_H */
