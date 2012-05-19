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

#ifndef _PDUMP_KM_H_
#define _PDUMP_KM_H_


#define PDUMP_FLAGS_NEVER			0x08000000
#define PDUMP_FLAGS_TOOUT2MEM		0x10000000
#define PDUMP_FLAGS_LASTFRAME		0x20000000
#define PDUMP_FLAGS_RESETLFBUFFER	0x40000000
#define PDUMP_FLAGS_CONTINUOUS		0x80000000

#define PDUMP_PD_UNIQUETAG			(IMG_HANDLE)0
#define PDUMP_PT_UNIQUETAG			(IMG_HANDLE)0

#ifndef PDUMP
#define MAKEUNIQUETAG(hMemInfo)	(0)
#endif

#ifdef PDUMP

#define MAKEUNIQUETAG(hMemInfo)	(((BM_BUF *)(((PVRSRV_KERNEL_MEM_INFO *)hMemInfo)->sMemBlk.hBuffer))->pMapping)

#define PDUMP_REG_FUNC_NAME PDumpReg

	IMG_IMPORT PVRSRV_ERROR PDumpMemPolKM(PVRSRV_KERNEL_MEM_INFO *
					      psMemInfo, IMG_UINT32 ui32Offset,
					      IMG_UINT32 ui32Value,
					      IMG_UINT32 ui32Mask,
					      PDUMP_POLL_OPERATOR eOperator,
					      IMG_BOOL bLastFrame,
					      IMG_BOOL bOverwrite,
					      IMG_HANDLE hUniqueTag);

	IMG_IMPORT PVRSRV_ERROR PDumpMemKM(IMG_PVOID pvAltLinAddr,
					   PVRSRV_KERNEL_MEM_INFO * psMemInfo,
					   IMG_UINT32 ui32Offset,
					   IMG_UINT32 ui32Bytes,
					   IMG_UINT32 ui32Flags,
					   IMG_HANDLE hUniqueTag);
	PVRSRV_ERROR PDumpMemPagesKM(PVRSRV_DEVICE_TYPE eDeviceType,
				     IMG_DEV_PHYADDR * pPages,
				     IMG_UINT32 ui32NumPages,
				     IMG_DEV_VIRTADDR sDevAddr,
				     IMG_UINT32 ui32Start,
				     IMG_UINT32 ui32Length,
				     IMG_UINT32 ui32Flags,
				     IMG_HANDLE hUniqueTag);

	PVRSRV_ERROR PDumpMem2KM(PVRSRV_DEVICE_TYPE eDeviceType,
				 IMG_CPU_VIRTADDR pvLinAddr,
				 IMG_UINT32 ui32Bytes,
				 IMG_UINT32 ui32Flags,
				 IMG_BOOL bInitialisePages,
				 IMG_HANDLE hUniqueTag1,
				 IMG_HANDLE hUniqueTag2);
	IMG_VOID PDumpInit(IMG_VOID);
	IMG_VOID PDumpDeInit(IMG_VOID);
	IMG_IMPORT PVRSRV_ERROR PDumpSetFrameKM(IMG_UINT32 ui32Frame);
	IMG_IMPORT PVRSRV_ERROR PDumpCommentKM(IMG_CHAR * pszComment,
					       IMG_UINT32 ui32Flags);
	IMG_IMPORT PVRSRV_ERROR PDumpDriverInfoKM(IMG_CHAR * pszString,
						  IMG_UINT32 ui32Flags);
	PVRSRV_ERROR PDumpRegWithFlagsKM(IMG_UINT32 ui32RegAddr,
					 IMG_UINT32 ui32RegValue,
					 IMG_UINT32 ui32Flags);
	IMG_IMPORT PVRSRV_ERROR PDumpBitmapKM(IMG_CHAR * pszFileName,
					      IMG_UINT32 ui32FileOffset,
					      IMG_UINT32 ui32Width,
					      IMG_UINT32 ui32Height,
					      IMG_UINT32 ui32StrideInBytes,
					      IMG_DEV_VIRTADDR sDevBaseAddr,
					      IMG_UINT32 ui32Size,
					      PDUMP_PIXEL_FORMAT ePixelFormat,
					      PDUMP_MEM_FORMAT eMemFormat,
					      IMG_UINT32 ui32PDumpFlags);
	IMG_IMPORT PVRSRV_ERROR PDumpReadRegKM(IMG_CHAR * pszFileName,
					       IMG_UINT32 ui32FileOffset,
					       IMG_UINT32 ui32Address,
					       IMG_UINT32 ui32Size,
					       IMG_UINT32 ui32PDumpFlags);
	IMG_VOID PDUMP_REG_FUNC_NAME(IMG_UINT32 dwReg, IMG_UINT32 dwData);

	IMG_VOID PDumpMsvdxRegRead(const IMG_CHAR * const pRegRegion,
				   const IMG_UINT32 dwRegOffset);

	IMG_VOID PDumpMsvdxRegWrite(const IMG_CHAR * const pRegRegion,
				    const IMG_UINT32 dwRegOffset,
				    const IMG_UINT32 dwData);

	PVRSRV_ERROR PDumpMsvdxRegPol(const IMG_CHAR * const pRegRegion,
				      const IMG_UINT32 ui32Offset,
				      const IMG_UINT32 ui32CheckFuncIdExt,
				      const IMG_UINT32 ui32RequValue,
				      const IMG_UINT32 ui32Enable,
				      const IMG_UINT32 ui32PollCount,
				      const IMG_UINT32 ui32TimeOut);

	PVRSRV_ERROR PDumpMsvdxWriteRef(const IMG_CHAR * const pRegRegion,
					const IMG_UINT32 ui32VLROffset,
					const IMG_UINT32 ui32Physical);

	IMG_VOID PDumpComment(IMG_CHAR * pszFormat, ...);
	IMG_VOID PDumpCommentWithFlags(IMG_UINT32 ui32Flags,
				       IMG_CHAR * pszFormat, ...);
	PVRSRV_ERROR PDumpRegPolKM(IMG_UINT32 ui32RegAddr,
				   IMG_UINT32 ui32RegValue,
				   IMG_UINT32 ui32Mask);
	PVRSRV_ERROR PDumpRegPolWithFlagsKM(IMG_UINT32 ui32RegAddr,
					    IMG_UINT32 ui32RegValue,
					    IMG_UINT32 ui32Mask,
					    IMG_UINT32 ui32Flags);
	IMG_BOOL PDumpIsLastCaptureFrameKM(IMG_VOID);
	IMG_IMPORT IMG_BOOL PDumpIsCaptureFrameKM(IMG_VOID);

	IMG_VOID PDumpMallocPages(PVRSRV_DEVICE_TYPE eDeviceType,
				  IMG_UINT32 ui32DevVAddr,
				  IMG_CPU_VIRTADDR pvLinAddr,
				  IMG_HANDLE hOSMemHandle,
				  IMG_UINT32 ui32NumBytes,
				  IMG_HANDLE hUniqueTag);
	IMG_VOID PDumpMallocPagesPhys(PVRSRV_DEVICE_TYPE eDeviceType,
				      IMG_UINT32 ui32DevVAddr,
				      IMG_PUINT32 pui32PhysPages,
				      IMG_UINT32 ui32NumPages,
				      IMG_HANDLE hUniqueTag);
	IMG_VOID PDumpMallocPageTable(PVRSRV_DEVICE_TYPE eDeviceType,
				      IMG_CPU_VIRTADDR pvLinAddr,
				      IMG_UINT32 ui32NumBytes,
				      IMG_HANDLE hUniqueTag);
	IMG_VOID PDumpFreePages(struct _BM_HEAP_ *psBMHeap,
				IMG_DEV_VIRTADDR sDevVAddr,
				IMG_UINT32 ui32NumBytes,
				IMG_HANDLE hUniqueTag, IMG_BOOL bInterleaved);
	IMG_VOID PDumpFreePageTable(PVRSRV_DEVICE_TYPE eDeviceType,
				    IMG_CPU_VIRTADDR pvLinAddr,
				    IMG_UINT32 ui32NumBytes,
				    IMG_HANDLE hUniqueTag);
	IMG_VOID PDumpPDReg(IMG_UINT32 ui32Reg,
			    IMG_UINT32 ui32dwData, IMG_HANDLE hUniqueTag);
	IMG_VOID PDumpPDRegWithFlags(IMG_UINT32 ui32Reg,
				     IMG_UINT32 ui32Data,
				     IMG_UINT32 ui32Flags,
				     IMG_HANDLE hUniqueTag);

	PVRSRV_ERROR PDumpPDDevPAddrKM(PVRSRV_KERNEL_MEM_INFO * psMemInfo,
				       IMG_UINT32 ui32Offset,
				       IMG_DEV_PHYADDR sPDDevPAddr,
				       IMG_HANDLE hUniqueTag1,
				       IMG_HANDLE hUniqueTag2);

	IMG_BOOL PDumpTestNextFrame(IMG_UINT32 ui32CurrentFrame);

	IMG_VOID PDumpTASignatureRegisters(IMG_UINT32 ui32DumpFrameNum,
					   IMG_UINT32 ui32TAKickCount,
					   IMG_BOOL bLastFrame,
					   IMG_UINT32 * pui32Registers,
					   IMG_UINT32 ui32NumRegisters);

	IMG_VOID PDump3DSignatureRegisters(IMG_UINT32 ui32DumpFrameNum,
					   IMG_BOOL bLastFrame,
					   IMG_UINT32 * pui32Registers,
					   IMG_UINT32 ui32NumRegisters);

	IMG_VOID PDumpRegRead(const IMG_UINT32 dwRegOffset,
			      IMG_UINT32 ui32Flags);

	IMG_VOID PDumpCycleCountRegRead(const IMG_UINT32 dwRegOffset,
					IMG_BOOL bLastFrame);

	IMG_VOID PDumpCounterRegisters(IMG_UINT32 ui32DumpFrameNum,
				       IMG_BOOL bLastFrame,
				       IMG_UINT32 * pui32Registers,
				       IMG_UINT32 ui32NumRegisters);

	IMG_VOID PDumpEndInitPhase(IMG_VOID);

	void PDumpCBP(PPVRSRV_KERNEL_MEM_INFO psROffMemInfo,
		      IMG_UINT32 ui32ROffOffset,
		      IMG_UINT32 ui32WPosVal,
		      IMG_UINT32 ui32PacketSize,
		      IMG_UINT32 ui32BufferSize,
		      IMG_UINT32 ui32Flags, IMG_HANDLE hUniqueTag);

	IMG_VOID PDumpIDLWithFlags(IMG_UINT32 ui32Clocks, IMG_UINT32 ui32Flags);
	IMG_VOID PDumpIDL(IMG_UINT32 ui32Clocks);

	IMG_VOID PDumpSuspendKM(IMG_VOID);
	IMG_VOID PDumpResumeKM(IMG_VOID);

