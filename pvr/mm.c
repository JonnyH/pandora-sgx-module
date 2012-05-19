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

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#include <linux/version.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
#include <linux/wrapper.h>
#endif
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/sched.h>

#include "img_defs.h"
#include "services.h"
#include "servicesint.h"
#include "syscommon.h"
#include "mm.h"
#include "pvrmmap.h"
#include "mmap.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "proc.h"
#include "mutex.h"

#if defined(CONFIG_ARCH_OMAP)
#define	PVR_FLUSH_CACHE_BEFORE_KMAP
#endif

#if defined(PVR_FLUSH_CACHE_BEFORE_KMAP)
#include <asm/cacheflush.h>
#endif

extern PVRSRV_LINUX_MUTEX gPVRSRVLock;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
typedef enum {
	DEBUG_MEM_ALLOC_TYPE_KMALLOC,
	DEBUG_MEM_ALLOC_TYPE_VMALLOC,
	DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES,
	DEBUG_MEM_ALLOC_TYPE_IOREMAP,
	DEBUG_MEM_ALLOC_TYPE_IO,
	DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE,
	DEBUG_MEM_ALLOC_TYPE_KMAP,
	DEBUG_MEM_ALLOC_TYPE_COUNT
} DEBUG_MEM_ALLOC_TYPE;

typedef struct _DEBUG_MEM_ALLOC_REC {
	DEBUG_MEM_ALLOC_TYPE eAllocType;
	IMG_VOID *pvKey;
	IMG_VOID *pvCpuVAddr;
	unsigned long ulCpuPAddr;
	IMG_VOID *pvPrivateData;
	IMG_UINT32 ui32Bytes;
	pid_t pid;
	IMG_CHAR *pszFileName;
	IMG_UINT32 ui32Line;

	struct _DEBUG_MEM_ALLOC_REC *psNext;
} DEBUG_MEM_ALLOC_REC;

static DEBUG_MEM_ALLOC_REC *g_MemoryRecords;

static IMG_UINT32 g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_COUNT];
static IMG_UINT32 g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_COUNT];

static IMG_UINT32 g_SysRAMWaterMark;
static IMG_UINT32 g_SysRAMHighWaterMark;

static IMG_UINT32 g_IOMemWaterMark;
static IMG_UINT32 g_IOMemHighWaterMark;

static IMG_VOID DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE eAllocType,
				       IMG_VOID * pvKey,
				       IMG_VOID * pvCpuVAddr,
				       unsigned long ulCpuPAddr,
				       IMG_VOID * pvPrivateData,
				       IMG_UINT32 ui32Bytes,
				       IMG_CHAR * pszFileName,
				       IMG_UINT32 ui32Line);

static IMG_VOID DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE eAllocType,
					  IMG_VOID * pvKey,
					  IMG_CHAR * pszFileName,
					  IMG_UINT32 ui32Line);

static IMG_CHAR *DebugMemAllocRecordTypeToString(DEBUG_MEM_ALLOC_TYPE
						 eAllocType);

static off_t printMemoryRecords(char *buffer, size_t size, off_t off);
#endif

#if defined(DEBUG_LINUX_MEM_AREAS)
typedef struct _DEBUG_LINUX_MEM_AREA_REC {
	LinuxMemArea *psLinuxMemArea;
	IMG_UINT32 ui32Flags;
	pid_t pid;

	struct _DEBUG_LINUX_MEM_AREA_REC *psNext;
} DEBUG_LINUX_MEM_AREA_REC;

static DEBUG_LINUX_MEM_AREA_REC *g_LinuxMemAreaRecords;
static IMG_UINT32 g_LinuxMemAreaCount;
static IMG_UINT32 g_LinuxMemAreaWaterMark;
static IMG_UINT32 g_LinuxMemAreaHighWaterMark;

static off_t printLinuxMemAreaRecords(char *buffer, size_t size, off_t off);
#endif

static LinuxKMemCache *psLinuxMemAreaCache;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
static IMG_VOID ReservePages(IMG_VOID * pvAddress, IMG_UINT32 ui32Length);
static IMG_VOID UnreservePages(IMG_VOID * pvAddress, IMG_UINT32 ui32Length);
#endif

static LinuxMemArea *LinuxMemAreaStructAlloc(IMG_VOID);
static IMG_VOID LinuxMemAreaStructFree(LinuxMemArea * psLinuxMemArea);
#if defined(DEBUG_LINUX_MEM_AREAS)
static IMG_VOID DebugLinuxMemAreaRecordAdd(LinuxMemArea * psLinuxMemArea,
					   IMG_UINT32 ui32Flags);
static DEBUG_LINUX_MEM_AREA_REC *DebugLinuxMemAreaRecordFind(LinuxMemArea *
							     psLinuxMemArea);
static IMG_VOID DebugLinuxMemAreaRecordRemove(LinuxMemArea * psLinuxMemArea);
#endif

