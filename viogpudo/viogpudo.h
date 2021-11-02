#pragma once

#include "helper.h"
#include "qxl_escape.h"

#pragma pack(push)
#pragma pack(1)

typedef struct
{
    UINT DriverStarted : 1; // ( 1) 1 after StartDevice and 0 after StopDevice
    UINT HardwareInit : 1; // ( 1) 1 after StartDevice and 0 after StopDevice
    UINT VirtIOInit : 1; // ( 1) 1 after VirtIoDeviceInit and 0 after StopDevice
    UINT Unused : 29;
} DRIVER_STATUS_FLAG;

#pragma pack(pop)

#ifdef VIOGPU_X86

#pragma pack(push)
#pragma pack(1)


typedef struct
{
    CHAR Signature[4];
    USHORT Version;
    ULONG OemStringPtr;
    LONG Capabilities;
    ULONG VideoModePtr;
    USHORT TotalMemory;
    USHORT OemSoftwareRevision;
    ULONG OemVendorNamePtr;
    ULONG OemProductNamePtr;
    ULONG OemProductRevPtr;
    CHAR Reserved[222];
} VBE_INFO, *PVBE_INFO;

typedef struct
{
/* Mandatory information for all VBE revisions */
    USHORT ModeAttributes;
    UCHAR WinAAttributes;
    UCHAR WinBAttributes;
    USHORT WinGranularity;
    USHORT WinSize;
    USHORT WinASegment;
    USHORT WinBSegment;
    ULONG WinFuncPtr;
    USHORT BytesPerScanLine;
/* Mandatory information for VBE 1.2 and above */
    USHORT XResolution;
    USHORT YResolution;
    UCHAR XCharSize;
    UCHAR YCharSize;
    UCHAR NumberOfPlanes;
    UCHAR BitsPerPixel;
    UCHAR NumberOfBanks;
    UCHAR MemoryModel;
    UCHAR BankSize;
    UCHAR NumberOfImagePages;
    UCHAR Reserved1;
/* Direct Color fields (required for Direct/6 and YUV/7 memory models) */
    UCHAR RedMaskSize;
    UCHAR RedFieldPosition;
    UCHAR GreenMaskSize;
    UCHAR GreenFieldPosition;
    UCHAR BlueMaskSize;
    UCHAR BlueFieldPosition;
    UCHAR ReservedMaskSize;
    UCHAR ReservedFieldPosition;
    UCHAR DirectColorModeInfo;
/* Mandatory information for VBE 2.0 and above */
    ULONG PhysBasePtr;
    ULONG Reserved2;
    USHORT Reserved3;
    /* Mandatory information for VBE 3.0 and above */
    USHORT LinBytesPerScanLine;
    UCHAR BnkNumberOfImagePages;
    UCHAR LinNumberOfImagePages;
    UCHAR LinRedMaskSize;
    UCHAR LinRedFieldPosition;
    UCHAR LinGreenMaskSize;
    UCHAR LinGreenFieldPosition;
    UCHAR LinBlueMaskSize;
    UCHAR LinBlueFieldPosition;
    UCHAR LinReservedMaskSize;
    UCHAR LinReservedFieldPosition;
    ULONG MaxPixelClock;
    CHAR Reserved4[189];
} VBE_MODEINFO, *PVBE_MODEINFO;

#pragma pack(pop)

typedef struct _X86BIOS_REGISTERS	// invented names
{
    ULONG Eax;
    ULONG Ecx;
    ULONG Edx;
    ULONG Ebx;
    ULONG Ebp;
    ULONG Esi;
    ULONG Edi;
    USHORT SegDs;
    USHORT SegEs;
} X86BIOS_REGISTERS, *PX86BIOS_REGISTERS;

