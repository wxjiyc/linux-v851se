/*************************************************************************/ /*!
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

/* Services headers */
#include <powervr/imgpixfmts.h>
#include <powervr/buffer_attribs.h>
#include "dc_osfuncs.h"
#include "dc_example.h"
#include "img_defs.h"
#include "kerneldisplay.h"



#if defined(LMA)
#define DC_PHYS_HEAP_ID		1
#else
#define DC_PHYS_HEAP_ID		0
#endif

/* FIXME remove this copy pasted function and use a proper #include */
static IMG_UINT32 IMGPixFmtGetBPP(IMG_PIXFMT eFmt)
{
	switch (eFmt)
	{
		case IMG_PIXFMT_B10G10R10A2_UNORM:
		case IMG_PIXFMT_R8G8B8A8_UNORM:
		case IMG_PIXFMT_R8G8B8X8_UNORM:
		case IMG_PIXFMT_B8G8R8A8_UNORM:
		case IMG_PIXFMT_B8G8R8X8_UNORM:
			return 32;

		case IMG_PIXFMT_B5G6R5_UNORM:
		case IMG_PIXFMT_B5G5R5A1_UNORM:
		case IMG_PIXFMT_B5G5R5X1_UNORM:
		case IMG_PIXFMT_UYVY:
		case IMG_PIXFMT_VYUY:
		case IMG_PIXFMT_YUYV:
		case IMG_PIXFMT_YVYU:
			return 16;

		case IMG_PIXFMT_YVU420_2PLANE:
		case IMG_PIXFMT_YUV420_2PLANE:
		case IMG_PIXFMT_YVU420_2PLANE_MACRO_BLOCK:
		case IMG_PIXFMT_YUV420_3PLANE:
		case IMG_PIXFMT_YVU420_3PLANE:
			return 12;

		default: return 0;
	}
}


/* FIXME: the following should be replaced by
 * GetBIFTilingHeapXStride(TILING_HEAP_TO_USE, &result). There was a problem
 * that the symbol "GetBIFTilingHeapXStride" was not found when loading
 * dc_example.ko and because of timing constraints, the solution used was to
 * hardcode the value. But really, anywhere the following macro is used must be
 * replaced by GetBIFTilingHeapXStride(TILING_HEAP_TO_USE, &result) */

/* Note Xstride is used to compute the numbers of tiles side by side on the X
 * axis according to the following formula: nb_tiles_on_x_axis = 2^{xstride +
 * 1}. */
#define XSTRIDE_FOR_TILING_HEAP_1 0
#define XSTRIDE_FOR_TILING_HEAP_2 1
#define XSTRIDE_FOR_TILING_HEAP_3 2
#define XSTRIDE_FOR_TILING_HEAP_4 3


static const unsigned int TILING_HEAP_1_WIDTH_BYTES_ON_TILING_MODE_0 = (1 << (XSTRIDE_FOR_TILING_HEAP_1 + 1)) * 256 / 2;
static const unsigned int TILING_HEAP_2_WIDTH_BYTES_ON_TILING_MODE_0 = (1 << (XSTRIDE_FOR_TILING_HEAP_2 + 1)) * 256 / 2;
static const unsigned int TILING_HEAP_3_WIDTH_BYTES_ON_TILING_MODE_0 = (1 << (XSTRIDE_FOR_TILING_HEAP_3 + 1)) * 256 / 2;
static const unsigned int TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0 = (1 << (XSTRIDE_FOR_TILING_HEAP_4 + 1)) * 256 / 2;

/*
	Enable to track contexts and buffers
*/
/* #define DCEX_DEBUG 1 */

/*
	Enable to get more debug. Only supported on UMA
*/
/* #define DCEX_VERBOSE 1*/

#if defined(DCEX_DEBUG)
	#define DCEX_DEBUG_PRINT(fmt, ...) \
		DC_OSDebugPrintf(DBGLVL_WARNING, fmt, __VA_ARGS__)
#else
	#define DCEX_DEBUG_PRINT(fmt, ...)
#endif

/*
	The number of inflight commands this display driver can handle
*/
#define MAX_COMMANDS_INFLIGHT 2

#ifdef NO_HARDWARE
#define MAX_PIPES 1
#else
#define MAX_PIPES 8
#endif

static IMG_UINT32 ui32ByteStride;
static IMG_UINT32 ePixelFormat;