PVRSRV_ERROR LinuxMMInit(IMG_VOID)
{

#if defined(DEBUG_LINUX_MEM_AREAS)
	{
		int iStatus;
		iStatus =
		    CreateProcReadEntry("mem_areas", printLinuxMemAreaRecords);
		if (iStatus != 0) {
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	{
		int iStatus;
		iStatus = CreateProcReadEntry("meminfo", printMemoryRecords);
		if (iStatus != 0) {
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif

	psLinuxMemAreaCache =
	    KMemCacheCreateWrapper("img-mm", sizeof(LinuxMemArea), 0, 0);
	if (!psLinuxMemAreaCache) {
		PVR_DPF((PVR_DBG_ERROR, "%s: failed to allocate kmem_cache",
			 __FUNCTION__));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	return PVRSRV_OK;
}

IMG_VOID LinuxMMCleanup(IMG_VOID)
{

#if defined(DEBUG_LINUX_MEM_AREAS)
	{
		DEBUG_LINUX_MEM_AREA_REC *psCurrentRecord =
		    g_LinuxMemAreaRecords, *psNextRecord;

		if (g_LinuxMemAreaCount) {
			PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: There are %d LinuxMemArea allocation unfreed (%ld bytes)",
				 __FUNCTION__, g_LinuxMemAreaCount,
				 g_LinuxMemAreaWaterMark));
		}

		while (psCurrentRecord) {
			LinuxMemArea *psLinuxMemArea;

			psNextRecord = psCurrentRecord->psNext;
			psLinuxMemArea = psCurrentRecord->psLinuxMemArea;
			PVR_DPF((PVR_DBG_ERROR,
				 "%s: BUG!: Cleaning up Linux memory area (%p), type=%s, size=%ld bytes",
				 __FUNCTION__, psCurrentRecord->psLinuxMemArea,
				 LinuxMemAreaTypeToString(psCurrentRecord->
							  psLinuxMemArea->
							  eAreaType),
				 psCurrentRecord->psLinuxMemArea->
				 ui32ByteSize));

			LinuxMemAreaDeepFree(psLinuxMemArea);

			psCurrentRecord = psNextRecord;
		}
		RemoveProcEntry("mem_areas");
	}
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	{
		DEBUG_MEM_ALLOC_REC *psCurrentRecord =
		    g_MemoryRecords, *psNextRecord;

		while (psCurrentRecord) {
			psNextRecord = psCurrentRecord->psNext;
			PVR_DPF((PVR_DBG_ERROR, "%s: BUG!: Cleaning up memory: "
				 "type=%s "
				 "CpuVAddr=%p "
				 "CpuPAddr=0x%08lx, "
				 "allocated @ file=%s,line=%d",
				 __FUNCTION__,
				 DebugMemAllocRecordTypeToString
				 (psCurrentRecord->eAllocType),
				 psCurrentRecord->pvCpuVAddr,
				 psCurrentRecord->ulCpuPAddr,
				 psCurrentRecord->pszFileName,
				 psCurrentRecord->ui32Line));
			switch (psCurrentRecord->eAllocType) {
			case DEBUG_MEM_ALLOC_TYPE_KMALLOC:
				KFreeWrapper(psCurrentRecord->pvCpuVAddr);
				break;
			case DEBUG_MEM_ALLOC_TYPE_IOREMAP:
				IOUnmapWrapper(psCurrentRecord->pvCpuVAddr);
				break;
			case DEBUG_MEM_ALLOC_TYPE_IO:

				DebugMemAllocRecordRemove
				    (DEBUG_MEM_ALLOC_TYPE_IO,
				     psCurrentRecord->pvKey, __FILE__,
				     __LINE__);
				break;
			case DEBUG_MEM_ALLOC_TYPE_VMALLOC:
				VFreeWrapper(psCurrentRecord->pvCpuVAddr);
				break;
			case DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES:

				DebugMemAllocRecordRemove
				    (DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES,
				     psCurrentRecord->pvKey, __FILE__,
				     __LINE__);
				break;
			case DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE:
				KMemCacheFreeWrapper(psCurrentRecord->
						     pvPrivateData,
						     psCurrentRecord->
						     pvCpuVAddr);
				break;
			case DEBUG_MEM_ALLOC_TYPE_KMAP:
				KUnMapWrapper(psCurrentRecord->pvKey);
				break;
			default:
				PVR_ASSERT(0);
			}
			psCurrentRecord = psNextRecord;
		}
		RemoveProcEntry("meminfo");
	}
#endif

	if (psLinuxMemAreaCache) {
		KMemCacheDestroyWrapper(psLinuxMemAreaCache);
		psLinuxMemAreaCache = NULL;
	}
}

IMG_VOID *_KMallocWrapper(IMG_UINT32 ui32ByteSize, IMG_CHAR * pszFileName,
			  IMG_UINT32 ui32Line)
{
	IMG_VOID *pvRet;
	pvRet = kmalloc(ui32ByteSize, GFP_KERNEL);
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	if (pvRet) {
		DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_KMALLOC,
				       pvRet,
				       pvRet,
				       0,
				       NULL,
				       ui32ByteSize, pszFileName, ui32Line);
	}
#endif
	return pvRet;
}

IMG_VOID
_KFreeWrapper(IMG_VOID * pvCpuVAddr, IMG_CHAR * pszFileName,
	      IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_KMALLOC, pvCpuVAddr,
				  pszFileName, ui32Line);
#endif
	kfree(pvCpuVAddr);
}

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
static IMG_VOID
DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE eAllocType,
		       IMG_VOID * pvKey,
		       IMG_VOID * pvCpuVAddr,
		       unsigned long ulCpuPAddr,
		       IMG_VOID * pvPrivateData,
		       IMG_UINT32 ui32Bytes,
		       IMG_CHAR * pszFileName, IMG_UINT32 ui32Line)
{
	DEBUG_MEM_ALLOC_REC *psRecord;

	psRecord = kmalloc(sizeof(DEBUG_MEM_ALLOC_REC), GFP_KERNEL);

	psRecord->eAllocType = eAllocType;
	psRecord->pvKey = pvKey;
	psRecord->pvCpuVAddr = pvCpuVAddr;
	psRecord->ulCpuPAddr = ulCpuPAddr;
	psRecord->pvPrivateData = pvPrivateData;
	psRecord->pid = current->pid;
	psRecord->ui32Bytes = ui32Bytes;
	psRecord->pszFileName = pszFileName;
	psRecord->ui32Line = ui32Line;

	psRecord->psNext = g_MemoryRecords;
	g_MemoryRecords = psRecord;

	g_WaterMarkData[eAllocType] += ui32Bytes;
	if (g_WaterMarkData[eAllocType] > g_HighWaterMarkData[eAllocType]) {
		g_HighWaterMarkData[eAllocType] = g_WaterMarkData[eAllocType];
	}

	if (eAllocType == DEBUG_MEM_ALLOC_TYPE_KMALLOC
	    || eAllocType == DEBUG_MEM_ALLOC_TYPE_VMALLOC
	    || eAllocType == DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES
	    || eAllocType == DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE) {
		g_SysRAMWaterMark += ui32Bytes;
		if (g_SysRAMWaterMark > g_SysRAMHighWaterMark) {
			g_SysRAMHighWaterMark = g_SysRAMWaterMark;
		}
	} else if (eAllocType == DEBUG_MEM_ALLOC_TYPE_IOREMAP
		   || eAllocType == DEBUG_MEM_ALLOC_TYPE_IO) {
		g_IOMemWaterMark += ui32Bytes;
		if (g_IOMemWaterMark > g_IOMemHighWaterMark) {
			g_IOMemHighWaterMark = g_IOMemWaterMark;
		}
	}
}

static IMG_VOID
DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE eAllocType, IMG_VOID * pvKey,
			  IMG_CHAR * pszFileName, IMG_UINT32 ui32Line)
{
	DEBUG_MEM_ALLOC_REC **ppsCurrentRecord;

	for (ppsCurrentRecord = &g_MemoryRecords;
	     *ppsCurrentRecord;
	     ppsCurrentRecord = &((*ppsCurrentRecord)->psNext)) {
		if ((*ppsCurrentRecord)->eAllocType == eAllocType
		    && (*ppsCurrentRecord)->pvKey == pvKey) {
			DEBUG_MEM_ALLOC_REC *psNextRecord;
			DEBUG_MEM_ALLOC_TYPE eAllocType;

			psNextRecord = (*ppsCurrentRecord)->psNext;
			eAllocType = (*ppsCurrentRecord)->eAllocType;
			g_WaterMarkData[eAllocType] -=
			    (*ppsCurrentRecord)->ui32Bytes;

			if (eAllocType == DEBUG_MEM_ALLOC_TYPE_KMALLOC
			    || eAllocType == DEBUG_MEM_ALLOC_TYPE_VMALLOC
			    || eAllocType == DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES
			    || eAllocType == DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE) {
				g_SysRAMWaterMark -=
				    (*ppsCurrentRecord)->ui32Bytes;
			} else if (eAllocType == DEBUG_MEM_ALLOC_TYPE_IOREMAP
				   || eAllocType == DEBUG_MEM_ALLOC_TYPE_IO) {
				g_IOMemWaterMark -=
				    (*ppsCurrentRecord)->ui32Bytes;
			}

			kfree(*ppsCurrentRecord);
			*ppsCurrentRecord = psNextRecord;
			return;
		}
	}

	PVR_DPF((PVR_DBG_ERROR,
		 "%s: couldn't find an entry for type=%s with pvKey=%p (called from %s, line %d\n",
		 __FUNCTION__, DebugMemAllocRecordTypeToString(eAllocType),
		 pvKey, pszFileName, ui32Line));
}

static IMG_CHAR *DebugMemAllocRecordTypeToString(DEBUG_MEM_ALLOC_TYPE
						 eAllocType)
{
	char *apszDebugMemoryRecordTypes[] = {
		"KMALLOC",
		"VMALLOC",
		"ALLOC_PAGES",
		"IOREMAP",
		"IO",
		"KMEM_CACHE_ALLOC",
		"KMAP"
	};
	return apszDebugMemoryRecordTypes[eAllocType];
}
#endif

