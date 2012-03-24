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

#if defined (SUPPORT_SGX)
#if defined (PDUMP)
#include <asm/atomic.h>
#include <stdarg.h>
#include "sgxdefs.h"
#include "services_headers.h"

#include "pvrversion.h"
#include "pvr_debug.h"

#include "dbgdrvif.h"
#include "sgxmmu.h"
#include "mm.h"
#include "pdump_km.h"

#include <linux/tty.h>			

static IMG_BOOL PDumpWriteString2		(IMG_CHAR * pszString, IMG_UINT32 ui32Flags);
static IMG_BOOL PDumpWriteILock			(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32Count, IMG_UINT32 ui32Flags);
static IMG_VOID DbgSetFrame				(PDBG_STREAM psStream, IMG_UINT32 ui32Frame);
static IMG_UINT32 DbgGetFrame			(PDBG_STREAM psStream);
static IMG_VOID DbgSetMarker			(PDBG_STREAM psStream, IMG_UINT32 ui32Marker);
static IMG_UINT32 DbgWrite				(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32BCount, IMG_UINT32 ui32Flags);

#define PDUMP_DATAMASTER_PIXEL		(1)

#define MIN(a,b)       (a > b ? b : a)

#define MAX_FILE_SIZE	0x40000000

static atomic_t gsPDumpSuspended = ATOMIC_INIT(0);

static PDBGKM_SERVICE_TABLE gpfnDbgDrv = IMG_NULL;

#define PDUMP_STREAM_PARAM2			0
#define PDUMP_STREAM_SCRIPT2		1
#define PDUMP_STREAM_DRIVERINFO		2
#define PDUMP_NUM_STREAMS			3



IMG_CHAR *pszStreamName[PDUMP_NUM_STREAMS] = {	"ParamStream2",
												"ScriptStream2",
												"DriverInfoStream"};

#define __PDBG_PDUMP_STATE_GET_MSG_STRING(ERROR) \
    	IMG_CHAR *pszMsg = gsDBGPdumpState.pszMsg; \
	if ((!pszMsg) || PDumpSuspended()) return ERROR

#define __PDBG_PDUMP_STATE_GET_SCRIPT_STRING(ERROR) \
    	IMG_CHAR *pszScript = gsDBGPdumpState.pszScript; \
	if ((!pszScript) || PDumpSuspended()) return ERROR

#define __PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING(ERROR) \
    	IMG_CHAR *pszScript = gsDBGPdumpState.pszScript; \
    	IMG_CHAR *pszFile = gsDBGPdumpState.pszFile; \
	if ((!pszScript) || (!pszFile) || PDumpSuspended()) return ERROR

typedef struct PDBG_PDUMP_STATE_TAG 
{
	PDBG_STREAM psStream[PDUMP_NUM_STREAMS];
	IMG_UINT32 ui32ParamFileNum;

	IMG_CHAR *pszMsg;
	IMG_CHAR *pszScript;
	IMG_CHAR *pszFile;

} PDBG_PDUMP_STATE;

static PDBG_PDUMP_STATE gsDBGPdumpState = {{IMG_NULL}, 0, IMG_NULL, IMG_NULL, IMG_NULL};

#define SZ_MSG_SIZE_MAX			PVRSRV_PDUMP_MAX_COMMENT_SIZE-1
#define SZ_SCRIPT_SIZE_MAX		PVRSRV_PDUMP_MAX_COMMENT_SIZE-1
#define SZ_FILENAME_SIZE_MAX	PVRSRV_PDUMP_MAX_COMMENT_SIZE-1




IMG_VOID DBGDrvGetServiceTable(IMG_VOID **fn_table);

static inline IMG_BOOL PDumpSuspended(IMG_VOID)
{
	return atomic_read(&gsPDumpSuspended) != 0;
}

IMG_VOID PDumpInit(IMG_VOID)
{	
	IMG_UINT32 i=0;

	
	if (!gpfnDbgDrv)
	{
		DBGDrvGetServiceTable((IMG_VOID **)&gpfnDbgDrv);

		

		
		if (gpfnDbgDrv == IMG_NULL)
		{	
			return;
		}
	
		if(!gsDBGPdumpState.pszFile)
		{
			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_FILENAME_SIZE_MAX, (IMG_PVOID *)&gsDBGPdumpState.pszFile, 0) != PVRSRV_OK)
			{
				goto init_failed;
			}
		}	
		
		if(!gsDBGPdumpState.pszMsg)
		{
			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_MSG_SIZE_MAX, (IMG_PVOID *)&gsDBGPdumpState.pszMsg, 0) != PVRSRV_OK)
			{
				goto init_failed;
			}
		}
		
		if(!gsDBGPdumpState.pszScript)
		{
			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_SCRIPT_SIZE_MAX, (IMG_PVOID *)&gsDBGPdumpState.pszScript, 0) != PVRSRV_OK)
			{
				goto init_failed;		
			}
		}
		
		for(i=0; i < PDUMP_NUM_STREAMS; i++)
		{
			gsDBGPdumpState.psStream[i] = gpfnDbgDrv->pfnCreateStream(pszStreamName[i], 
														DEBUG_CAPMODE_FRAMED, 
														DEBUG_OUTMODE_STREAMENABLE, 
														0,
														10);
			
			gpfnDbgDrv->pfnSetCaptureMode(gsDBGPdumpState.psStream[i],DEBUG_CAPMODE_FRAMED,0xFFFFFFFF, 0xFFFFFFFF, 1);
			gpfnDbgDrv->pfnSetFrame(gsDBGPdumpState.psStream[i],0);
		}

		PDUMPCOMMENT("Driver Product Name: %s", VS_PRODUCT_NAME);
		PDUMPCOMMENT("Driver Product Version: %s (%s)", PVRVERSION_STRING, PVRVERSION_FILE);
		PDUMPCOMMENT("Start of Init Phase");
	}

	return;

init_failed:	

	if(gsDBGPdumpState.pszFile)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_FILENAME_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszFile, 0);
		gsDBGPdumpState.pszFile = IMG_NULL;
	}
	
	if(gsDBGPdumpState.pszScript)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_SCRIPT_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszScript, 0);
		gsDBGPdumpState.pszScript = IMG_NULL;
	}

	if(gsDBGPdumpState.pszMsg)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_MSG_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszMsg, 0);
		gsDBGPdumpState.pszMsg = IMG_NULL;
	}

	gpfnDbgDrv = IMG_NULL;
}


