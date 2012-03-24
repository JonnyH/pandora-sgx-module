/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#if !defined(__SOCCONFIG_H__)
#define __SOCCONFIG_H__

#include "syscommon.h"

#define VS_PRODUCT_NAME	"OMAP3630"

#define SYS_SGX_CLOCK_SPEED	330000000
#define SYS_SGX_HWRECOVERY_TIMEOUT_FREQ		(100)	
#define SYS_SGX_PDS_TIMER_FREQ			(1000)	
#define SYS_SGX_ACTIVE_POWER_LATENCY_MS		(1)


#define	SYS_OMAP3430_VDD2_OPP3_SGX_CLOCK_SPEED SYS_SGX_CLOCK_SPEED
#define SYS_OMAP3430_VDD2_OPP2_SGX_CLOCK_SPEED (SYS_SGX_CLOCK_SPEED / 2)

#define SYS_OMAP3430_SGX_REGS_SYS_PHYS_BASE  0x56000000
#define SYS_OMAP3430_SGX_REGS_SIZE           0x10000

#define SYS_OMAP3430_SGX_IRQ				37 

#define SYS_OMAP3430_GP11TIMER_ENABLE_SYS_PHYS_BASE      0x48048038
#define SYS_OMAP3430_GP11TIMER_REGS_SYS_PHYS_BASE	 0x4804803C
#define SYS_OMAP3430_GP11TIMER_TSICR_SYS_PHYS_BASE	 0x48048054

 
#endif	