IMG_VOID *_VMallocWrapper(IMG_UINT32 ui32Bytes,
			  IMG_UINT32 ui32AllocFlags,
			  IMG_CHAR * pszFileName, IMG_UINT32 ui32Line)
{
	pgprot_t PGProtFlags;
	IMG_VOID *pvRet;

	switch (ui32AllocFlags & PVRSRV_HAP_CACHETYPE_MASK) {
	case PVRSRV_HAP_CACHED:
		PGProtFlags = PAGE_KERNEL;
		break;
	case PVRSRV_HAP_WRITECOMBINE:
#if defined(__arm__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
		PGProtFlags = pgprot_writecombine(PAGE_KERNEL);
#else
		PGProtFlags = pgprot_noncached(PAGE_KERNEL);
#endif
		break;
	case PVRSRV_HAP_UNCACHED:
		PGProtFlags = pgprot_noncached(PAGE_KERNEL);
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR,
			 "VMAllocWrapper: unknown mapping flags=0x%08lx",
			 ui32AllocFlags));
		dump_stack();
		return NULL;
	}

	pvRet = __vmalloc(ui32Bytes, GFP_KERNEL | __GFP_HIGHMEM, PGProtFlags);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	if (pvRet) {
		DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_VMALLOC,
				       pvRet,
				       pvRet,
				       0,
				       NULL,
				       PAGE_ALIGN(ui32Bytes),
				       pszFileName, ui32Line);
	}
#endif

	return pvRet;
}

IMG_VOID
_VFreeWrapper(IMG_VOID * pvCpuVAddr, IMG_CHAR * pszFileName,
	      IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_VMALLOC, pvCpuVAddr,
				  pszFileName, ui32Line);
#endif
	vfree(pvCpuVAddr);
}

LinuxMemArea *NewVMallocLinuxMemArea(IMG_UINT32 ui32Bytes,
				     IMG_UINT32 ui32AreaFlags)
{
	LinuxMemArea *psLinuxMemArea;
	IMG_VOID *pvCpuVAddr;

	psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea) {
		goto failed;
	}

	pvCpuVAddr = VMallocWrapper(ui32Bytes, ui32AreaFlags);
	if (!pvCpuVAddr) {
		goto failed;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))

	ReservePages(pvCpuVAddr, ui32Bytes);
#endif

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_VMALLOC;
	psLinuxMemArea->uData.sVmalloc.pvVmallocAddress = pvCpuVAddr;
	psLinuxMemArea->ui32ByteSize = ui32Bytes;

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

	return psLinuxMemArea;

failed:
	PVR_DPF((PVR_DBG_ERROR, "%s: failed!", __FUNCTION__));
	if (psLinuxMemArea)
		LinuxMemAreaStructFree(psLinuxMemArea);
	return NULL;
}

IMG_VOID FreeVMallocLinuxMemArea(LinuxMemArea * psLinuxMemArea)
{
	PVR_ASSERT(psLinuxMemArea);
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_VMALLOC);
	PVR_ASSERT(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
	UnreservePages(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress,
		       psLinuxMemArea->ui32ByteSize);
#endif

	PVR_DPF((PVR_DBG_MESSAGE, "%s: pvCpuVAddr: %p",
		 __FUNCTION__,
		 psLinuxMemArea->uData.sVmalloc.pvVmallocAddress));
	VFreeWrapper(psLinuxMemArea->uData.sVmalloc.pvVmallocAddress);

	LinuxMemAreaStructFree(psLinuxMemArea);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
static IMG_VOID ReservePages(IMG_VOID * pvAddress, IMG_UINT32 ui32Length)
{
	IMG_VOID *pvPage;
	IMG_VOID *pvEnd = pvAddress + ui32Length;

	for (pvPage = pvAddress; pvPage < pvEnd; pvPage += PAGE_SIZE) {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		SetPageReserved(vmalloc_to_page(pvPage));
#else
		mem_map_reserve(vmalloc_to_page(pvPage));
#endif
	}
}

static IMG_VOID UnreservePages(IMG_VOID * pvAddress, IMG_UINT32 ui32Length)
{
	IMG_VOID *pvPage;
	IMG_VOID *pvEnd = pvAddress + ui32Length;

	for (pvPage = pvAddress; pvPage < pvEnd; pvPage += PAGE_SIZE) {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		ClearPageReserved(vmalloc_to_page(pvPage));
#else
		mem_map_unreserve(vmalloc_to_page(pvPage));
#endif
	}
}
#endif

IMG_VOID *_IORemapWrapper(IMG_CPU_PHYADDR BasePAddr,
			  IMG_UINT32 ui32Bytes,
			  IMG_UINT32 ui32MappingFlags,
			  IMG_CHAR * pszFileName, IMG_UINT32 ui32Line)
{
	IMG_VOID *pvIORemapCookie = IMG_NULL;

	switch (ui32MappingFlags & PVRSRV_HAP_CACHETYPE_MASK) {
	case PVRSRV_HAP_CACHED:
#if defined(__arm__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
		pvIORemapCookie =
		    (IMG_VOID *) ioremap_cached(BasePAddr.uiAddr, ui32Bytes);
#else
		pvIORemapCookie =
		    (IMG_VOID *) ioremap(BasePAddr.uiAddr, ui32Bytes);
#endif
		break;
	case PVRSRV_HAP_WRITECOMBINE:
#if defined(__arm__)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22))
		pvIORemapCookie =
		    (IMG_VOID *) ioremap_nocache(BasePAddr.uiAddr, ui32Bytes);
#else
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)) || (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
		pvIORemapCookie =
		    (IMG_VOID *) __ioremap(BasePAddr.uiAddr, ui32Bytes,
					   L_PTE_BUFFERABLE);
#else
		pvIORemapCookie =
		    (IMG_VOID *) __ioremap(BasePAddr.uiAddr, ui32Bytes,
					   L_PTE_BUFFERABLE, 1);
#endif
#endif
#else
#if defined(__i386__) && defined(SUPPORT_LINUX_X86_WRITECOMBINE)
		pvIORemapCookie =
		    (IMG_VOID *) __ioremap(BasePAddr.uiAddr, ui32Bytes,
					   _PAGE_PCD);
#else
		pvIORemapCookie =
		    (IMG_VOID *) ioremap_nocache(BasePAddr.uiAddr, ui32Bytes);
#endif
#endif
		break;
	case PVRSRV_HAP_UNCACHED:
		pvIORemapCookie =
		    (IMG_VOID *) ioremap_nocache(BasePAddr.uiAddr, ui32Bytes);
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR,
			 "IORemapWrapper: unknown mapping flags"));
		return NULL;
	}

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	if (pvIORemapCookie) {
		DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_IOREMAP,
				       pvIORemapCookie,
				       pvIORemapCookie,
				       BasePAddr.uiAddr,
				       NULL, ui32Bytes, pszFileName, ui32Line);
	}
#endif

	return pvIORemapCookie;
}

IMG_VOID
_IOUnmapWrapper(IMG_VOID * pvIORemapCookie, IMG_CHAR * pszFileName,
		IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_IOREMAP, pvIORemapCookie,
				  pszFileName, ui32Line);
#endif
	iounmap(pvIORemapCookie);
}