IMG_VOID PDumpDeInit(IMG_VOID)
{	
	IMG_UINT32 i=0;

	for(i=0; i < PDUMP_NUM_STREAMS; i++)
	{
		gpfnDbgDrv->pfnDestroyStream(gsDBGPdumpState.psStream[i]);
	}

	if(gsDBGPdumpState.pszFile)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_FILENAME_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszFile, 0);
		gsDBGPdumpState.pszFile = IMG_NULL;
	}
	
	if(gsDBGPdumpState.pszScript)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_SCRIPT_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszScript, 0);
		gsDBGPdumpState.pszScript = IMG_NULL;
	}

	if(gsDBGPdumpState.pszMsg)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, SZ_MSG_SIZE_MAX, (IMG_PVOID) gsDBGPdumpState.pszMsg, 0);
		gsDBGPdumpState.pszMsg = IMG_NULL;
	}

	gpfnDbgDrv = IMG_NULL;
}

PVRSRV_ERROR PDumpStartInitPhaseKM(IMG_VOID)
{
	IMG_UINT32 i;
	
	if (gpfnDbgDrv)
	{
		PDUMPCOMMENT("Start Init Phase");
		for(i=0; i < PDUMP_NUM_STREAMS; i++)
		{
			gpfnDbgDrv->pfnStartInitPhase(gsDBGPdumpState.psStream[i]);
		}
	}
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpStopInitPhaseKM(IMG_VOID)
{
	IMG_UINT32 i;

	if (gpfnDbgDrv)
	{
		PDUMPCOMMENT("Stop Init Phase");

		for(i=0; i < PDUMP_NUM_STREAMS; i++)
		{
			gpfnDbgDrv->pfnStopInitPhase(gsDBGPdumpState.psStream[i]);
		}
	}
	return PVRSRV_OK;
}

IMG_VOID PDumpComment(IMG_CHAR *pszFormat, ...)
{
	va_list ap;

	__PDBG_PDUMP_STATE_GET_MSG_STRING();	

	
	va_start(ap, pszFormat);
	vsnprintf(pszMsg, SZ_MSG_SIZE_MAX, pszFormat, ap);
	va_end(ap);

	PDumpCommentKM(pszMsg, PDUMP_FLAGS_CONTINUOUS);
}

IMG_VOID PDumpCommentWithFlags(IMG_UINT32 ui32Flags, IMG_CHAR * pszFormat, ...)
{	
	va_list ap;

	__PDBG_PDUMP_STATE_GET_MSG_STRING();

	
	va_start(ap, pszFormat);
	vsnprintf(pszMsg, SZ_MSG_SIZE_MAX, pszFormat, ap);
	va_end(ap);

	PDumpCommentKM(pszMsg, ui32Flags);
}

IMG_BOOL PDumpIsLastCaptureFrameKM(IMG_VOID)
{
	return gpfnDbgDrv->pfnIsLastCaptureFrame(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2]);
}


IMG_BOOL PDumpIsCaptureFrameKM(IMG_VOID)
{
	if (PDumpSuspended())
	{
		return IMG_FALSE;
	}
	return gpfnDbgDrv->pfnIsCaptureFrame(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2], IMG_FALSE);
}

PVRSRV_ERROR PDumpRegWithFlagsKM(IMG_UINT32 ui32Reg, IMG_UINT32 ui32Data, IMG_UINT32 ui32Flags)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING(PVRSRV_ERROR_GENERIC);

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "WRW :SGXREG:0x%8.8lX 0x%8.8lX\r\n", ui32Reg, ui32Data);
	PDumpWriteString2(pszScript, ui32Flags);

	return PVRSRV_OK;
}

IMG_VOID PDumpReg(IMG_UINT32 ui32Reg,IMG_UINT32 ui32Data)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "WRW :SGXREG:0x%8.8lX 0x%8.8lX\r\n", ui32Reg, ui32Data);
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
}

PVRSRV_ERROR PDumpRegPolWithFlagsKM(IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue, IMG_UINT32 ui32Mask, IMG_UINT32 ui32Flags)
{
	#define POLL_DELAY			1000
	#define POLL_COUNT_LONG		(2000000000 / POLL_DELAY)
	#define POLL_COUNT_SHORT	(1000000 / POLL_DELAY)

	IMG_UINT32	ui32PollCount;
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING(PVRSRV_ERROR_GENERIC);

	if (((ui32RegAddr == EUR_CR_EVENT_STATUS) && 
		(ui32RegValue & ui32Mask & EUR_CR_EVENT_STATUS_TA_FINISHED_MASK)) ||
		((ui32RegAddr == EUR_CR_EVENT_STATUS) && 
		(ui32RegValue & ui32Mask & EUR_CR_EVENT_STATUS_PIXELBE_END_RENDER_MASK)) ||
		((ui32RegAddr == EUR_CR_EVENT_STATUS) && 
		(ui32RegValue & ui32Mask & EUR_CR_EVENT_STATUS_DPM_3D_MEM_FREE_MASK)))
	{
		ui32PollCount = POLL_COUNT_LONG;
	}
	else
	{
		ui32PollCount = POLL_COUNT_SHORT;
	}

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "POL :SGXREG:0x%8.8lX 0x%8.8lX 0x%8.8lX %d %lu %d\r\n", ui32RegAddr, ui32RegValue, ui32Mask, 0, ui32PollCount, POLL_DELAY);
	PDumpWriteString2(pszScript, ui32Flags);

	return PVRSRV_OK;
}


PVRSRV_ERROR PDumpRegPolKM(IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue, IMG_UINT32 ui32Mask)
{
	return PDumpRegPolWithFlagsKM(ui32RegAddr, ui32RegValue, ui32Mask, PDUMP_FLAGS_CONTINUOUS);
}

IMG_VOID PDumpMallocPages (PVRSRV_DEVICE_TYPE eDeviceType,
                           IMG_UINT32         ui32DevVAddr,
                           IMG_CPU_VIRTADDR   pvLinAddr,
                           IMG_HANDLE         hOSMemHandle,
                           IMG_UINT32         ui32NumBytes,
                           IMG_UINT32         ui32PageSize,
                           IMG_HANDLE         hUniqueTag)
{
    IMG_UINT32      ui32Offset;
	IMG_UINT32		ui32NumPages;
	IMG_CPU_PHYADDR	sCpuPAddr;
	IMG_DEV_PHYADDR	sDevPAddr;
	IMG_UINT32		ui32Page;
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();
	PVR_UNREFERENCED_PARAMETER(pvLinAddr);


	PVR_ASSERT(((IMG_UINT32) ui32DevVAddr & (ui32PageSize - 1)) == 0);
	PVR_ASSERT(hOSMemHandle);
	PVR_ASSERT(((IMG_UINT32) ui32NumBytes & (ui32PageSize - 1)) == 0);

	

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "-- MALLOC :SGXMEM:VA_%8.8lX 0x%8.8lX %lu\r\n", ui32DevVAddr, ui32NumBytes, ui32PageSize);
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);

	

	ui32Offset = 0;
	ui32NumPages	= ui32NumBytes / ui32PageSize;
	while (ui32NumPages--)
	{
		sCpuPAddr   = OSMemHandleToCpuPAddr(hOSMemHandle, ui32Offset);
		PVR_ASSERT((sCpuPAddr.uiAddr & (ui32PageSize - 1)) == 0);
		ui32Offset  += ui32PageSize;
		sDevPAddr	= SysCpuPAddrToDevPAddr(eDeviceType, sCpuPAddr);
		ui32Page	= sDevPAddr.uiAddr / ui32PageSize;

		snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "MALLOC :SGXMEM:PA_%8.8lX%8.8lX %lu %lu 0x%8.8lX\r\n", 
												(IMG_UINT32) hUniqueTag,
												ui32Page * ui32PageSize,
												ui32PageSize, 
												ui32PageSize, 
												ui32Page * ui32PageSize);
		PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
	}
}