#ifdef __cplusplus
extern "C" {
#endif

NTHALAPI BOOLEAN x86BiosCall (ULONG, PX86BIOS_REGISTERS);

NTHALAPI NTSTATUS x86BiosAllocateBuffer (ULONG *, USHORT *, USHORT *);
NTHALAPI NTSTATUS x86BiosFreeBuffer (USHORT, USHORT);

NTHALAPI NTSTATUS x86BiosReadMemory (USHORT, USHORT, PVOID, ULONG);
NTHALAPI NTSTATUS x86BiosWriteMemory (USHORT, USHORT, PVOID, ULONG);

#ifdef __cplusplus
}
#endif

#endif // VIOGPU_X86

/*  Undocumented imports from the HAL  */

typedef enum {
    GPU_DEVICE,
    VGA_DEVICE,
    INVALID_DEVICE,
}WIN_GPU_DEVICE_TYPE;

// Represents the current mode, may not always be set (i.e. frame buffer mapped) if representing the mode passed in on single mode setups.
typedef struct _CURRENT_BDD_MODE
{
    // The source mode currently set for HW Framebuffer
    // For sample driver this info filled in StartDevice by the OS and never changed.
    DXGK_DISPLAY_INFORMATION             DispInfo;

    // The rotation of the current mode. Rotation is performed in software during Present call
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION  Rotation;

    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    // This mode might be different from one which are supported for HW frame buffer
    // Scaling/displasment might be needed (if supported)
    UINT SrcModeWidth;
    UINT SrcModeHeight;

    // Various boolean flags the struct uses
    struct _CURRENT_BDD_MODE_FLAGS
    {
        UINT SourceNotVisible     : 1; // 0 if source is visible
        UINT FullscreenPresent    : 1; // 0 if should use dirty rects for present
        UINT FrameBufferIsActive  : 1; // 0 if not currently active (i.e. target not connected to source)
        UINT DoNotMapOrUnmap      : 1; // 1 if the FrameBuffer should not be (un)mapped during normal execution
        UINT IsInternal           : 1; // 1 if it was determined (i.e. through ACPI) that an internal panel is being driven
        UINT Unused               : 27;
    } Flags;

    // The start and end of physical memory known to be all zeroes. Used to optimize the BlackOutScreen function to not write
    // zeroes to memory already known to be zero. (Physical address is located in DispInfo)
    PHYSICAL_ADDRESS ZeroedOutStart;
    PHYSICAL_ADDRESS ZeroedOutEnd;

    union
    {
        VOID*                            Ptr;
        ULONG64                          Force8Bytes;
    } FrameBuffer;
} CURRENT_BDD_MODE;

class VioGpuDod;

class IVioGpuAdapter {
public:
    IVioGpuAdapter(_In_ VioGpuDod* pVioGpuDod) {m_pVioGpuDod = pVioGpuDod; m_Type = INVALID_DEVICE;}
    virtual ~IVioGpuAdapter(void) {;}
    virtual NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_BDD_MODE* pCurrentBddMode) = 0;
    virtual NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
    virtual NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
    virtual NTSTATUS HWClose(void) = 0;
    virtual BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber) = 0;
    virtual VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface) = 0;
    virtual VOID ResetDevice(void) = 0;

    virtual ULONG GetModeCount(void) = 0;
    PVIDEO_MODE_INFORMATION GetModeInfo(UINT idx) {return &m_ModeInfo[idx];}
    USHORT GetModeNumber(USHORT idx) {return m_ModeNumbers[idx];}
    USHORT GetCurrentModeIndex(void) {return m_CurrentMode;}
    VOID SetCurrentModeIndex(USHORT idx) {m_CurrentMode = idx;}
    virtual BOOLEAN EnablePointer(void) = 0;
    virtual NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*             DstAddr,
                                 _In_ UINT              DstBitPerPixel,
                                 _In_ BYTE*             SrcAddr,
                                 _In_ UINT              SrcBytesPerPixel,
                                 _In_ LONG              SrcPitch,
                                 _In_ ULONG             NumMoves,
                                 _In_ D3DKMT_MOVE_RECT* pMoves,
                                 _In_ ULONG             NumDirtyRects,
                                 _In_ RECT*             pDirtyRect,
                                 _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                                 _In_ const CURRENT_BDD_MODE* pModeCur) = 0;

    virtual VOID BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod) = 0;
    virtual NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pModeCur) = 0;
    virtual NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_BDD_MODE* pModeCur) = 0;
    virtual NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE *pEscape) = 0;
    ULONG GetId(void) { return m_Id; }
    WIN_GPU_DEVICE_TYPE GetType(void) { return m_Type;}
    VioGpuDod* GetVioGpu(void) {return m_pVioGpuDod;}
    virtual NTSTATUS AcquireFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode) = 0;
    virtual NTSTATUS ReleaseFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode) = 0;