LinuxMemArea *NewIORemapLinuxMemArea(IMG_CPU_PHYADDR BasePAddr,
				     IMG_UINT32 ui32Bytes,
				     IMG_UINT32 ui32AreaFlags)
{
	LinuxMemArea *psLinuxMemArea;
	IMG_VOID *pvIORemapCookie;

	psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea) {
		return NULL;
	}

	pvIORemapCookie = IORemapWrapper(BasePAddr, ui32Bytes, ui32AreaFlags);
	if (!pvIORemapCookie) {
		LinuxMemAreaStructFree(psLinuxMemArea);
		return NULL;
	}

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_IOREMAP;
	psLinuxMemArea->uData.sIORemap.pvIORemapCookie = pvIORemapCookie;
	psLinuxMemArea->uData.sIORemap.CPUPhysAddr = BasePAddr;
	psLinuxMemArea->ui32ByteSize = ui32Bytes;

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

	return psLinuxMemArea;
}

IMG_VOID FreeIORemapLinuxMemArea(LinuxMemArea * psLinuxMemArea)
{
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_IOREMAP);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

	IOUnmapWrapper(psLinuxMemArea->uData.sIORemap.pvIORemapCookie);

	LinuxMemAreaStructFree(psLinuxMemArea);
}

LinuxMemArea *NewExternalKVLinuxMemArea(IMG_SYS_PHYADDR * pBasePAddr,
					IMG_VOID * pvCPUVAddr,
					IMG_UINT32 ui32Bytes,
					IMG_BOOL bPhysContig,
					IMG_UINT32 ui32AreaFlags)
{
	LinuxMemArea *psLinuxMemArea;

	psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea) {
		return NULL;
	}

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_EXTERNAL_KV;
	psLinuxMemArea->uData.sExternalKV.pvExternalKV = pvCPUVAddr;
	psLinuxMemArea->uData.sExternalKV.bPhysContig = bPhysContig;
	if (bPhysContig) {
		psLinuxMemArea->uData.sExternalKV.uPhysAddr.SysPhysAddr =
		    *pBasePAddr;
	} else {
		psLinuxMemArea->uData.sExternalKV.uPhysAddr.pSysPhysAddr =
		    pBasePAddr;
	}
	psLinuxMemArea->ui32ByteSize = ui32Bytes;

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

	return psLinuxMemArea;
}

IMG_VOID FreeExternalKVLinuxMemArea(LinuxMemArea * psLinuxMemArea)
{
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_EXTERNAL_KV);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

	LinuxMemAreaStructFree(psLinuxMemArea);
}

LinuxMemArea *NewIOLinuxMemArea(IMG_CPU_PHYADDR BasePAddr,
				IMG_UINT32 ui32Bytes, IMG_UINT32 ui32AreaFlags)
{
	LinuxMemArea *psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea) {
		return NULL;
	}

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_IO;
	psLinuxMemArea->uData.sIO.CPUPhysAddr.uiAddr = BasePAddr.uiAddr;
	psLinuxMemArea->ui32ByteSize = ui32Bytes;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_IO,
			       (IMG_VOID *) BasePAddr.uiAddr,
			       0,
			       BasePAddr.uiAddr, NULL, ui32Bytes, "unknown", 0);
#endif

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

	return psLinuxMemArea;
}

IMG_VOID FreeIOLinuxMemArea(LinuxMemArea * psLinuxMemArea)
{
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_IO);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_IO,
				  (IMG_VOID *) psLinuxMemArea->uData.sIO.
				  CPUPhysAddr.uiAddr, __FILE__, __LINE__);
#endif

	LinuxMemAreaStructFree(psLinuxMemArea);
}

LinuxMemArea *NewAllocPagesLinuxMemArea(IMG_UINT32 ui32Bytes,
					IMG_UINT32 ui32AreaFlags)
{
	LinuxMemArea *psLinuxMemArea;
	IMG_UINT32 ui32PageCount;
	struct page **pvPageList;
	IMG_UINT32 i;

	psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea) {
		goto failed_area_alloc;
	}

	ui32PageCount = RANGE_TO_PAGES(ui32Bytes);
	pvPageList =
	    VMallocWrapper(sizeof(void *) * ui32PageCount, PVRSRV_HAP_CACHED);
	if (!pvPageList) {
		goto failed_vmalloc;
	}

	for (i = 0; i < ui32PageCount; i++) {
		pvPageList[i] = alloc_pages(GFP_KERNEL, 0);
		if (!pvPageList[i]) {
			goto failed_alloc_pages;
		}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		SetPageReserved(pvPageList[i]);
#else
		mem_map_reserve(pvPageList[i]);
#endif
#endif

	}

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES,
			       pvPageList,
			       0, 0, NULL, PAGE_ALIGN(ui32Bytes), "unknown", 0);
#endif

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_ALLOC_PAGES;
	psLinuxMemArea->uData.sPageList.pvPageList = pvPageList;
	psLinuxMemArea->ui32ByteSize = ui32Bytes;

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordAdd(psLinuxMemArea, ui32AreaFlags);
#endif

	return psLinuxMemArea;

failed_alloc_pages:
	for (i--; i >= 0; i--) {
		__free_pages(pvPageList[i], 0);
	}
	VFreeWrapper(pvPageList);
failed_vmalloc:
	LinuxMemAreaStructFree(psLinuxMemArea);
failed_area_alloc:
	PVR_DPF((PVR_DBG_ERROR, "%s: failed", __FUNCTION__));

	return NULL;
}

IMG_VOID FreeAllocPagesLinuxMemArea(LinuxMemArea * psLinuxMemArea)
{
	IMG_UINT32 ui32PageCount;
	struct page **pvPageList;
	IMG_UINT32 i;

	PVR_ASSERT(psLinuxMemArea);
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_ALLOC_PAGES);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

	ui32PageCount = RANGE_TO_PAGES(psLinuxMemArea->ui32ByteSize);
	pvPageList = psLinuxMemArea->uData.sPageList.pvPageList;

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES, pvPageList,
				  __FILE__, __LINE__);
#endif

	for (i = 0; i < ui32PageCount; i++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		ClearPageReserved(pvPageList[i]);
#else
		mem_map_reserve(pvPageList[i]);
#endif
#endif
		__free_pages(pvPageList[i], 0);
	}
	VFreeWrapper(psLinuxMemArea->uData.sPageList.pvPageList);

	LinuxMemAreaStructFree(psLinuxMemArea);
}

struct page *LinuxMemAreaOffsetToPage(LinuxMemArea * psLinuxMemArea,
				      IMG_UINT32 ui32ByteOffset)
{
	IMG_UINT32 ui32PageIndex;
	IMG_CHAR *pui8Addr;

	switch (psLinuxMemArea->eAreaType) {
	case LINUX_MEM_AREA_ALLOC_PAGES:
		ui32PageIndex = PHYS_TO_PFN(ui32ByteOffset);
		return psLinuxMemArea->uData.sPageList.
		    pvPageList[ui32PageIndex];
		break;
	case LINUX_MEM_AREA_VMALLOC:
		pui8Addr = psLinuxMemArea->uData.sVmalloc.pvVmallocAddress;
		pui8Addr += ui32ByteOffset;
		return vmalloc_to_page(pui8Addr);
		break;
	case LINUX_MEM_AREA_SUB_ALLOC:
		return LinuxMemAreaOffsetToPage(psLinuxMemArea->uData.sSubAlloc.
						psParentLinuxMemArea,
						psLinuxMemArea->uData.sSubAlloc.
						ui32ByteOffset +
						ui32ByteOffset);
	default:
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Unsupported request for struct page from LinuxMemArea with type=%s",
			 LinuxMemAreaTypeToString(psLinuxMemArea->eAreaType)));
		return NULL;
	}
}