IMG_VOID PDumpMallocPageTable (PVRSRV_DEVICE_TYPE eDeviceType,
                               IMG_CPU_VIRTADDR   pvLinAddr,
								IMG_UINT32        ui32PTSize,
                               IMG_HANDLE         hUniqueTag)
{
	IMG_PUINT8		pui8LinAddr;
	IMG_UINT32		ui32NumPages;
	IMG_CPU_PHYADDR	sCpuPAddr;
	IMG_DEV_PHYADDR	sDevPAddr;
	IMG_UINT32		ui32Page;
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	PVR_ASSERT(((IMG_UINT32) pvLinAddr & (ui32PTSize - 1)) == 0);

	

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "-- MALLOC :SGXMEM:PAGE_TABLE 0x%8.8lX %lu\r\n", ui32PTSize, SGX_MMU_PAGE_SIZE);
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);

	

	pui8LinAddr		= (IMG_PUINT8) pvLinAddr;

	
	
	
	

	
	ui32NumPages = 1;

	while (ui32NumPages--)
	{
		sCpuPAddr	= OSMapLinToCPUPhys(pui8LinAddr);
		sDevPAddr	= SysCpuPAddrToDevPAddr(eDeviceType, sCpuPAddr);
		ui32Page	= sDevPAddr.uiAddr >> SGX_MMU_PAGE_SHIFT;

		snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "MALLOC :SGXMEM:PA_%8.8lX%8.8lX 0x%lX %lu 0x%8.8lX\r\n",
												(IMG_UINT32) hUniqueTag,
												ui32Page * SGX_MMU_PAGE_SIZE, 
												SGX_MMU_PAGE_SIZE, 
												SGX_MMU_PAGE_SIZE, 
												ui32Page * SGX_MMU_PAGE_SIZE);
		PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
		pui8LinAddr	+= SGX_MMU_PAGE_SIZE;
	}
}

IMG_VOID PDumpFreePages	(BM_HEAP 			*psBMHeap,
                         IMG_DEV_VIRTADDR  sDevVAddr,
                         IMG_UINT32        ui32NumBytes,
                         IMG_UINT32        ui32PageSize,                         
                         IMG_HANDLE        hUniqueTag,
						 IMG_BOOL		   bInterleaved)
{
	IMG_UINT32 ui32NumPages, ui32PageCounter;
	IMG_DEV_PHYADDR	sDevPAddr;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	PVR_ASSERT(((IMG_UINT32) sDevVAddr.uiAddr & (ui32PageSize - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32) ui32NumBytes & (ui32PageSize - 1)) == 0);

	

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "-- FREE :SGXMEM:VA_%8.8lX\r\n", sDevVAddr.uiAddr);
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);

	

	ui32NumPages = ui32NumBytes / ui32PageSize;
	psDeviceNode = psBMHeap->pBMContext->psDeviceNode;	
	for (ui32PageCounter = 0; ui32PageCounter < ui32NumPages; ui32PageCounter++)
	{
		if (!bInterleaved || (ui32PageCounter % 2) == 0)
		{
			sDevPAddr = psDeviceNode->pfnMMUGetPhysPageAddr(psBMHeap->pMMUHeap, sDevVAddr);

			snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "FREE :SGXMEM:PA_%8.8lX%8.8lX\r\n", (IMG_UINT32) hUniqueTag, sDevPAddr.uiAddr);
			PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
		}
		else
		{
			
		}

		sDevVAddr.uiAddr += ui32PageSize;
	}
}

IMG_VOID PDumpFreePageTable	(PVRSRV_DEVICE_TYPE eDeviceType,
							 IMG_CPU_VIRTADDR   pvLinAddr,
							 IMG_UINT32         ui32PTSize,
							 IMG_HANDLE         hUniqueTag)
{
	IMG_PUINT8		pui8LinAddr;
	IMG_UINT32		ui32NumPages;
	IMG_CPU_PHYADDR	sCpuPAddr;
	IMG_DEV_PHYADDR	sDevPAddr;
	IMG_UINT32		ui32Page;
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	PVR_ASSERT(((IMG_UINT32) pvLinAddr & (ui32PTSize - 1)) == 0);

	

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "-- FREE :SGXMEM:PAGE_TABLE\r\n");
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);

	

	pui8LinAddr		= (IMG_PUINT8) pvLinAddr;

	
	
	
	

	
	ui32NumPages = 1;

	while (ui32NumPages--)
	{
		sCpuPAddr	= OSMapLinToCPUPhys(pui8LinAddr);
		sDevPAddr	= SysCpuPAddrToDevPAddr(eDeviceType, sCpuPAddr);
		ui32Page	= sDevPAddr.uiAddr >> SGX_MMU_PAGE_SHIFT;
		pui8LinAddr	+= SGX_MMU_PAGE_SIZE;

		snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "FREE :SGXMEM:PA_%8.8lX%8.8lX\r\n", (IMG_UINT32) hUniqueTag, ui32Page * SGX_MMU_PAGE_SIZE);
		PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
	}
}

IMG_VOID PDumpPDReg	(IMG_UINT32 ui32Reg,
					 IMG_UINT32 ui32Data,
					 IMG_HANDLE hUniqueTag)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	

	snprintf	(pszScript,
				SZ_SCRIPT_SIZE_MAX,
				"WRW :SGXREG:0x%8.8lX :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX\r\n",
				ui32Reg,
				(IMG_UINT32) hUniqueTag,
				ui32Data & ~(SGX_MMU_PAGE_SIZE - 1),
				ui32Data & (SGX_MMU_PAGE_SIZE - 1));
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
}

IMG_VOID PDumpPDRegWithFlags(IMG_UINT32 ui32Reg,
							 IMG_UINT32 ui32Data,
							 IMG_UINT32	ui32Flags,
							 IMG_HANDLE hUniqueTag)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	

	snprintf	(pszScript,
			SZ_SCRIPT_SIZE_MAX,
			 "WRW :SGXREG:0x%8.8lX :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX\r\n",
			 ui32Reg,
			 (IMG_UINT32) hUniqueTag,
			 ui32Data & ~(SGX_MMU_PAGE_SIZE - 1),
			 ui32Data & (SGX_MMU_PAGE_SIZE - 1));
	PDumpWriteString2(pszScript, ui32Flags);
}