protected:
    virtual NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
protected:
    VioGpuDod* m_pVioGpuDod;
    PVIDEO_MODE_INFORMATION m_ModeInfo;
    ULONG m_ModeCount;
    PUSHORT m_ModeNumbers;
    USHORT m_CurrentMode;
    USHORT m_CustomMode;
    ULONG  m_Id;
    WIN_GPU_DEVICE_TYPE m_Type;
};

#ifdef VIOGPU_X86

class VgaAdapter:
    public IVioGpuAdapter
{
public:
    VgaAdapter(_In_ VioGpuDod* pVioGpuDod);
    ~VgaAdapter(void);
    NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_BDD_MODE* pCurrentBddMode);
    ULONG GetModeCount(void) {return m_ModeCount;}
    NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWClose(void);
    BOOLEAN EnablePointer(void) { return FALSE; }
    NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*             DstAddr,
                                 _In_ UINT              DstBitPerPixel,
                                 _In_ BYTE*             SrcAddr,
                                 _In_ UINT              SrcBytesPerPixel,
                                 _In_ LONG              SrcPitch,
                                 _In_ ULONG             NumMoves,
                                 _In_ D3DKMT_MOVE_RECT* pMoves,
                                 _In_ ULONG             NumDirtyRects,
                                 _In_ RECT*             pDirtyRect,
                                 _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                                 _In_ const CURRENT_BDD_MODE* pModeCur);
    VOID BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber);
    VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface);
    VOID ResetDevice(VOID);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pModeCur);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_BDD_MODE* pModeCur);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE *pEscape);
    NTSTATUS AcquireFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode);
    NTSTATUS ReleaseFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode);
protected:
    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);
private:
    void SetVideoModeInfo(UINT Idx, PVBE_MODEINFO pModeInfo);
};

#endif // VIOGPU_X86

class VioGpuAdapter :
    public IVioGpuAdapter
{
public:
    VioGpuAdapter(_In_ VioGpuDod* pVioGpuDod);
    ~VioGpuAdapter(void);
    NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_BDD_MODE* pCurrentBddMode);
    ULONG GetModeCount(void) {return m_ModeCount;}
    NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWClose(void);
    BOOLEAN EnablePointer(void) { return TRUE; }
    NTSTATUS ExecutePresentDisplayOnly(_In_ BYTE*       DstAddr,
                                 _In_ UINT              DstBitPerPixel,
                                 _In_ BYTE*             SrcAddr,
                                 _In_ UINT              SrcBytesPerPixel,
                                 _In_ LONG              SrcPitch,
                                 _In_ ULONG             NumMoves,
                                 _In_ D3DKMT_MOVE_RECT* pMoves,
                                 _In_ ULONG             NumDirtyRects,
                                 _In_ RECT*             pDirtyRect,
                                 _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                                 _In_ const CURRENT_BDD_MODE* pModeCur);
    VOID BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod);
    BOOLEAN InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber);
    VOID DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface);
    VOID ResetDevice(VOID);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pModeCur);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_BDD_MODE* pModeCur);
    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE *pEscape);
    CPciResources* GetPciResources(void) { return &m_PciResources; }
    NTSTATUS AcquireFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode) { UNREFERENCED_PARAMETER(pCurrentBddMode);  return STATUS_SUCCESS; }
    NTSTATUS ReleaseFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode) { UNREFERENCED_PARAMETER(pCurrentBddMode);  return STATUS_SUCCESS; }