static IMG_BOOL CheckBufferDimensions(void)
{
	const DC_EXAMPLE_MODULE_PARAMETERS *psModuleParams;
	IMG_UINT32 ui32BytesPP = 0;

	psModuleParams = DCExampleGetModuleParameters();
	if (NULL == psModuleParams)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, ": Cannot fetch module parameters\n");
		return IMG_FALSE;
	}

	if (psModuleParams->ui32Width == 0 ||
	    psModuleParams->ui32Height == 0 ||
	    psModuleParams->ui32Depth == 0)
	{
		DC_OSDebugPrintf(
			DBGLVL_WARNING,
			": Illegal module parameters (width %u, height %u, depth %u)\n",
			psModuleParams->ui32Width,
			psModuleParams->ui32Height,
			psModuleParams->ui32Depth);

		return IMG_FALSE;
	}

	switch (psModuleParams->ui32Depth)
	{
		case 32:
			switch (psModuleParams->ui32Format)
			{
				case 0:
					ePixelFormat = IMG_PIXFMT_B8G8R8A8_UNORM;
					break;
				case 1:
					ePixelFormat = IMG_PIXFMT_B10G10R10A2_UNORM;
					break;
				default:
					ePixelFormat = IMG_PIXFMT_UNKNOWN;
					break;
			}

			ui32BytesPP = 4;
			break;
		case 16:
			switch (psModuleParams->ui32Format)
			{
				case 0:
					ePixelFormat = IMG_PIXFMT_B5G6R5_UNORM;
					break;
				default:
					ePixelFormat = IMG_PIXFMT_UNKNOWN;
					break;
			}

			ui32BytesPP = 2;
			break;
		default:
			DC_OSDebugPrintf(DBGLVL_WARNING, ": Display depth %lu not supported\n", psModuleParams->ui32Depth);

			ePixelFormat = IMG_PIXFMT_UNKNOWN;
			break;
	}

	if (ePixelFormat == IMG_PIXFMT_UNKNOWN)
	{
		DC_OSDebugPrintf(DBGLVL_WARNING,
						 ": Display format %lu not supported for depth %lu\n",
						 psModuleParams->ui32Format, psModuleParams->ui32Depth);
		return IMG_FALSE;
	}

	if ((psModuleParams->ui32MemLayout == 1 && psModuleParams->ui32FBCFormat == 0) ||
	    (psModuleParams->ui32MemLayout != 1 && psModuleParams->ui32FBCFormat != 0))
	{
		DC_OSDebugPrintf(DBGLVL_ERROR,
				 ": Invalid memory layout/FBC mode combination (memory layout %u, FBC format %u)\n",
				 psModuleParams->ui32MemLayout,
				 psModuleParams->ui32FBCFormat);

		return IMG_FALSE;
	}

	ui32ByteStride = psModuleParams->ui32Width * ui32BytesPP;

	if (psModuleParams->ui32MemLayout == 2)
	{

		const unsigned int width_bytes = ui32ByteStride;

		if (width_bytes <= TILING_HEAP_1_WIDTH_BYTES_ON_TILING_MODE_0)
		{
			ui32ByteStride = TILING_HEAP_1_WIDTH_BYTES_ON_TILING_MODE_0;
		}
		else if (width_bytes <= TILING_HEAP_2_WIDTH_BYTES_ON_TILING_MODE_0)
		{
			ui32ByteStride = TILING_HEAP_2_WIDTH_BYTES_ON_TILING_MODE_0;
		}
		else if (width_bytes <= TILING_HEAP_3_WIDTH_BYTES_ON_TILING_MODE_0)
		{
			ui32ByteStride = TILING_HEAP_3_WIDTH_BYTES_ON_TILING_MODE_0;
		}
		else if (width_bytes <= TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0)
		{
			ui32ByteStride = TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0;
		}
		else
		{
			ui32ByteStride = TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0;
		}

	}

	DC_OSDebugPrintf(DBGLVL_INFO, " Width: %u pixels\n", psModuleParams->ui32Width);
	DC_OSDebugPrintf(DBGLVL_INFO, " Height: %u pixels\n", psModuleParams->ui32Height);
	DC_OSDebugPrintf(DBGLVL_INFO, " Stride: %u bytes\n", ui32ByteStride);
	DC_OSDebugPrintf(DBGLVL_INFO, " Depth: %u bits\n", psModuleParams->ui32Depth);
	DC_OSDebugPrintf(DBGLVL_INFO, " Format: %u\n", psModuleParams->ui32Format);
	DC_OSDebugPrintf(DBGLVL_INFO, " Memory layout: %u\n", psModuleParams->ui32MemLayout);
	DC_OSDebugPrintf(DBGLVL_INFO, " FBC format: %u\n", psModuleParams->ui32FBCFormat);
	DC_OSDebugPrintf(DBGLVL_INFO, " X-Dpi: %u\n", psModuleParams->ui32XDpi);
	DC_OSDebugPrintf(DBGLVL_INFO, " Y-Dpi: %u\n", psModuleParams->ui32YDpi);

	return IMG_TRUE;
}

/*
	As we only allow one display context to be created the config data
	fifo can be global data
*/
IMG_HANDLE g_hConfigData[MAX_COMMANDS_INFLIGHT];
IMG_UINT32 g_ui32Head = 0;
IMG_UINT32 g_ui32Tail = 0;

IMG_BOOL g_DisplayContextActive = IMG_FALSE;

static INLINE void DCExampleConfigPush(IMG_HANDLE hConfigData)
{
	g_hConfigData[g_ui32Head] = hConfigData;
	g_ui32Head++;
	if (g_ui32Head >= MAX_COMMANDS_INFLIGHT)
	{
		g_ui32Head = 0;
	}
}

static INLINE IMG_HANDLE DCExampleConfigPop(void)
{
	IMG_HANDLE hConfigData = g_hConfigData[g_ui32Tail];
	g_ui32Tail++;
	if (g_ui32Tail >= MAX_COMMANDS_INFLIGHT)
	{
		g_ui32Tail = 0;
	}
	return hConfigData;
}

static INLINE IMG_BOOL DCExampleConfigIsEmpty(void)
{
	return (g_ui32Tail == g_ui32Head);
}

typedef struct _DCEX_DEVICE_
{
	IMG_HANDLE		hPVRServicesConnection;
	IMG_HANDLE		hPVRServicesDevice;
	DC_SERVICES_FUNCS	sPVRServicesFuncs;

	IMG_HANDLE		hSrvHandle;
#if defined (LMA)
	PHYS_HEAP		*psPhysHeap;
	IMG_CPU_PHYADDR		sDispStartAddr;
	IMG_UINT64		uiDispMemSize;
	IMG_UINT32		ui32BufferSize;
	IMG_UINT32		ui32BufferCount;
	IMG_UINT32		ui32BufferUseMask;
#endif
} DCEX_DEVICE;

typedef enum
{
	DCEX_BUFFER_SOURCE_ALLOC,
	DCEX_BUFFER_SOURCE_IMPORT,
} DCEX_BUFFER_SOURCE;

typedef struct _DCEX_BUFFER_
{
	ATOMIC_T		i32RefCount;	/* Only required for system buffer */
	IMG_UINT32		ePixFormat;
	IMG_UINT32		ui32Width;
	IMG_UINT32		ui32Height;
	IMG_UINT32		ui32ByteStride;
	IMG_UINT32		ui32Size;
	IMG_UINT32		ui32PageCount;
	IMG_HANDLE		hImport;
	IMG_DEV_PHYADDR		*pasDevPAddr;
	DCEX_DEVICE		*psDevice;
#if defined (LMA)
	IMG_UINT64		uiAllocHandle;
#else
	void			*pvAllocHandle;
#endif
	DCEX_BUFFER_SOURCE eSource;
} DCEX_BUFFER;

DCEX_DEVICE *g_psDeviceData = NULL;
DCEX_BUFFER *g_psSystemBuffer = NULL;

#if defined (LMA)
/*
	Simple unit size allocator
*/
static
IMG_UINT64 _DCExampleAllocLMABuffer(DCEX_DEVICE *psDevice)
{
	IMG_UINT32 i;
	IMG_UINT64 pvRet = 0;

	for (i = 0; i < psDevice->ui32BufferCount; i++)
	{
		if ((psDevice->ui32BufferUseMask & (1UL << i)) == 0)
		{
			pvRet = psDevice->sDispStartAddr.uiAddr +
					(i * psDevice->ui32BufferSize);
			psDevice->ui32BufferUseMask |= (1UL << i);
			break;
		}
	}

	return pvRet;
}

static
void _DCExampleFreeLMABuffer(DCEX_DEVICE *psDevice, IMG_UINT64 uiAddr)
{
	IMG_UINT64 ui64Offset;

	DC_ASSERT(uiAddr >= psDevice->sDispStartAddr.uiAddr);

	ui64Offset = uiAddr - psDevice->sDispStartAddr.uiAddr;
	ui64Offset = DC_OSDiv64(ui64Offset, psDevice->ui32BufferSize);
	DC_ASSERT(ui64Offset <= psDevice->ui32BufferCount);
	psDevice->ui32BufferUseMask &= ~(1UL << ui64Offset);
}
#endif /* LMA */