#define PDUMPMEMPOL				PDumpMemPolKM
#define PDUMPMEM				PDumpMemKM
#define PDUMPMEM2				PDumpMem2KM
#define PDUMPINIT				PDumpInit
#define PDUMPDEINIT				PDumpDeInit
#define PDUMPISLASTFRAME		PDumpIsLastCaptureFrameKM
#define PDUMPTESTFRAME			PDumpIsCaptureFrameKM
#define PDUMPTESTNEXTFRAME		PDumpTestNextFrame
#define PDUMPREGWITHFLAGS		PDumpRegWithFlagsKM
#define PDUMPREG				PDUMP_REG_FUNC_NAME
#define PDUMPCOMMENT			PDumpComment
#define PDUMPCOMMENTWITHFLAGS	PDumpCommentWithFlags
#define PDUMPREGPOL				PDumpRegPolKM
#define PDUMPREGPOLWITHFLAGS	PDumpRegPolWithFlagsKM
#define PDUMPMALLOCPAGES		PDumpMallocPages
#define PDUMPMALLOCPAGETABLE	PDumpMallocPageTable
#define PDUMPFREEPAGES			PDumpFreePages
#define PDUMPFREEPAGETABLE		PDumpFreePageTable
#define PDUMPPDREG				PDumpPDReg
#define PDUMPPDREGWITHFLAGS		PDumpPDRegWithFlags
#define PDUMPCBP				PDumpCBP
#define PDUMPMALLOCPAGESPHYS	PDumpMallocPagesPhys
#define PDUMPENDINITPHASE		PDumpEndInitPhase
#define PDUMPMSVDXREGWRITE		PDumpMsvdxRegWrite
#define PDUMPMSVDXREGREAD		PDumpMsvdxRegRead
#define PDUMPMSVDXPOL			PDumpMsvdxRegPol
#define PDUMPMSVDXWRITEREF		PDumpMsvdxWriteRef
#define PDUMPBITMAPKM			PDumpBitmapKM
#define PDUMPDRIVERINFO			PDumpDriverInfoKM
#define PDUMPIDLWITHFLAGS		PDumpIDLWithFlags
#define PDUMPIDL				PDumpIDL
#define PDUMPSUSPEND			PDumpSuspendKM
#define PDUMPRESUME				PDumpResumeKM