protected:
private:
    NTSTATUS VioGpuAdapterInit(DXGK_DISPLAY_INFORMATION* pDispInfo);
    void SetVideoModeInfo(UINT Idx, PGPU_DISP_MODE pModeInfo);
    void VioGpuAdapterClose(void);
    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);
    BOOLEAN AckFeature(UINT64 Feature);
    BOOLEAN GetDisplayInfo(void);
    NTSTATUS UpdateChildStatus(BOOLEAN connect);
    void SetCustomDisplay(_In_ USHORT xres,
                              _In_ USHORT yres);
    NTSTATUS CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_BDD_MODE* pCurrentBddMode);
    void DestroyFrameBufferObj(void);
    BOOLEAN CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pCurrentBddMode);
    void DestroyCursor(void);
    BOOLEAN GpuObjectAttach(UINT res_id, VioGpuObj* obj);
    void static ThreadWork(_In_ PVOID Context);
    void ThreadWorkRoutine(void);
    void ConfigChanged(void);
    NTSTATUS VirtIoDeviceInit(void);

    NTSTATUS EscapeCreateObject(VOID *data, UINT32 size);
    NTSTATUS EscapeUpdateObject(VOID *data, UINT32 size);
    NTSTATUS EscapeDeleteObject(VOID *data, UINT32 size);
    NTSTATUS SetCustomDisplay(QXLEscapeSetCustomDisplay* custom_display);
private:
    VirtIODevice m_VioDev;
    PUCHAR  m_IoBase;
    BOOLEAN m_IoMapped;
    ULONG   m_IoSize;
    CPciResources m_PciResources;
    UINT64 m_u64HostFeatures;
    UINT64 m_u64GuestFeatures;
    UINT32 m_u32NumCapsets;
    UINT32 m_u32NumScanouts;
    CtrlQueue m_CtrlQueue;
    CrsrQueue m_CursorQueue;
    VioGpuBuf m_GpuBuf;
    VioGpuIdr m_Idr;
    VioGpuObj* m_pFrameBuf;
    VioGpuObj* m_pCursorBuf;
    VioGpuMemSegment m_CursorSegment;
    VioGpuMemSegment m_FrameSegment;
    VioGpuMemSegment m_ICDSegment;
    volatile ULONG m_PendingWorks;
    KEVENT m_ConfigUpdateEvent;
    PETHREAD m_pWorkThread;
    BOOLEAN m_bStopWorkThread;
};

class VioGpuDod {
private:
    DEVICE_OBJECT* m_pPhysicalDevice;
    DXGKRNL_INTERFACE m_DxgkInterface;
    DXGK_DEVICE_INFO m_DeviceInfo;

    DEVICE_POWER_STATE m_MonitorPowerState;
    DEVICE_POWER_STATE m_AdapterPowerState;
    DRIVER_STATUS_FLAG m_Flags;

    CURRENT_BDD_MODE m_CurrentModes[MAX_VIEWS];

    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_SystemDisplaySourceId;
    DXGKARG_SETPOINTERSHAPE m_PointerShape;
    IVioGpuAdapter* m_pHWDevice;
public:
    VioGpuDod(_In_ DEVICE_OBJECT* pPhysicalDeviceObject);
    ~VioGpuDod(void);
#pragma code_seg(push)
#pragma code_seg()
    BOOLEAN IsDriverActive() const
    {
        return m_Flags.DriverStarted;
    }
    BOOLEAN IsHardwareInit() const
    {
        return m_Flags.HardwareInit;
    }
    void SetHardwareInit(BOOLEAN init)
    {
        m_Flags.HardwareInit = init;
    }
    BOOLEAN IsVirtIOInit() const
    {
        return m_Flags.VirtIOInit;
    }
    void SetVirtIOInit(BOOLEAN init)
    {
        m_Flags.VirtIOInit = init;
    }
#pragma code_seg(pop)