IMG_VOID *_KMapWrapper(struct page * psPage, IMG_CHAR * pszFileName,
		       IMG_UINT32 ui32Line)
{
	IMG_VOID *pvRet;

#if defined(PVR_FLUSH_CACHE_BEFORE_KMAP)

	flush_cache_all();
#endif

	pvRet = kmap(psPage);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	if (pvRet) {
		DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_KMAP,
				       psPage,
				       pvRet, 0, NULL, PAGE_SIZE, "unknown", 0);
	}
#endif

	return pvRet;
}

IMG_VOID
_KUnMapWrapper(struct page * psPage, IMG_CHAR * pszFileName,
	       IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_KMAP, psPage,
				  pszFileName, ui32Line);
#endif

	kunmap(psPage);
}

LinuxKMemCache *KMemCacheCreateWrapper(IMG_CHAR * pszName,
				       size_t Size,
				       size_t Align, IMG_UINT32 ui32Flags)
{
#if defined(DEBUG_LINUX_SLAB_ALLOCATIONS)
	ui32Flags |= SLAB_POISON | SLAB_RED_ZONE;
#endif
	return kmem_cache_create(pszName, Size, Align, ui32Flags, NULL
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22))
				 , NULL
#endif
	    );
}

IMG_VOID KMemCacheDestroyWrapper(LinuxKMemCache * psCache)
{
	kmem_cache_destroy(psCache);
}

IMG_VOID *_KMemCacheAllocWrapper(LinuxKMemCache * psCache,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
				 gfp_t Flags,
#else
				 int Flags,
#endif
				 IMG_CHAR * pszFileName, IMG_UINT32 ui32Line)
{
	IMG_VOID *pvRet;

	pvRet = kmem_cache_alloc(psCache, Flags);

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordAdd(DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE,
			       pvRet,
			       pvRet,
			       0,
			       psCache,
			       kmem_cache_size(psCache), pszFileName, ui32Line);
#endif

	return pvRet;
}

IMG_VOID
_KMemCacheFreeWrapper(LinuxKMemCache * psCache, IMG_VOID * pvObject,
		      IMG_CHAR * pszFileName, IMG_UINT32 ui32Line)
{
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
	DebugMemAllocRecordRemove(DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE, pvObject,
				  pszFileName, ui32Line);
#endif

	kmem_cache_free(psCache, pvObject);
}

const IMG_CHAR *KMemCacheNameWrapper(LinuxKMemCache * psCache)
{

	return "";
}

LinuxMemArea *NewSubLinuxMemArea(LinuxMemArea * psParentLinuxMemArea,
				 IMG_UINT32 ui32ByteOffset,
				 IMG_UINT32 ui32Bytes)
{
	LinuxMemArea *psLinuxMemArea;

	PVR_ASSERT((ui32ByteOffset + ui32Bytes) <=
		   psParentLinuxMemArea->ui32ByteSize);

	psLinuxMemArea = LinuxMemAreaStructAlloc();
	if (!psLinuxMemArea) {
		return NULL;
	}

	psLinuxMemArea->eAreaType = LINUX_MEM_AREA_SUB_ALLOC;
	psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea =
	    psParentLinuxMemArea;
	psLinuxMemArea->uData.sSubAlloc.ui32ByteOffset = ui32ByteOffset;
	psLinuxMemArea->ui32ByteSize = ui32Bytes;

#if defined(DEBUG_LINUX_MEM_AREAS)
	{
		DEBUG_LINUX_MEM_AREA_REC *psParentRecord;
		psParentRecord =
		    DebugLinuxMemAreaRecordFind(psParentLinuxMemArea);
		DebugLinuxMemAreaRecordAdd(psLinuxMemArea,
					   psParentRecord->ui32Flags);
	}
#endif

	return psLinuxMemArea;
}

IMG_VOID FreeSubLinuxMemArea(LinuxMemArea * psLinuxMemArea)
{
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC);

#if defined(DEBUG_LINUX_MEM_AREAS)
	DebugLinuxMemAreaRecordRemove(psLinuxMemArea);
#endif

	LinuxMemAreaStructFree(psLinuxMemArea);
}

static LinuxMemArea *LinuxMemAreaStructAlloc(IMG_VOID)
{
#if 0
	LinuxMemArea *psLinuxMemArea;
	psLinuxMemArea = kmem_cache_alloc(psLinuxMemAreaCache, GFP_KERNEL);
	printk(KERN_ERR "%s: psLinuxMemArea=%p\n", __FUNCTION__,
	       psLinuxMemArea);
	dump_stack();
	return psLinuxMemArea;
#else
	return KMemCacheAllocWrapper(psLinuxMemAreaCache, GFP_KERNEL);
#endif
}

static IMG_VOID LinuxMemAreaStructFree(LinuxMemArea * psLinuxMemArea)
{
	KMemCacheFreeWrapper(psLinuxMemAreaCache, psLinuxMemArea);

}

IMG_VOID LinuxMemAreaDeepFree(LinuxMemArea * psLinuxMemArea)
{
	switch (psLinuxMemArea->eAreaType) {
	case LINUX_MEM_AREA_VMALLOC:
		FreeVMallocLinuxMemArea(psLinuxMemArea);
		break;
	case LINUX_MEM_AREA_ALLOC_PAGES:
		FreeAllocPagesLinuxMemArea(psLinuxMemArea);
		break;
	case LINUX_MEM_AREA_IOREMAP:
		FreeIORemapLinuxMemArea(psLinuxMemArea);
		break;
	case LINUX_MEM_AREA_EXTERNAL_KV:
		FreeExternalKVLinuxMemArea(psLinuxMemArea);
		break;
	case LINUX_MEM_AREA_IO:
		FreeIOLinuxMemArea(psLinuxMemArea);
		break;
	case LINUX_MEM_AREA_SUB_ALLOC:
		FreeSubLinuxMemArea(psLinuxMemArea);
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR, "%s: Unknown are type (%d)\n",
			 __FUNCTION__, psLinuxMemArea->eAreaType));
	}
}

#if defined(DEBUG_LINUX_MEM_AREAS)
static IMG_VOID
DebugLinuxMemAreaRecordAdd(LinuxMemArea * psLinuxMemArea, IMG_UINT32 ui32Flags)
{
	DEBUG_LINUX_MEM_AREA_REC *psNewRecord;
	const char *pi8FlagsString;

	if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC) {
		g_LinuxMemAreaWaterMark += psLinuxMemArea->ui32ByteSize;
		if (g_LinuxMemAreaWaterMark > g_LinuxMemAreaHighWaterMark) {
			g_LinuxMemAreaHighWaterMark = g_LinuxMemAreaWaterMark;
		}
	}
	g_LinuxMemAreaCount++;

	psNewRecord = kmalloc(sizeof(DEBUG_LINUX_MEM_AREA_REC), GFP_KERNEL);
	if (psNewRecord) {

		psNewRecord->psLinuxMemArea = psLinuxMemArea;
		psNewRecord->ui32Flags = ui32Flags;
		psNewRecord->pid = current->pid;
		psNewRecord->psNext = g_LinuxMemAreaRecords;
		g_LinuxMemAreaRecords = psNewRecord;
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: failed to allocate linux memory area record.",
			 __FUNCTION__));
	}

	pi8FlagsString = HAPFlagsToString(ui32Flags);
	if (strstr(pi8FlagsString, "UNKNOWN")) {
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Unexpected flags (0x%08lx) associated with psLinuxMemArea @ 0x%08lx",
			 __FUNCTION__, ui32Flags, psLinuxMemArea));

	}
}