static
void DCExampleGetInfo(IMG_HANDLE hDeviceData,
						  DC_DISPLAY_INFO *psDisplayInfo)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	/*
		Copy our device name
	*/
	DC_OSStringNCopy(psDisplayInfo->szDisplayName, DRVNAME " 1", DC_NAME_SIZE);

	/*
		Report what our minimum and maximum display period is.
	*/
	psDisplayInfo->ui32MinDisplayPeriod	= 0;
	psDisplayInfo->ui32MaxDisplayPeriod	= 1;
	psDisplayInfo->ui32MaxPipes			= MAX_PIPES;
	psDisplayInfo->bUnlatchedSupported	= IMG_FALSE;
}

static
PVRSRV_ERROR DCExamplePanelQueryCount(IMG_HANDLE hDeviceData,
										 IMG_UINT32 *pui32NumPanels)
{
	PVR_UNREFERENCED_PARAMETER(hDeviceData);
	/*
		If you know the panel count at compile time just hardcode it, if it's
		dynamic you should probe it here
	*/
	*pui32NumPanels = 1;

	return PVRSRV_OK;
}

static
PVRSRV_ERROR DCExamplePanelQuery(IMG_HANDLE hDeviceData,
									IMG_UINT32 ui32PanelsArraySize,
									IMG_UINT32 *pui32NumPanels,
									PVRSRV_PANEL_INFO *psPanelInfo)
{
	const DC_EXAMPLE_MODULE_PARAMETERS *psModuleParams = DCExampleGetModuleParameters();

	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	if (NULL == psModuleParams)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, ": Cannot fetch module parameters\n");
		return PVRSRV_ERROR_NO_DEVICEDATA_FOUND;
	}

	/*
		If we have hotplug displays then there is a chance a display could
		have been removed so return the number of panels we have queried
	*/
	*pui32NumPanels = 1;

	/*
		Either hard code the values here or probe each panel here. If a new
		panel has been hotplugged then ignore it as we've not been given
		room to store its data
	*/
	psPanelInfo[0].sSurfaceInfo.sFormat.ePixFormat = ePixelFormat;
	psPanelInfo[0].sSurfaceInfo.sDims.ui32Width = psModuleParams->ui32Width;
	psPanelInfo[0].sSurfaceInfo.sDims.ui32Height = psModuleParams->ui32Height;

	psPanelInfo[0].ui32RefreshRate = psModuleParams->ui32RefreshRate;
	psPanelInfo[0].ui32XDpi = psModuleParams->ui32XDpi;
	psPanelInfo[0].ui32YDpi = psModuleParams->ui32YDpi;

	switch (psModuleParams->ui32MemLayout)
	{
		case 0:
			psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout = PVRSRV_SURFACE_MEMLAYOUT_STRIDED;
			break;
		case 1:
			psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout = PVRSRV_SURFACE_MEMLAYOUT_FBC;
			break;
		case 2:
			psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout = PVRSRV_SURFACE_MEMLAYOUT_BIF_PAGE_TILED;
			break;
		default:
			DC_OSDebugPrintf(DBGLVL_ERROR, ": Unknown memory layout %ld\n", psModuleParams->ui32MemLayout);

			return PVRSRV_ERROR_INVALID_PARAMS;
	}

	switch (psModuleParams->ui32FBCFormat)
	{
		case 0:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_NONE;
			break;
		case 1:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_DIRECT_8x8;
			break;
		case 2:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_DIRECT_16x4;
			break;
		case 3:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_INDIRECT_8x8;
			break;
		case 4:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_INDIRECT_16x4;
			break;
		case 5:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_DIRECT_32x2;
			break;
		case 6:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_DIRECT_LOSSY_8x8;
			break;
		case 7:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_DIRECT_LOSSY_16x4;
			break;
		case 8:
			psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode = IMG_FB_COMPRESSION_DIRECT_LOSSY_32x2;
			break;
		default:
			DC_OSDebugPrintf(DBGLVL_ERROR, ": Unknown FBC format %ld\n", psModuleParams->ui32FBCFormat);

			return PVRSRV_ERROR_INVALID_PARAMS;
	}

	DC_ASSERT((psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout == PVRSRV_SURFACE_MEMLAYOUT_FBC &&
		   psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode != IMG_FB_COMPRESSION_NONE) ||
		  (psPanelInfo[0].sSurfaceInfo.sFormat.eMemLayout != PVRSRV_SURFACE_MEMLAYOUT_FBC &&
		   psPanelInfo[0].sSurfaceInfo.sFormat.u.sFBCLayout.eFBCompressionMode == IMG_FB_COMPRESSION_NONE));

	return PVRSRV_OK;
}

static
PVRSRV_ERROR DCExampleFormatQuery(IMG_HANDLE hDeviceData,
									IMG_UINT32 ui32NumFormats,
									PVRSRV_SURFACE_FORMAT *pasFormat,
									IMG_UINT32 *pui32Supported)
{
	IMG_UINT32 i;

	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	for (i = 0; i < ui32NumFormats; i++)
	{
		pui32Supported[i] = 0;

		/*
			If the display controller has multiple display pipes (DMA engines)
			each one should be checked to see if it supports the specified
			format.
		*/
		if ((pasFormat[i].ePixFormat == IMG_PIXFMT_B10G10R10A2_UNORM) ||
		    (pasFormat[i].ePixFormat == IMG_PIXFMT_B8G8R8A8_UNORM) ||
		    (pasFormat[i].ePixFormat == IMG_PIXFMT_B5G6R5_UNORM))
		{
			pui32Supported[i]++;
		}
	}

	return PVRSRV_OK;
}

static
PVRSRV_ERROR DCExampleDimQuery(IMG_HANDLE hDeviceData,
								 IMG_UINT32 ui32NumDims,
								 PVRSRV_SURFACE_DIMS *psDim,
								 IMG_UINT32 *pui32Supported)
{
	IMG_UINT32 i;
	const DC_EXAMPLE_MODULE_PARAMETERS *psModuleParams = DCExampleGetModuleParameters();

	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	if (NULL == psModuleParams)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, ": Cannot fetch module parameters\n");
		return PVRSRV_ERROR_NO_DEVICEDATA_FOUND;
	}

	for (i = 0; i < ui32NumDims; i++)
	{
		pui32Supported[i] = 0;

		/*
			If the display controller has multiple display pipes (DMA engines)
			each one should be checked to see if it supports the specified
			dimension.
		*/
		if ((psDim[i].ui32Width == psModuleParams->ui32Width)
		&&  (psDim[i].ui32Height == psModuleParams->ui32Height))
		{
			pui32Supported[i]++;
		}
	}

	return PVRSRV_OK;
}

