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

#include <stddef.h>

#include "sgxdefs.h"
#include "services_headers.h"
#include "sgxapi_km.h"
#include "sgxinfokm.h"
#include "sgxutils.h"
#include "pdump_km.h"


typedef enum _PVR_DEVICE_POWER_STATE_
{
	
	PVR_DEVICE_POWER_STATE_ON		= 0,
	PVR_DEVICE_POWER_STATE_IDLE		= 1,
	PVR_DEVICE_POWER_STATE_OFF		= 2,

	PVR_DEVICE_POWER_STATE_FORCE_I32 = 0x7fffffff

} PVR_DEVICE_POWER_STATE, *PPVR_DEVICE_POWER_STATE;	


static PVR_DEVICE_POWER_STATE MapDevicePowerState(PVR_POWER_STATE	ePowerState)
{
	PVR_DEVICE_POWER_STATE eDevicePowerState;
	
	switch (ePowerState)
	{
		case PVRSRV_POWER_STATE_D0:
		{
			eDevicePowerState = PVR_DEVICE_POWER_STATE_ON;
			break;
		}
		case PVRSRV_POWER_STATE_D3:
		{
			eDevicePowerState = PVR_DEVICE_POWER_STATE_OFF;
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR,"MapDevicePowerState: Invalid state: %ld", ePowerState));
			eDevicePowerState = PVR_DEVICE_POWER_STATE_FORCE_I32;
			PVR_DBG_BREAK;
		}
	}

	return eDevicePowerState;
}


static IMG_VOID SGXGetTimingInfo(PVRSRV_DEVICE_NODE	*psDeviceNode)
{
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
#if defined(SGX_DYNAMIC_TIMING_INFO)
	SGX_TIMING_INFORMATION	sSGXTimingInfo = {0};
#else
	SGX_DEVICE_MAP		*psSGXDeviceMap;
#endif
	IMG_UINT32		ui32ActivePowManSampleRate;
	SGX_TIMING_INFORMATION	*psSGXTimingInfo;
	

#if defined(SGX_DYNAMIC_TIMING_INFO)
	psSGXTimingInfo = &sSGXTimingInfo;
	SysGetSGXTimingInformation(psSGXTimingInfo);
#else
	SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
						  (IMG_VOID**)&psSGXDeviceMap);
	psSGXTimingInfo = &psSGXDeviceMap->sTimingInfo;
#endif

#if defined(SUPPORT_HW_RECOVERY)
	{
		PVRSRV_ERROR			eError;
		IMG_UINT32	ui32OlduKernelFreq;
		
		if (psDevInfo->hTimer != IMG_NULL)
		{
			ui32OlduKernelFreq = psDevInfo->ui32CoreClockSpeed / psDevInfo->ui32uKernelTimerClock;
			if (ui32OlduKernelFreq != psSGXTimingInfo->ui32uKernelFreq)
			{
				

				eError = OSRemoveTimer(psDevInfo->hTimer);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,"SGXGetTimingInfo: Failed to remove timer"));
				}
				psDevInfo->hTimer = IMG_NULL;
			}
		}
		
		if (psDevInfo->hTimer == IMG_NULL)
		{
			


			psDevInfo->hTimer = OSAddTimer(SGXOSTimer, psDeviceNode,
										   1000 * 50 / psSGXTimingInfo->ui32uKernelFreq);
			if(psDevInfo->hTimer == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXGetTimingInfo : Failed to register timer callback function"));
			}
		}

		psDevInfo->psSGXHostCtl->ui32HWRecoverySampleRate =
			psSGXTimingInfo->ui32uKernelFreq / psSGXTimingInfo->ui32HWRecoveryFreq;
	}
#endif 
	
	
	psDevInfo->ui32CoreClockSpeed = psSGXTimingInfo->ui32CoreClockSpeed;
	psDevInfo->ui32uKernelTimerClock = psSGXTimingInfo->ui32CoreClockSpeed / psSGXTimingInfo->ui32uKernelFreq;

	ui32ActivePowManSampleRate =
		psSGXTimingInfo->ui32uKernelFreq * psSGXTimingInfo->ui32ActivePowManLatencyms / 1000;
	






	
	ui32ActivePowManSampleRate += 1;
	
	psDevInfo->psSGXHostCtl->ui32ActivePowManSampleRate = ui32ActivePowManSampleRate;
}


IMG_VOID SGXStartTimer(PVRSRV_SGXDEV_INFO	*psDevInfo,
					   IMG_BOOL				bStartOSTimer)
{
	IMG_UINT32		ui32RegVal;

	#if !defined(SUPPORT_HW_RECOVERY)
	PVR_UNREFERENCED_PARAMETER(bStartOSTimer);
	#endif 

	

	ui32RegVal = EUR_CR_EVENT_TIMER_ENABLE_MASK | psDevInfo->ui32uKernelTimerClock;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_TIMER, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_EVENT_TIMER, ui32RegVal, PDUMP_FLAGS_CONTINUOUS);

	#if defined(SUPPORT_HW_RECOVERY)
	if (bStartOSTimer)
	{
		PVRSRV_ERROR	eError;
		
		eError = OSEnableTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXStartTimer : Failed to enable host timer"));
		}
	}
	#endif 
}