PVRSRV_ERROR PDumpMemPolKM(PVRSRV_KERNEL_MEM_INFO		*psMemInfo,
						   IMG_UINT32			ui32Offset,
						   IMG_UINT32			ui32Value,
						   IMG_UINT32			ui32Mask,
						   PDUMP_POLL_OPERATOR	eOperator,
						   IMG_BOOL				bLastFrame,
						   IMG_BOOL				bOverwrite,
						   IMG_HANDLE			hUniqueTag)
{
	#define MEMPOLL_DELAY		(1000)
	#define MEMPOLL_COUNT		(2000000000 / MEMPOLL_DELAY)
	
	IMG_UINT32			ui32PageOffset;
	IMG_DEV_PHYADDR		sDevPAddr;
	IMG_DEV_VIRTADDR	sDevVPageAddr;
    IMG_CPU_PHYADDR     CpuPAddr;
	IMG_UINT32			ui32Flags;
	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING(PVRSRV_ERROR_GENERIC);
	
	
	PVR_ASSERT((ui32Offset + sizeof(IMG_UINT32)) <= psMemInfo->ui32AllocSize);
	
	if (gsDBGPdumpState.ui32ParamFileNum == 0)
	{
		snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%.prm");
	}
	else
	{
		snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%%lu.prm", gsDBGPdumpState.ui32ParamFileNum);
	}

	
	ui32Flags = 0;
	
	if (bLastFrame)
	{
		ui32Flags |= PDUMP_FLAGS_LASTFRAME;
	}

	if (bOverwrite)
	{
		ui32Flags |= PDUMP_FLAGS_RESETLFBUFFER;
	}

	


    CpuPAddr = OSMemHandleToCpuPAddr(psMemInfo->sMemBlk.hOSMemHandle, ui32Offset);
    ui32PageOffset = CpuPAddr.uiAddr & (PAGE_SIZE -1);
	
	
	sDevVPageAddr.uiAddr = psMemInfo->sDevVAddr.uiAddr + ui32Offset - ui32PageOffset;
	
	
	BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);
	
	
	sDevPAddr.uiAddr += ui32PageOffset;
	
	snprintf(pszScript,
			 SZ_SCRIPT_SIZE_MAX,
			 "POL :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX 0x%8.8lX 0x%8.8lX %d %d %d\r\n",
			 (IMG_UINT32) hUniqueTag,
			 sDevPAddr.uiAddr & ~(SGX_MMU_PAGE_SIZE - 1),
			 sDevPAddr.uiAddr & (SGX_MMU_PAGE_SIZE - 1),
			 ui32Value,
			 ui32Mask,
			 eOperator,
			 MEMPOLL_COUNT,
			 MEMPOLL_DELAY);
	PDumpWriteString2(pszScript, ui32Flags);

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpMemKM(IMG_PVOID pvAltLinAddr,
						PVRSRV_KERNEL_MEM_INFO *psMemInfo,
						IMG_UINT32 ui32Offset,
						IMG_UINT32 ui32Bytes,
						IMG_UINT32 ui32Flags,
						IMG_HANDLE hUniqueTag)
{
	IMG_UINT32 ui32PageByteOffset;
	IMG_UINT8* pui8DataLinAddr = IMG_NULL;
	IMG_DEV_VIRTADDR sDevVPageAddr;
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_CPU_PHYADDR CpuPAddr;
	IMG_UINT32 ui32ParamOutPos;
	IMG_UINT32 ui32CurrentOffset;
	IMG_UINT32 ui32BytesRemaining;

	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING(PVRSRV_ERROR_GENERIC);
	

	PVR_ASSERT((ui32Offset + ui32Bytes) <= psMemInfo->ui32AllocSize);

	if (ui32Bytes == 0)
	{
		return PVRSRV_OK;
	}

	if(pvAltLinAddr)
	{
		pui8DataLinAddr = pvAltLinAddr;
	}
	else if(psMemInfo->pvLinAddrKM)
	{
		pui8DataLinAddr = (IMG_UINT8 *)psMemInfo->pvLinAddrKM + ui32Offset;
	}
    
	PVR_ASSERT(pui8DataLinAddr);

	ui32ParamOutPos = gpfnDbgDrv->pfnGetStreamOffset(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2]);

	

        if(!PDumpWriteILock(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2],
                            pui8DataLinAddr,
                            ui32Bytes,
                            ui32Flags))
        {
            return PVRSRV_ERROR_GENERIC;
        }

	if (gsDBGPdumpState.ui32ParamFileNum == 0)
	{
		snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%.prm");
	}
	else
	{
		snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%%lu.prm", gsDBGPdumpState.ui32ParamFileNum);
	}

	

	snprintf(pszScript,
			 SZ_SCRIPT_SIZE_MAX,
			 "-- LDB :SGXMEM:VA_%8.8lX:0x%8.8lX 0x%8.8lX 0x%8.8lX %s\r\n",
			 psMemInfo->sDevVAddr.uiAddr,
			 ui32Offset,
			 ui32Bytes,
			 ui32ParamOutPos,
			 pszFile);
	PDumpWriteString2(pszScript, ui32Flags);

	


	CpuPAddr = OSMemHandleToCpuPAddr(psMemInfo->sMemBlk.hOSMemHandle, ui32Offset);
	ui32PageByteOffset = CpuPAddr.uiAddr & (PAGE_SIZE -1);
    
    
	sDevVAddr = psMemInfo->sDevVAddr;
	sDevVAddr.uiAddr += ui32Offset;

	ui32BytesRemaining = ui32Bytes;
	ui32CurrentOffset = ui32Offset;

	while(ui32BytesRemaining > 0)
	{
		IMG_UINT32 ui32BlockBytes = MIN(ui32BytesRemaining, PAGE_SIZE);
		CpuPAddr = OSMemHandleToCpuPAddr(psMemInfo->sMemBlk.hOSMemHandle,
						 ui32CurrentOffset);

		sDevVPageAddr.uiAddr = psMemInfo->sDevVAddr.uiAddr + ui32CurrentOffset - ui32PageByteOffset;
		
		BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);

			
		sDevPAddr.uiAddr += ui32PageByteOffset;

		if(ui32PageByteOffset)
		{
		    ui32BlockBytes =
			MIN(ui32BytesRemaining, PAGE_ALIGN(CpuPAddr.uiAddr) - CpuPAddr.uiAddr);
		    
		    ui32PageByteOffset = 0;
		}

		snprintf(pszScript,
					 SZ_SCRIPT_SIZE_MAX,
					 "LDB :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX 0x%8.8lX 0x%8.8lX %s\r\n",
					 (IMG_UINT32) hUniqueTag,
					 sDevPAddr.uiAddr & ~(SGX_MMU_PAGE_SIZE - 1),
					 sDevPAddr.uiAddr & (SGX_MMU_PAGE_SIZE - 1),
					 ui32BlockBytes,
					 ui32ParamOutPos,
					 pszFile);
			PDumpWriteString2(pszScript, ui32Flags);

		ui32BytesRemaining -= ui32BlockBytes;
		ui32CurrentOffset += ui32BlockBytes;
		ui32ParamOutPos += ui32BlockBytes;
	}

	PVR_ASSERT(ui32BytesRemaining == 0);

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpMem2KM(PVRSRV_DEVICE_TYPE eDeviceType,
						 IMG_CPU_VIRTADDR pvLinAddr,
						 IMG_UINT32 ui32Bytes,
						 IMG_UINT32 ui32Flags,
						 IMG_BOOL bInitialisePages,
						 IMG_HANDLE hUniqueTag1,
						 IMG_HANDLE hUniqueTag2)
{
	IMG_UINT32 ui32NumPages;
	IMG_UINT32 ui32PageOffset;
	IMG_UINT32 ui32BlockBytes;
	IMG_UINT8* pui8LinAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	IMG_CPU_PHYADDR sCpuPAddr;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32ParamOutPos;

	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING(PVRSRV_ERROR_GENERIC);

	if (ui32Flags);

	if (!pvLinAddr)
	{
		return PVRSRV_ERROR_GENERIC;
	}

	ui32ParamOutPos = gpfnDbgDrv->pfnGetStreamOffset(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2]);
    
	if (bInitialisePages)
	{
		


		if (!PDumpWriteILock(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2],
							pvLinAddr,
							ui32Bytes,
							PDUMP_FLAGS_CONTINUOUS))
		{		
			return PVRSRV_ERROR_GENERIC;	
		}
	
		if (gsDBGPdumpState.ui32ParamFileNum == 0)
		{
			snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%.prm");
		}
		else
		{
			snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%%lu.prm", gsDBGPdumpState.ui32ParamFileNum);
		}
	}

	

	
	ui32PageOffset	= (IMG_UINT32) pvLinAddr & (HOST_PAGESIZE() - 1);
	ui32NumPages	= (ui32PageOffset + ui32Bytes + HOST_PAGESIZE() - 1) / HOST_PAGESIZE();
	pui8LinAddr		= (IMG_UINT8*) pvLinAddr;
	
	while (ui32NumPages--)
	{
    	sCpuPAddr = OSMapLinToCPUPhys(pui8LinAddr); 
    	sDevPAddr = SysCpuPAddrToDevPAddr(eDeviceType, sCpuPAddr);

		
		if (ui32PageOffset + ui32Bytes > HOST_PAGESIZE())
		{
			
			ui32BlockBytes = HOST_PAGESIZE() - ui32PageOffset;
		}
		else
		{
			
			ui32BlockBytes = ui32Bytes;
		}

		

		if (bInitialisePages)
		{
			snprintf(pszScript,
					 SZ_SCRIPT_SIZE_MAX,
					 "LDB :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX 0x%8.8lX 0x%8.8lX %s\r\n",
					 (IMG_UINT32) hUniqueTag1,
					 sDevPAddr.uiAddr & ~(SGX_MMU_PAGE_SIZE - 1),
					 sDevPAddr.uiAddr & (SGX_MMU_PAGE_SIZE - 1),
					 ui32BlockBytes,
					 ui32ParamOutPos,
					 pszFile);
			PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
		}
		else
		{
			for (ui32Offset = 0; ui32Offset < ui32BlockBytes; ui32Offset += sizeof(IMG_UINT32))
			{
				IMG_UINT32		ui32PTE = *((IMG_UINT32 *) (pui8LinAddr + ui32Offset));

				if ((ui32PTE & SGX_MMU_PDE_ADDR_MASK) != 0)
				{				
					snprintf(pszScript,
							SZ_SCRIPT_SIZE_MAX,
							 "WRW :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX\r\n",
							 (IMG_UINT32) hUniqueTag1,
							 (sDevPAddr.uiAddr + ui32Offset) & ~(SGX_MMU_PAGE_SIZE - 1),
							 (sDevPAddr.uiAddr + ui32Offset) & (SGX_MMU_PAGE_SIZE - 1),
							 (IMG_UINT32) hUniqueTag2,
							 ui32PTE & SGX_MMU_PDE_ADDR_MASK,
							 ui32PTE & ~SGX_MMU_PDE_ADDR_MASK);
				}
				else
				{
					PVR_ASSERT(!(ui32PTE & SGX_MMU_PTE_VALID));
					snprintf(pszScript,
							 SZ_SCRIPT_SIZE_MAX,
							 "WRW :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX 0x%8.8lX%8.8lX\r\n",
							 (IMG_UINT32) hUniqueTag1,
							 (sDevPAddr.uiAddr + ui32Offset) & ~(SGX_MMU_PAGE_SIZE - 1),
							 (sDevPAddr.uiAddr + ui32Offset) & (SGX_MMU_PAGE_SIZE - 1),
							 ui32PTE,
							 (IMG_UINT32) hUniqueTag2);
				}
				PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);
			}
		}

		

		
		ui32PageOffset = 0;
		
		ui32Bytes -= ui32BlockBytes;
		
		pui8LinAddr += ui32BlockBytes;
		
		ui32ParamOutPos += ui32BlockBytes;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpPDDevPAddrKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo,
							   IMG_UINT32 ui32Offset,
							   IMG_DEV_PHYADDR sPDDevPAddr,
							   IMG_HANDLE hUniqueTag1,
							   IMG_HANDLE hUniqueTag2)
{
	IMG_UINT32 ui32ParamOutPos;
    IMG_CPU_PHYADDR CpuPAddr;
	IMG_UINT32 ui32PageByteOffset;
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEV_VIRTADDR sDevVPageAddr;
	IMG_DEV_PHYADDR sDevPAddr;

	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING(PVRSRV_ERROR_GENERIC);

	ui32ParamOutPos = gpfnDbgDrv->pfnGetStreamOffset(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2]);

	if(!PDumpWriteILock(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2],
						(IMG_UINT8 *)&sPDDevPAddr,
						sizeof(IMG_DEV_PHYADDR),
						PDUMP_FLAGS_CONTINUOUS))
	{
		return PVRSRV_ERROR_GENERIC;
	}
        
	if (gsDBGPdumpState.ui32ParamFileNum == 0)
	{
		snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%.prm");
	}
	else
	{
		snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "%%0%%%lu.prm", gsDBGPdumpState.ui32ParamFileNum);
	}

    CpuPAddr = OSMemHandleToCpuPAddr(psMemInfo->sMemBlk.hOSMemHandle, ui32Offset);
	ui32PageByteOffset = CpuPAddr.uiAddr & (PAGE_SIZE -1);
    
	sDevVAddr = psMemInfo->sDevVAddr;
	sDevVAddr.uiAddr += ui32Offset;

	sDevVPageAddr.uiAddr = sDevVAddr.uiAddr - ui32PageByteOffset;
	BM_GetPhysPageAddr(psMemInfo, sDevVPageAddr, &sDevPAddr);
	sDevPAddr.uiAddr += ui32PageByteOffset;

	if ((sPDDevPAddr.uiAddr & SGX_MMU_PDE_ADDR_MASK) != 0)
	{
		snprintf(pszScript,
				 SZ_SCRIPT_SIZE_MAX,
				 "WRW :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX\r\n",
				 (IMG_UINT32) hUniqueTag1,
				 sDevPAddr.uiAddr & ~(SGX_MMU_PAGE_SIZE - 1),
				 sDevPAddr.uiAddr & (SGX_MMU_PAGE_SIZE - 1),
				 (IMG_UINT32) hUniqueTag2,
				 sPDDevPAddr.uiAddr & SGX_MMU_PDE_ADDR_MASK,
				 sPDDevPAddr.uiAddr & ~SGX_MMU_PDE_ADDR_MASK);
	}
	else
	{
		PVR_ASSERT(!(sDevPAddr.uiAddr & SGX_MMU_PTE_VALID));
		snprintf(pszScript,
				 SZ_SCRIPT_SIZE_MAX,
				 "WRW :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX 0x%8.8lX\r\n",
				 (IMG_UINT32) hUniqueTag1,
				 sDevPAddr.uiAddr & ~(SGX_MMU_PAGE_SIZE - 1),
				 sDevPAddr.uiAddr & (SGX_MMU_PAGE_SIZE - 1),
				 sPDDevPAddr.uiAddr);
	}
	PDumpWriteString2(pszScript, PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpSetFrameKM(IMG_UINT32 ui32Frame)
{
	IMG_UINT32	ui32Stream;

	for	(ui32Stream = 0; ui32Stream < PDUMP_NUM_STREAMS; ui32Stream++)
	{
		if	(gsDBGPdumpState.psStream[ui32Stream])
		{
			DbgSetFrame(gsDBGPdumpState.psStream[ui32Stream], ui32Frame);
		}
	}
		
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpGetFrameKM(IMG_PUINT32 pui32Frame)
{
	*pui32Frame = DbgGetFrame(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2]);

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpCommentKM(IMG_CHAR *pszComment, IMG_UINT32 ui32Flags)
{
	IMG_UINT32 ui32Count = 0;
	PVRSRV_ERROR eError;
	__PDBG_PDUMP_STATE_GET_MSG_STRING(PVRSRV_ERROR_GENERIC);

	if(ui32Flags & PDUMP_FLAGS_CONTINUOUS)
	{
		eError = PVRSRV_ERROR_GENERIC;
	}
	else
	{
		eError = PVRSRV_ERROR_CMD_NOT_PROCESSED;
	}
	
	if (!PDumpWriteString2("-- ", ui32Flags))
	{
		return eError;
	}

	
	snprintf(pszMsg, SZ_MSG_SIZE_MAX, "%s",pszComment);

	
	while ((pszMsg[ui32Count]!=0) && (ui32Count<SZ_MSG_SIZE_MAX) )
	{
		ui32Count++;
	}
	
	
	if ( (pszMsg[ui32Count-1] != '\n') && (ui32Count<SZ_MSG_SIZE_MAX))
	{
		pszMsg[ui32Count] = '\n';
		ui32Count++;
		pszMsg[ui32Count] = '\0';
	}
	if ( (pszMsg[ui32Count-2] != '\r') && (ui32Count<SZ_MSG_SIZE_MAX) )
	{
		pszMsg[ui32Count-1] = '\r';
		pszMsg[ui32Count] = '\n';
		ui32Count++;
		pszMsg[ui32Count] = '\0';
	}

	PDumpWriteString2(pszMsg, ui32Flags);

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpDriverInfoKM(IMG_CHAR *pszString, IMG_UINT32 ui32Flags)
{
	IMG_UINT32 ui32Count = 0;
	__PDBG_PDUMP_STATE_GET_MSG_STRING(PVRSRV_ERROR_GENERIC);

	
	snprintf(pszMsg, SZ_MSG_SIZE_MAX, "%s", pszString);

	
	while ((pszMsg[ui32Count]!=0) && (ui32Count<SZ_MSG_SIZE_MAX) )
	{
		ui32Count++;
	}
	
	
	if ( (pszMsg[ui32Count-1] != '\n') && (ui32Count<SZ_MSG_SIZE_MAX))
	{
		pszMsg[ui32Count] = '\n';
		ui32Count++;
		pszMsg[ui32Count] = '\0';
	}
	if ( (pszMsg[ui32Count-2] != '\r') && (ui32Count<SZ_MSG_SIZE_MAX) )
	{
		pszMsg[ui32Count-1] = '\r';
		pszMsg[ui32Count] = '\n';
		ui32Count++;
		pszMsg[ui32Count] = '\0';
	}

	if	(!PDumpWriteILock(gsDBGPdumpState.
						  psStream[PDUMP_STREAM_DRIVERINFO],
						  (IMG_UINT8 *)pszMsg,
						  ui32Count,
						  ui32Flags))
	{
		if	(ui32Flags & PDUMP_FLAGS_CONTINUOUS)
		{
			return PVRSRV_ERROR_GENERIC;
		}
		else
		{
			return PVRSRV_ERROR_CMD_NOT_PROCESSED;
		}
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpBitmapKM(	IMG_CHAR *pszFileName,
							IMG_UINT32 ui32FileOffset,
							IMG_UINT32 ui32Width,
							IMG_UINT32 ui32Height,
							IMG_UINT32 ui32StrideInBytes,
							IMG_DEV_VIRTADDR sDevBaseAddr,
							IMG_UINT32 ui32Size,
							PDUMP_PIXEL_FORMAT ePixelFormat,
							PDUMP_MEM_FORMAT eMemFormat,
							IMG_UINT32 ui32PDumpFlags)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING(PVRSRV_ERROR_GENERIC);
	PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "\r\n-- Dump bitmap of render\r\n");

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	
	snprintf(pszScript,
				SZ_SCRIPT_SIZE_MAX,
				"SII %s %s.bin :SGXMEM:v%x:0x%08lX 0x%08lX 0x%08lX 0x%08X 0x%08lX 0x%08lX 0x%08lX 0x%08X\r\n",
				pszFileName,
				pszFileName,
				PDUMP_DATAMASTER_PIXEL,
				sDevBaseAddr.uiAddr,
				ui32Size,
				ui32FileOffset,
				ePixelFormat,
				ui32Width,
				ui32Height,
				ui32StrideInBytes,
				eMemFormat);
#else
	snprintf(pszScript,
				SZ_SCRIPT_SIZE_MAX,
				"SII %s %s.bin :SGXMEM:v:0x%08lX 0x%08lX 0x%08lX 0x%08X 0x%08lX 0x%08lX 0x%08lX 0x%08X\r\n",
				pszFileName,
				pszFileName,
				sDevBaseAddr.uiAddr,
				ui32Size,
				ui32FileOffset,
				ePixelFormat,
				ui32Width,
				ui32Height,
				ui32StrideInBytes,
				eMemFormat);
#endif

	PDumpWriteString2( pszScript, ui32PDumpFlags);
	return PVRSRV_OK;
}

PVRSRV_ERROR PDumpReadRegKM		(	IMG_CHAR *pszFileName,
									IMG_UINT32 ui32FileOffset,
									IMG_UINT32 ui32Address,
									IMG_UINT32 ui32Size,
									IMG_UINT32 ui32PDumpFlags)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING(PVRSRV_ERROR_GENERIC);

	snprintf(pszScript,
			SZ_SCRIPT_SIZE_MAX,
			"SAB :SGXREG:0x%08lX 0x%08lX %s\r\n",
			ui32Address,
			ui32FileOffset,
			pszFileName);

	PDumpWriteString2( pszScript, ui32PDumpFlags);

	return PVRSRV_OK;
}


static IMG_BOOL PDumpWriteString2(IMG_CHAR * pszString, IMG_UINT32 ui32Flags)
{
	return PDumpWriteILock(gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2], (IMG_UINT8 *) pszString, strlen(pszString), ui32Flags);
}


static IMG_BOOL PDumpWriteILock(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32Count, IMG_UINT32 ui32Flags)
{
	IMG_UINT32 ui32Written = 0;
	IMG_UINT32 ui32Off = 0;

	if (!psStream || PDumpSuspended() || (ui32Flags & PDUMP_FLAGS_NEVER))
	{
		return IMG_TRUE;
	}
	

	

	if (psStream == gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2])
	{
		IMG_UINT32 ui32ParamOutPos = gpfnDbgDrv->pfnGetStreamOffset(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2]);

		if (ui32ParamOutPos + ui32Count > MAX_FILE_SIZE)
		{
			if ((gsDBGPdumpState.psStream[PDUMP_STREAM_SCRIPT2] && PDumpWriteString2("\r\n-- Splitting pdump output file\r\n\r\n", ui32Flags)))
			{
				DbgSetMarker(gsDBGPdumpState.psStream[PDUMP_STREAM_PARAM2], ui32ParamOutPos);
				gsDBGPdumpState.ui32ParamFileNum++;
			}
		}
	}
	

	while (((IMG_UINT32) ui32Count > 0) && (ui32Written != 0xFFFFFFFF))
	{
		ui32Written = DbgWrite(psStream, &pui8Data[ui32Off], ui32Count, ui32Flags);

		


		if (ui32Written == 0)
		{
			OSReleaseThreadQuanta();
		}

		if (ui32Written != 0xFFFFFFFF)
		{
			ui32Off += ui32Written;
			ui32Count -= ui32Written;
		}
	}

	if (ui32Written == 0xFFFFFFFF)
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