static
PVRSRV_ERROR DCExampleBufferSystemAcquire(IMG_HANDLE hDeviceData,
											IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
											IMG_UINT32 *pui32PageCount,
											IMG_UINT32 *pui32PhysHeapID,
											IMG_UINT32 *pui32ByteStride,
											IMG_HANDLE *phSystemBuffer)
{
	DCEX_BUFFER *psBuffer;

	PVR_UNREFERENCED_PARAMETER(hDeviceData);

	/*
		This function is optional. It provides a method for services
		to acquire a display buffer which it didn't setup but was created
		by the OS (e.g. Linux frame buffer).
		If the OS should trigger a mode change then it's not allowed to free
		the previous buffer until services has released it via BufferSystemRelease
	*/


	/*
		Take a reference to the system buffer 1st to make sure it isn't freed
	*/
	DC_OSAtomicIncrement(&g_psSystemBuffer->i32RefCount);
	psBuffer = g_psSystemBuffer;

	*puiLog2PageSize = DC_OSGetPageShift();
	*pui32PageCount = psBuffer->ui32Size >> DC_OSGetPageShift();
	*pui32PhysHeapID = DC_PHYS_HEAP_ID;
	*pui32ByteStride = psBuffer->ui32ByteStride;
	*phSystemBuffer = psBuffer;

	return PVRSRV_OK;
}

static
void DCExampleBufferSystemRelease(IMG_HANDLE hSystemBuffer)
{
	DCEX_BUFFER *psBuffer = hSystemBuffer;

	DC_OSAtomicDecrement(&psBuffer->i32RefCount);

	/*
		If the system buffer has changed and we've just dropped the last
		refcount then free the buffer
	*/
	if ((g_psSystemBuffer != psBuffer) && (DC_OSAtomicRead(&psBuffer->i32RefCount) == 0))
	{
		/* Free the buffer and it's memory (if the memory was allocated) */
	}
}

static
PVRSRV_ERROR DCExampleContextCreate(IMG_HANDLE hDeviceData,
									  IMG_HANDLE *hDisplayContext)
{
	/*
		The creation of a display context is a software concept and
		it's an "agreement" between the services client and the DC driver
		as to what this means (if anything)
	*/
	if (!g_DisplayContextActive)
	{
		*hDisplayContext = hDeviceData;

		g_DisplayContextActive = IMG_TRUE;
		DCEX_DEBUG_PRINT("Create context (%p)\n", *hDisplayContext);
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
}

static
PVRSRV_ERROR DCExampleContextConfigureCheck(IMG_HANDLE hDisplayContext,
											  IMG_UINT32 ui32PipeCount,
											  PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
											  IMG_HANDLE *ahBuffers)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 i;
	const DC_EXAMPLE_MODULE_PARAMETERS *psModuleParams = DCExampleGetModuleParameters();

	PVR_UNREFERENCED_PARAMETER(hDisplayContext);

	if (NULL == psModuleParams)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, ": Cannot fetch module parameters\n");
		return PVRSRV_ERROR_NO_DEVICEDATA_FOUND;
	}

	/*
		The display engine might have a limit on the number of discretely
		configurable pipes. In such a case an error should be returned.
	*/
	if (ui32PipeCount > MAX_PIPES)
	{
		eError = PVRSRV_ERROR_DC_TOO_MANY_PIPES;
		goto fail_max_pipes;
	}

	/*
		This optional function allows the display driver to check if the display
		configuration passed in is valid.
		It's possible that due to HW constraints that although the client has
		honoured the DimQuery and FormatQuery results the configuration it
		has requested is still not possible (e.g. there isn't enough space in
		the display controller's MMU, or due restrictions on display pipes.
	*/

	for (i = 0; i < ui32PipeCount; i++)
	{
		DCEX_BUFFER *psBuffer = ahBuffers[i];

		if (pasSurfAttrib[i].sCrop.sDims.ui32Width  != psModuleParams->ui32Width  ||
		    pasSurfAttrib[i].sCrop.sDims.ui32Height != psModuleParams->ui32Height ||
		    pasSurfAttrib[i].sCrop.i32XOffset != 0 ||
		    pasSurfAttrib[i].sCrop.i32YOffset != 0)
		{
			eError = PVRSRV_ERROR_DC_INVALID_CROP_RECT;
			break;
		}

		if (pasSurfAttrib[i].sDisplay.sDims.ui32Width !=
			pasSurfAttrib[i].sCrop.sDims.ui32Width ||
			pasSurfAttrib[i].sDisplay.sDims.ui32Height !=
			pasSurfAttrib[i].sCrop.sDims.ui32Height ||
			pasSurfAttrib[i].sDisplay.i32XOffset !=
			pasSurfAttrib[i].sCrop.i32XOffset ||
			pasSurfAttrib[i].sDisplay.i32YOffset !=
			pasSurfAttrib[i].sCrop.i32YOffset)
		{
			eError = PVRSRV_ERROR_DC_INVALID_DISPLAY_RECT;
			break;
		}

		if (psBuffer->ui32Width != psModuleParams->ui32Width
		||  psBuffer->ui32Height != psModuleParams->ui32Height)
		{
			eError = PVRSRV_ERROR_DC_INVALID_BUFFER_DIMS;
			break;
		}
	}

fail_max_pipes:
	return eError;
}

static
void DCExampleContextConfigure(IMG_HANDLE hDisplayContext,
									 IMG_UINT32 ui32PipeCount,
									 PVRSRV_SURFACE_CONFIG_INFO *pasSurfAttrib,
									 IMG_HANDLE *ahBuffers,
									 IMG_UINT32 ui32DisplayPeriod,
									 IMG_HANDLE hConfigData)
{
	DCEX_DEVICE *psDeviceData = hDisplayContext;
	IMG_UINT32 i;

	/*
		As we have no HW and thus no VSync IRQ we just activate the
		new config here
	*/
	for (i = 0; i < ui32PipeCount; i++)
	{
#if defined(DCEX_DEBUG)
		DCEX_BUFFER *psBuffer = ahBuffers[i];
		/*
			There is no checking to be done here as we can't fail,
			any checking should have been DCExampleContextConfigureCheck.
		*/

		/*
			If the display controller supports scaling it should set it up
			here.
		*/

		/*
			Setup the DMA from the display buffer
		*/

		/*
			Save the config data as we need to pass it back once this
			configuration gets retired
		*/
		DCEX_DEBUG_PRINT("Display buffer (%p)\n", psBuffer);
#endif /* DCEX_DEBUG */
	}

	/*
		As we have no HW and thus no VSync IRQ we just retire the
		previous config as soon as we get a new one
	*/
	if (!DCExampleConfigIsEmpty())
	{
		/* Retire the current config */
		psDeviceData->sPVRServicesFuncs.pfnDCDisplayConfigurationRetired(DCExampleConfigPop());
	}

	if (ui32PipeCount != 0)
	{
		/* Save our new config data */
		DCExampleConfigPush(hConfigData);
	}
	else
	{
		/*
			When the client requests the display context to be destroyed
			services will issue a "NULL" flip to us so we can retire
			the current configuration.

			We need to pop the current (and last) configuration off the our
			stack and retire it which we're already done above as it's our
			default behaviour to retire the previous config immediately when we
			get a new one. We also need to "retire" the NULL flip so the DC core
			knows that we've done what ever we need to do so it can start to
			tear down the display context

			In real devices we could have a number of configurations in flight,
			all these configurations _and the NULL flip_ need to be processed
			in order with the NULL flip being completed when it's active rather
			then when it will be retired (as it will never be retired)

			At this point there is nothing that the display is being asked to
			display by services and it's the DC driver implementation decision
			as to what it should then do. Typically, for systems that have a
			system surface the DC would switch back to displaying that.
		*/
		DCEX_DEBUG_PRINT("Display flushed (%p)\n", hDisplayContext);

		psDeviceData->sPVRServicesFuncs.pfnDCDisplayConfigurationRetired(hConfigData);
	}
}

