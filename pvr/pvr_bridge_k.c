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

#include <linux/sched.h>

#include "img_defs.h"
#include "services.h"
#include "pvr_bridge.h"
#include "pvr_bridge_km.h"
#include "perproc.h"
#include "syscommon.h"
#include "pvr_debug.h"
#include "proc.h"
#include "private_data.h"

#include "sgx_bridge.h"

#include "bridged_pvr_bridge.h"

/* Global driver lock protecting all HW and SW state tracking objects. */
DEFINE_MUTEX(gPVRSRVLock);
int pvr_dvfs_active;
DECLARE_WAIT_QUEUE_HEAD(pvr_dvfs_wq);
int pvr_disabled;

/*
 * The pvr_dvfs_* interface is needed to suppress a lockdep warning in
 * the following situation during a clock rate change:
 * On the CLK_PRE_RATE_CHANGE:
 * 1. lock A           <- __blocking_notifier_call_chain:nh->rwsem
 * 2. lock B           <- vdd2_pre_post_func:gPVRSRVLock
 * 3. unlock A
 *
 * On the CLK_POST_RATE_CHANGE/CLK_ABORT_RATE_CHANGE:
 * 4. lock A
 * 5. unlock B
 * 6. unlock A
 *
 * The above has an ABA lock pattern which triggers the warning. This can't
 * lead to a dead lock though since at 3. we always release A, before it's
 * again acquired at 4. To avoid the warning use a wait queue based approach
 * so that we can unlock B before 3.
 *
 * Must be called with gPVRSRVLock held.
 */
void pvr_dvfs_wait_active(void)
{
	while (pvr_dvfs_active) {
		DEFINE_WAIT(pvr_dvfs_wait);
		prepare_to_wait(&pvr_dvfs_wq, &pvr_dvfs_wait,
				TASK_UNINTERRUPTIBLE);
		mutex_unlock(&gPVRSRVLock);
		schedule();
		mutex_lock(&gPVRSRVLock);
		finish_wait(&pvr_dvfs_wq, &pvr_dvfs_wait);
	}
}

#if defined(DEBUG_BRIDGE_KM)
static off_t printLinuxBridgeStats(char *buffer, size_t size, off_t off);
#endif

enum PVRSRV_ERROR LinuxBridgeInit(void)
{
#if defined(DEBUG_BRIDGE_KM)
	{
		int iStatus;
		iStatus =
		    CreateProcReadEntry("bridge_stats", printLinuxBridgeStats);
		if (iStatus != 0)
			return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
#endif
	return CommonBridgeInit();
}

void LinuxBridgeDeInit(void)
{
#if defined(DEBUG_BRIDGE_KM)
	RemoveProcEntry("bridge_stats");
#endif
}

#if defined(DEBUG_BRIDGE_KM)
static off_t printLinuxBridgeStats(char *buffer, size_t count, off_t off)
{
	struct PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psEntry;
	off_t Ret;

	pvr_lock();

	if (!off) {
		if (count < 500) {
			Ret = 0;
			goto unlock_and_return;
		}
		Ret = printAppend(buffer, count, 0,
			"Total ioctl call count = %u\n"
			"Total number of bytes copied via copy_from_user = %u\n"
			"Total number of bytes copied via copy_to_user = %u\n"
			"Total number of bytes copied via copy_*_user = %u\n\n"
			"%-45s | %-40s | %10s | %20s | %10s\n",
		  g_BridgeGlobalStats.ui32IOCTLCount,
		  g_BridgeGlobalStats.ui32TotalCopyFromUserBytes,
		  g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
		  g_BridgeGlobalStats.ui32TotalCopyFromUserBytes +
			  g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
		  "Bridge Name", "Wrapper Function",
		  "Call Count", "copy_from_user Bytes",
		  "copy_to_user Bytes");

		goto unlock_and_return;
	}

	if (off > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT) {
		Ret = END_OF_FILE;
		goto unlock_and_return;
	}

	if (count < 300) {
		Ret = 0;
		goto unlock_and_return;
	}

	psEntry = &g_BridgeDispatchTable[off - 1];
	Ret = printAppend(buffer, count, 0,
			  "%-45s   %-40s   %-10u   %-20u   %-10u\n",
			  psEntry->pszIOCName,
			  psEntry->pszFunctionName,
			  psEntry->ui32CallCount,
			  psEntry->ui32CopyFromUserTotalBytes,
			  psEntry->ui32CopyToUserTotalBytes);

unlock_and_return:
	pvr_unlock();

	return Ret;
}
#endif

long PVRSRV_BridgeDispatchKM(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	u32 ui32BridgeID = PVRSRV_GET_BRIDGE_ID(cmd);
	struct PVRSRV_BRIDGE_PACKAGE __user *psBridgePackageUM =
	    (struct PVRSRV_BRIDGE_PACKAGE __user *)arg;
	struct PVRSRV_BRIDGE_PACKAGE sBridgePackageKM;
	u32 ui32PID = OSGetCurrentProcessIDKM();
	struct PVRSRV_PER_PROCESS_DATA *psPerProc;
	int err = -EFAULT;

	pvr_lock();

	if (pvr_is_disabled()) {
		pvr_unlock();
		return -ENODEV;
	}

	if (!OSAccessOK(PVR_VERIFY_WRITE, psBridgePackageUM,
			sizeof(struct PVRSRV_BRIDGE_PACKAGE))) {
		PVR_DPF(PVR_DBG_ERROR,
			 "%s: Received invalid pointer to function arguments",
			 __func__);

		goto unlock_and_return;
	}

	if (OSCopyFromUser(NULL, &sBridgePackageKM, psBridgePackageUM,
			   sizeof(struct PVRSRV_BRIDGE_PACKAGE)) != PVRSRV_OK)
		goto unlock_and_return;

	if (ui32BridgeID !=
	    PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_CONNECT_SERVICES)) {
		enum PVRSRV_ERROR eError;

		eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
					    (void **)&psPerProc,
					    sBridgePackageKM.hKernelServices,
					    PVRSRV_HANDLE_TYPE_PERPROC_DATA);
		if (eError != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR,
				 "%s: Invalid kernel services handle (%d)",
				 __func__, eError);
			goto unlock_and_return;
		}

		if (psPerProc->ui32PID != ui32PID) {
			PVR_DPF(PVR_DBG_ERROR,
				 "%s: Process %d tried to access data "
				 "belonging to process %d", __func__,
				 ui32PID, psPerProc->ui32PID);
			goto unlock_and_return;
		}
	} else {
		psPerProc = PVRSRVPerProcessData(ui32PID);
		if (psPerProc == NULL) {
			PVR_DPF(PVR_DBG_ERROR, "PVRSRV_BridgeDispatchKM: "
				 "Couldn't create per-process data area");
			goto unlock_and_return;
		}
	}

	sBridgePackageKM.ui32BridgeID = PVRSRV_GET_BRIDGE_ID(
						sBridgePackageKM.ui32BridgeID);

	err = BridgedDispatchKM(filp, psPerProc, &sBridgePackageKM);
	if (err != PVRSRV_OK)
		goto unlock_and_return;

unlock_and_return:
	pvr_unlock();

	return err;
}