static IMG_VOID SGXPollForClockGating (PVRSRV_SGXDEV_INFO	*psDevInfo, 
									   IMG_UINT32			ui32Register,
									   IMG_UINT32			ui32RegisterValue,
									   IMG_CHAR				*pszComment)
{
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(ui32Register);
	PVR_UNREFERENCED_PARAMETER(ui32RegisterValue);
	PVR_UNREFERENCED_PARAMETER(pszComment);

	#if !defined(NO_HARDWARE)
	if (psDevInfo != IMG_NULL)
	{
		if (PollForValueKM((IMG_UINT32 *)psDevInfo->pvRegsBaseKM + (ui32Register >> 2),
							0,
							ui32RegisterValue,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							WAIT_TRY_COUNT) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: %s failed.", pszComment));
		}
	}
	#endif 
	
	PDUMPCOMMENT(pszComment);
	PDUMPREGPOL(ui32Register, 0, ui32RegisterValue);
}


static PVRSRV_ERROR SGXPrePowerState (IMG_HANDLE				hDevHandle, 
									  PVR_DEVICE_POWER_STATE	eNewPowerState, 
									  PVR_DEVICE_POWER_STATE	eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
		(eNewPowerState != PVR_DEVICE_POWER_STATE_ON))
	{
		PVRSRV_ERROR		eError;
		PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
		PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
		IMG_UINT32			ui32PowerCmd, ui32CompleteStatus;
		SGXMKIF_COMMAND		sCommand = {0};
	
		#if defined(SUPPORT_HW_RECOVERY)
		
		eError = OSDisableTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: Failed to disable timer"));
			return eError;
		}
		#endif 

		if (eNewPowerState == PVR_DEVICE_POWER_STATE_OFF)
		{
			
			ui32PowerCmd = PVRSRV_POWERCMD_POWEROFF;
			ui32CompleteStatus = PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE;
			PDUMPCOMMENT("SGX power off request");
		}
		else
		{
			
			ui32PowerCmd = PVRSRV_POWERCMD_IDLE;
			ui32CompleteStatus = PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE;			
			PDUMPCOMMENT("SGX idle request");
		}

		sCommand.ui32Data[0] = PVRSRV_CCBFLAGS_POWERCMD;
		sCommand.ui32Data[1] = ui32PowerCmd;
		
		eError = SGXScheduleCCBCommand(psDevInfo, SGXMKIF_COMMAND_EDM_KICK, &sCommand, KERNEL_ID, 0);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: Failed to submit power down command"));
			return eError;
		}

		
		#if !defined(NO_HARDWARE)
		if (PollForValueKM(&psDevInfo->psSGXHostCtl->ui32PowerStatus,
							ui32CompleteStatus,
							ui32CompleteStatus,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							WAIT_TRY_COUNT) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: Wait for SGX ukernel power transition failed."));
			PVR_DBG_BREAK;
		}
		#endif 

		#if defined(PDUMP)
		PDUMPCOMMENT("TA/3D CCB Control - Wait for power event on uKernel.");
		PDUMPMEMPOL(psDevInfo->psKernelSGXHostCtlMemInfo,
					offsetof(SGXMKIF_HOST_CTL, ui32PowerStatus),
					ui32CompleteStatus,
					ui32CompleteStatus,
					PDUMP_POLL_OPERATOR_EQUAL,
					IMG_FALSE, IMG_FALSE,
					MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif 

		
		SGXPollForClockGating(psDevInfo,
							  psDevInfo->ui32ClkGateStatusReg,
							  psDevInfo->ui32ClkGateStatusMask,
							  "Wait for SGX clock gating");

		#if defined(SGX_FEATURE_MP)
		
		SGXPollForClockGating(psDevInfo,
							  psDevInfo->ui32MasterClkGateStatusReg,
							  psDevInfo->ui32MasterClkGateStatusMask,
							  "Wait for SGX master clock gating");
		#endif 
				
		if (eNewPowerState == PVR_DEVICE_POWER_STATE_OFF)
		{
			
			eError = SGXDeinitialise(psDevInfo);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPrePowerState: SGXDeinitialise failed: %lu", eError));
				return eError;				
			}
		}
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR SGXPostPowerState (IMG_HANDLE				hDevHandle, 
									   PVR_DEVICE_POWER_STATE	eNewPowerState, 
									   PVR_DEVICE_POWER_STATE	eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
		(eCurrentPowerState != PVR_DEVICE_POWER_STATE_ON))
	{
		PVRSRV_ERROR		eError;
		PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
		PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
		SGXMKIF_HOST_CTL *psSGXHostCtl = psDevInfo->psSGXHostCtl;

		
		psSGXHostCtl->ui32PowerStatus = 0;
		PDUMPCOMMENT("TA/3D CCB Control - Reset power status");
		#if defined(PDUMP)
		PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
				 offsetof(SGXMKIF_HOST_CTL, ui32PowerStatus),
				 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
				 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif 

		if (eCurrentPowerState == PVR_DEVICE_POWER_STATE_OFF)
		{
			

			
			

			SGXGetTimingInfo(psDeviceNode);

			

			eError = SGXInitialise(psDevInfo, IMG_FALSE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState: SGXInitialise failed"));
				return eError;
			}
		}
		else
		{
			

			SGXMKIF_COMMAND		sCommand = {0};

			SGXStartTimer(psDevInfo, IMG_TRUE);
			
			sCommand.ui32Data[0] = PVRSRV_CCBFLAGS_PROCESS_QUEUESCMD;
			eError = SGXScheduleCCBCommand(psDevInfo, SGXMKIF_COMMAND_EDM_KICK, &sCommand, ISR_ID, 0);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState failed to schedule CCB command: %lu", eError));
				return PVRSRV_ERROR_GENERIC;
			}
		}
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR SGXPrePowerStateExt (IMG_HANDLE		hDevHandle, 
								  PVR_POWER_STATE	eNewPowerState, 
								  PVR_POWER_STATE	eCurrentPowerState)
{
	PVR_DEVICE_POWER_STATE	eNewDevicePowerState = MapDevicePowerState(eNewPowerState);
	PVR_DEVICE_POWER_STATE	eCurrentDevicePowerState = MapDevicePowerState(eCurrentPowerState);
	
	return SGXPrePowerState(hDevHandle, eNewDevicePowerState, eCurrentDevicePowerState);
}