static
void DCExampleContextDestroy(IMG_HANDLE hDisplayContext)
{
	PVR_UNREFERENCED_PARAMETER(hDisplayContext);

	DC_ASSERT(DCExampleConfigIsEmpty());

	/*
		Counter part to ContextCreate. Any buffers created/imported
		on this display context will have been freed before this call
		so all the display driver needs to do is release any resources
		allocated at ContextCreate time.
	*/
	g_DisplayContextActive = IMG_FALSE;
	DCEX_DEBUG_PRINT("Destroy display context (%p)\n", hDisplayContext);
}

static
PVRSRV_ERROR DCExampleBufferAlloc(IMG_HANDLE hDisplayContext,
									DC_BUFFER_CREATE_INFO *psCreateInfo,
									IMG_DEVMEM_LOG2ALIGN_T *puiLog2PageSize,
									IMG_UINT32 *pui32PageCount,
									IMG_UINT32 *pui32PhysHeapID,
									IMG_UINT32 *pui32ByteStride,
									IMG_HANDLE *phBuffer)
{
	DCEX_BUFFER *psBuffer;
	PVRSRV_SURFACE_INFO *psSurfInfo = &psCreateInfo->sSurface;
	PVRSRV_ERROR eError;
	DCEX_DEVICE *psDevice = hDisplayContext;

	/*
		Allocate the buffer control structure
	*/

	psBuffer = DC_OSCallocMem(sizeof(DCEX_BUFFER));

	if (psBuffer == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_bufferalloc;
	}

	/* Common setup */
	psBuffer->eSource = DCEX_BUFFER_SOURCE_ALLOC;
	DC_OSAtomicWrite(&psBuffer->i32RefCount, 1);
	psBuffer->ePixFormat = psSurfInfo->sFormat.ePixFormat;

	/*
		Store the display buffer size, that is the pixel width and height that
		will be displayed which might be less then the pixel width and height
		of the buffer
	*/
	psBuffer->ui32Width = psSurfInfo->sDims.ui32Width;
	psBuffer->ui32Height = psSurfInfo->sDims.ui32Height;

	switch (psSurfInfo->sFormat.eMemLayout)
	{
		case PVRSRV_SURFACE_MEMLAYOUT_STRIDED:
			/*
				As we're been asked to allocate this buffer we decide what it's
				stride should be.
			*/
			psBuffer->ui32ByteStride = psSurfInfo->sDims.ui32Width * psCreateInfo->ui32BPP;
			psBuffer->ui32Size = psBuffer->ui32Height * psBuffer->ui32ByteStride;
			break;
		case PVRSRV_SURFACE_MEMLAYOUT_FBC:
			psBuffer->ui32ByteStride = psCreateInfo->u.sFBC.ui32FBCStride * psCreateInfo->ui32BPP;
			psBuffer->ui32Size = psCreateInfo->u.sFBC.ui32Size;

			/*
				Here we should program the FBC registers in the display
				controller according to the information we have in
				psSurfInfo->u.sFBC
			*/
			break;
		case PVRSRV_SURFACE_MEMLAYOUT_BIF_PAGE_TILED:
		{

			const unsigned int width_bytes = psBuffer->ui32Width *
				// divide by 8 .. because IMGPixFmtGetBPP returns _bits_ per pixel
				(IMGPixFmtGetBPP(psBuffer->ePixFormat) >> 3);

			if (width_bytes <= TILING_HEAP_1_WIDTH_BYTES_ON_TILING_MODE_0)
			{
				psBuffer->ui32ByteStride = TILING_HEAP_1_WIDTH_BYTES_ON_TILING_MODE_0;
			}
			else if (width_bytes <= TILING_HEAP_2_WIDTH_BYTES_ON_TILING_MODE_0)
			{
				psBuffer->ui32ByteStride = TILING_HEAP_2_WIDTH_BYTES_ON_TILING_MODE_0;
			}
			else if (width_bytes <= TILING_HEAP_3_WIDTH_BYTES_ON_TILING_MODE_0)
			{
				psBuffer->ui32ByteStride = TILING_HEAP_3_WIDTH_BYTES_ON_TILING_MODE_0;
			}
			else if (width_bytes <= TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0)
			{
				psBuffer->ui32ByteStride = TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0;
			}
			else
			{
				psBuffer->ui32ByteStride = TILING_HEAP_4_WIDTH_BYTES_ON_TILING_MODE_0;
			}

			psBuffer->ui32Size = psBuffer->ui32Height * psBuffer->ui32ByteStride;
			break;
		}
		default:
			eError = PVRSRV_ERROR_NOT_SUPPORTED;
			break;
	}

	psBuffer->psDevice = psDevice;

	/*
		Allocate display addressable memory. We only need physical addresses
		at this stage.

		Note: This could be deferred until the 1st map or acquire call.
	*/
#if defined (LMA)
	psBuffer->uiAllocHandle = _DCExampleAllocLMABuffer(psDevice);
	if (psBuffer->uiAllocHandle == 0)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_buffermemalloc;
	}
#else
	psBuffer->pvAllocHandle = DCExampleVirtualAllocUncached(psBuffer->ui32Size);
	if (psBuffer->pvAllocHandle == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_buffermemalloc;
	}
#endif
	*pui32ByteStride = psBuffer->ui32ByteStride;
	*puiLog2PageSize = DC_OSGetPageShift();
	*pui32PageCount = DC_OS_BYTES_TO_PAGES(psBuffer->ui32Size);
	*pui32PhysHeapID = DC_PHYS_HEAP_ID;
	*phBuffer = psBuffer;

	DCEX_DEBUG_PRINT("Allocate buffer (%p)\n", psBuffer);
	return PVRSRV_OK;

fail_buffermemalloc:
	DC_OSFreeMem(psBuffer);

fail_bufferalloc:
	return eError;
}