static IMG_VOID DbgSetFrame(PDBG_STREAM psStream, IMG_UINT32 ui32Frame)
{	
	gpfnDbgDrv->pfnSetFrame(psStream, ui32Frame);
}


static IMG_UINT32 DbgGetFrame(PDBG_STREAM psStream)
{	
	return gpfnDbgDrv->pfnGetFrame(psStream);
}

static IMG_VOID DbgSetMarker(PDBG_STREAM psStream, IMG_UINT32 ui32Marker)
{	
	gpfnDbgDrv->pfnSetMarker(psStream, ui32Marker);
}

static IMG_UINT32 DbgWrite(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32BCount, IMG_UINT32 ui32Flags)
{
	IMG_UINT32	ui32BytesWritten;

	if (ui32Flags & PDUMP_FLAGS_CONTINUOUS)
	{
		

		if ((psStream->ui32CapMode & DEBUG_CAPMODE_FRAMED) && 
			(psStream->ui32Start == 0xFFFFFFFF) &&
			(psStream->ui32End == 0xFFFFFFFF) &&
			psStream->bInitPhaseComplete)
		{
			ui32BytesWritten = ui32BCount;
		}
		else
		{
			ui32BytesWritten = gpfnDbgDrv->pfnDBGDrivWrite2(psStream, pui8Data, ui32BCount, 1);
		}
	}
	else
	{
		if (ui32Flags & PDUMP_FLAGS_LASTFRAME)
		{
			IMG_UINT32	ui32DbgFlags;

			ui32DbgFlags = 0;
			if (ui32Flags & PDUMP_FLAGS_RESETLFBUFFER)
			{
				ui32DbgFlags |= WRITELF_FLAGS_RESETBUF;
			}

			ui32BytesWritten = gpfnDbgDrv->pfnWriteLF(psStream, pui8Data, ui32BCount, 1, ui32DbgFlags);
		}
		else
		{
			ui32BytesWritten = gpfnDbgDrv->pfnWriteBINCM(psStream, pui8Data, ui32BCount, 1);
		}
	}

	return ui32BytesWritten;
}