#else
#define PDUMPMEMPOL(args...)
#define PDUMPMEM(args...)
#define PDUMPMEM2(args...)
#define PDUMPINIT(args...)
#define PDUMPDEINIT(args...)
#define PDUMPISLASTFRAME(args...)
#define PDUMPTESTFRAME(args...)
#define PDUMPTESTNEXTFRAME(args...)
#define PDUMPREGWITHFLAGS(args...)
#define PDUMPREG(args...)
#define PDUMPCOMMENT(args...)
#define PDUMPREGPOL(args...)
#define PDUMPREGPOLWITHFLAGS(args...)
#define PDUMPMALLOCPAGES(args...)
#define PDUMPMALLOCPAGETABLE(args...)
#define PDUMPFREEPAGES(args...)
#define PDUMPFREEPAGETABLE(args...)
#define PDUMPPDREG(args...)
#define PDUMPPDREGWITHFLAGS(args...)
#define PDUMPSYNC(args...)
#define PDUMPCOPYTOMEM(args...)
#define PDUMPWRITE(args...)
#define PDUMPCBP(args...)
#define PDUMPCOMMENTWITHFLAGS(args...)
#define PDUMPMALLOCPAGESPHYS(args...)
#define PDUMPENDINITPHASE(args...)
#define PDUMPMSVDXREG(args...)
#define PDUMPMSVDXREGWRITE(args...)
#define PDUMPMSVDXREGREAD(args...)
#define PDUMPMSVDXPOLEQ(args...)
#define PDUMPMSVDXPOL(args...)
#define PDUMPBITMAPKM(args...)
#define PDUMPDRIVERINFO(args...)
#define PDUMPIDLWITHFLAGS(args...)
#define PDUMPIDL(args...)
#define PDUMPSUSPEND(args...)
#define PDUMPRESUME(args...)
#define PDUMPMSVDXWRITEREF(args...)
#endif

#endif