#if !defined (LMA)
static
PVRSRV_ERROR DCExampleBufferImport(IMG_HANDLE hDisplayContext,
									 IMG_UINT32 ui32NumPlanes,
									 IMG_HANDLE **paphImport,
									 DC_BUFFER_IMPORT_INFO *psSurfAttrib,
									 IMG_HANDLE *phBuffer)
{
	/*
		This it optional and should only be provided if the display controller
		can access "general" memory (e.g. the memory doesn't have to contiguous)
	*/
	const DC_EXAMPLE_MODULE_PARAMETERS *psModuleParams = DCExampleGetModuleParameters();
	DCEX_BUFFER *psBuffer;
	DCEX_DEVICE *psDevice = hDisplayContext;

	if (NULL == psModuleParams)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, ": Cannot fetch module parameters\n");
		return PVRSRV_ERROR_NO_DEVICEDATA_FOUND;
	}

	/*
		Check to see if our display hardware supports this buffer
	*/
	if ((psSurfAttrib->ePixFormat != IMG_PIXFMT_B8G8R8A8_UNORM) ||
		(psSurfAttrib->ui32Width[0] != psModuleParams->ui32Width) ||
		(psSurfAttrib->ui32Height[0] != psModuleParams->ui32Height) ||
		(psSurfAttrib->ui32ByteStride[0] != ui32ByteStride))
	{
		return PVRSRV_ERROR_UNSUPPORTED_PIXEL_FORMAT;
	}

	psBuffer = DC_OSCallocMem(sizeof(DCEX_BUFFER));

	if (psBuffer == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuffer->eSource = DCEX_BUFFER_SOURCE_IMPORT;
	psBuffer->ui32Width = psSurfAttrib->ui32Width[0];
	DC_OSAtomicWrite(&psBuffer->i32RefCount, 1);
	psBuffer->ePixFormat = psSurfAttrib->ePixFormat;
	psBuffer->ui32ByteStride = psSurfAttrib->ui32ByteStride[0];
	psBuffer->ui32Width = psSurfAttrib->ui32Width[0];
	psBuffer->ui32Height = psSurfAttrib->ui32Height[0];
	psBuffer->psDevice = psDevice;

	/*
		If the display controller supports mapping "general" memory, but has
		limitations (e.g. if it doesn't have full range addressing) these
		should be checked here by calling DCImportBufferAcquire. In this case
		it lock down the physical address of the buffer at this strange rather
		then being able to defer it to map time.
	*/
	psBuffer->hImport = paphImport[0];

	*phBuffer = psBuffer;
	DCEX_DEBUG_PRINT("Import buffer (%p)\n", psBuffer);
	return PVRSRV_OK;
}
#endif

#if defined(CONFIG_L4)
/* In L4, the kernel PFN space is a virtual space and not a physical space */
#define	VMALLOC_TO_PAGE_PHYS(vAddr) l4x_virt_to_phys((void *)((phys_addr_t)page_to_pfn(vmalloc_to_page(vAddr)) << PAGE_SHIFT));
#else
#define	VMALLOC_TO_PAGE_PHYS(vAddr) page_to_phys(vmalloc_to_page(vAddr))
#endif

static
PVRSRV_ERROR DCExampleBufferAcquire(IMG_HANDLE hBuffer,
									IMG_DEV_PHYADDR *pasDevPAddr,
									void **ppvLinAddr)
{
	DCEX_BUFFER *psBuffer = hBuffer;
#if defined (LMA)
	IMG_UINT32 i;
	unsigned long ulPages = DC_OS_BYTES_TO_PAGES(psBuffer->ui32Size);
	PHYS_HEAP *psPhysHeap = psBuffer->psDevice->psPhysHeap;
	IMG_CPU_PHYADDR sCpuPAddr;
#else
	void *pvLinAddr;
#endif
	/*
		If we didn't allocate the display memory at buffer alloc time
		we would have to do it here.
	*/

	/*
		Fill in the array of addresses we where passed
	*/
#if defined (LMA)
	sCpuPAddr.uiAddr = psBuffer->uiAllocHandle;
	for (i = 0; i < ulPages; i++)
	{
		psBuffer->psDevice->sPVRServicesFuncs.pfnPhysHeapCpuPAddrToDevPAddr(
				psPhysHeap,
				1,
				&pasDevPAddr[i],
				&sCpuPAddr);

		sCpuPAddr.uiAddr += DC_OSGetPageSize();
	}
	*ppvLinAddr = NULL;
#else
	pvLinAddr = psBuffer->pvAllocHandle;
	DCExampleLinAddrToDevPAddrs(pvLinAddr, pasDevPAddr, psBuffer->ui32Size);
	*ppvLinAddr = psBuffer->pvAllocHandle;
#endif

	DCEX_DEBUG_PRINT("Acquire buffer (%p) memory\n", psBuffer);
	return PVRSRV_OK;
}

static
void DCExampleBufferRelease(IMG_HANDLE hBuffer)
{
#if defined(DCEX_DEBUG)
	DCEX_BUFFER *psBuffer = hBuffer;
#endif
	/*
		We could release the display memory here (assuming it wasn't
		still mapped into the display controller).

		As the buffer hasn't been freed the contents must be preserved, i.e.
		in the next call to Acquire different physical pages can be returned,
		but they must have the same contents as the old pages had at Release
		time.
	*/
	DCEX_DEBUG_PRINT("Release buffer (%p) memory\n", psBuffer);
}

static
void DCExampleBufferFree(IMG_HANDLE hBuffer)
{
	DCEX_BUFFER *psBuffer = hBuffer;

	DCEX_DEBUG_PRINT("Free buffer (%p)\n", psBuffer);
	if (psBuffer->eSource == DCEX_BUFFER_SOURCE_ALLOC)
	{
#if defined (LMA)
		DCEX_DEBUG_PRINT("Free buffer memory (%llu)\n", psBuffer->uiAllocHandle);
		_DCExampleFreeLMABuffer(psBuffer->psDevice, psBuffer->uiAllocHandle);
#else
		DCEX_DEBUG_PRINT("Free buffer memory (%p)\n", psBuffer->pvAllocHandle);
		DCExampleVirtualFree(psBuffer->pvAllocHandle, psBuffer->ui32Size);
#endif
	}
	DC_OSFreeMem(psBuffer);
}