IMG_BOOL PDumpTestNextFrame(IMG_UINT32 ui32CurrentFrame)
{
	IMG_BOOL	bFrameDumped;

	

	bFrameDumped = IMG_FALSE;
	PDumpSetFrameKM(ui32CurrentFrame + 1);
	bFrameDumped = PDumpIsCaptureFrameKM();
	PDumpSetFrameKM(ui32CurrentFrame);

	return bFrameDumped;
}

IMG_VOID PDump3DSignatureRegisters(IMG_UINT32 ui32DumpFrameNum,
															IMG_BOOL bLastFrame,
															IMG_UINT32 *pui32Registers,
															IMG_UINT32 ui32NumRegisters)
{
	IMG_UINT32	ui32FileOffset, ui32Flags;
	IMG_UINT32 i;

	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING();

	ui32Flags = bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0;
	ui32FileOffset = 0;

	PDUMPCOMMENTWITHFLAGS(ui32Flags, "\r\n-- Dump 3D signature registers\r\n");
	snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "out%lu_3d.sig", ui32DumpFrameNum);

	for (i = 0; i < ui32NumRegisters; i++)
	{
		PDumpReadRegKM(pszFile, ui32FileOffset, pui32Registers[i], sizeof(IMG_UINT32), ui32Flags);
		ui32FileOffset += sizeof(IMG_UINT32);
	}
}