static DEBUG_LINUX_MEM_AREA_REC *DebugLinuxMemAreaRecordFind(LinuxMemArea *
							     psLinuxMemArea)
{
	DEBUG_LINUX_MEM_AREA_REC *psCurrentRecord;

	for (psCurrentRecord = g_LinuxMemAreaRecords;
	     psCurrentRecord; psCurrentRecord = psCurrentRecord->psNext) {
		if (psCurrentRecord->psLinuxMemArea == psLinuxMemArea) {
			return psCurrentRecord;
		}
	}
	return NULL;
}

static IMG_VOID DebugLinuxMemAreaRecordRemove(LinuxMemArea * psLinuxMemArea)
{
	DEBUG_LINUX_MEM_AREA_REC **ppsCurrentRecord;

	if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC) {
		g_LinuxMemAreaWaterMark -= psLinuxMemArea->ui32ByteSize;
	}
	g_LinuxMemAreaCount--;

	for (ppsCurrentRecord = &g_LinuxMemAreaRecords;
	     *ppsCurrentRecord;
	     ppsCurrentRecord = &((*ppsCurrentRecord)->psNext)) {
		if ((*ppsCurrentRecord)->psLinuxMemArea == psLinuxMemArea) {
			DEBUG_LINUX_MEM_AREA_REC *psNextRecord;

			psNextRecord = (*ppsCurrentRecord)->psNext;
			kfree(*ppsCurrentRecord);
			*ppsCurrentRecord = psNextRecord;
			return;
		}
	}

	PVR_DPF((PVR_DBG_ERROR,
		 "%s: couldn't find an entry for psLinuxMemArea=%p\n",
		 __FUNCTION__, psLinuxMemArea));
}
#endif

IMG_VOID *LinuxMemAreaToCpuVAddr(LinuxMemArea * psLinuxMemArea)
{
	switch (psLinuxMemArea->eAreaType) {
	case LINUX_MEM_AREA_VMALLOC:
		return psLinuxMemArea->uData.sVmalloc.pvVmallocAddress;
	case LINUX_MEM_AREA_IOREMAP:
		return psLinuxMemArea->uData.sIORemap.pvIORemapCookie;
	case LINUX_MEM_AREA_EXTERNAL_KV:
		return psLinuxMemArea->uData.sExternalKV.pvExternalKV;
	case LINUX_MEM_AREA_SUB_ALLOC:
		{
			IMG_CHAR *pAddr =
			    LinuxMemAreaToCpuVAddr(psLinuxMemArea->uData.
						   sSubAlloc.
						   psParentLinuxMemArea);
			if (!pAddr) {
				return NULL;
			}
			return pAddr +
			    psLinuxMemArea->uData.sSubAlloc.ui32ByteOffset;
		}
	default:
		return NULL;
	}
}

IMG_CPU_PHYADDR
LinuxMemAreaToCpuPAddr(LinuxMemArea * psLinuxMemArea, IMG_UINT32 ui32ByteOffset)
{
	IMG_CPU_PHYADDR CpuPAddr;

	CpuPAddr.uiAddr = 0;

	switch (psLinuxMemArea->eAreaType) {
	case LINUX_MEM_AREA_IOREMAP:
		{
			CpuPAddr = psLinuxMemArea->uData.sIORemap.CPUPhysAddr;
			CpuPAddr.uiAddr += ui32ByteOffset;
			break;
		}
	case LINUX_MEM_AREA_EXTERNAL_KV:
		{
			if (psLinuxMemArea->uData.sExternalKV.bPhysContig) {
				CpuPAddr =
				    SysSysPAddrToCpuPAddr(psLinuxMemArea->uData.
							  sExternalKV.uPhysAddr.
							  SysPhysAddr);
				CpuPAddr.uiAddr += ui32ByteOffset;
			} else {
				IMG_UINT32 ui32PageIndex =
				    PHYS_TO_PFN(ui32ByteOffset);
				IMG_SYS_PHYADDR SysPAddr =
				    psLinuxMemArea->uData.sExternalKV.uPhysAddr.
				    pSysPhysAddr[ui32PageIndex];

				CpuPAddr = SysSysPAddrToCpuPAddr(SysPAddr);
				CpuPAddr.uiAddr +=
				    ADDR_TO_PAGE_OFFSET(ui32ByteOffset);
			}
			break;
		}
	case LINUX_MEM_AREA_IO:
		{
			CpuPAddr = psLinuxMemArea->uData.sIO.CPUPhysAddr;
			CpuPAddr.uiAddr += ui32ByteOffset;
			break;
		}
	case LINUX_MEM_AREA_VMALLOC:
		{
			IMG_CHAR *pCpuVAddr;
			pCpuVAddr =
			    (IMG_CHAR *) psLinuxMemArea->uData.sVmalloc.
			    pvVmallocAddress;
			pCpuVAddr += ui32ByteOffset;
			CpuPAddr.uiAddr = VMallocToPhys(pCpuVAddr);
			break;
		}
	case LINUX_MEM_AREA_ALLOC_PAGES:
		{
			struct page *page;
			IMG_UINT32 ui32PageIndex = PHYS_TO_PFN(ui32ByteOffset);
			page =
			    psLinuxMemArea->uData.sPageList.
			    pvPageList[ui32PageIndex];
			CpuPAddr.uiAddr = page_to_phys(page);
			CpuPAddr.uiAddr += ADDR_TO_PAGE_OFFSET(ui32ByteOffset);
			break;
		}
	case LINUX_MEM_AREA_SUB_ALLOC:
		{
			CpuPAddr =
			    OSMemHandleToCpuPAddr(psLinuxMemArea->uData.
						  sSubAlloc.
						  psParentLinuxMemArea,
						  psLinuxMemArea->uData.
						  sSubAlloc.ui32ByteOffset +
						  ui32ByteOffset);
			break;
		}
	default:
		PVR_DPF((PVR_DBG_ERROR, "%s: Unknown LinuxMemArea type (%d)\n",
			 __FUNCTION__, psLinuxMemArea->eAreaType));
	}

	PVR_ASSERT(CpuPAddr.uiAddr);
	return CpuPAddr;
}

IMG_BOOL LinuxMemAreaPhysIsContig(LinuxMemArea * psLinuxMemArea)
{
	switch (psLinuxMemArea->eAreaType) {
	case LINUX_MEM_AREA_IOREMAP:
	case LINUX_MEM_AREA_IO:
		return IMG_TRUE;

	case LINUX_MEM_AREA_EXTERNAL_KV:
		return psLinuxMemArea->uData.sExternalKV.bPhysContig;

	case LINUX_MEM_AREA_VMALLOC:
	case LINUX_MEM_AREA_ALLOC_PAGES:
		return IMG_FALSE;

	case LINUX_MEM_AREA_SUB_ALLOC:
		PVR_DPF((PVR_DBG_WARNING,
			 "%s is meaningless for LinuxMemArea type (%d)",
			 __FUNCTION__, psLinuxMemArea->eAreaType));
		break;

	default:
		PVR_DPF((PVR_DBG_ERROR, "%s: Unknown LinuxMemArea type (%d)\n",
			 __FUNCTION__, psLinuxMemArea->eAreaType));
		break;
	}
	return IMG_FALSE;
}