    NTSTATUS StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                         _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                         _Out_ ULONG*             pNumberOfViews,
                         _Out_ ULONG*             pNumberOfChildren);
    NTSTATUS StopDevice(VOID);
    // Must be Non-Paged
    VOID ResetDevice(VOID);

    NTSTATUS DispatchIoRequest(_In_  ULONG VidPnSourceId,
                               _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket);
    NTSTATUS SetPowerState(_In_  ULONG HardwareUid,
                               _In_  DEVICE_POWER_STATE DevicePowerState,
                               _In_  POWER_ACTION       ActionType);
    // Report back child capabilities
    NTSTATUS QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                                 _In_                             ULONG                  ChildRelationsSize);

    NTSTATUS QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
                              _In_    BOOLEAN            NonDestructiveOnly);

    // Return EDID if previously retrieved
    NTSTATUS QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
                                   _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor);

    // Must be Non-Paged
    // BDD doesn't have interrupts, so just returns false
    BOOLEAN InterruptRoutine(_In_  ULONG MessageNumber);

    VOID DpcRoutine(VOID);

    // Return DriverCaps, doesn't support other queries though
    NTSTATUS QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo);

    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition);

    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape);

    NTSTATUS PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly);

    NTSTATUS QueryInterface(_In_ CONST PQUERY_INTERFACE     QueryInterface);

    NTSTATUS IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn);

    NTSTATUS RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn);

    NTSTATUS RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST pRecommendVidPnTopology);

    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);

    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality);

    NTSTATUS SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility);

    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn);

    NTSTATUS UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath);

    NTSTATUS QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps);

    NTSTATUS Escape(_In_ CONST DXGKARG_ESCAPE *pEscape);

    // Part of PnPStop (PnP instance only), returns current mode information (which will be passed to fallback instance by dxgkrnl)
    NTSTATUS StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                      _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo);

    // Must be Non-Paged
    // Call to initialize as part of bugcheck
    NTSTATUS SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID       TargetId,
                                 _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                 _Out_ UINT*                                pWidth,
                                 _Out_ UINT*                                pHeight,
                                 _Out_ D3DDDIFORMAT*                        pColorFormat);

    // Must be Non-Paged
    // Write out pixels as part of bugcheck
    VOID SystemDisplayWrite(_In_reads_bytes_(SourceHeight * SourceStride) VOID* pSource,
                                 _In_                                     UINT  SourceWidth,
                                 _In_                                     UINT  SourceHeight,
                                 _In_                                     UINT  SourceStride,
                                 _In_                                     INT   PositionX,
                                 _In_                                     INT   PositionY);
    PDXGKRNL_INTERFACE GetDxgkInterface(void) { return &m_DxgkInterface;}
private:
    VOID CleanUp(VOID);
    BOOLEAN CheckHardware();
    NTSTATUS WriteHWInfoStr(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue);
    // Set the given source mode on the given path
    NTSTATUS SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode,
                                  CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath);

    // Add the current mode to the given monitor source mode set
    NTSTATUS AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes);

    // Add the current mode to the given VidPn source mode set
    NTSTATUS AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
                                 D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);

    // Add the current mode (or the matching to pinned source mode) to the give VidPn target mode set
    NTSTATUS AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface,
                                 D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                 _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnPinnedSourceModeInfo,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    D3DDDI_VIDEO_PRESENT_SOURCE_ID FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero);
    NTSTATUS IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode) const;
    NTSTATUS IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath) const;
    NTSTATUS RegisterHWInfo(_In_ ULONG Id);
};