PVRSRV_ERROR SGXPostPowerStateExt (IMG_HANDLE		hDevHandle, 
								   PVR_POWER_STATE	eNewPowerState, 
								   PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR			eError;
	PVR_DEVICE_POWER_STATE	eNewDevicePowerState = MapDevicePowerState(eNewPowerState);
	PVR_DEVICE_POWER_STATE	eCurrentDevicePowerState = MapDevicePowerState(eCurrentPowerState);
	
	eError = SGXPostPowerState(hDevHandle, eNewDevicePowerState, eCurrentDevicePowerState);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	
	PVR_DPF((PVR_DBG_MESSAGE,
			"SGXPostPowerState : SGX Power Transition from %d to %d OK",
			eCurrentPowerState, eNewPowerState));
	
	return eError;		
}


PVRSRV_ERROR SGXPreClockSpeedChange (IMG_HANDLE			hDevHandle,
									 IMG_BOOL			bIdleDevice,
									 PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	if (eCurrentPowerState == PVRSRV_POWER_STATE_D0)
	{
		if (bIdleDevice)
		{
			
			PDUMPSUSPEND();
			
			eError = SGXPrePowerState(hDevHandle, PVR_DEVICE_POWER_STATE_IDLE,
									  PVR_DEVICE_POWER_STATE_ON);

			if (eError != PVRSRV_OK)
			{
				PDUMPRESUME();
				return eError;
			}
		}
	}
		
	PVR_DPF((PVR_DBG_MESSAGE,"SGXPreClockSpeedChange: SGX clock speed was %luHz",
			psDevInfo->ui32CoreClockSpeed));

	return PVRSRV_OK;
}


PVRSRV_ERROR SGXPostClockSpeedChange (IMG_HANDLE		hDevHandle,
									  IMG_BOOL			bIdleDevice,
									  PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32			ui32OldClockSpeed = psDevInfo->ui32CoreClockSpeed;

	PVR_UNREFERENCED_PARAMETER(ui32OldClockSpeed);

	if (eCurrentPowerState == PVRSRV_POWER_STATE_D0)
	{
		

		SGXGetTimingInfo(psDeviceNode);

		if (bIdleDevice)
		{
			PVRSRV_ERROR eError;
			
			eError = SGXPostPowerState(hDevHandle, PVR_DEVICE_POWER_STATE_ON,
									   PVR_DEVICE_POWER_STATE_IDLE);

			PDUMPRESUME();
			
			if (eError != PVRSRV_OK)
			{
				return eError;
			}
		}
		else
		{
			SGXStartTimer(psDevInfo, IMG_TRUE);
		}

	}

	PVR_DPF((PVR_DBG_MESSAGE,"SGXPostClockSpeedChange: SGX clock speed changed from %luHz to %luHz",
			ui32OldClockSpeed, psDevInfo->ui32CoreClockSpeed));

	return PVRSRV_OK;
}