LINUX_MEM_AREA_TYPE LinuxMemAreaRootType(LinuxMemArea * psLinuxMemArea)
{
	if (psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC) {
		return LinuxMemAreaRootType(psLinuxMemArea->uData.sSubAlloc.
					    psParentLinuxMemArea);
	} else {
		return psLinuxMemArea->eAreaType;
	}
}

const IMG_CHAR *LinuxMemAreaTypeToString(LINUX_MEM_AREA_TYPE eMemAreaType)
{

	switch (eMemAreaType) {
	case LINUX_MEM_AREA_IOREMAP:
		return "LINUX_MEM_AREA_IOREMAP";
	case LINUX_MEM_AREA_EXTERNAL_KV:
		return "LINUX_MEM_AREA_EXTERNAL_KV";
	case LINUX_MEM_AREA_IO:
		return "LINUX_MEM_AREA_IO";
	case LINUX_MEM_AREA_VMALLOC:
		return "LINUX_MEM_AREA_VMALLOC";
	case LINUX_MEM_AREA_SUB_ALLOC:
		return "LINUX_MEM_AREA_SUB_ALLOC";
	case LINUX_MEM_AREA_ALLOC_PAGES:
		return "LINUX_MEM_AREA_ALLOC_PAGES";
	default:
		PVR_ASSERT(0);
	}

	return "";
}

#if defined(DEBUG_LINUX_MEM_AREAS)
static off_t printLinuxMemAreaRecords(char *buffer, size_t count, off_t off)
{
	DEBUG_LINUX_MEM_AREA_REC *psRecord;
	off_t Ret;

	LinuxLockMutex(&gPVRSRVLock);

	if (!off) {
		if (count < 500) {
			Ret = 0;
			goto unlock_and_return;
		}
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
		Ret = printAppend(buffer, count, 0,
				  "Number of Linux Memory Areas: %lu\n"
				  "At the current water mark these areas correspond to %lu bytes (excluding SUB areas)\n"
				  "At the highest water mark these areas corresponded to %lu bytes (excluding SUB areas)\n"
				  "\nDetails for all Linux Memory Areas:\n"
				  "%s %-24s %s %s %-8s %-5s %s\n",
				  g_LinuxMemAreaCount,
				  g_LinuxMemAreaWaterMark,
				  g_LinuxMemAreaHighWaterMark,
				  "psLinuxMemArea",
				  "LinuxMemType",
				  "CpuVAddr",
				  "CpuPAddr", "Bytes", "Pid", "Flags");
#else
		Ret = printAppend(buffer, count, 0,
				  "<mem_areas_header>\n"
				  "\t<count>%lu</count>\n"
				  "\t<watermark key=\"mar0\" description=\"current\" bytes=\"%lu\"/>\n"
				  "\t<watermark key=\"mar1\" description=\"high\" bytes=\"%lu\"/>\n"
				  "</mem_areas_header>\n",
				  g_LinuxMemAreaCount,
				  g_LinuxMemAreaWaterMark,
				  g_LinuxMemAreaHighWaterMark);
#endif
		goto unlock_and_return;
	}

	for (psRecord = g_LinuxMemAreaRecords; --off && psRecord;
	     psRecord = psRecord->psNext) ;
	if (!psRecord) {
		Ret = END_OF_FILE;
		goto unlock_and_return;
	}

	if (count < 500) {
		Ret = 0;
		goto unlock_and_return;
	}

	Ret = printAppend(buffer, count, 0,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
			  "%8p       %-24s %8p %08lx %-8ld %-5u %08lx=(%s)\n",
#else
			  "<linux_mem_area>\n"
			  "\t<pointer>%8p</pointer>\n"
			  "\t<type>%s</type>\n"
			  "\t<cpu_virtual>%8p</cpu_virtual>\n"
			  "\t<cpu_physical>%08lx</cpu_physical>\n"
			  "\t<bytes>%ld</bytes>\n"
			  "\t<pid>%u</pid>\n"
			  "\t<flags>%08lx</flags>\n"
			  "\t<flags_string>%s</flags_string>\n"
			  "</linux_mem_area>\n",
#endif
			  psRecord->psLinuxMemArea,
			  LinuxMemAreaTypeToString(psRecord->psLinuxMemArea->
						   eAreaType),
			  LinuxMemAreaToCpuVAddr(psRecord->psLinuxMemArea),
			  LinuxMemAreaToCpuPAddr(psRecord->psLinuxMemArea,
						 0).uiAddr,
			  psRecord->psLinuxMemArea->ui32ByteSize, psRecord->pid,
			  psRecord->ui32Flags,
			  HAPFlagsToString(psRecord->ui32Flags)
	    );

unlock_and_return:

	LinuxUnLockMutex(&gPVRSRVLock);
	return Ret;
}
#endif

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
static off_t printMemoryRecords(char *buffer, size_t count, off_t off)
{
	DEBUG_MEM_ALLOC_REC *psRecord;
	off_t Ret;

	LinuxLockMutex(&gPVRSRVLock);

	if (!off) {
		if (count < 1000) {
			Ret = 0;
			goto unlock_and_return;
		}
#if !defined(DEBUG_LINUX_XML_PROC_FILES)

		Ret = printAppend(buffer, count, 0, "%-60s: %ld bytes\n",
				  "Current Water Mark of bytes allocated via kmalloc",
				  g_WaterMarkData
				  [DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes allocated via kmalloc",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Current Water Mark of bytes allocated via vmalloc",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes allocated via vmalloc",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Current Water Mark of bytes allocated via alloc_pages",
				g_WaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes allocated via alloc_pages",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Current Water Mark of bytes allocated via ioremap",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes allocated via ioremap",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Current Water Mark of bytes reserved for \"IO\" memory areas",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes allocated for \"IO\" memory areas",
				g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Current Water Mark of bytes allocated via kmem_cache_alloc",
				g_WaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes allocated via kmem_cache_alloc",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Current Water Mark of bytes mapped via kmap",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMAP]);
		Ret =
		    printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				"Highest Water Mark of bytes mapped via kmap",
				g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMAP]);

		Ret = printAppend(buffer, count, Ret, "\n");

		Ret = printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				  "The Current Water Mark for memory allocated from system RAM",
				  g_SysRAMWaterMark);
		Ret = printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				  "The Highest Water Mark for memory allocated from system RAM",
				  g_SysRAMHighWaterMark);
		Ret = printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				  "The Current Water Mark for memory allocated from IO memory",
				  g_IOMemWaterMark);
		Ret = printAppend(buffer, count, Ret, "%-60s: %ld bytes\n",
				  "The Highest Water Mark for memory allocated from IO memory",
				  g_IOMemHighWaterMark);

		Ret = printAppend(buffer, count, Ret, "\n");

		Ret =
		    printAppend(buffer, count, Ret,
				"Details for all known allocations:\n"
				"%-16s %-8s %-8s %-10s %-5s %-10s %s\n", "Type",
				"CpuVAddr", "CpuPAddr", "Bytes", "PID",
				"PrivateData", "Filename:Line");