static
PVRSRV_ERROR DCExampleBufferMap(IMG_HANDLE hBuffer)
{
	DCEX_BUFFER *psBuffer = hBuffer;
	IMG_UINT32 ui32PageCount;
	PVRSRV_ERROR eError;
#if defined (DCEX_VERBOSE)
	IMG_UINT32 i;
#endif
	/*
		If the display controller needs memory to be mapped into it
		(e.g. it has an MMU) and didn't do it in the alloc and import then it
		should provide this function.
	*/

	if (psBuffer->hImport)
	{
		IMG_DEV_PHYADDR *pasDevPAddr;
		/*
			In the case of an import buffer we didn't allocate the buffer and
			so need to ask for it's pages
		*/
		eError = psBuffer->psDevice->sPVRServicesFuncs.pfnDCImportBufferAcquire(
					psBuffer->hImport,
				   	DC_OSGetPageShift(),
				   	&ui32PageCount,
				   	&pasDevPAddr);

		if (eError != PVRSRV_OK)
		{
			goto fail_import;
		}
#if defined (DCEX_VERBOSE)
		for (i = 0; i < ui32PageCount; i++)
		{
			DCEX_DEBUG_PRINT(": DCExampleBufferMap: DCExample map address 0x%016llx\n", pasDevPAddr[i].uiAddr);
		}
#endif
		psBuffer->pasDevPAddr = pasDevPAddr;
		psBuffer->ui32PageCount = ui32PageCount;
	}
#if defined (DCEX_VERBOSE)
	else
	{
		unsigned long ulPages = DC_OS_BYTES_TO_PAGES(psBuffer->ui32Size);
		IMG_CPU_VIRTADDR pvLinAddr = psBuffer->pvAllocHandle;
		IMG_DEV_PHYADDR sDevPAddr;

		for (i = 0; i < ulPages; i++)
		{
			eError = DCExampleLinAddrToDevPAddrs(pvLinAddr, &sDevPAddr, DC_OSGetPageSize());
			if (PVRSRV_OK == eError)
			{
				DC_OSDebugPrintf(DBGLVL_INFO, ": DCExampleBufferMap: DCExample map address 0x%016llx\n", sDevPAddr.uiAddr);
			}

			pvLinAddr = (IMG_CPU_VIRTADDR)(((uintptr_t)pvLinAddr) + DC_OSGetPageSize());
		}
	}
#endif
	DCEX_DEBUG_PRINT("Map buffer (%p) into display\n", psBuffer);
	return PVRSRV_OK;

fail_import:
	return eError;
}

static
void DCExampleBufferUnmap(IMG_HANDLE hBuffer)
{
	DCEX_BUFFER *psBuffer = hBuffer;
#if defined (DCEX_VERBOSE)
	IMG_UINT32 i;
#endif
	/*
		If the display controller provided buffer map then it must provide
		this function
	*/

	/*
		Unmap the memory from the display controller's MMU
	*/
	if (psBuffer->hImport)
	{
#if defined (DCEX_VERBOSE)
		for (i = 0; i < psBuffer->ui32PageCount; i++)
		{
			DC_OSDebugPrintf(DBGLVL_INFO, ": DCExampleBufferUnmap: DCExample unmap address 0x%016llx\n", psBuffer->pasDevPAddr[i].uiAddr);
		}
#endif
		/*
			As this was an imported buffer we need to release it
		*/
		psBuffer->psDevice->sPVRServicesFuncs.pfnDCImportBufferRelease(
				psBuffer->hImport,
				psBuffer->pasDevPAddr);
	}
#if defined (DCEX_VERBOSE)
	else
	{
		unsigned long ulPages = DC_OS_BYTES_TO_PAGES(psBuffer->ui32Size);
		IMG_CPU_VIRTADDR pvLinAddr = psBuffer->pvAllocHandle;
		IMG_DEV_PHYADDR sDevPAddr;

		for (i = 0; i < ulPages; i++)
		{
			PVRSRV_ERROR eError = DCExampleLinAddrToDevPAddrs(pvLinAddr, &sDevPAddr, DC_OSGetPageSize());
			if (PVRSRV_OK == eError)
			{
				DC_OSDebugPrintf(DBGLVL_INFO, ": DCExampleBufferUnmap: DCExample unmap address 0x%016llx\n", sDevPAddr.uiAddr);
			}

			pvLinAddr = (IMG_CPU_VIRTADDR)(((uintptr_t)pvLinAddr) + DC_OSGetPageSize());
		}
	}
#endif
	DCEX_DEBUG_PRINT("Unmap buffer (%p) from display\n", psBuffer);
}

/*
	In this example driver we provide (almost) the full range of functions
*/

static DC_DEVICE_FUNCTIONS sDCFunctions;

/*
	functions exported by kernel services for use by 3rd party kernel display
	class device driver
*/
PVRSRV_ERROR DCExampleInit(void)
{
	DCEX_BUFFER *psBuffer = NULL;
	DCEX_DEVICE *psDeviceData = NULL;
	PVRSRV_ERROR eError;
#if defined (LMA)
	IMG_UINT64 ui64BufferCount;
#endif
	const DC_EXAMPLE_MODULE_PARAMETERS *psModuleParams;

	DC_OSSetDrvName(DRVNAME);

	/* Check the module params and setup global buffer size state */
	if (!CheckBufferDimensions())
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psModuleParams = DCExampleGetModuleParameters();
	if (NULL == psModuleParams)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, ": Cannot fetch module parameters\n");
		return PVRSRV_ERROR_NO_DEVICEDATA_FOUND;
	}

	/*
		If the display controller hasn't already been initialised elsewhere in
		the system then it should initialised here.

		Create the private data structure (psDeviceData) and store all the
		device specific data we will need later (e.g. pointer to mapped registered)
		This device specific private data will be passed into our callbacks so
		we don't need global data and can have more then one display controller
		driven by the same driver (we would just create an "instance" of a device
		by registering the same callbacks with different private data)
	*/

	psDeviceData = DC_OSAllocMem(sizeof(DCEX_DEVICE));
	if (psDeviceData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_devicealloc;
	}

	eError = DC_OSPVRServicesConnectionOpen(&psDeviceData->hPVRServicesConnection);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to open connection to PVR Services (%d)\n", __func__, eError);
		goto fail_servicesconnectionclose;
	}

	eError = DC_OSPVRServicesSetupFuncs(psDeviceData->hPVRServicesConnection, &psDeviceData->sPVRServicesFuncs);
	if (eError != PVRSRV_OK)
	{
		DC_OSDebugPrintf(DBGLVL_ERROR, " - %s: Failed to setup PVR Services function table (%d)\n", __func__, eError);
		goto fail_servicesconnectionclose;
	}

#if defined (LMA)
	/*
		If the display is using card memory then we need to know
		where that memory is so we have to acquire the heap we want
		to use (a carveout of the card memory) so we can get it's address
	*/
	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapAcquire(
				DC_PHYS_HEAP_ID,
				&psDeviceData->psPhysHeap);

	if (eError != PVRSRV_OK)
	{
		goto fail_heapacquire;
	}

	/* Sanity check we're operating on a LMA heap */
	DC_ASSERT(psDeviceData->sPVRServicesFuncs.pfnPhysHeapGetType(psDeviceData->psPhysHeap) == PHYS_HEAP_TYPE_LMA);

	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapRegionGetCpuPAddr(
				psDeviceData->psPhysHeap,
				0,
				&psDeviceData->sDispStartAddr);

	DC_ASSERT(eError == PVRSRV_OK);

	eError = psDeviceData->sPVRServicesFuncs.pfnPhysHeapRegionGetSize(
				psDeviceData->psPhysHeap,
				0,
			 	&psDeviceData->uiDispMemSize);

	DC_ASSERT(eError == PVRSRV_OK);