static IMG_VOID PDumpCountRead(IMG_CHAR *pszFileName,
									 IMG_UINT32		ui32Address,
									 IMG_UINT32		ui32Size,
									 IMG_UINT32		*pui32FileOffset,
									 IMG_BOOL		bLastFrame)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "SAB :SGXREG:0x%08lX 0x%08lX %s\r\n", ui32Address, *pui32FileOffset, pszFileName);
	PDumpWriteString2(pszScript, bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0);
	
	*pui32FileOffset += ui32Size;
}

IMG_VOID PDumpCounterRegisters	(IMG_UINT32 ui32DumpFrameNum,
																			IMG_BOOL	bLastFrame,
																			IMG_UINT32 *pui32Registers,
																			IMG_UINT32 ui32NumRegisters)
{
	IMG_UINT32	ui32FileOffset;
	IMG_UINT32	i;

	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING();
	
	PDUMPCOMMENTWITHFLAGS(bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0, "\r\n-- Dump counter registers\r\n");
	snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "out%lu.perf", ui32DumpFrameNum);
	ui32FileOffset = 0;
	
	for (i = 0; i < ui32NumRegisters; i++)
	{
		PDumpCountRead(pszFile, pui32Registers[i], sizeof(IMG_UINT32), &ui32FileOffset, bLastFrame);
	}
}