#else

		Ret =
		    printAppend(buffer, count, 0,
				"<meminfo>\n<meminfo_header>\n");
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr0\" description=\"kmalloc_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr1\" description=\"kmalloc_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_KMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr2\" description=\"vmalloc_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr3\" description=\"vmalloc_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_VMALLOC]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr4\" description=\"alloc_pages_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr5\" description=\"alloc_pages_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_ALLOC_PAGES]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr6\" description=\"ioremap_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr7\" description=\"ioremap_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_IOREMAP]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr8\" description=\"io_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr9\" description=\"io_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_IO]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr10\" description=\"kmem_cache_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr11\" description=\"kmem_cache_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData
				[DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr12\" description=\"kmap_current\" bytes=\"%ld\"/>\n",
				g_WaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMAP]);
		Ret =
		    printAppend(buffer, count, Ret,
				"<watermark key=\"mr13\" description=\"kmap_high\" bytes=\"%ld\"/>\n",
				g_HighWaterMarkData[DEBUG_MEM_ALLOC_TYPE_KMAP]);

		Ret = printAppend(buffer, count, Ret, "\n");

		Ret = printAppend(buffer, count, Ret,
				  "<watermark key=\"mr14\" description=\"system_ram_current\" bytes=\"%ld\"/>\n",
				  g_SysRAMWaterMark);
		Ret = printAppend(buffer, count, Ret,
				  "<watermark key=\"mr15\" description=\"system_ram_high\" bytes=\"%ld\"/>\n",
				  g_SysRAMHighWaterMark);
		Ret = printAppend(buffer, count, Ret,
				  "<watermark key=\"mr16\" description=\"system_io_current\" bytes=\"%ld\"/>\n",
				  g_IOMemWaterMark);
		Ret = printAppend(buffer, count, Ret,
				  "<watermark key=\"mr17\" description=\"system_io_high\" bytes=\"%ld\"/>\n",
				  g_IOMemHighWaterMark);

		Ret = printAppend(buffer, count, Ret, "</meminfo_header>\n");

#endif

		goto unlock_and_return;
	}

	if (count < 1000) {
		Ret = 0;
		goto unlock_and_return;
	}

	for (psRecord = g_MemoryRecords; --off && psRecord;
	     psRecord = psRecord->psNext) ;
	if (!psRecord) {
#if defined(DEBUG_LINUX_XML_PROC_FILES)
		if (off == 0) {
			Ret = printAppend(buffer, count, 0, "</meminfo>\n");
			goto unlock_and_return;
		}
#endif
		Ret = END_OF_FILE;
		goto unlock_and_return;
	}

	if (psRecord->eAllocType != DEBUG_MEM_ALLOC_TYPE_KMEM_CACHE) {
		Ret = printAppend(buffer, count, 0,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
				  "%-16s %-8p %08lx %-10ld %-5d %-10s %s:%ld\n",
#else
				  "<allocation>\n"
				  "\t<type>%s</type>\n"
				  "\t<cpu_virtual>%-8p</cpu_virtual>\n"
				  "\t<cpu_physical>%08lx</cpu_physical>\n"
				  "\t<bytes>%ld</bytes>\n"
				  "\t<pid>%d</pid>\n"
				  "\t<private>%s</private>\n"
				  "\t<filename>%s</filename>\n"
				  "\t<line>%ld</line>\n" "</allocation>\n",
#endif
				  DebugMemAllocRecordTypeToString(psRecord->
								  eAllocType),
				  psRecord->pvCpuVAddr, psRecord->ulCpuPAddr,
				  psRecord->ui32Bytes, psRecord->pid, "NULL",
				  psRecord->pszFileName, psRecord->ui32Line);
	} else {
		Ret = printAppend(buffer, count, 0,
#if !defined(DEBUG_LINUX_XML_PROC_FILES)
				  "%-16s %-8p %08lx %-10ld %-5d %-10s %s:%ld\n",
#else
				  "<allocation>\n"
				  "\t<type>%s</type>\n"
				  "\t<cpu_virtual>%-8p</cpu_virtual>\n"
				  "\t<cpu_physical>%08lx</cpu_physical>\n"
				  "\t<bytes>%ld</bytes>\n"
				  "\t<pid>%d</pid>\n"
				  "\t<private>%s</private>\n"
				  "\t<filename>%s</filename>\n"
				  "\t<line>%ld</line>\n" "</allocation>\n",
#endif
				  DebugMemAllocRecordTypeToString(psRecord->
								  eAllocType),
				  psRecord->pvCpuVAddr, psRecord->ulCpuPAddr,
				  psRecord->ui32Bytes, psRecord->pid,
				  KMemCacheNameWrapper(psRecord->pvPrivateData),
				  psRecord->pszFileName, psRecord->ui32Line);
	}

unlock_and_return:

	LinuxUnLockMutex(&gPVRSRVLock);
	return Ret;
}
#endif

#if defined(DEBUG_LINUX_MEM_AREAS) || defined(DEBUG_LINUX_MMAP_AREAS)
const IMG_CHAR *HAPFlagsToString(IMG_UINT32 ui32Flags)
{
	static IMG_CHAR szFlags[50];
	IMG_UINT32 ui32Pos = 0;
	IMG_UINT32 ui32CacheTypeIndex, ui32MapTypeIndex;
	IMG_CHAR *apszCacheTypes[] = {
		"UNCACHED",
		"CACHED",
		"WRITECOMBINE",
		"UNKNOWN"
	};
	IMG_CHAR *apszMapType[] = {
		"KERNEL_ONLY",
		"SINGLE_PROCESS",
		"MULTI_PROCESS",
		"FROM_EXISTING_PROCESS",
		"NO_CPU_VIRTUAL",
		"UNKNOWN"
	};

	if (ui32Flags & PVRSRV_HAP_UNCACHED) {
		ui32CacheTypeIndex = 0;
	} else if (ui32Flags & PVRSRV_HAP_CACHED) {
		ui32CacheTypeIndex = 1;
	} else if (ui32Flags & PVRSRV_HAP_WRITECOMBINE) {
		ui32CacheTypeIndex = 2;
	} else {
		ui32CacheTypeIndex = 3;
		PVR_DPF((PVR_DBG_ERROR, "%s: unknown cache type (%d)",
			 __FUNCTION__,
			 (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK)));
	}

	if (ui32Flags & PVRSRV_HAP_KERNEL_ONLY) {
		ui32MapTypeIndex = 0;
	} else if (ui32Flags & PVRSRV_HAP_SINGLE_PROCESS) {
		ui32MapTypeIndex = 1;
	} else if (ui32Flags & PVRSRV_HAP_MULTI_PROCESS) {
		ui32MapTypeIndex = 2;
	} else if (ui32Flags & PVRSRV_HAP_FROM_EXISTING_PROCESS) {
		ui32MapTypeIndex = 3;
	} else if (ui32Flags & PVRSRV_HAP_NO_CPU_VIRTUAL) {
		ui32MapTypeIndex = 4;
	} else {
		ui32MapTypeIndex = 5;
		PVR_DPF((PVR_DBG_ERROR, "%s: unknown map type (%d)",
			 __FUNCTION__, (ui32Flags & PVRSRV_HAP_MAPTYPE_MASK)));
	}

	ui32Pos = sprintf(szFlags, "%s|", apszCacheTypes[ui32CacheTypeIndex]);
	sprintf(szFlags + ui32Pos, "%s", apszMapType[ui32MapTypeIndex]);

	return szFlags;
}
#endif