#endif
	/*
		If the display driver has a system surface create the buffer structure
		that describes it here.

		Note:
		All this data and the buffer should be queried from the OS, but in this
		example we don't have the ability to fetch this data from an OS driver,
		so we create the data.
	*/
	psBuffer = DC_OSCallocMem(sizeof(DCEX_BUFFER));

	if (psBuffer == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_bufferalloc;
	}

	DC_OSAtomicWrite(&psBuffer->i32RefCount, 1);
	psBuffer->ePixFormat = ePixelFormat;
	psBuffer->ui32Width = psModuleParams->ui32Width;
	psBuffer->ui32Height = psModuleParams->ui32Height;
	psBuffer->ui32ByteStride = ui32ByteStride;
	psBuffer->ui32Size = psBuffer->ui32Height * psBuffer->ui32ByteStride;
	psBuffer->ui32Size = (psBuffer->ui32Size + DC_OSGetPageSize() - 1) & DC_OSGetPageMask();
	psBuffer->psDevice = psDeviceData;

#if defined(LMA)
	/* Simple allocator, assume all buffers are going to be the same size. */
	ui64BufferCount = psDeviceData->uiDispMemSize;
	ui64BufferCount = DC_OSDiv64(ui64BufferCount, psBuffer->ui32Size);

	psDeviceData->ui32BufferCount = (IMG_UINT32) ui64BufferCount;
	DC_ASSERT((IMG_UINT32) ui64BufferCount == psDeviceData->ui32BufferCount);

	if (psDeviceData->ui32BufferCount > 32)
	{
		psDeviceData->ui32BufferCount = 32;
	}

	psDeviceData->ui32BufferSize = psBuffer->ui32Size;
	psDeviceData->ui32BufferUseMask = 0;

	psBuffer->uiAllocHandle = _DCExampleAllocLMABuffer(psDeviceData);
	if (psBuffer->uiAllocHandle == 0)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_buffermemalloc;
	}
#else
	psBuffer->pvAllocHandle = DCExampleVirtualAllocUncached(psBuffer->ui32Size);
	if (psBuffer->pvAllocHandle == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_buffermemalloc;
	}
#endif
	DCEX_DEBUG_PRINT("Allocate system buffer = %p\n", psBuffer);

	/* Initialise DC Function Table */
	DC_OSMemSet(&sDCFunctions, 0, sizeof(sDCFunctions));

	sDCFunctions.pfnGetInfo					= DCExampleGetInfo;
	sDCFunctions.pfnPanelQueryCount			= DCExamplePanelQueryCount;
	sDCFunctions.pfnPanelQuery				= DCExamplePanelQuery;
	sDCFunctions.pfnFormatQuery				= DCExampleFormatQuery;
	sDCFunctions.pfnDimQuery				= DCExampleDimQuery;
	sDCFunctions.pfnSetBlank				= NULL;
	sDCFunctions.pfnSetVSyncReporting		= NULL;
	sDCFunctions.pfnLastVSyncQuery			= NULL;
	sDCFunctions.pfnContextCreate			= DCExampleContextCreate;
	sDCFunctions.pfnContextDestroy			= DCExampleContextDestroy;
	sDCFunctions.pfnContextConfigure		= DCExampleContextConfigure;
	sDCFunctions.pfnContextConfigureCheck	= DCExampleContextConfigureCheck;
	sDCFunctions.pfnBufferAlloc				= DCExampleBufferAlloc;
	sDCFunctions.pfnBufferAcquire			= DCExampleBufferAcquire;
	sDCFunctions.pfnBufferRelease			= DCExampleBufferRelease;
	sDCFunctions.pfnBufferFree				= DCExampleBufferFree;
#if !defined (LMA)
	sDCFunctions.pfnBufferImport			= DCExampleBufferImport;
#endif
	sDCFunctions.pfnBufferMap				= DCExampleBufferMap;
	sDCFunctions.pfnBufferUnmap				= DCExampleBufferUnmap;
	sDCFunctions.pfnBufferSystemAcquire		= DCExampleBufferSystemAcquire;
	sDCFunctions.pfnBufferSystemRelease		= DCExampleBufferSystemRelease;

	/*
		Register our DC driver with services
	*/
	eError = psDeviceData->sPVRServicesFuncs.pfnDCRegisterDevice(
				&sDCFunctions,
				MAX_COMMANDS_INFLIGHT,
				psDeviceData,
				&psDeviceData->hSrvHandle);

	if (eError != PVRSRV_OK)
	{
		goto fail_register;
	}

	/* Save the device data somewhere we can retrieve it */
	g_psDeviceData = psDeviceData;

	/* Store the system buffer on the global hook */
	g_psSystemBuffer = psBuffer;

	return PVRSRV_OK;

fail_register:
#if defined (LMA)
	_DCExampleFreeLMABuffer(psDeviceData, psBuffer->uiAllocHandle);
#else
	DCExampleVirtualFree(psBuffer->pvAllocHandle, psBuffer->ui32Size);
#endif
fail_buffermemalloc:
	DC_OSFreeMem(psBuffer);

fail_bufferalloc:
#if defined (LMA)
	psDeviceData->sPVRServicesFuncs.pfnPhysHeapRelease(psDeviceData->psPhysHeap);
fail_heapacquire:
#endif

	DC_OSPVRServicesConnectionClose(psDeviceData->hPVRServicesConnection);

fail_servicesconnectionclose:
	DC_OSFreeMem(psDeviceData);

fail_devicealloc:
	return eError;
}

void DCExampleDeinit(void)
{
	DCEX_DEVICE *psDeviceData = g_psDeviceData;
	DCEX_BUFFER *psBuffer = g_psSystemBuffer;

	psDeviceData->sPVRServicesFuncs.pfnDCUnregisterDevice(psDeviceData->hSrvHandle);

	DCEX_DEBUG_PRINT("Free system buffer = %p\n", psBuffer);

#if defined (LMA)
	_DCExampleFreeLMABuffer(psDeviceData, psBuffer->uiAllocHandle);
	psDeviceData->sPVRServicesFuncs.pfnPhysHeapRelease(psDeviceData->psPhysHeap);
#else
	DCExampleVirtualFree(psBuffer->pvAllocHandle, psBuffer->ui32Size);
#endif

	DC_OSPVRServicesConnectionClose(psDeviceData->hPVRServicesConnection);

	DC_OSFreeMem(psBuffer);
	DC_OSFreeMem(psDeviceData);
}