IMG_VOID PDumpTASignatureRegisters	(IMG_UINT32 ui32DumpFrameNum,
									 IMG_UINT32	ui32TAKickCount,
									 IMG_BOOL	bLastFrame,
																			 IMG_UINT32 *pui32Registers,
																			 IMG_UINT32 ui32NumRegisters)
{
	IMG_UINT32	ui32FileOffset, ui32Flags;
	IMG_UINT32	i;

	__PDBG_PDUMP_STATE_GET_SCRIPT_AND_FILE_STRING();
	
	ui32Flags = bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0;
	PDUMPCOMMENTWITHFLAGS(ui32Flags, "\r\n-- Dump TA signature registers\r\n");
	snprintf(pszFile, SZ_FILENAME_SIZE_MAX, "out%lu_ta.sig", ui32DumpFrameNum);
	
	ui32FileOffset = ui32TAKickCount * ui32NumRegisters * sizeof(IMG_UINT32);

	for (i = 0; i < ui32NumRegisters; i++)
	{
		PDumpReadRegKM(pszFile, ui32FileOffset, pui32Registers[i], sizeof(IMG_UINT32), ui32Flags);
		ui32FileOffset += sizeof(IMG_UINT32);
	}
}

IMG_VOID PDumpRegRead(const IMG_UINT32 ui32RegOffset, IMG_UINT32 ui32Flags)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "RDW :SGXREG:0x%lX\r\n", ui32RegOffset);
	PDumpWriteString2(pszScript, ui32Flags);
}

IMG_VOID PDumpCycleCountRegRead(const IMG_UINT32 ui32RegOffset, IMG_BOOL bLastFrame)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	snprintf(pszScript, SZ_SCRIPT_SIZE_MAX, "RDW :SGXREG:0x%lX\r\n", ui32RegOffset);
	PDumpWriteString2(pszScript, bLastFrame ? PDUMP_FLAGS_LASTFRAME : 0);
}


IMG_VOID PDumpHWPerfCBKM (IMG_CHAR			*pszFileName,
						  IMG_UINT32		ui32FileOffset,
						  IMG_DEV_VIRTADDR	sDevBaseAddr,
						  IMG_UINT32 		ui32Size,
						  IMG_UINT32 		ui32PDumpFlags)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();
	PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "\r\n-- Dump Hardware Performance Circular Buffer\r\n");

	snprintf(pszScript,
				SZ_SCRIPT_SIZE_MAX,
#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
				"SAB :SGXMEM:v%x:0x%08lX 0x%08lX 0x%08lX %s.bin\r\n",
				PDUMP_DATAMASTER_PIXEL,
#else
				"SAB :SGXMEM:v:0x%08lX 0x%08lX 0x%08lX %s.bin\r\n",
#endif
				sDevBaseAddr.uiAddr,
				ui32Size,
				ui32FileOffset,
				pszFileName);

	PDumpWriteString2( pszScript, ui32PDumpFlags);
}


IMG_VOID PDumpCBP(PPVRSRV_KERNEL_MEM_INFO		psROffMemInfo,
			  IMG_UINT32					ui32ROffOffset,
			  IMG_UINT32					ui32WPosVal,
			  IMG_UINT32					ui32PacketSize,
			  IMG_UINT32					ui32BufferSize,
			  IMG_UINT32					ui32Flags,
			  IMG_HANDLE					hUniqueTag)
{
	IMG_UINT32			ui32PageOffset;
	IMG_DEV_VIRTADDR	sDevVAddr;
	IMG_DEV_PHYADDR		sDevPAddr;
	IMG_DEV_VIRTADDR 	sDevVPageAddr;
    IMG_CPU_PHYADDR     CpuPAddr;

	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	
	PVR_ASSERT((ui32ROffOffset + sizeof(IMG_UINT32)) <= psROffMemInfo->ui32AllocSize);
	
	sDevVAddr = psROffMemInfo->sDevVAddr;
	
	
	sDevVAddr.uiAddr += ui32ROffOffset;

	


    CpuPAddr = OSMemHandleToCpuPAddr(psROffMemInfo->sMemBlk.hOSMemHandle, ui32ROffOffset);
    ui32PageOffset = CpuPAddr.uiAddr & (PAGE_SIZE -1);

	
	sDevVPageAddr.uiAddr = sDevVAddr.uiAddr - ui32PageOffset;
	
	
	BM_GetPhysPageAddr(psROffMemInfo, sDevVPageAddr, &sDevPAddr);
	
	
	sDevPAddr.uiAddr += ui32PageOffset;
	
	snprintf(pszScript,
			 SZ_SCRIPT_SIZE_MAX,
			 "CBP :SGXMEM:PA_%8.8lX%8.8lX:0x%8.8lX 0x%8.8lX 0x%8.8lX 0x%8.8lX\r\n",
			 (IMG_UINT32) hUniqueTag,
			 sDevPAddr.uiAddr & ~(SGX_MMU_PAGE_SIZE - 1),
			 sDevPAddr.uiAddr & (SGX_MMU_PAGE_SIZE - 1),
			 ui32WPosVal,
			 ui32PacketSize,
			 ui32BufferSize);
	PDumpWriteString2(pszScript, ui32Flags);
}


IMG_VOID PDumpIDLWithFlags(IMG_UINT32 ui32Clocks, IMG_UINT32 ui32Flags)
{
	__PDBG_PDUMP_STATE_GET_SCRIPT_STRING();

	sprintf(pszScript, "IDL %lu\r\n", ui32Clocks);
	PDumpWriteString2(pszScript, ui32Flags);
}


IMG_VOID PDumpIDL(IMG_UINT32 ui32Clocks)
{
	PDumpIDLWithFlags(ui32Clocks, PDUMP_FLAGS_CONTINUOUS);
}


IMG_VOID PDumpSuspendKM(IMG_VOID)
{
	atomic_inc(&gsPDumpSuspended);
}

IMG_VOID PDumpResumeKM(IMG_VOID)
{
	atomic_dec(&gsPDumpSuspended);
}

#endif 
#endif 
