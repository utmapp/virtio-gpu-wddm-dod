#include "helper.h"
#include "driver.h"
#include "viogpudo.h"
#include "baseobj.h"
#include "bitops.h"

VioGpuDod::VioGpuDod(_In_ DEVICE_OBJECT* pPhysicalDeviceObject) : m_pPhysicalDevice(pPhysicalDeviceObject),
                                                            m_MonitorPowerState(PowerDeviceD0),
                                                            m_AdapterPowerState(PowerDeviceD0)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    *((UINT*)&m_Flags) = 0;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    RtlZeroMemory(&m_PointerShape, sizeof(m_PointerShape));
    m_pHWDevice = NULL;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


VioGpuDod::~VioGpuDod(void)
{
    PAGED_CODE();

    CleanUp();
    delete m_pHWDevice;
    m_pHWDevice = NULL;
}

BOOLEAN VioGpuDod::CheckHardware()
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_GRAPHICS_DRIVER_MISMATCH;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));


    // Get the Vendor & Device IDs on PCI system
    PCI_COMMON_HEADER Header = {0};
    ULONG BytesRead;

    Status = m_DxgkInterface.DxgkCbReadDeviceSpace(m_DxgkInterface.DeviceHandle,
                                                   DXGK_WHICHSPACE_CONFIG,
                                                   &Header,
                                                   0,
                                                   sizeof(Header),
                                                   &BytesRead);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbReadDeviceSpace failed with status 0x%X\n", Status));
        return FALSE;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s VendorId = 0x%04X DeviceId = 0x%04X\n", __FUNCTION__, Header.VendorID, Header.DeviceID));
    if (Header.VendorID == REDHAT_PCI_VENDOR_ID &&
        Header.DeviceID == 0x1050)
    {
        return TRUE;
    }

    return FALSE;
}

NTSTATUS VioGpuDod::StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                         _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                         _Out_ ULONG*             pNumberOfViews,
                         _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();

    NTSTATUS Status;
    PHYSICAL_ADDRESS PhysicAddress;

    VIOGPU_ASSERT(pDxgkStartInfo != NULL);
    VIOGPU_ASSERT(pDxgkInterface != NULL);
    VIOGPU_ASSERT(pNumberOfViews != NULL);
    VIOGPU_ASSERT(pNumberOfChildren != NULL);
    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    m_CurrentModes[0].DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;
    // Get device information from OS.
    Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        VIOGPU_LOG_ASSERTION1("DxgkCbGetDeviceInformation failed with status 0x%X\n",
                           Status);
        return Status;
    }

    if (CheckHardware())
    {
        m_pHWDevice = new(NonPagedPoolNx) VioGpuAdapter(this);
    }
    else
    {
#ifdef VIOGPU_X86
        m_pHWDevice = new(NonPagedPoolNx) VgaAdapter(this);
#else
        return STATUS_NOT_IMPLEMENTED;
#endif // VIOGPU_X86
    }

    if (!m_pHWDevice)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("StartDevice failed to allocate memory\n"));
        return Status;
    }

    Status = m_pHWDevice->HWInit(m_DeviceInfo.TranslatedResourceList, &m_CurrentModes[0].DispInfo);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("HWInit failed with status 0x%X\n", Status));
        return Status;
    }

    Status = RegisterHWInfo(m_pHWDevice->GetId());
    if (!NT_SUCCESS(Status))
    {
        VIOGPU_LOG_ASSERTION1("RegisterHWInfo failed with status 0x%X\n",
                           Status);
        return Status;
    }

    PhysicAddress.QuadPart = m_CurrentModes[0].DispInfo.PhysicAddress.QuadPart;
    if (m_pHWDevice->GetId() == 0)
    {
         Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &(m_CurrentModes[0].DispInfo));
    }

    if (!NT_SUCCESS(Status) )
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbAcquirePostDisplayOwnership failed with status 0x%X Width = %d\n",
                           Status, m_CurrentModes[0].DispInfo.Width));
        return STATUS_UNSUCCESSFUL;
    }
/*
    if (m_CurrentModes[0].DispInfo.Width == 0)
    {
        m_CurrentModes[0].DispInfo.Width = MIN_WIDTH_SIZE; 
        m_CurrentModes[0].DispInfo.Height = MIN_HEIGHT_SIZE;
        m_CurrentModes[0].DispInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
        m_CurrentModes[0].DispInfo.Pitch = BPPFromPixelFormat(m_CurrentModes[0].DispInfo.ColorFormat) / BITS_PER_BYTE;
        m_CurrentModes[0].DispInfo.TargetId = 0;
        if (PhysicAddress.QuadPart != 0L) {
             m_CurrentModes[0].DispInfo.PhysicAddress.QuadPart = PhysicAddress.QuadPart;
        }
    }
*/
    m_CurrentModes[0].DispInfo.Width = max(MIN_WIDTH_SIZE, m_CurrentModes[0].DispInfo.Width);
    m_CurrentModes[0].DispInfo.Height = max(MIN_HEIGHT_SIZE, m_CurrentModes[0].DispInfo.Height);
    m_CurrentModes[0].DispInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
    m_CurrentModes[0].DispInfo.Pitch = BPPFromPixelFormat(m_CurrentModes[0].DispInfo.ColorFormat) / BITS_PER_BYTE;
    m_CurrentModes[0].DispInfo.TargetId = 0;
    if (PhysicAddress.QuadPart != 0LL) {
         m_CurrentModes[0].DispInfo.PhysicAddress.QuadPart = PhysicAddress.QuadPart;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s ColorFormat = %d\n", __FUNCTION__, m_CurrentModes[0].DispInfo.ColorFormat));

   *pNumberOfViews = MAX_VIEWS;
   *pNumberOfChildren = MAX_CHILDREN;
    m_Flags.DriverStarted = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::StopDevice(VOID)
{
    PAGED_CODE();

    m_Flags.DriverStarted = FALSE;
    return STATUS_SUCCESS;
}

VOID VioGpuDod::CleanUp(VOID)
{
    PAGED_CODE();

    for (UINT Source = 0; Source < MAX_VIEWS; ++Source)
    {
        if (m_CurrentModes[Source].FrameBuffer.Ptr)
        {
            m_pHWDevice->ReleaseFrameBuffer(&m_CurrentModes[Source]);
        }
    }
}


NTSTATUS VioGpuDod::DispatchIoRequest(_In_  ULONG VidPnSourceId,
                                   _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(pVideoRequestPacket);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

PCHAR
DbgDevicePowerString(
    __in DEVICE_POWER_STATE Type
    )
{
    PAGED_CODE();

    switch (Type)
    {
    case PowerDeviceUnspecified:
        return "PowerDeviceUnspecified";
    case PowerDeviceD0:
        return "PowerDeviceD0";
    case PowerDeviceD1:
        return "PowerDeviceD1";
    case PowerDeviceD2:
        return "PowerDeviceD2";
    case PowerDeviceD3:
        return "PowerDeviceD3";
    case PowerDeviceMaximum:
        return "PowerDeviceMaximum";
    default:
        return "UnKnown Device Power State";
    }
}

PCHAR
DbgPowerActionString(
    __in POWER_ACTION Type
    )
{
    PAGED_CODE();

    switch (Type)
    {
    case PowerActionNone:
        return "PowerActionNone";
    case PowerActionReserved:
        return "PowerActionReserved";
    case PowerActionSleep:
        return "PowerActionSleep";
    case PowerActionHibernate:
        return "PowerActionHibernate";
    case PowerActionShutdown:
        return "PowerActionShutdown";
    case PowerActionShutdownReset:
        return "PowerActionShutdownReset";
    case PowerActionShutdownOff:
        return "PowerActionShutdownOff";
    case PowerActionWarmEject:
        return "PowerActionWarmEject";
    default:
        return "UnKnown Device Power State";
    }
}

NTSTATUS VioGpuDod::SetPowerState(_In_  ULONG HardwareUid,
                               _In_  DEVICE_POWER_STATE DevicePowerState,
                               _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ActionType);
    NTSTATUS Status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s HardwareUid = 0x%x ActionType = %s DevicePowerState = %s AdapterPowerState = %s\n", __FUNCTION__, HardwareUid, DbgPowerActionString(ActionType), DbgDevicePowerString(DevicePowerState), DbgDevicePowerString(m_AdapterPowerState)));

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID)
    {
        Status = m_pHWDevice->SetPowerState(DevicePowerState, &(m_CurrentModes[0].DispInfo));
        if (NT_SUCCESS(Status) && DevicePowerState == PowerDeviceD0)
        {
            // When returning from D3 the device visibility defined to be off for all targets
            if (m_AdapterPowerState == PowerDeviceD3)
            {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = D3DDDI_ID_ALL;
                Visibility.Visible = FALSE;
                SetVidPnSourceVisibility(&Visibility);
            }
            m_AdapterPowerState = DevicePowerState;
        }

        // There is nothing to do to specifically power up/down the display adapter
        Status = m_pHWDevice->SetPowerState(DevicePowerState, &(m_CurrentModes[0].DispInfo));
    }
    // TODO: This is where the specified monitor should be powered up/down
    
    return Status;
}

NTSTATUS VioGpuDod::QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                                     _In_  ULONG  ChildRelationsSize)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pChildRelations != NULL);

    // The last DXGK_CHILD_DESCRIPTOR in the array of pChildRelations must remain zeroed out, so we subtract this from the count
    ULONG ChildRelationsCount = (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR)) - 1;
    ULONG DeviceId = m_pHWDevice->GetId();
    VIOGPU_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex)
    {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = (DeviceId == 0) ? HpdAwarenessAlwaysConnected : HpdAwarenessInterruptible;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = (DeviceId == 0) ? D3DKMDT_VOT_INTERNAL : D3DKMDT_VOT_HD15;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        // TODO: Replace 0 with the actual ACPI ID of the child device, if available
        pChildRelations[ChildIndex].AcpiUid = 0;
        pChildRelations[ChildIndex].ChildUid = ChildIndex;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
                                                _In_    BOOLEAN            NonDestructiveOnly)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    VIOGPU_ASSERT(pChildStatus != NULL);
    VIOGPU_ASSERT(pChildStatus->ChildUid < MAX_CHILDREN);

    switch (pChildStatus->Type)
    {
        case StatusConnection:
        {
            // HpdAwarenessInterruptible was reported since HpdAwarenessNone is deprecated.
            // However, BDD has no knowledge of HotPlug events, so just always return connected.
            pChildStatus->HotPlug.Connected = IsDriverActive();
            return STATUS_SUCCESS;
        }

        case StatusRotation:
        {
            // D3DKMDT_MOA_NONE was reported, so this should never be called
            DbgPrint(TRACE_LEVEL_ERROR, ("Child status being queried for StatusRotation even though D3DKMDT_MOA_NONE was reported"));
            return STATUS_INVALID_PARAMETER;
        }

        default:
        {
            DbgPrint(TRACE_LEVEL_WARNING, ("Unknown pChildStatus->Type (0x%I64x) requested.", pChildStatus->Type));
            return STATUS_NOT_SUPPORTED;
        }
    }
}

// EDID retrieval
NTSTATUS VioGpuDod::QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
                                       _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pDeviceDescriptor != NULL);
    VIOGPU_ASSERT(ChildUid < MAX_CHILDREN);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

NTSTATUS VioGpuDod::QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pQueryAdapterInfo != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            if (!pQueryAdapterInfo->OutputDataSize)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pQueryAdapterInfo->OutputDataSize (0x%u) is smaller than sizeof(DXGK_DRIVERCAPS) (0x%u)\n", pQueryAdapterInfo->OutputDataSize, sizeof(DXGK_DRIVERCAPS)));
                return STATUS_BUFFER_TOO_SMALL;
            }

            DXGK_DRIVERCAPS* pDriverCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;
            DbgPrint(TRACE_LEVEL_ERROR, ("InterruptMessageNumber = %d, WDDMVersion = %d\n", pDriverCaps->InterruptMessageNumber, pDriverCaps->WDDMVersion));
            RtlZeroMemory(pDriverCaps, pQueryAdapterInfo->OutputDataSize/*sizeof(DXGK_DRIVERCAPS)*/);
            pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            pDriverCaps->HighestAcceptableAddress.QuadPart = (ULONG64)-1;

            if (m_pHWDevice->EnablePointer()) {
                pDriverCaps->MaxPointerWidth  = POINTER_SIZE;
                pDriverCaps->MaxPointerHeight = POINTER_SIZE;
                pDriverCaps->PointerCaps.Monochrome = 0;
                pDriverCaps->PointerCaps.Color = 1;
                pDriverCaps->PointerCaps.MaskedColor = 0;
            }
            pDriverCaps->SupportNonVGA = (m_pHWDevice->GetType() == VGA_DEVICE);
            pDriverCaps->SupportSmoothRotation = TRUE;
            DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s 1\n", __FUNCTION__));
            return STATUS_SUCCESS;
        }

        default:
        {
            // BDD does not need to support any other adapter information types
            DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
            return STATUS_NOT_SUPPORTED;
        }
    }
}

NTSTATUS VioGpuDod::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pSetPointerPosition != NULL);
    VIOGPU_ASSERT(pSetPointerPosition->VidPnSourceId < MAX_VIEWS);

    if (!(pSetPointerPosition->Flags.Visible))
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s Cursor is not visible\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }
    return m_pHWDevice->SetPointerPosition(pSetPointerPosition, &m_CurrentModes[pSetPointerPosition->VidPnSourceId]);
}

NTSTATUS VioGpuDod::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pSetPointerShape != NULL);

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<---> %s Height = %d, Width = %d, XHot= %d, YHot = %d SourceId = %d\n", 
        __FUNCTION__, pSetPointerShape->Height, pSetPointerShape->Width, pSetPointerShape->XHot, pSetPointerShape->YHot, pSetPointerShape->VidPnSourceId));
    return m_pHWDevice->SetPointerShape(pSetPointerShape, &m_CurrentModes[pSetPointerShape->VidPnSourceId]);
}

NTSTATUS VioGpuDod::PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT(pPresentDisplayOnly != NULL);
    VIOGPU_ASSERT(pPresentDisplayOnly->VidPnSourceId < MAX_VIEWS);

    if (pPresentDisplayOnly->BytesPerPixel < 4)
    {
        // Only >=32bpp modes are reported, therefore this Present should never pass anything less than 4 bytes per pixel
        DbgPrint(TRACE_LEVEL_ERROR, ("pPresentDisplayOnly->BytesPerPixel is 0x%d, which is lower than the allowed.\n", pPresentDisplayOnly->BytesPerPixel));
        return STATUS_INVALID_PARAMETER;
    }

    // If it is in monitor off state or source is not supposed to be visible, don't present anything to the screen
    if ((m_MonitorPowerState > PowerDeviceD0) ||
        (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Flags.SourceNotVisible))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Source is not visiable\n", __FUNCTION__));
        return STATUS_SUCCESS;
    }


    // If actual pixels are coming through, will need to completely zero out physical address next time in BlackOutScreen
    m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].ZeroedOutStart.QuadPart = 0;
    m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].ZeroedOutEnd.QuadPart = 0;

    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION RotationNeededByFb = pPresentDisplayOnly->Flags.Rotate ?
                                                             m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Rotation :
                                                             D3DKMDT_VPPR_IDENTITY;
    BYTE* pDst = (BYTE*)m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].FrameBuffer.Ptr;
    UINT DstBitPerPixel = BPPFromPixelFormat(m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.ColorFormat);
    if (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Scaling == D3DKMDT_VPPS_CENTERED)
    {
        UINT CenterShift = (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Height -
            m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].SrcModeHeight)*m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Pitch;
        CenterShift += (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Width -
            m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].SrcModeWidth)*DstBitPerPixel/8;
        pDst += (int)CenterShift/2;
    }
    Status = m_pHWDevice->ExecutePresentDisplayOnly(
                        pDst,
                        DstBitPerPixel,
                        (BYTE*)pPresentDisplayOnly->pSource,
                        pPresentDisplayOnly->BytesPerPixel,
                        pPresentDisplayOnly->Pitch,
                        pPresentDisplayOnly->NumMoves,
                        pPresentDisplayOnly->pMoves,
                        pPresentDisplayOnly->NumDirtyRects,
                        pPresentDisplayOnly->pDirtyRect,
                        RotationNeededByFb,
                        &m_CurrentModes[0]);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::QueryInterface(_In_ CONST PQUERY_INTERFACE pQueryInterface)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pQueryInterface != NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s Version = %d\n", __FUNCTION__, pQueryInterface->Version));

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VioGpuDod::StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                          _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(pDisplayInfo);
    VIOGPU_ASSERT(TargetId < MAX_CHILDREN);
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = FindSourceForTarget(TargetId, TRUE);

    // In case BDD is the next driver to run, the monitor should not be off, since
    // this could cause the BIOS to hang when the EDID is retrieved on Start.
    if (m_MonitorPowerState > PowerDeviceD0)
    {
        SetPowerState(TargetId, PowerDeviceD0, PowerActionNone);
    }

    // The driver has to black out the display and ensure it is visible when releasing ownership
    m_pHWDevice->BlackOutScreen(&m_CurrentModes[SourceId]);

    *pDisplayInfo = m_CurrentModes[SourceId].DispInfo;

    return StopDevice();
}


NTSTATUS VioGpuDod::QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pVidPnHWCaps != NULL);
    VIOGPU_ASSERT(pVidPnHWCaps->SourceId < MAX_VIEWS);
    VIOGPU_ASSERT(pVidPnHWCaps->TargetId < MAX_CHILDREN);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation             = 1; // BDD does rotation in software
    pVidPnHWCaps->VidPnHWCaps.DriverScaling              = 0; // BDD does not support scaling
    pVidPnHWCaps->VidPnHWCaps.DriverCloning              = 0; // BDD does not support clone
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert         = 1; // BDD does color conversions in software
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0; // BDD does not support linked adapters
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay        = 0; // BDD does not support remote displays

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();
    NTSTATUS res;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    VIOGPU_ASSERT_CHK(pEscape != NULL);

    res = m_pHWDevice->Escape(pEscape);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return res;
}


// TODO: Need to also check pinned modes and the path parameters, not just topology
NTSTATUS VioGpuDod::IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pIsSupportedVidPn != NULL);

    if (pIsSupportedVidPn->hDesiredVidPn == 0)
    {
        // A null desired VidPn is supported
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        return STATUS_SUCCESS;
    }

    // Default to not supported, until shown it is supported
    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    CONST DXGK_VIDPN_INTERFACE* pVidPnInterface;
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pIsSupportedVidPn->hDesiredVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hDesiredVidPn = 0x%I64x\n", Status, pIsSupportedVidPn->hDesiredVidPn));
        return Status;
    }

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(pIsSupportedVidPn->hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetTopology failed with Status = 0x%X, hDesiredVidPn = 0x%I64x\n", Status, pIsSupportedVidPn->hDesiredVidPn));
        return Status;
    }

    // For every source in this topology, make sure they don't have more paths than there are targets
    for (D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        SIZE_T NumPathsFromSource = 0;
        Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, SourceId, &NumPathsFromSource);
        if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
        {
            continue;
        }
        else if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetNumPathsFromSource failed with Status = 0x%X hVidPnTopology = 0x%I64x, SourceId = 0x%I64x",
                           Status, hVidPnTopology, SourceId));
            return Status;
        }
        else if (NumPathsFromSource > MAX_CHILDREN)
        {
            // This VidPn is not supported, which has already been set as the default
            return STATUS_SUCCESS;
        }
    }

    // All sources succeeded so this VidPn is supported
    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pRecommendFunctionalVidPn == NULL);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS VioGpuDod::RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST pRecommendVidPnTopology)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pRecommendVidPnTopology == NULL);

    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS VioGpuDod::RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    return AddSingleMonitorMode(pRecommendMonitorModes);
}


NTSTATUS VioGpuDod::AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface,
                                                   D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                                   D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(SourceId);

    // There is only one source format supported by display-only drivers, but more can be added in a 
    // full WDDM driver if the hardware supports them
    for (ULONG idx = 0; idx < m_pHWDevice->GetModeCount(); ++idx)
    {
        // Create new mode info that will be populated
        D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo = NULL;
        PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(idx);
        NTSTATUS Status = pVidPnSourceModeSetInterface->pfnCreateNewModeInfo(hVidPnSourceModeSet, &pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x", Status, hVidPnSourceModeSet));
            return Status;
        }

        // Populate mode info with values from current mode and hard-coded values
        // Always report 32 bpp format, this will be color converted during the present if the mode is < 32bpp
        pVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = pModeInfo->VisScreenWidth;
        pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = pModeInfo->VisScreenHeight;
        pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
        pVidPnSourceModeInfo->Format.Graphics.Stride = pModeInfo->ScreenStride;
        pVidPnSourceModeInfo->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        pVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        // Add the mode to the source mode set
        Status = pVidPnSourceModeSetInterface->pfnAddMode(hVidPnSourceModeSet, pVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            NTSTATUS TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnSourceModeInfo);
            UNREFERENCED_PARAMETER(TempStatus);
            NT_ASSERT(NT_SUCCESS(TempStatus));

            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x, pVidPnSourceModeInfo = %p", Status, hVidPnSourceModeSet, pVidPnSourceModeInfo));
                return Status;
            }
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

// Add the current mode information (acquired from the POST frame buffer) as the target mode.
NTSTATUS VioGpuDod::AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface,
                                                   D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                                   _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnPinnedSourceModeInfo,
                                                   D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pVidPnPinnedSourceModeInfo);

    D3DKMDT_VIDPN_TARGET_MODE* pVidPnTargetModeInfo = NULL;
    NTSTATUS Status  = STATUS_SUCCESS;

    for (UINT ModeIndex = 0; ModeIndex < m_pHWDevice->GetModeCount(); ++ModeIndex)
    {
        PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(SourceId);
        pVidPnTargetModeInfo = NULL;
        Status = pVidPnTargetModeSetInterface->pfnCreateNewModeInfo(hVidPnTargetModeSet, &pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%I64x", Status, hVidPnTargetModeSet));
            return Status;
        }
        pVidPnTargetModeInfo->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cx = pModeInfo->VisScreenWidth;
        pVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cy = pModeInfo->VisScreenHeight;
        pVidPnTargetModeInfo->VideoSignalInfo.ActiveSize = pVidPnTargetModeInfo->VideoSignalInfo.TotalSize;
        pVidPnTargetModeInfo->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pVidPnTargetModeInfo->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    // We add this as PREFERRED since it is the only supported target
        pVidPnTargetModeInfo->Preference = D3DKMDT_MP_NOTPREFERRED; // TODO: another logic for prefferred mode. Maybe the pinned source mode

        Status = pVidPnTargetModeSetInterface->pfnAddMode(hVidPnTargetModeSet, pVidPnTargetModeInfo);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%X, hVidPnTargetModeSet = 0x%I64x, pVidPnTargetModeInfo = %p", Status, hVidPnTargetModeSet, pVidPnTargetModeInfo));
            }
            
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            Status = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnTargetModeInfo);
            NT_ASSERT(NT_SUCCESS(Status));
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


NTSTATUS VioGpuDod::AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST pRecommendMonitorModes)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;
    D3DKMDT_MONITOR_SOURCE_MODE* pMonitorSourceMode = NULL;
    PVIDEO_MODE_INFORMATION pVbeModeInfo = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        // If failed to create a new mode info, mode doesn't need to be released since it was never created
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
        return Status;
    }

    pVbeModeInfo = m_pHWDevice->GetModeInfo(m_pHWDevice->GetCurrentModeIndex());

    // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
    pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->VisScreenWidth;
    pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->VisScreenHeight;
    pMonitorSourceMode->VideoSignalInfo.ActiveSize = pMonitorSourceMode->VideoSignalInfo.TotalSize;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    pMonitorSourceMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    // We set the preference to PREFERRED since this is the only supported mode
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_PREFERRED;
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x, pMonitorSourceMode = 0x%I64x",
                            Status, pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode));
        }
        else
        {
            Status = STATUS_SUCCESS;
        }

        // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
        NTSTATUS TempStatus = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        UNREFERENCED_PARAMETER(TempStatus);
        NT_ASSERT(NT_SUCCESS(TempStatus));
        return Status;
    }
    // If AddMode succeeded with something other than STATUS_SUCCESS treat it as such anyway when propagating up
    for (UINT Idx = 0; Idx < m_pHWDevice->GetModeCount(); ++Idx)
    {
        // There is only one source format supported by display-only drivers, but more can be added in a 
        // full WDDM driver if the hardware supports them

        pVbeModeInfo = m_pHWDevice->GetModeInfo(Idx);
        // TODO: add routine for filling Monitor modepMonitorSourceMode = NULL;
        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, &pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            // If failed to create a new mode info, mode doesn't need to be released since it was never created
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewModeInfo failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x", Status, pRecommendMonitorModes->hMonitorSourceModeSet));
            return Status;
        }

        
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: add pref mode, dimensions %ux%u, taken from DxgkCbAcquirePostDisplayOwnership at StartDevice\n", __FUNCTION__,
                   pVbeModeInfo->VisScreenWidth, pVbeModeInfo->VisScreenHeight));

        // Since we don't know the real monitor timing information, just use the current display mode (from the POST device) with unknown frequencies
        pMonitorSourceMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        pMonitorSourceMode->VideoSignalInfo.TotalSize.cx = pVbeModeInfo->VisScreenWidth;
        pMonitorSourceMode->VideoSignalInfo.TotalSize.cy = pVbeModeInfo->VisScreenHeight;
        pMonitorSourceMode->VideoSignalInfo.ActiveSize = pMonitorSourceMode->VideoSignalInfo.TotalSize;
        pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
        pMonitorSourceMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

        pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
        pMonitorSourceMode->Preference = D3DKMDT_MP_NOTPREFERRED;
        pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
        pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
        pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 8;

        Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAddMode failed with Status = 0x%X, hMonitorSourceModeSet = 0x%I64x, pMonitorSourceMode = 0x%p",
                                Status, pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode));
            }
        
            // If adding the mode failed, release the mode, if this doesn't work there is nothing that can be done, some memory will get leaked
            Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(pRecommendMonitorModes->hMonitorSourceModeSet, pMonitorSourceMode);
            NT_ASSERT(NT_SUCCESS(Status));
        }
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

// Tell DMM about all the modes, etc. that are supported
NTSTATUS VioGpuDod::EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();

    VIOGPU_ASSERT(pEnumCofuncModality != NULL);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    D3DKMDT_HVIDPNTOPOLOGY                   hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET              hVidPnSourceModeSet = 0;
    D3DKMDT_HVIDPNTARGETMODESET              hVidPnTargetModeSet = 0;
    CONST DXGK_VIDPN_INTERFACE*              pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE*      pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface = NULL;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*        pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*        pVidPnPresentPathTemp = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE*         pVidPnPinnedSourceModeInfo = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE*         pVidPnPinnedTargetModeInfo = NULL;

    // Get the VidPn Interface so we can get the 'Source Mode Set', 'Target Mode Set' and 'VidPn Topology' interfaces
    NTSTATUS Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pEnumCofuncModality->hConstrainingVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
        return Status;
    }

    // Get the VidPn Topology interface so we can enumerate all paths
    Status = pVidPnInterface->pfnGetTopology(pEnumCofuncModality->hConstrainingVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
        return Status;
    }

    // Get the first path before we start looping through them
    Status = pVidPnTopologyInterface->pfnAcquireFirstPathInfo(hVidPnTopology, &pVidPnPresentPath);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireFirstPathInfo failed with Status =0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
        return Status;
    }

    // Loop through all available paths.
    while (Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    {
        // Get the Source Mode Set interface so the pinned mode can be retrieved
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                          pVidPnPresentPath->VidPnSourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, SourceId = 0x%I64x",
                           Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId));
            break;
        }

        // Get the pinned mode, needed when VidPnSource isn't pivot, and when VidPnTarget isn't pivot
        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet, &pVidPnPinnedSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x", Status, hVidPnSourceModeSet));
            break;
        }

        // SOURCE MODES: If this source mode isn't the pivot point, do work on the source mode set
        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId)))
        {
            // If there's no pinned source add possible modes (otherwise they've already been added)
            if (pVidPnPinnedSourceModeInfo == NULL)
            {
                // Release the acquired source mode set, since going to create a new one to put all modes in
                Status = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnSourceModeSet = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet));
                    break;
                }
                hVidPnSourceModeSet = 0; // Successfully released it

                // Create a new source mode set which will be added to the constraining VidPn with all the possible modes
                Status = pVidPnInterface->pfnCreateNewSourceModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                    pVidPnPresentPath->VidPnSourceId,
                                                                    &hVidPnSourceModeSet,
                                                                    &pVidPnSourceModeSetInterface);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, SourceId = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId));
                    break;
                }

                // Add the appropriate modes to the source mode set
                {
                    Status = AddSingleSourceMode(pVidPnSourceModeSetInterface, hVidPnSourceModeSet, pVidPnPresentPath->VidPnSourceId);
                }

                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("AddSingleSourceMode failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
                    break;
                }

                // Give DMM back the source modes just populated
                Status = pVidPnInterface->pfnAssignSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId, hVidPnSourceModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnAssignSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, SourceId = 0x%I64x, hVidPnSourceModeSet = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnSourceId, hVidPnSourceModeSet));
                    break;
                }
                hVidPnSourceModeSet = 0; // Successfully assigned it (equivalent to releasing it)
            }
        }// End: SOURCE MODES

        // TARGET MODES: If this target mode isn't the pivot point, do work on the target mode set
        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            // Get the Target Mode Set interface so modes can be added if necessary
            Status = pVidPnInterface->pfnAcquireTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                              pVidPnPresentPath->VidPnTargetId,
                                                              &hVidPnTargetModeSet,
                                                              &pVidPnTargetModeSetInterface);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, TargetId = 0x%I64x",
                               Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId));
                break;
            }

            Status = pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnTargetModeSet, &pVidPnPinnedTargetModeInfo);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%I64x", Status, hVidPnTargetModeSet));
                break;
            }

            // If there's no pinned target add possible modes (otherwise they've already been added)
            if (pVidPnPinnedTargetModeInfo == NULL)
            {
                // Release the acquired target mode set, since going to create a new one to put all modes in
                Status = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnTargetModeSet = 0x%I64x",
                                       Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet));
                    break;
                }
                hVidPnTargetModeSet = 0; // Successfully released it

                // Create a new target mode set which will be added to the constraining VidPn with all the possible modes
                Status = pVidPnInterface->pfnCreateNewTargetModeSet(pEnumCofuncModality->hConstrainingVidPn,
                                                                    pVidPnPresentPath->VidPnTargetId,
                                                                    &hVidPnTargetModeSet,
                                                                    &pVidPnTargetModeSetInterface);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnCreateNewTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, TargetId = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId));
                    break;
                }

                Status = AddSingleTargetMode(pVidPnTargetModeSetInterface, hVidPnTargetModeSet, pVidPnPinnedSourceModeInfo, pVidPnPresentPath->VidPnSourceId);

                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("AddSingleTargetMode failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pEnumCofuncModality->hConstrainingVidPn));
                    break;
                }

                // Give DMM back the source modes just populated
                Status = pVidPnInterface->pfnAssignTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId, hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnAssignTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, TargetId = 0x%I64x, hVidPnTargetModeSet = 0x%I64x",
                                   Status, pEnumCofuncModality->hConstrainingVidPn, pVidPnPresentPath->VidPnTargetId, hVidPnTargetModeSet));
                    break;
                }
                hVidPnTargetModeSet = 0; // Successfully assigned it (equivalent to releasing it)
            }
            else
            {
                // Release the pinned target as there's no other work to do
                Status = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseModeInfo failed with Status = 0x%X, hVidPnTargetModeSet = 0x%I64x, pVidPnPinnedTargetModeInfo = %p",
                                        Status, hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo));
                    break;
                }
                pVidPnPinnedTargetModeInfo = NULL; // Successfully released it

                // Release the acquired target mode set, since it is no longer needed
                Status = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseTargetModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnTargetModeSet = 0x%I64x",
                                       Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet));
                    break;
                }
                hVidPnTargetModeSet = 0; // Successfully released it
            }
        }// End: TARGET MODES

        // Nothing else needs the pinned source mode so release it
        if (pVidPnPinnedSourceModeInfo != NULL)
        {
            Status = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseModeInfo failed with Status = 0x%X, hVidPnSourceModeSet = 0x%I64x, pVidPnPinnedSourceModeInfo = %p",
                                    Status, hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo));
                break;
            }
            pVidPnPinnedSourceModeInfo = NULL; // Successfully released it
        }

        // With the pinned source mode now released, if the source mode set hasn't been released, release that as well
        if (hVidPnSourceModeSet != 0)
        {
            Status = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleaseSourceModeSet failed with Status = 0x%X, hConstrainingVidPn = 0x%I64x, hVidPnSourceModeSet = 0x%I64x",
                               Status, pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet));
                break;
            }
            hVidPnSourceModeSet = 0; // Successfully released it
        }

        // If modifying support fields, need to modify a local version of a path structure since the retrieved one is const
        D3DKMDT_VIDPN_PRESENT_PATH LocalVidPnPresentPath = *pVidPnPresentPath;
        BOOLEAN SupportFieldsModified = FALSE;

        // SCALING: If this path's scaling isn't the pivot point, do work on the scaling support
        if (!((pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_SCALING) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            // If the scaling is unpinned, then modify the scaling support field
            if (pVidPnPresentPath->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
            {
                // Identity and centered scaling are supported, but not any stretch modes
                RtlZeroMemory(&(LocalVidPnPresentPath.ContentTransformation.ScalingSupport), sizeof(D3DKMDT_VIDPN_PRESENT_PATH_SCALING_SUPPORT));
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Identity = 1;
                LocalVidPnPresentPath.ContentTransformation.ScalingSupport.Centered = 1;
                SupportFieldsModified = TRUE;
            }
        } // End: SCALING

        // ROTATION: If this path's rotation isn't the pivot point, do work on the rotation support
        if (!((pEnumCofuncModality->EnumPivotType != D3DKMDT_EPT_ROTATION) &&
              (pEnumCofuncModality->EnumPivot.VidPnSourceId == pVidPnPresentPath->VidPnSourceId) &&
              (pEnumCofuncModality->EnumPivot.VidPnTargetId == pVidPnPresentPath->VidPnTargetId)))
        {
            // If the rotation is unpinned, then modify the rotation support field
            if (pVidPnPresentPath->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
            {
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Identity = 1;
                // Sample supports only Rotate90
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate90 = 1;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate180 = 0;
                LocalVidPnPresentPath.ContentTransformation.RotationSupport.Rotate270 = 0;
                SupportFieldsModified = TRUE;
            }
        } // End: ROTATION

        if (SupportFieldsModified)
        {
            // The correct path will be found by this function and the appropriate fields updated
            Status = pVidPnTopologyInterface->pfnUpdatePathSupportInfo(hVidPnTopology, &LocalVidPnPresentPath);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint(TRACE_LEVEL_ERROR, ("pfnUpdatePathSupportInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
                break;
            }
        }

        // Get the next path...
        // (NOTE: This is the value of Status that will return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET when it's time to quit the loop)
        pVidPnPresentPathTemp = pVidPnPresentPath;
        Status = pVidPnTopologyInterface->pfnAcquireNextPathInfo(hVidPnTopology, pVidPnPresentPathTemp, &pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireNextPathInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x, pVidPnPresentPathTemp = %p", Status, hVidPnTopology, pVidPnPresentPathTemp));
            break;
        }

        // ...and release the last path
        NTSTATUS TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathTemp);
        if (!NT_SUCCESS(TempStatus))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleasePathInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x, pVidPnPresentPathTemp = %p", TempStatus, hVidPnTopology, pVidPnPresentPathTemp));
            Status = TempStatus;
            break;
        }
        pVidPnPresentPathTemp = NULL; // Successfully released it
    }// End: while loop for paths in topology

    // If quit the while loop normally, set the return value to success
    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    {
        Status = STATUS_SUCCESS;
    }

    // Release any resources hanging around because the loop was quit early.
    // Since in normal execution everything should be released by this point, TempStatus is initialized to a bogus error to be used as an
    //  assertion that if anything had to be released now (TempStatus changing) Status isn't successful.
    NTSTATUS TempStatus = STATUS_NOT_FOUND;

    if ((pVidPnSourceModeSetInterface != NULL) &&
        (pVidPnPinnedSourceModeInfo != NULL))
    {
        TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pVidPnPinnedSourceModeInfo);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnTargetModeSetInterface != NULL) &&
        (pVidPnPinnedTargetModeInfo != NULL))
    {
        TempStatus = pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pVidPnPinnedTargetModeInfo);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (pVidPnPresentPath != NULL)
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (pVidPnPresentPathTemp != NULL)
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathTemp);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (hVidPnSourceModeSet != 0)
    {
        TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnSourceModeSet);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    if (hVidPnTargetModeSet != 0)
    {
        TempStatus = pVidPnInterface->pfnReleaseTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, hVidPnTargetModeSet);
        VIOGPU_ASSERT_CHK(NT_SUCCESS(TempStatus));
    }

    VIOGPU_ASSERT_CHK(TempStatus == STATUS_NOT_FOUND || Status != STATUS_SUCCESS);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pSetVidPnSourceVisibility != NULL);
    VIOGPU_ASSERT((pSetVidPnSourceVisibility->VidPnSourceId < MAX_VIEWS) ||
               (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL));

    UINT StartVidPnSourceId = (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL) ? 0 : pSetVidPnSourceVisibility->VidPnSourceId;
    UINT MaxVidPnSourceId = (pSetVidPnSourceVisibility->VidPnSourceId == D3DDDI_ID_ALL) ? MAX_VIEWS : pSetVidPnSourceVisibility->VidPnSourceId + 1;

    for (UINT SourceId = StartVidPnSourceId; SourceId < MaxVidPnSourceId; ++SourceId)
    {
        if (pSetVidPnSourceVisibility->Visible)
        {
            m_CurrentModes[SourceId].Flags.FullscreenPresent = TRUE;
        }
        else
        {
            m_pHWDevice->BlackOutScreen(&m_CurrentModes[SourceId]);
        }

        // Store current visibility so it can be dealt with during Present call
        m_CurrentModes[SourceId].Flags.SourceNotVisible = !(pSetVidPnSourceVisibility->Visible);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

// NOTE: The value of pCommitVidPn->MonitorConnectivityChecks is ignored, since BDD is unable to recognize whether a monitor is connected or not
// The value of pCommitVidPn->hPrimaryAllocation is also ignored, since BDD is a display only driver and does not deal with allocations
NTSTATUS VioGpuDod::CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pCommitVidPn != NULL);
    VIOGPU_ASSERT(pCommitVidPn->AffectedVidPnSourceId < MAX_VIEWS);

    NTSTATUS                                 Status;
    SIZE_T                                   NumPaths = 0;
    D3DKMDT_HVIDPNTOPOLOGY                   hVidPnTopology = 0;
    D3DKMDT_HVIDPNSOURCEMODESET              hVidPnSourceModeSet = 0;
    CONST DXGK_VIDPN_INTERFACE*              pVidPnInterface = NULL;
    CONST DXGK_VIDPNTOPOLOGY_INTERFACE*      pVidPnTopologyInterface = NULL;
    CONST DXGK_VIDPNSOURCEMODESET_INTERFACE* pVidPnSourceModeSetInterface = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH*        pVidPnPresentPath = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE*         pPinnedVidPnSourceModeInfo = NULL;

    // Check this CommitVidPn is for the mode change notification when monitor is in power off state.
    if (pCommitVidPn->Flags.PathPoweredOff)
    {
        // Ignore the commitVidPn call for the mode change notification when monitor is in power off state.
        Status = STATUS_SUCCESS;
        goto CommitVidPnExit;
    }

    // Get the VidPn Interface so we can get the 'Source Mode Set' and 'VidPn Topology' interfaces
    Status = m_DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPn->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("DxgkCbQueryVidPnInterface failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn));
        goto CommitVidPnExit;
    }

    // Get the VidPn Topology interface so can enumerate paths from source
    Status = pVidPnInterface->pfnGetTopology(pCommitVidPn->hFunctionalVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetTopology failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn));
        goto CommitVidPnExit;
    }

    // Find out the number of paths now, if it's 0 don't bother with source mode set and pinned mode, just clear current and then quit
    Status = pVidPnTopologyInterface->pfnGetNumPaths(hVidPnTopology, &NumPaths);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetNumPaths failed with Status = 0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
        goto CommitVidPnExit;
    }

    if (NumPaths != 0)
    {
        // Get the Source Mode Set interface so we can get the pinned mode
        Status = pVidPnInterface->pfnAcquireSourceModeSet(pCommitVidPn->hFunctionalVidPn,
                                                          pCommitVidPn->AffectedVidPnSourceId,
                                                          &hVidPnSourceModeSet,
                                                          &pVidPnSourceModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquireSourceModeSet failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x, SourceId = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn, pCommitVidPn->AffectedVidPnSourceId));
            goto CommitVidPnExit;
        }

        // Get the mode that is being pinned
        Status = pVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePinnedModeInfo failed with Status = 0x%X, hFunctionalVidPn = 0x%I64x", Status, pCommitVidPn->hFunctionalVidPn));
            goto CommitVidPnExit;
        }
    }
    else
    {
        // This will cause the successful quit below
        pPinnedVidPnSourceModeInfo = NULL;
    }

    if (m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].FrameBuffer.Ptr &&
        !m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId].Flags.DoNotMapOrUnmap)
    {
        Status = m_pHWDevice->ReleaseFrameBuffer(&m_CurrentModes[pCommitVidPn->AffectedVidPnSourceId]);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }
    }

    if (pPinnedVidPnSourceModeInfo == NULL)
    {
        // There is no mode to pin on this source, any old paths here have already been cleared
        Status = STATUS_SUCCESS;
        goto CommitVidPnExit;
    }

    Status = IsVidPnSourceModeFieldsValid(pPinnedVidPnSourceModeInfo);
    if (!NT_SUCCESS(Status))
    {
        goto CommitVidPnExit;
    }

    // Get the number of paths from this source so we can loop through all paths
    SIZE_T NumPathsFromSource = 0;
    Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, &NumPathsFromSource);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pfnGetNumPathsFromSource failed with Status = 0x%X, hVidPnTopology = 0x%I64x", Status, hVidPnTopology));
        goto CommitVidPnExit;
    }

    // Loop through all paths to set this mode
    for (SIZE_T PathIndex = 0; PathIndex < NumPathsFromSource; ++PathIndex)
    {
        // Get the target id for this path
        D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = D3DDDI_ID_UNINITIALIZED;
        Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, PathIndex, &TargetId);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnEnumPathTargetsFromSource failed with Status = 0x%X, hVidPnTopology = 0x%I64x, SourceId = 0x%I64x, PathIndex = 0x%I64x",
                            Status, hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, PathIndex));
            goto CommitVidPnExit;
        }

        // Get the actual path info
        Status = pVidPnTopologyInterface->pfnAcquirePathInfo(hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, TargetId, &pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnAcquirePathInfo failed with Status = 0x%X, hVidPnTopology = 0x%I64x, SourceId = 0x%I64x, TargetId = 0x%I64x",
                            Status, hVidPnTopology, pCommitVidPn->AffectedVidPnSourceId, TargetId));
            goto CommitVidPnExit;
        }

        Status = IsVidPnPathFieldsValid(pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }

        Status = SetSourceModeAndPath(pPinnedVidPnSourceModeInfo, pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            goto CommitVidPnExit;
        }

        Status = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("pfnReleasePathInfo failed with Status = 0x%X, hVidPnTopoogy = 0x%I64x, pVidPnPresentPath = %p",
                            Status, hVidPnTopology, pVidPnPresentPath));
            goto CommitVidPnExit;
        }
        pVidPnPresentPath = NULL; // Successfully released it
    }

CommitVidPnExit:

    NTSTATUS TempStatus;
    UNREFERENCED_PARAMETER(TempStatus);

    if ((pVidPnSourceModeSetInterface != NULL) &&
        (hVidPnSourceModeSet != 0) &&
        (pPinnedVidPnSourceModeInfo != NULL))
    {
        TempStatus = pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnInterface != NULL) &&
        (pCommitVidPn->hFunctionalVidPn != 0) &&
        (hVidPnSourceModeSet != 0))
    {
        TempStatus = pVidPnInterface->pfnReleaseSourceModeSet(pCommitVidPn->hFunctionalVidPn, hVidPnSourceModeSet);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    if ((pVidPnTopologyInterface != NULL) &&
        (hVidPnTopology != 0) &&
        (pVidPnPresentPath != NULL))
    {
        TempStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPath);
        NT_ASSERT(NT_SUCCESS(TempStatus));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return Status;
}

NTSTATUS VioGpuDod::SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode,
                                         CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    CURRENT_BDD_MODE* pCurrentBddMode = &m_CurrentModes[pPath->VidPnSourceId];

    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s (%dx%d)\n", __FUNCTION__, pSourceMode->Format.Graphics.VisibleRegionSize.cx, pSourceMode->Format.Graphics.VisibleRegionSize.cy));
    pCurrentBddMode->Scaling = pPath->ContentTransformation.Scaling;
    pCurrentBddMode->SrcModeWidth = pSourceMode->Format.Graphics.VisibleRegionSize.cx;
    pCurrentBddMode->SrcModeHeight = pSourceMode->Format.Graphics.VisibleRegionSize.cy;
    pCurrentBddMode->Rotation = pPath->ContentTransformation.Rotation;

    pCurrentBddMode->DispInfo.Width = pSourceMode->Format.Graphics.PrimSurfSize.cx;
    pCurrentBddMode->DispInfo.Height = pSourceMode->Format.Graphics.PrimSurfSize.cy;
    pCurrentBddMode->DispInfo.Pitch = pSourceMode->Format.Graphics.PrimSurfSize.cx * BPPFromPixelFormat(pCurrentBddMode->DispInfo.ColorFormat) / BITS_PER_BYTE;

    Status = m_pHWDevice->AcquireFrameBuffer(pCurrentBddMode);

    if (NT_SUCCESS(Status))
    {
        pCurrentBddMode->Flags.FullscreenPresent = TRUE;
        for (USHORT ModeIndex = 0; ModeIndex < m_pHWDevice->GetModeCount(); ++ModeIndex)
        {
             PVIDEO_MODE_INFORMATION pModeInfo = m_pHWDevice->GetModeInfo(ModeIndex);
             if (pCurrentBddMode->DispInfo.Width == pModeInfo->VisScreenWidth &&
                 pCurrentBddMode->DispInfo.Height == pModeInfo->VisScreenHeight )
             {
                 Status = m_pHWDevice->SetCurrentMode(m_pHWDevice->GetModeNumber(ModeIndex), pCurrentBddMode);
                 if (NT_SUCCESS(Status))
                 {
                     m_pHWDevice->SetCurrentModeIndex(ModeIndex);
                 }
                 break;
             }
        }
    }

    return Status;
}

NTSTATUS VioGpuDod::IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH* pPath) const
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (pPath->VidPnSourceId >= MAX_VIEWS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("VidPnSourceId is 0x%I64x is too high (MAX_VIEWS is 0x%I64x)",
                        pPath->VidPnSourceId, MAX_VIEWS));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }
    else if (pPath->VidPnTargetId >= MAX_CHILDREN)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("VidPnTargetId is 0x%I64x is too high (MAX_CHILDREN is 0x%I64x)",
                        pPath->VidPnTargetId, MAX_CHILDREN));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    else if (pPath->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a gamma ramp (0x%I64x)", pPath->GammaRamp.Type));
        return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
    }
    else if ((pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_CENTERED) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED) &&
             (pPath->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a non-identity scaling (0x%I64x)", pPath->ContentTransformation.Scaling));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    else if ((pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_ROTATE90) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED) &&
             (pPath->ContentTransformation.Rotation != D3DKMDT_VPPR_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath contains a not-supported rotation (0x%I64x)", pPath->ContentTransformation.Rotation));
        return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
    }
    else if ((pPath->VidPnTargetColorBasis != D3DKMDT_CB_SCRGB) &&
             (pPath->VidPnTargetColorBasis != D3DKMDT_CB_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pPath has a non-linear RGB color basis (0x%I64x)", pPath->VidPnTargetColorBasis));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuDod::IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE* pSourceMode) const
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (pSourceMode->Type != D3DKMDT_RMT_GRAPHICS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode is a non-graphics mode (0x%I64x)", pSourceMode->Type));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else if ((pSourceMode->Format.Graphics.ColorBasis != D3DKMDT_CB_SCRGB) &&
             (pSourceMode->Format.Graphics.ColorBasis != D3DKMDT_CB_UNINITIALIZED))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode has a non-linear RGB color basis (0x%I64x)", pSourceMode->Format.Graphics.ColorBasis));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else if (pSourceMode->Format.Graphics.PixelValueAccessMode != D3DKMDT_PVAM_DIRECT)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode has a palettized access mode (0x%I64x)", pSourceMode->Format.Graphics.PixelValueAccessMode));
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }
    else
    {
        if (pSourceMode->Format.Graphics.PixelFormat == D3DDDIFMT_A8R8G8B8)
        {
            return STATUS_SUCCESS;
        }
    }

    DbgPrint(TRACE_LEVEL_ERROR, ("pSourceMode has an unknown pixel format (0x%I64x)", pSourceMode->Format.Graphics.PixelFormat));

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

NTSTATUS VioGpuDod::UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    VIOGPU_ASSERT(pUpdateActiveVidPnPresentPath != NULL);

    NTSTATUS Status = IsVidPnPathFieldsValid(&(pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // Mark the next present as fullscreen to make sure the full rotation comes through
    m_CurrentModes[pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId].Flags.FullscreenPresent = TRUE;

    m_CurrentModes[pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId].Rotation = pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.ContentTransformation.Rotation;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}



//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()

VOID VioGpuDod::DpcRoutine(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_pHWDevice->DpcRoutine(&m_DxgkInterface);
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuDod::InterruptRoutine(_In_  ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--> %s\n", __FUNCTION__));
    if(IsHardwareInit()) {
        return m_pHWDevice ? m_pHWDevice->InterruptRoutine(&m_DxgkInterface, MessageNumber) : FALSE;
    }
    return FALSE;
}

VOID VioGpuDod::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<---> %s\n", __FUNCTION__));
    m_pHWDevice->ResetDevice();
}

// Must be Non-Paged, as it sets up the display for a bugcheck
NTSTATUS VioGpuDod::SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                   _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                                   _Out_ UINT* pWidth,
                                                   _Out_ UINT* pHeight,
                                                   _Out_ D3DDDIFORMAT* pColorFormat)
{
    UNREFERENCED_PARAMETER(Flags);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_SystemDisplaySourceId = D3DDDI_ID_UNINITIALIZED;

    VIOGPU_ASSERT((TargetId < MAX_CHILDREN) || (TargetId == D3DDDI_ID_UNINITIALIZED));

    // Find the frame buffer for displaying the bugcheck, if it was successfully mapped
    if (TargetId == D3DDDI_ID_UNINITIALIZED)
    {
        for (UINT SourceIdx = 0; SourceIdx < MAX_VIEWS; ++SourceIdx)
        {
            if (m_CurrentModes[SourceIdx].FrameBuffer.Ptr != NULL)
            {
                m_SystemDisplaySourceId = SourceIdx;
                break;
            }
        }
    }
    else
    {
        m_SystemDisplaySourceId = FindSourceForTarget(TargetId, FALSE);
    }

    if (m_SystemDisplaySourceId == D3DDDI_ID_UNINITIALIZED)
    {
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    if ((m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE90) ||
        (m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE270))
    {
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }
    else
    {
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }

    *pColorFormat = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s ColorFormat = %d\n", __FUNCTION__, m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat));

    return STATUS_SUCCESS;
}

// Must be Non-Paged, as it is called to display the bugcheck screen
VOID VioGpuDod::SystemDisplayWrite(_In_reads_bytes_(SourceHeight * SourceStride) VOID* pSource,
                                              _In_ UINT SourceWidth,
                                              _In_ UINT SourceHeight,
                                              _In_ UINT SourceStride,
                                              _In_ INT PositionX,
                                              _In_ INT PositionY)
{
    UNREFERENCED_PARAMETER(pSource);
    UNREFERENCED_PARAMETER(SourceStride);
    // Rect will be Offset by PositionX/Y in the src to reset it back to 0
    RECT Rect;
    Rect.left = PositionX;
    Rect.top = PositionY;
    Rect.right =  Rect.left + SourceWidth;
    Rect.bottom = Rect.top + SourceHeight;

    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = m_CurrentModes[m_SystemDisplaySourceId].FrameBuffer.Ptr;
    DstBltInfo.Pitch = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Pitch;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = m_CurrentModes[m_SystemDisplaySourceId].Rotation;
    DstBltInfo.Width = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
    DstBltInfo.Height = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = pSource;
    SrcBltInfo.Pitch = SourceStride;
    SrcBltInfo.BitsPerPel = 32;

    SrcBltInfo.Offset.x = -PositionX;
    SrcBltInfo.Offset.y = -PositionY;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    SrcBltInfo.Width = SourceWidth;
    SrcBltInfo.Height = SourceHeight;

    BltBits(&DstBltInfo,
            &SrcBltInfo,
            1,
            &Rect);

}

#pragma code_seg(pop) // End Non-Paged Code

NTSTATUS VioGpuDod::WriteHWInfoStr(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue)
{
    PAGED_CODE();

    NTSTATUS Status;
    ANSI_STRING AnsiStrValue;
    UNICODE_STRING UnicodeStrValue;
    UNICODE_STRING UnicodeStrValueName;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // ZwSetValueKey wants the ValueName as a UNICODE_STRING
    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    // REG_SZ is for WCHARs, there is no equivalent for CHARs
    // Use the ansi/unicode conversion functions to get from PSTR to PWSTR
    RtlInitAnsiString(&AnsiStrValue, pszValue);
    Status = RtlAnsiStringToUnicodeString(&UnicodeStrValue, &AnsiStrValue, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("RtlAnsiStringToUnicodeString failed with Status: 0x%X\n", Status));
        return Status;
    }

    // Write the value to the registry
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &UnicodeStrValueName,
                           0,
                           REG_SZ,
                           UnicodeStrValue.Buffer,
                           UnicodeStrValue.MaximumLength);

    // Free the earlier allocated unicode string
    RtlFreeUnicodeString(&UnicodeStrValue);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey failed with Status: 0x%X\n", Status));
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VioGpuDod::RegisterHWInfo(_In_ ULONG Id)
{
    PAGED_CODE();

    NTSTATUS Status;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PCSTR StrHWInfoChipType = "QEMU VIRTIO GPU";
    PCSTR StrHWInfoDacType = "VIRTIO GPU";
    PCSTR StrHWInfoAdapterString = "VIRTIO GPU";
    PCSTR StrHWInfoBiosString = "SEABIOS VIRTIO GPU";

    HANDLE DevInstRegKeyHandle;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("IoOpenDeviceRegistryKey failed for PDO: 0x%I64x, Status: 0x%X", m_pPhysicalDevice, Status));
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.ChipType", StrHWInfoChipType);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.DacType", StrHWInfoDacType);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.AdapterString", StrHWInfoAdapterString);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.BiosString", StrHWInfoBiosString);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // MemorySize is a ULONG, unlike the others which are all strings
    UNICODE_STRING ValueNameMemorySize;
    RtlInitUnicodeString(&ValueNameMemorySize, L"HardwareInformation.MemorySize");
    DWORD MemorySize = 0; // BDD has no access to video memory
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &ValueNameMemorySize,
                           0,
                           REG_DWORD,
                           &MemorySize,
                           sizeof(MemorySize));
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey for MemorySize failed with Status: 0x%X\n", Status));
        return Status;
    }

    UNICODE_STRING VioGpuAdapterID;
    RtlInitUnicodeString(&VioGpuAdapterID, L"VioGpuAdapterID");
    DWORD DeviceId = Id;
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &VioGpuAdapterID,
                           0,
                           REG_BINARY,
                           &DeviceId,
                           sizeof(DeviceId));
    if (!NT_SUCCESS(Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("ZwSetValueKey for MemorySize failed with Status: 0x%X\n", Status));
        return Status;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}


//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()
D3DDDI_VIDEO_PRESENT_SOURCE_ID VioGpuDod::FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero)
{
    UNREFERENCED_PARAMETER(TargetId);
    for (UINT SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        if (m_CurrentModes[SourceId].FrameBuffer.Ptr != NULL)
        {
            return SourceId;
        }
    }

    return DefaultToZero ? 0 : D3DDDI_ID_UNINITIALIZED;
}

#pragma code_seg(pop) // End Non-Paged Code

#ifdef VIOGPU_X86

VgaAdapter::VgaAdapter(_In_ VioGpuDod* pVioGpuDod) : IVioGpuAdapter(pVioGpuDod)
{
    m_ModeInfo = NULL;
    m_ModeCount = 0;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_Id = 0;
    m_Type = VGA_DEVICE;
}

VgaAdapter::~VgaAdapter(void)
{
    HWClose();
    delete [] reinterpret_cast<BYTE*>(m_ModeInfo);
    delete [] reinterpret_cast<BYTE*>(m_ModeNumbers);
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_ModeCount = 0;
    m_Id = 0;
}

void VgaAdapter::SetVideoModeInfo(UINT Idx, PVBE_MODEINFO pModeInfo)
{
    PVIDEO_MODE_INFORMATION pMode = NULL;
    PAGED_CODE();

    pMode = &m_ModeInfo[Idx];
    pMode->Length = sizeof(VIDEO_MODE_INFORMATION);
    pMode->ModeIndex = Idx;
    pMode->VisScreenWidth = pModeInfo->XResolution;
    pMode->VisScreenHeight = pModeInfo->YResolution;
    pMode->ScreenStride = pModeInfo->LinBytesPerScanLine;
    pMode->NumberOfPlanes = pModeInfo->NumberOfPlanes;
    pMode->BitsPerPlane = pModeInfo->BitsPerPixel / pModeInfo->NumberOfPlanes;
    pMode->Frequency = 60;
    pMode->XMillimeter = pModeInfo->XResolution * 254 / 720;
    pMode->YMillimeter = pModeInfo->YResolution * 254 / 720;

    if (pModeInfo->BitsPerPixel == 15 && pModeInfo->NumberOfPlanes == 1)
    {
        pMode->BitsPerPlane = 16;
    }

    pMode->NumberRedBits = pModeInfo->LinRedMaskSize;
    pMode->NumberGreenBits = pModeInfo->LinGreenMaskSize;
    pMode->NumberBlueBits = pModeInfo->LinBlueMaskSize;
    pMode->RedMask = ((1 << pModeInfo->LinRedMaskSize) - 1) << pModeInfo->LinRedFieldPosition;
    pMode->GreenMask = ((1 << pModeInfo->LinGreenMaskSize) - 1) << pModeInfo->LinGreenFieldPosition;
    pMode->BlueMask = ((1 << pModeInfo->LinBlueMaskSize) - 1) << pModeInfo->LinBlueFieldPosition;

    pMode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS | VIDEO_MODE_NO_OFF_SCREEN;
    pMode->VideoMemoryBitmapWidth = pModeInfo->XResolution;
    pMode->VideoMemoryBitmapHeight = pModeInfo->YResolution;
    pMode->DriverSpecificAttributeFlags = 0;
}

NTSTATUS VgaAdapter::GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();
    USHORT m_Segment;
    USHORT m_Offset;
    USHORT ModeCount;
    ULONG SuitableModeCount;
    USHORT ModeTemp;
    USHORT CurrentMode;
    VBE_INFO VbeInfo = {0};
    ULONG Length;
    VBE_MODEINFO tmpModeInfo;
    UINT Height = pDispInfo->Height;
    UINT Width = pDispInfo->Width;
    UINT BitsPerPixel = BPPFromPixelFormat(pDispInfo->ColorFormat);
    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    Length = 0x400;
    Status = x86BiosAllocateBuffer (&Length, &m_Segment, &m_Offset);
    if (!NT_SUCCESS (Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosAllocateBuffer failed with Status: 0x%X\n", Status));
        return Status;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION, ("x86BiosAllocateBuffer 0x%x (%x.%x)\n", VbeInfo.VideoModePtr, m_Segment, m_Offset));

    Status = x86BiosWriteMemory (m_Segment, m_Offset, "VBE2", 4);

    if (!NT_SUCCESS (Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosWriteMemory failed with Status: 0x%X\n", Status));
        return Status;
    }

    X86BIOS_REGISTERS regs = {0};
    regs.SegEs = m_Segment;
    regs.Edi = m_Offset;
    regs.Eax = 0x4F00;
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }

    Status = x86BiosReadMemory (m_Segment, m_Offset, &VbeInfo, sizeof (VbeInfo));
    if (!NT_SUCCESS (Status))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosReadMemory failed with Status: 0x%X\n", Status));
        return Status;
    }

    if (!RtlEqualMemory(VbeInfo.Signature, "VESA", 4))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("No VBE BIOS present\n"));
        return STATUS_UNSUCCESSFUL;
    }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("VBE BIOS Present (%d.%d, %8ld Kb)\n", VbeInfo.Version / 0x100, VbeInfo.Version & 0xFF, VbeInfo.TotalMemory * 64));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("Capabilities = 0x%x\n", VbeInfo.Capabilities));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("VideoModePtr = 0x%x (0x%x.0x%x)\n", VbeInfo.VideoModePtr, HIWORD( VbeInfo.VideoModePtr), LOWORD( VbeInfo.VideoModePtr)));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("pDispInfo = %p %dx%d@%d\n", pDispInfo, Width, Height, BitsPerPixel));

   for (ModeCount = 0; ; ModeCount++)
   {
      /* Read the VBE mode number. */
        Status = x86BiosReadMemory (
                    HIWORD(VbeInfo.VideoModePtr),
                    LOWORD(VbeInfo.VideoModePtr) + (ModeCount << 1),
                    &ModeTemp,
                    sizeof(ModeTemp));

        if (!NT_SUCCESS (Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosReadMemory failed with Status: 0x%X\n", Status));
            break;
        }
      /* End of list? */
        if (ModeTemp == 0xFFFF || ModeTemp == 0)
        {
            break;
        }
   }

    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount %d\n", ModeCount));
    m_pVioGpuDod->SetHardwareInit(TRUE);

    delete [] reinterpret_cast<BYTE*>(m_ModeInfo);
    delete [] reinterpret_cast<BYTE*>(m_ModeNumbers);
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;

    m_ModeInfo = reinterpret_cast<PVIDEO_MODE_INFORMATION> (new (PagedPool) BYTE[sizeof (VIDEO_MODE_INFORMATION) * ModeCount]);
    if (!m_ModeInfo)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("VgaAdapter::GetModeList failed to allocate m_ModeInfo memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeInfo, sizeof (VIDEO_MODE_INFORMATION) * ModeCount);

    m_ModeNumbers = reinterpret_cast<PUSHORT> (new (PagedPool)  BYTE [sizeof (USHORT) * ModeCount]);
    if (!m_ModeNumbers)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("VgaAdapter::GetModeList failed to allocate m_ModeNumbers memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeNumbers, sizeof (USHORT) * ModeCount);

    m_CurrentMode = 0;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("m_ModeInfo = 0x%p, m_ModeNumbers = 0x%p\n", m_ModeInfo, m_ModeNumbers));
    if (Width == 0 || Height == 0 || BitsPerPixel != VGA_BPP)
    {
        Width = MIN_WIDTH_SIZE;
        Height = MIN_HEIGHT_SIZE;
        BitsPerPixel = VGA_BPP;
    }
    for (CurrentMode = 0, SuitableModeCount = 0;
         CurrentMode < ModeCount;
         CurrentMode++)
    {
        Status = x86BiosReadMemory (
                    HIWORD(VbeInfo.VideoModePtr),
                    LOWORD(VbeInfo.VideoModePtr) + (CurrentMode << 1),
                    &ModeTemp,
                    sizeof(ModeTemp));

        if (!NT_SUCCESS (Status))
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosReadMemory failed with Status: 0x%X\n", Status));
            break;
        }

        RtlZeroMemory(&regs, sizeof(regs));
        regs.Eax = 0x4F01;
        regs.Ecx = ModeTemp;
        regs.Edi = m_Offset + sizeof (VbeInfo);
        regs.SegEs = m_Segment;
        if (!x86BiosCall (0x10, &regs))
        {
           DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
           return STATUS_UNSUCCESSFUL;
        }
        Status = x86BiosReadMemory (
                    m_Segment,
                    m_Offset + sizeof (VbeInfo),
                    &tmpModeInfo,
                    sizeof(VBE_MODEINFO));

        DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeTemp = 0x%X %dx%d@%d\n", ModeTemp, tmpModeInfo.XResolution, tmpModeInfo.YResolution, tmpModeInfo.BitsPerPixel));

        if (tmpModeInfo.XResolution >= Width &&
            tmpModeInfo.YResolution >= Height &&
            tmpModeInfo.BitsPerPixel == BitsPerPixel &&
            tmpModeInfo.PhysBasePtr != 0)
        {
            m_ModeNumbers[SuitableModeCount] = ModeTemp;
            SetVideoModeInfo(SuitableModeCount, &tmpModeInfo);
            if (tmpModeInfo.XResolution == MIN_WIDTH_SIZE &&
                tmpModeInfo.YResolution == MIN_HEIGHT_SIZE)
            {
                m_CurrentMode = (USHORT)SuitableModeCount;
            }
            SuitableModeCount++;
        }
    }

    if (SuitableModeCount == 0)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("No video modes supported\n"));
        Status = STATUS_UNSUCCESSFUL;
    }

    m_ModeCount = SuitableModeCount;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount filtered %d\n", m_ModeCount));
    for (ULONG idx = 0; idx < m_ModeCount; idx++)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("type %x, XRes = %d, YRes = %d, BPP = %d\n",
                                    m_ModeNumbers[idx],
                                    m_ModeInfo[idx].VisScreenWidth,
                                    m_ModeInfo[idx].VisScreenHeight,
                                    m_ModeInfo[idx].BitsPerPlane));
    }

    if (m_Segment != 0)
    {
        x86BiosFreeBuffer (m_Segment, m_Offset);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VgaAdapter::SetCurrentMode(ULONG Mode, CURRENT_BDD_MODE* pCurrentBddMode)
{
    UNREFERENCED_PARAMETER(pCurrentBddMode);

    NTSTATUS Status = STATUS_SUCCESS;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s Mode = %x\n", __FUNCTION__, Mode));
    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F02;
    regs.Ebx = Mode | 0x000;
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return Status;
}

NTSTATUS VgaAdapter::HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pResList);
    UNREFERENCED_PARAMETER(pDispInfo);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return GetModeList(pDispInfo);
}

NTSTATUS VgaAdapter::HWClose(void)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_pVioGpuDod->SetHardwareInit(FALSE);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VgaAdapter::SetPowerState(_In_  DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    UNREFERENCED_PARAMETER(pDispInfo);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

    X86BIOS_REGISTERS regs = {0};
    regs.Eax = 0x4F10;
    regs.Ebx = 0;
    switch (DevicePowerState)
    {
        case PowerDeviceUnspecified: 
        case PowerDeviceD0: regs.Ebx |= 0x1; break;
        case PowerDeviceD1:
        case PowerDeviceD2: 
        case PowerDeviceD3: regs.Ebx |= 0x400; break;
    }
    if (!x86BiosCall (0x10, &regs))
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("x86BiosCall failed\n"));
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS
VgaAdapter::ExecutePresentDisplayOnly(
    _In_ BYTE*             DstAddr,
    _In_ UINT              DstBitPerPixel,
    _In_ BYTE*             SrcAddr,
    _In_ UINT              SrcBytesPerPixel,
    _In_ LONG              SrcPitch,
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* Moves,
    _In_ ULONG             NumDirtyRects,
    _In_ RECT*             DirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _In_ const CURRENT_BDD_MODE* pModeCur)
{

    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = DstAddr;
    DstBltInfo.Pitch = pModeCur->DispInfo.Pitch;
    DstBltInfo.BitsPerPel = DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = Rotation;
    DstBltInfo.Width = pModeCur->SrcModeWidth;
    DstBltInfo.Height = pModeCur->SrcModeHeight;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = SrcAddr;
    SrcBltInfo.Pitch = SrcPitch;
    SrcBltInfo.BitsPerPel = SrcBytesPerPixel * BITS_PER_BYTE;;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (Rotation == D3DKMDT_VPPR_ROTATE90 ||
        Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }

    // Copy all the scroll rects from source image to video frame buffer.
    for (UINT i = 0; i < NumMoves; i++)
    {
        RECT*    pDestRect = &Moves[i].DestRect;
        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1,
        pDestRect);
    }

    // Copy all the dirty rects from source image to video frame buffer.
    for (UINT i = 0; i < NumDirtyRects; i++)
    {
        RECT*    pDirtyRect = &DirtyRect[i];
        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1,
        pDirtyRect);
    } 

    return STATUS_SUCCESS;
}

VOID VgaAdapter::BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod)
{
    PAGED_CODE();

    UINT ScreenHeight = pCurrentBddMod->DispInfo.Height;
    UINT ScreenPitch = pCurrentBddMod->DispInfo.Pitch;

    PHYSICAL_ADDRESS NewPhysAddrStart = pCurrentBddMod->DispInfo.PhysicAddress;
    PHYSICAL_ADDRESS NewPhysAddrEnd;
    NewPhysAddrEnd.QuadPart = NewPhysAddrStart.QuadPart + (ScreenHeight * ScreenPitch);

    if (pCurrentBddMod->Flags.FrameBufferIsActive)
    {
        BYTE* MappedAddr = reinterpret_cast<BYTE*>(pCurrentBddMod->FrameBuffer.Ptr);

        // Zero any memory at the start that hasn't been zeroed recently
        if (NewPhysAddrStart.QuadPart < pCurrentBddMod->ZeroedOutStart.QuadPart)
        {
            if (NewPhysAddrEnd.QuadPart < pCurrentBddMod->ZeroedOutStart.QuadPart)
            {
                // No overlap
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(pCurrentBddMod->ZeroedOutStart.QuadPart - NewPhysAddrStart.QuadPart));
            }
        }

        // Zero any memory at the end that hasn't been zeroed recently
        if (NewPhysAddrEnd.QuadPart > pCurrentBddMod->ZeroedOutEnd.QuadPart)
        {
            if (NewPhysAddrStart.QuadPart > pCurrentBddMod->ZeroedOutEnd.QuadPart)
            {
                // No overlap
                // NOTE: When actual pixels were the most recent thing drawn, ZeroedOutStart & ZeroedOutEnd will both be 0
                // and this is the path that will be used to black out the current screen.
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(NewPhysAddrEnd.QuadPart - pCurrentBddMod->ZeroedOutEnd.QuadPart));
            }
        }
    }

    pCurrentBddMod->ZeroedOutStart.QuadPart = NewPhysAddrStart.QuadPart;
    pCurrentBddMod->ZeroedOutEnd.QuadPart = NewPhysAddrEnd.QuadPart;
}

BOOLEAN VgaAdapter::InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(pDxgkInterface);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}

VOID VgaAdapter::DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface)
{
    UNREFERENCED_PARAMETER(pDxgkInterface);
}

VOID VgaAdapter::ResetDevice(VOID)
{
}

NTSTATUS VgaAdapter::AcquireFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode)
{
    if (pCurrentBddMode->Flags.DoNotMapOrUnmap) {
        return STATUS_UNSUCCESSFUL;
    }

// Map the new frame buffer
    VIOGPU_ASSERT(pCurrentBddMode->FrameBuffer.Ptr == NULL);
    NTSTATUS status = MapFrameBuffer(pCurrentBddMode->DispInfo.PhysicAddress,
        pCurrentBddMode->DispInfo.Pitch * pCurrentBddMode->DispInfo.Height,
        &(pCurrentBddMode->FrameBuffer.Ptr));
    if (NT_SUCCESS(status)) {
        pCurrentBddMode->Flags.FrameBufferIsActive = TRUE;
    }
    return status;
}

NTSTATUS VgaAdapter::ReleaseFrameBuffer(CURRENT_BDD_MODE* pCurrentBddMode)
{
    NTSTATUS status = UnmapFrameBuffer(pCurrentBddMode->FrameBuffer.Ptr, pCurrentBddMode->DispInfo.Height * pCurrentBddMode->DispInfo.Pitch);
    pCurrentBddMode->FrameBuffer.Ptr = NULL;
    pCurrentBddMode->Flags.FrameBufferIsActive = FALSE;
    return status;
}

NTSTATUS  VgaAdapter::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pModeCur)
{
    UNREFERENCED_PARAMETER(pSetPointerShape);
    UNREFERENCED_PARAMETER(pModeCur);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VgaAdapter::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_BDD_MODE* pModeCur)
{
    UNREFERENCED_PARAMETER(pSetPointerPosition);
    UNREFERENCED_PARAMETER(pModeCur);
    return STATUS_SUCCESS;
}

NTSTATUS VgaAdapter::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UNREFERENCED_PARAMETER(pEscape);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_NOT_SUPPORTED;
}

#endif // VIOGPU_X86

VioGpuAdapter::VioGpuAdapter(_In_ VioGpuDod* pVioGpuDod) : IVioGpuAdapter(pVioGpuDod)
{
    PAGED_CODE();

    m_ModeInfo = NULL;
    m_ModeCount = 0;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_Id = 0;
    m_Type = GPU_DEVICE;
    m_pFrameBuf = NULL;
    m_pCursorBuf = NULL;
    m_PendingWorks = 0;
    KeInitializeEvent(&m_ConfigUpdateEvent,
                      SynchronizationEvent,
                      FALSE);
    m_bStopWorkThread = FALSE;
    m_pWorkThread = NULL;
}

VioGpuAdapter::~VioGpuAdapter(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    DestroyCursor();
    DestroyFrameBufferObj();
    VioGpuAdapterClose();
    HWClose();
    delete [] reinterpret_cast<BYTE*>(m_ModeInfo);
    delete [] reinterpret_cast<BYTE*>(m_ModeNumbers);
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_CustomMode = 0;
    m_ModeCount = 0;
    m_Id = 0;
    m_Type = INVALID_DEVICE;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapter::SetCurrentMode(ULONG Mode, CURRENT_BDD_MODE* pCurrentBddMode)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s - %d: Mode = %d\n", __FUNCTION__, m_Id, Mode));
    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        if (Mode == m_ModeNumbers[idx])
        {
            DestroyFrameBufferObj();
            pCurrentBddMode->Flags.FrameBufferIsActive = FALSE;
            pCurrentBddMode->DispInfo.PhysicAddress.QuadPart = 0LL;
            status = CreateFrameBufferObj(&m_ModeInfo[idx], pCurrentBddMode);
            DbgPrint(TRACE_LEVEL_ERROR, ("%s device %d: setting current mode %d (%d x %d), return: %x\n",
                __FUNCTION__, m_Id, Mode, m_ModeInfo[idx].VisScreenWidth,
                m_ModeInfo[idx].VisScreenHeight, status));
            return status;
        }
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s failed\n", __FUNCTION__));
    return status;
}

NTSTATUS VioGpuAdapter::VioGpuAdapterInit(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UNREFERENCED_PARAMETER(pDispInfo);
    status = VirtIoDeviceInit();
    if (!NT_SUCCESS(status)) {
        DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio device, error %x\n", status));
        return status;
    }
    m_pVioGpuDod->SetVirtIOInit(TRUE);

    m_u64HostFeatures = virtio_get_features(&m_VioDev);
    do
    {
        struct virtqueue *vqs[2];
        if (!AckFeature(VIRTIO_F_VERSION_1))
        {
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        status = virtio_set_features(&m_VioDev, m_u64GuestFeatures);
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s virtio_set_features failed with %x\n", __FUNCTION__, status));
            break;
        }

        status = virtio_find_queues(
            &m_VioDev,
            2,
            vqs);
        if (!NT_SUCCESS(status)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("virtio_find_queues failed with error %x\n", status));
            break;
        }

        if (!m_CtrlQueue.Init(&m_VioDev, vqs[0], 0) ||
            !m_CursorQueue.Init(&m_VioDev, vqs[1], 1)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio queues\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_scanouts),
                          &m_u32NumScanouts, sizeof(m_u32NumScanouts));

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_capsets),
                          &m_u32NumCapsets, sizeof(m_u32NumCapsets));
    } while (0);
    if (status == STATUS_SUCCESS)
    {
       virtio_device_ready(&m_VioDev);
       m_pVioGpuDod->SetHardwareInit(TRUE);
    }
    else
    {
        virtio_add_status(&m_VioDev, VIRTIO_CONFIG_S_FAILED);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

void VioGpuAdapter::VioGpuAdapterClose()
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    m_pVioGpuDod->SetHardwareInit(FALSE);
    if (m_pVioGpuDod->IsVirtIOInit())
    {
        virtio_device_reset(&m_VioDev);
        virtio_delete_queues(&m_VioDev);
    }
    m_CtrlQueue.Close();
    m_CursorQueue.Close();
    if (m_pVioGpuDod->IsVirtIOInit())
    {
        virtio_device_shutdown(&m_VioDev);
    }
    m_pVioGpuDod->SetVirtIOInit(FALSE);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapter::SetPowerState(DEVICE_POWER_STATE DevicePowerState, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s DevicePowerState = %d\n", __FUNCTION__, DevicePowerState));

    switch (DevicePowerState)
    {
        case PowerDeviceUnspecified:
        case PowerDeviceD0: VioGpuAdapterInit(pDispInfo); break;
        case PowerDeviceD1:
        case PowerDeviceD2:
        case PowerDeviceD3: VioGpuAdapterClose(); break;
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

BOOLEAN VioGpuAdapter::AckFeature(UINT64 Feature)
{
    if (virtio_is_feature_enabled(m_u64HostFeatures, Feature))
    {
        virtio_feature_enable(m_u64GuestFeatures, Feature);
        return TRUE;
    }
    return FALSE;
}

NTSTATUS VioGpuAdapter::VirtIoDeviceInit()
{
    PAGED_CODE();

    return virtio_device_initialize(
        &m_VioDev,
        &VioGpuSystemOps,
        this,
        FALSE);
}

NTSTATUS VioGpuAdapter::HWInit(PCM_RESOURCE_LIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    HANDLE   threadHandle = 0;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

    struct virtqueue *vqs[2];
    UINT size = 0;
    do
    {
        if (m_PciResources.Init(GetVioGpu()->GetDxgkInterface(), pResList))
        {
            status = VirtIoDeviceInit();
            if (!NT_SUCCESS(status)) {
                DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio device, error %x\n", status));
                return status; // cannot call virtio_add_status if we failed
            }
            m_pVioGpuDod->SetVirtIOInit(TRUE);
            m_u64HostFeatures = virtio_get_features(&m_VioDev);
        }
        else
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("Incomplete resources\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        if (!AckFeature(VIRTIO_F_VERSION_1))
        {
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        status = virtio_set_features(&m_VioDev, m_u64GuestFeatures);
        if (!NT_SUCCESS(status))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s virtio_set_features failed with %x\n", __FUNCTION__, status));
            break;
        }

        if (!m_CursorSegment.Init(POINTER_SIZE * POINTER_SIZE * 4, NULL))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to allocate Cursor memory segment\n", __FUNCTION__));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        if (!m_ICDSegment.Init(MAX_ICD_MEMORY, NULL))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to allocate ICD memory segment\n", __FUNCTION__));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        status = virtio_find_queues(
            &m_VioDev,
            2,
            vqs);
        if (!NT_SUCCESS(status)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("virtio_find_queues failed with error %x\n", status));
            break;
        }

        if (!m_CtrlQueue.Init(&m_VioDev, vqs[0], 0) ||
            !m_CursorQueue.Init(&m_VioDev, vqs[1], 1)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize virtio queues\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        size = m_CtrlQueue.QueryAllocation() + m_CursorQueue.QueryAllocation();
        DbgPrint(TRACE_LEVEL_FATAL, ("%s size %d\n", __FUNCTION__, size));
        ASSERT(size);

        if (!m_GpuBuf.Init(size)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize buffers\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        m_CtrlQueue.SetGpuBuf(&m_GpuBuf);
        m_CursorQueue.SetGpuBuf(&m_GpuBuf);

        if (!m_Idr.Init(1)) {
            DbgPrint(TRACE_LEVEL_FATAL, ("Failed to initialize id generator\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_scanouts),
                          &m_u32NumScanouts, sizeof(m_u32NumScanouts));

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_capsets),
                          &m_u32NumCapsets, sizeof(m_u32NumCapsets));
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s num_scanouts %d, num_capsets %d\n", __FUNCTION__, m_u32NumScanouts, m_u32NumCapsets));
    } while(0);

    if (status == STATUS_SUCCESS)
    {
       virtio_device_ready(&m_VioDev);
       m_pVioGpuDod->SetHardwareInit(TRUE);
    }
    else
    {
        virtio_add_status(&m_VioDev, VIRTIO_CONFIG_S_FAILED);
        return status;
    }
//FIXME!!
    status = PsCreateSystemThread(&threadHandle,
                                (ACCESS_MASK)0,
                                NULL,
                                (HANDLE) 0,
                                NULL,
                                VioGpuAdapter::ThreadWork,
                                this);

    if ( !NT_SUCCESS( status ))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to create system thread, status %x\n", __FUNCTION__, status));
        return status;
    }
    ObReferenceObjectByHandle(threadHandle,
                            THREAD_ALL_ACCESS,
                            NULL,
                            KernelMode,
                            (PVOID*)(&m_pWorkThread),
                            NULL );

    ZwClose(threadHandle);

    status = GetModeList(pDispInfo);
    if (!NT_SUCCESS(status))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s GetModeList failed with %x\n", __FUNCTION__, status));
    }
    return status;
}

NTSTATUS VioGpuAdapter::HWClose(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    m_pVioGpuDod->SetHardwareInit(FALSE);

    m_bStopWorkThread = TRUE;
    KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);

    if (m_pWorkThread != NULL) // NULL if HWInit failed
    {
        KeWaitForSingleObject(m_pWorkThread,
            Executive,
            KernelMode,
            FALSE,
            NULL);

        ObDereferenceObject(m_pWorkThread);
    }

    m_FrameSegment.Close();
    m_CursorSegment.Close();
    m_ICDSegment.Close();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));

    return STATUS_SUCCESS;
}

BOOLEAN FindUpdateRect(
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* pMoves,
    _In_ ULONG             NumDirtyRects,
    _In_ PRECT             pDirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _Out_ PRECT pUpdateRect)
{
    UNREFERENCED_PARAMETER(Rotation);
    BOOLEAN updated = FALSE;
    for (ULONG i = 0; i < NumMoves; i++)
    {
        PRECT  pRect = &pMoves[i].DestRect;
        if (!updated)
        {
            *pUpdateRect = *pRect;
            updated = TRUE;
        }
        else
        {
            pUpdateRect->bottom = max(pRect->bottom, pUpdateRect->bottom);
            pUpdateRect->left = min(pRect->left, pUpdateRect->left);
            pUpdateRect->right = max(pRect->right, pUpdateRect->right);
            pUpdateRect->top = min(pRect->top, pUpdateRect->top);
        }
    }
    for (ULONG i = 0; i < NumDirtyRects; i++)
    {
        PRECT  pRect = &pDirtyRect[i];
        if (!updated)
        {
            *pUpdateRect = *pRect;
            updated = TRUE;
        }
        else
        {
            pUpdateRect->bottom = max(pRect->bottom, pUpdateRect->bottom);
            pUpdateRect->left = min(pRect->left, pUpdateRect->left);
            pUpdateRect->right = max(pRect->right, pUpdateRect->right);
            pUpdateRect->top = min(pRect->top, pUpdateRect->top);
        }
    }
    if (Rotation == D3DKMDT_VPPR_ROTATE90 || Rotation == D3DKMDT_VPPR_ROTATE270)
    {
    }
    return updated;
}

NTSTATUS VioGpuAdapter::ExecutePresentDisplayOnly(
                             _In_ BYTE*             DstAddr,
                             _In_ UINT              DstBitPerPixel,
                             _In_ BYTE*             SrcAddr,
                             _In_ UINT              SrcBytesPerPixel,
                             _In_ LONG              SrcPitch,
                             _In_ ULONG             NumMoves,
                             _In_ D3DKMT_MOVE_RECT* pMoves,
                             _In_ ULONG             NumDirtyRects,
                             _In_ RECT*             pDirtyRect,
                             _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
                             _In_ const CURRENT_BDD_MODE* pModeCur)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    BLT_INFO SrcBltInfo = {0};
    BLT_INFO DstBltInfo = { 0 };
    UINT resid = 0;
    RECT updrect = {0};
    ULONG offset = 0UL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("SrcBytesPerPixel = %d DstBitPerPixel = %d (%dx%d)\n", SrcBytesPerPixel, DstBitPerPixel, pModeCur->SrcModeWidth, pModeCur->SrcModeHeight));

    DstBltInfo.pBits = DstAddr;
    DstBltInfo.Pitch = pModeCur->DispInfo.Pitch;
    DstBltInfo.BitsPerPel = DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = Rotation;
    DstBltInfo.Width = pModeCur->SrcModeWidth;
    DstBltInfo.Height = pModeCur->SrcModeHeight;

    SrcBltInfo.pBits = SrcAddr;
    SrcBltInfo.Pitch = SrcPitch;
    SrcBltInfo.BitsPerPel = SrcBytesPerPixel * BITS_PER_BYTE;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (Rotation == D3DKMDT_VPPR_ROTATE90 ||
        Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }

    for (UINT i = 0; i < NumMoves; i++)
    {
        RECT*  pDestRect = &pMoves[i].DestRect;
        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1,
        pDestRect);
    }

    for (UINT i = 0; i < NumDirtyRects; i++)
    {
        RECT*  pRect = &pDirtyRect[i];
        BltBits(&DstBltInfo,
        &SrcBltInfo,
        1,
        pRect);
    } 
    if (!FindUpdateRect(NumMoves, pMoves, NumDirtyRects, pDirtyRect, Rotation, &updrect))
    {
        updrect.top = 0;
        updrect.left = 0;
        updrect.bottom = pModeCur->SrcModeHeight;
        updrect.right = pModeCur->SrcModeWidth;
        offset = 0UL;
    }
//FIXME rotation
    offset = (updrect.top * pModeCur->DispInfo.Pitch) + (updrect.left * ((DstBitPerPixel + BITS_PER_BYTE -1 ) / BITS_PER_BYTE));

    resid = m_pFrameBuf->GetId();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("offset = %lu (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
                                        offset,
                                        updrect.left,
                                        updrect.top,
                                        updrect.right - updrect.left,
                                        updrect.bottom - updrect.top,
                                        0,
                                        0,
                                        pModeCur->SrcModeWidth,
                                        pModeCur->SrcModeHeight));

    m_CtrlQueue.TransferToHost2D(resid, offset, updrect.right - updrect.left, updrect.bottom - updrect.top, updrect.left, updrect.top, NULL);
    m_CtrlQueue.ResFlush(resid, updrect.right - updrect.left, updrect.bottom - updrect.top, updrect.left, updrect.top);

    return STATUS_SUCCESS;
}

VOID VioGpuAdapter::BlackOutScreen(CURRENT_BDD_MODE* pCurrentBddMod)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

    UINT ScreenHeight = pCurrentBddMod->DispInfo.Height;
    UINT ScreenPitch = pCurrentBddMod->DispInfo.Pitch;
    BYTE* pDst = (BYTE*)pCurrentBddMod->FrameBuffer.Ptr;
    if (pCurrentBddMod->Flags.FrameBufferIsActive && pDst)
    {
        RtlZeroMemory(pDst, ScreenHeight * ScreenPitch);
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapter::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pModeCur)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("<--> %s flag = %d pitch = %d, pixels = %p, id = %d, w = %d, h = %d, x = %d, y = %d\n", __FUNCTION__,
                                 pSetPointerShape->Flags.Value,
                                 pSetPointerShape->Pitch,
                                 pSetPointerShape->pPixels,
                                 pSetPointerShape->VidPnSourceId,
                                 pSetPointerShape->Width,
                                 pSetPointerShape->Height,
                                 pSetPointerShape->XHot,
                                 pSetPointerShape->YHot));

    DestroyCursor();
    if (CreateCursor(pSetPointerShape, pModeCur))
    {
        PGPU_UPDATE_CURSOR crsr;
        PGPU_VBUFFER vbuf;
        UINT ret = 0;
        crsr = (PGPU_UPDATE_CURSOR)m_CursorQueue.AllocCursor(&vbuf);
        RtlZeroMemory(crsr, sizeof(*crsr));

        crsr->hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
        crsr->resource_id = m_pCursorBuf->GetId();
        crsr->pos.x = 0;
        crsr->pos.y = 0;
        crsr->hot_x = pSetPointerShape->XHot;
        crsr->hot_y = pSetPointerShape->YHot;
        ret = m_CursorQueue.QueueCursor(vbuf);
        DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s vbuf = %p, ret = %d\n", __FUNCTION__, vbuf, ret));
        return STATUS_SUCCESS;
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Failed to create cursor\n", __FUNCTION__));
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS VioGpuAdapter::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_BDD_MODE* pModeCur)
{
    PAGED_CODE();
    PGPU_UPDATE_CURSOR crsr;
    PGPU_VBUFFER vbuf;
    UINT ret = 0;
    crsr = (PGPU_UPDATE_CURSOR)m_CursorQueue.AllocCursor(&vbuf);
    RtlZeroMemory(crsr, sizeof(*crsr));

    crsr->hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    crsr->resource_id = m_pCursorBuf->GetId();

    if (!pSetPointerPosition->Flags.Visible ||
        (UINT)pSetPointerPosition->X > pModeCur->SrcModeWidth ||
        (UINT)pSetPointerPosition->Y > pModeCur->SrcModeHeight ||
        pSetPointerPosition->X < 0 ||
        pSetPointerPosition->Y < 0) {
        DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s (%d - %d) Visiable = %d Value = %x VidPnSourceId = %d\n",
            __FUNCTION__,
            pSetPointerPosition->X,
            pSetPointerPosition->Y,
            pSetPointerPosition->Flags.Visible,
            pSetPointerPosition->Flags.Value,
            pSetPointerPosition->VidPnSourceId));
        crsr->pos.x = 0;
        crsr->pos.y = 0;
    } else {
        DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s (%d - %d) Visiable = %d Value = %x VidPnSourceId = %d\n",
            __FUNCTION__,
            pSetPointerPosition->X,
            pSetPointerPosition->Y,
            pSetPointerPosition->Flags.Visible,
            pSetPointerPosition->Flags.Value,
            pSetPointerPosition->VidPnSourceId));
        crsr->pos.x = pSetPointerPosition->X;
        crsr->pos.y = pSetPointerPosition->Y;
    }
    ret = m_CursorQueue.QueueCursor(vbuf);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s vbuf = %p, ret = %d\n", __FUNCTION__, vbuf, ret));
    return STATUS_SUCCESS;
}

struct gpu_allocate_object_t {
    UINT32 driver_cmd;
    UINT32 size;
    UINT64 handle;
};

struct gpu_update_object_t {
    UINT32 driver_cmd;
    UINT64 handle;
    UINT32 size;
    VOID* ptr;
};

struct gpu_delete_object_t {
    UINT32 driver_cmd;
    UINT64 handle;
};

NTSTATUS VioGpuAdapter::EscapeCreateObject(VOID *data, UINT32 size)
{
    PAGED_CODE();

    gpu_allocate_object_t *info = NULL;
    VioGpuObj *obj = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (size != sizeof(gpu_allocate_object_t))
        return STATUS_INVALID_BUFFER_SIZE;

    info = reinterpret_cast<gpu_allocate_object_t*>(data);

    obj = new(NonPagedPoolNx)VioGpuObj();

    if (!obj)
        return STATUS_NO_MEMORY;
    if (!obj->Init(info->size, &m_FrameSegment))
        return STATUS_NO_MEMORY;

    info->handle = reinterpret_cast<UINT64>(obj);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapter::EscapeUpdateObject(VOID *data, UINT32 size)
{
    PAGED_CODE();

    gpu_update_object_t *info = NULL;
    VioGpuObj *object = NULL;
    VOID *ptr = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (size != sizeof(gpu_update_object_t))
        return STATUS_INVALID_BUFFER_SIZE;
    if (!object)
        return STATUS_INVALID_PARAMETER_1;

    info = reinterpret_cast<gpu_update_object_t*>(data);
    object = reinterpret_cast<VioGpuObj*>(info->handle);

    ptr = object->GetVirtualAddress();
    memcpy_s(ptr, info->size, info->ptr, info->size);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapter::EscapeDeleteObject(VOID *data, UINT32 size)
{
    PAGED_CODE();

    gpu_delete_object_t *info = NULL;
    VioGpuObj *object = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    if (size != sizeof(gpu_delete_object_t))
        return STATUS_INVALID_BUFFER_SIZE;
    if (info->handle == 0)
        return STATUS_DATA_ERROR;

    info = reinterpret_cast<gpu_delete_object_t*>(data);
    object = reinterpret_cast<VioGpuObj*>(info->handle);

    delete object;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}


NTSTATUS VioGpuAdapter::Escape(_In_ CONST DXGKARG_ESCAPE *pEscape)
{
    PAGED_CODE();

    NTSTATUS res = STATUS_SUCCESS;
    UINT32 *cmd_type = NULL;
    UINT32 size;
    VOID *data = NULL;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %\n", __FUNCTION__));

    VIOGPU_ASSERT_CHK(pEscape != NULL);
    VIOGPU_ASSERT_CHK(pEscape->PrivateDriverDataSize >= sizeof(UINT32));

    cmd_type = reinterpret_cast<UINT32*>(pEscape->pPrivateDriverData);
    data = cmd_type + 1;
    size = pEscape->PrivateDriverDataSize - sizeof(UINT32);

    switch (*cmd_type) {
    case OPENGL_ICD_CMD_ALLOCATE:
        res = EscapeCreateObject(data, size);
        res = STATUS_NOT_IMPLEMENTED;
        break;
    case OPENGL_ICD_CMD_UPDATE:
        res = EscapeUpdateObject(data, size);
        res = STATUS_NOT_IMPLEMENTED;
        break;
    case OPENGL_ICD_CMD_FREE:
        res = EscapeDeleteObject(data, size);
        res = STATUS_NOT_IMPLEMENTED;
        break;
    case OPENGL_ICD_CMD_TRANSFER:
        m_CtrlQueue.SubmitCmd(data, size);
        res = STATUS_SUCCESS;
        break;
    default:
        res = STATUS_INVALID_TOKEN;
        break;
    }

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s ret = 0x%x\n", __FUNCTION__, res));
    return STATUS_SUCCESS;
}

BOOLEAN VioGpuAdapter::GetDisplayInfo(void)
{
    PAGED_CODE();

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf = NULL;
    ULONG xres = 0;
    ULONG yres = 0;

    m_CtrlQueue.AskDisplayInfo(&vbuf);
    for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
        m_CtrlQueue.GetDisplayInfo(vbuf, i, &xres, &yres);
    }

    DbgPrint(TRACE_LEVEL_FATAL, ("---> %s (%dx%d)\n", __FUNCTION__, xres, yres));
    if(xres && yres) {
        SetCustomDisplay((USHORT)xres, (USHORT)yres);
    }

    m_CtrlQueue.ReleaseBuffer(vbuf);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void VioGpuAdapter::SetVideoModeInfo(UINT Idx, PGPU_DISP_MODE pModeInfo)
{
    PVIDEO_MODE_INFORMATION pMode = NULL;
    UINT bytes_pp = (VGPU_BPP + 7) / 8;
    PAGED_CODE();

    pMode = &m_ModeInfo[Idx];
    pMode->Length = sizeof(VIDEO_MODE_INFORMATION);
    pMode->ModeIndex = Idx;
    pMode->VisScreenWidth = pModeInfo->XResolution;
    pMode->VisScreenHeight = pModeInfo->YResolution;
    pMode->ScreenStride = (pModeInfo->XResolution * bytes_pp + 3) & ~0x3;
    pMode->NumberOfPlanes = 1;
    pMode->BitsPerPlane = VGPU_BPP;
    pMode->Frequency = 100;
    pMode->XMillimeter = pModeInfo->XResolution * 254 / 720;
    pMode->YMillimeter = pModeInfo->YResolution * 254 / 720;

    pMode->NumberRedBits = 8;
    pMode->NumberGreenBits = 8;
    pMode->NumberBlueBits = 8;
    pMode->RedMask = 0xff;
    pMode->GreenMask = 0xff;
    pMode->BlueMask = 0xff;

    pMode->AttributeFlags = VIDEO_MODE_COLOR | VIDEO_MODE_GRAPHICS;
    pMode->VideoMemoryBitmapWidth = pModeInfo->XResolution;
    pMode->VideoMemoryBitmapHeight = pModeInfo->YResolution;
    pMode->DriverSpecificAttributeFlags = 0;
}

NTSTATUS VioGpuAdapter::UpdateChildStatus(BOOLEAN connect)
{
    PAGED_CODE();
    NTSTATUS           Status(STATUS_SUCCESS);
    DXGK_CHILD_STATUS  ChildStatus;
    PDXGKRNL_INTERFACE pDXGKInterface(m_pVioGpuDod->GetDxgkInterface());

    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));

    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = 0;
    ChildStatus.HotPlug.Connected = connect;
    Status = pDXGKInterface->DxgkCbIndicateChildStatus(pDXGKInterface->DeviceHandle, &ChildStatus);
    if (Status != STATUS_SUCCESS)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s DxgkCbIndicateChildStatus failed with status %x\n ", __FUNCTION__, Status));
    }
    return Status;
}

void VioGpuAdapter::SetCustomDisplay(_In_ USHORT xres, _In_ USHORT yres)
{
    PAGED_CODE();

    GPU_DISP_MODE tmpModeInfo = {0};

    DbgPrint(TRACE_LEVEL_FATAL, ("%s - %d (%dx%d#%d)\n", __FUNCTION__, m_Id, xres, yres, VGPU_BPP));

    if (xres < MIN_WIDTH_SIZE || yres < MIN_HEIGHT_SIZE) {
        DbgPrint(TRACE_LEVEL_WARNING, ("%s: (%dx%d#%d) less than (%dx%d)\n", __FUNCTION__,
            xres, yres, VGPU_BPP, MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE));
    }
    tmpModeInfo.XResolution = max(MIN_WIDTH_SIZE, xres);
    tmpModeInfo.YResolution = max(MIN_HEIGHT_SIZE, yres);

    tmpModeInfo.Bits = VGPU_BPP;
    m_CustomMode =(USHORT) ((m_CustomMode == m_ModeCount - 1)? m_ModeCount - 2 : m_ModeCount - 1);

    DbgPrint(TRACE_LEVEL_FATAL, ("%s - %d (%dx%d)\n", __FUNCTION__, m_CustomMode, tmpModeInfo.XResolution, tmpModeInfo.YResolution));

    SetVideoModeInfo(m_CustomMode, &tmpModeInfo);
}

GPU_DISP_MODE gpu_disp_modes[] =
{
    {1024, 768, VGPU_BPP},
    {1280, 800, VGPU_BPP},
    {1280, 1024, VGPU_BPP},
    {1440, 900, VGPU_BPP},
    {1680, 1050, VGPU_BPP},
    {1920, 1080, VGPU_BPP},
    {2560, 1600, VGPU_BPP},
};

NTSTATUS VioGpuAdapter::GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(pDispInfo);
    NTSTATUS Status = STATUS_SUCCESS;

    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UINT ModeCount;
    delete [] reinterpret_cast<BYTE*>(m_ModeInfo);
    delete [] reinterpret_cast<BYTE*>(m_ModeNumbers);
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;

    ModeCount = ARRAYSIZE(gpu_disp_modes);
    ModeCount += 2;
    m_ModeInfo = reinterpret_cast<PVIDEO_MODE_INFORMATION> (new (PagedPool) BYTE[sizeof (VIDEO_MODE_INFORMATION) * ModeCount]);
    if (!m_ModeInfo)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("VioGpuAdapter::GetModeList failed to allocate m_ModeInfo memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeInfo, sizeof (VIDEO_MODE_INFORMATION) * ModeCount);

    m_ModeNumbers = reinterpret_cast<PUSHORT> (new (PagedPool)  BYTE [sizeof (USHORT) * ModeCount]);
    if (!m_ModeNumbers)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrint(TRACE_LEVEL_ERROR, ("VioGpuAdapter::GetModeList failed to allocate m_ModeNumbers memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeNumbers, sizeof (USHORT) * ModeCount);

    m_CurrentMode = 0;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("m_ModeInfo = 0x%p, m_ModeNumbers = 0x%p\n", m_ModeInfo, m_ModeNumbers));


    UINT Height = pDispInfo->Height;
    UINT Width = pDispInfo->Width;
    UINT BitsPerPixel = BPPFromPixelFormat(pDispInfo->ColorFormat);
    if (Width == 0 || Height == 0 || BitsPerPixel != VGPU_BPP)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: Height = %d Width = %d BitsPerPixel = %d\n", __FUNCTION__, Height, Width, BitsPerPixel));
        DbgPrint(TRACE_LEVEL_INFORMATION, ("<--- %s ColorFormat = %d\n", __FUNCTION__, pDispInfo->ColorFormat));
        Width = MIN_WIDTH_SIZE;
        Height = MIN_HEIGHT_SIZE;
        BitsPerPixel = VGPU_BPP;
    }

    USHORT SuitableModeCount;
    USHORT CurrentMode;

    for (CurrentMode = 0, SuitableModeCount = 0;
         CurrentMode < ModeCount-2;
         CurrentMode++)
    {

        PGPU_DISP_MODE tmpModeInfo = &gpu_disp_modes[CurrentMode];

        DbgPrint(TRACE_LEVEL_INFORMATION, ("%s: modes[%d] x_res = %d, y_res = %d, bits = %d BitsPerPixel = %d\n", __FUNCTION__, CurrentMode, tmpModeInfo->XResolution, tmpModeInfo->YResolution, tmpModeInfo->Bits, BitsPerPixel));

        if (tmpModeInfo->XResolution >= Width &&
            tmpModeInfo->YResolution >= Height &&
            tmpModeInfo->Bits == VGPU_BPP)
        {
            m_ModeNumbers[SuitableModeCount] = SuitableModeCount;
            SetVideoModeInfo(SuitableModeCount, tmpModeInfo);
            if (tmpModeInfo->XResolution == MIN_WIDTH_SIZE &&
                tmpModeInfo->YResolution == MIN_HEIGHT_SIZE)
            {
                m_CurrentMode = SuitableModeCount;
            }
            SuitableModeCount++;
        }
    }

    if (SuitableModeCount == 0)
    {
        DbgPrint(TRACE_LEVEL_ERROR, ("No video modes supported\n"));
        Status = STATUS_UNSUCCESSFUL;
    }

    m_CustomMode = SuitableModeCount;
    for (CurrentMode = SuitableModeCount;
         CurrentMode < SuitableModeCount + 2;
         CurrentMode++)
    {
        m_ModeNumbers[CurrentMode] = CurrentMode;
        memcpy(&m_ModeInfo[CurrentMode], &m_ModeInfo[m_CurrentMode], sizeof(VIDEO_MODE_INFORMATION));
    }

    m_ModeCount = SuitableModeCount + 2;
    DbgPrint(TRACE_LEVEL_INFORMATION, ("ModeCount filtered %d\n", m_ModeCount));
    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("type %x, XRes = %d, YRes = %d, BPP = %d\n",
                                    m_ModeNumbers[idx],
                                    m_ModeInfo[idx].VisScreenWidth,
                                    m_ModeInfo[idx].VisScreenHeight,
                                    m_ModeInfo[idx].BitsPerPlane));
    }

    GetDisplayInfo();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}

BOOLEAN VioGpuAdapter::InterruptRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface, _In_  ULONG MessageNumber)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s MessageNumber = %d\n", __FUNCTION__, MessageNumber));
    UCHAR  isrstat = 0;
    BOOLEAN dpc = FALSE;
    isrstat = virtio_read_isr_status(&m_VioDev);
    UNREFERENCED_PARAMETER(MessageNumber);

    if (isrstat > 0) {
        if (isrstat == 1) {
            InterlockedOr((PLONG)&m_PendingWorks, (ISR_REASON_DISPLAY | ISR_REASON_CURSOR));
            dpc = TRUE;
        }
        else if (isrstat == 3)
        {
            InterlockedOr((PLONG)&m_PendingWorks, ISR_REASON_CHANGE);
            dpc = TRUE;
        }
        else
        {
            DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Unknown Interrupt Reason %d\n", __FUNCTION__, isrstat));
        }
    }
    if (dpc && !pDxgkInterface->DxgkCbQueueDpc(pDxgkInterface->DeviceHandle)) {
        DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s DxgkCbQueueDpc failed\n", __FUNCTION__));
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s Interrupt Reason = %d\n", __FUNCTION__, isrstat));

    return (isrstat > 0);
}

void VioGpuAdapter::ThreadWork(_In_ PVOID Context)
{
    VioGpuAdapter* pdev = reinterpret_cast<VioGpuAdapter*>(Context);
    pdev->ThreadWorkRoutine();
}

void VioGpuAdapter::ThreadWorkRoutine(void)
{
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY );

    for(;;)
    {
        KeWaitForSingleObject(&m_ConfigUpdateEvent,
                            Executive,
                            KernelMode,
                            FALSE,
                            NULL );

        if ( m_bStopWorkThread ) {
            PsTerminateSystemThread( STATUS_SUCCESS );
        }
        ConfigChanged();
     }
}

void VioGpuAdapter::ConfigChanged(void)
{
    DbgPrint(TRACE_LEVEL_FATAL, ("<--> %s\n", __FUNCTION__));
    UINT32 events_read, events_clear = 0;
    virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_read),
                      &events_read, sizeof(m_u32NumScanouts));
    if (events_read & VIRTIO_GPU_EVENT_DISPLAY) {
        GetDisplayInfo();
        events_clear |= VIRTIO_GPU_EVENT_DISPLAY;
        virtio_set_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_clear),
                          &events_clear, sizeof(m_u32NumScanouts));
        UpdateChildStatus(FALSE);
        UpdateChildStatus(TRUE);
    }
}

VOID VioGpuAdapter::DpcRoutine(_In_ PDXGKRNL_INTERFACE pDxgkInterface)
{
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UNREFERENCED_PARAMETER(pDxgkInterface);
    PGPU_VBUFFER pvbuf = NULL;
    UINT len = 0;
    ULONG reason;

    while ((reason = InterlockedExchange((PLONG)&m_PendingWorks, 0)) !=0 )
    {
        if ((reason & ISR_REASON_DISPLAY)) {
            while ((pvbuf = m_CtrlQueue.DequeueBuffer(&len)) != NULL)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s m_CtrlQueue pvbuf = %p len = %d\n", __FUNCTION__, pvbuf, len));
                PGPU_CTRL_HDR pcmd = (PGPU_CTRL_HDR)pvbuf->buf;
                PGPU_CTRL_HDR resp = (PGPU_CTRL_HDR)pvbuf->resp_buf;
                PKEVENT evnt = pvbuf->event;


                if (evnt == NULL)
                {
                    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA)
                    {
                        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s type = %x flags = %x fence_id = %x ctx_id = %x cmd_type = %x\n", __FUNCTION__, resp->type, resp->flags, resp->fence_id, resp->ctx_id, pcmd->type));
                    }

                    if (pvbuf->size > MAX_INLINE_CMD_SIZE) {
                        m_CtrlQueue.ReleaseCmdBuffer(pvbuf);
                    }
                    else {
                        m_CtrlQueue.ReleaseBuffer(pvbuf);
                    }

                    continue;
                }
                switch (pcmd->type)
                {
                    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
                    {
                        ASSERT(evnt);
                        KeSetEvent(evnt, IO_NO_INCREMENT, FALSE);
                    }
                    break;
                    default:
                        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Unknown cmd type 0x%x\n", __FUNCTION__, resp->type));
                        break;
                }
            };
        }
        if ((reason & ISR_REASON_CURSOR)) {
            while ((pvbuf = m_CursorQueue.DequeueCursor(&len)) != NULL)
            {
                DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s m_CursorQueue pvbuf = %p len = %u\n", __FUNCTION__, pvbuf, len));
                m_CursorQueue.ReleaseBuffer(pvbuf);
            };
        }
        if (reason & ISR_REASON_CHANGE) {
            DbgPrint(TRACE_LEVEL_FATAL, ("---> %s ConfigChanged\n", __FUNCTION__));
            KeSetEvent (&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID VioGpuAdapter::ResetDevice(VOID)
{
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

UINT ColorFormat(UINT format)
{
    switch (format)
    {
    case D3DDDIFMT_A8R8G8B8:
        return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    case D3DDDIFMT_X8R8G8B8:
        return VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    case D3DDDIFMT_A8B8G8R8:
        return VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
    case D3DDDIFMT_X8B8G8R8:
        return VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
    }
    DbgPrint(TRACE_LEVEL_ERROR, ("---> %s Unsupported color format\n", __FUNCTION__, format));
    return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
}

NTSTATUS VioGpuAdapter::CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_BDD_MODE* pCurrentBddMode)
{
    UINT resid, format, size;
    VioGpuObj* obj;
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s - %d: (%d x %d)\n", __FUNCTION__, m_Id,
        pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight));
    ASSERT (m_pFrameBuf == NULL);
    size = pModeInfo->ScreenStride * pModeInfo->VisScreenHeight;
    format = ColorFormat(pCurrentBddMode->DispInfo.ColorFormat);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s - (%d -> %d)\n", __FUNCTION__, pCurrentBddMode->DispInfo.ColorFormat, format));

    m_FrameSegment.Close(); // free existing memory
    if (!m_FrameSegment.Init(size, NULL))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("%s failed to allocate FB memory segment\n", __FUNCTION__));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    resid = m_Idr.GetId();
    m_CtrlQueue.CreateResource(resid, format, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);
    obj = new(NonPagedPoolNx) VioGpuObj();
    if (!obj->Init(size, &m_FrameSegment))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s Failed to init obj size = %d\n", __FUNCTION__, size));
        delete obj;
        return STATUS_UNSUCCESSFUL;
    }

    GpuObjectAttach(resid, obj);
    m_CtrlQueue.SetScanout(0/*FIXME m_Id*/, resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
    m_CtrlQueue.TransferToHost2D(resid, 0, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0, NULL);
    m_CtrlQueue.ResFlush(resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
    m_pFrameBuf = obj;
    ASSERT(pCurrentBddMode->DispInfo.PhysicAddress.QuadPart == 0L);
    pCurrentBddMode->DispInfo.PhysicAddress = obj->GetPhysicalAddress();
    pCurrentBddMode->FrameBuffer.Ptr = obj->GetVirtualAddress();
    pCurrentBddMode->Flags.FrameBufferIsActive = TRUE;
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return STATUS_SUCCESS;
}

void VioGpuAdapter::DestroyFrameBufferObj(void)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (m_pFrameBuf != NULL)
    {
        UINT id = m_pFrameBuf->GetId();
        m_CtrlQueue.InvalBacking(id);
        m_CtrlQueue.UnrefResource(id);
        delete m_pFrameBuf;
        m_pFrameBuf = NULL;
        m_Idr.PutId(id);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapter::CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_BDD_MODE* pCurrentBddMode)
{
    UINT resid, format, size;
    VioGpuObj* obj;
    PAGED_CODE();
    UNREFERENCED_PARAMETER(pCurrentBddMode);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s - %d: (%d x %d - %d) (%d + %d)\n", __FUNCTION__, m_Id,
        pSetPointerShape->Width, pSetPointerShape->Height, pSetPointerShape->Pitch, pSetPointerShape->XHot, pSetPointerShape->YHot));
    ASSERT (m_pCursorBuf == NULL);
    size = POINTER_SIZE * POINTER_SIZE * 4;
    format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;// VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;// ColorFormat(pCurrentBddMode->DispInfo.ColorFormat);
    DbgPrint(TRACE_LEVEL_INFORMATION, ("---> %s - (%x -> %x)\n", __FUNCTION__, pCurrentBddMode->DispInfo.ColorFormat, format));
    resid = m_Idr.GetId();
    m_CtrlQueue.CreateResource(resid, format, POINTER_SIZE, POINTER_SIZE);
    obj = new(NonPagedPoolNx) VioGpuObj();
    if (!obj->Init(size, &m_CursorSegment))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s Failed to init obj size = %d\n", __FUNCTION__, size));
        delete obj;
        return FALSE;
    }
    if (!GpuObjectAttach(resid, obj))
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s Failed to attach gpu object\n", __FUNCTION__));
        delete obj;
        return FALSE;
    }

    m_pCursorBuf = obj;

    RECT Rect;
    Rect.left = 0;
    Rect.top = 0;
    Rect.right =  Rect.left + pSetPointerShape->Width;
    Rect.bottom = Rect.top + pSetPointerShape->Height;

    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = m_pCursorBuf->GetVirtualAddress();
    DstBltInfo.Pitch = POINTER_SIZE * 4;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    DstBltInfo.Width = POINTER_SIZE;
    DstBltInfo.Height = POINTER_SIZE;

    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = (PVOID)pSetPointerShape->pPixels;
    SrcBltInfo.Pitch = pSetPointerShape->Pitch;
    if (pSetPointerShape->Flags.Color) {
        SrcBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
    } else if (pSetPointerShape->Flags.Monochrome) {
        SrcBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A1);
    } else {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Invalid cursor color %d\n", __FUNCTION__, pSetPointerShape->Flags.Value));
        return FALSE;
    }
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = pCurrentBddMode->Rotation;
    SrcBltInfo.Width = pSetPointerShape->Width;
    SrcBltInfo.Height = pSetPointerShape->Height;

    BltBits(&DstBltInfo,
            &SrcBltInfo,
            1,
            &Rect);

/*

    if (pSetPointerShape->Flags.Color) {
        for (UINT i = 0; i < pSetPointerShape->Height; ++i)
        {
            UINT SrcLn = pSetPointerShape->Pitch;
            UINT DstLn = POINTER_SIZE * 4;
            PVOID Dst = (PVOID)((ULONG_PTR)(m_pCursorBuf->GetVirtualAddress()) + (DstLn * i));
            PVOID Src = (PVOID)((ULONG_PTR)(pSetPointerShape->pPixels) + (i * SrcLn));
            memcpy(Dst, Src, SrcLn);
        }
    }
    else if (pSetPointerShape->Flags.Monochrome) {
        for (UINT i = 0; i < pSetPointerShape->Height; ++i)
        {
            UINT SrcLn = pSetPointerShape->Pitch;
            UINT DstLn = POINTER_SIZE * 4;
            PVOID Dst = (PVOID)((ULONG_PTR)(m_pCursorBuf->GetVirtualAddress()) + (DstLn * i));
            PVOID Src = (PVOID)((ULONG_PTR)(pSetPointerShape->pPixels) + (i * SrcLn));
            memcpy(Dst, Src, SrcLn);
        }
    }
    else {
        DbgPrint(TRACE_LEVEL_ERROR, ("<--- %s Invalid cursor color %d\n", __FUNCTION__, pSetPointerShape->Flags.Value));
        return FALSE;
    }
*/
    m_CtrlQueue.TransferToHost2D(resid, 0, pSetPointerShape->Width, pSetPointerShape->Height, 0, 0, NULL);

    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void VioGpuAdapter::DestroyCursor()
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (m_pCursorBuf != NULL)
    {
        UINT id = m_pCursorBuf->GetId();
        m_CtrlQueue.InvalBacking(id);
        m_CtrlQueue.UnrefResource(id);
        delete m_pCursorBuf;
        m_pCursorBuf = NULL;
        m_Idr.PutId(id);
    }
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapter::GpuObjectAttach(UINT res_id, VioGpuObj* obj)
{
    PAGED_CODE();
    DbgPrint(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_MEM_ENTRY ents = NULL;
    PSCATTER_GATHER_LIST sgl = NULL;
    UINT size = 0;
    sgl = obj->GetSGList();
    size = sizeof(GPU_MEM_ENTRY) * sgl->NumberOfElements;
    ents = reinterpret_cast<PGPU_MEM_ENTRY> (new (NonPagedPoolNx)  BYTE[size]);

    if (!ents)
    {
        DbgPrint(TRACE_LEVEL_FATAL, ("<--- %s cannot allocate memory %x bytes numberofentries = %d\n", __FUNCTION__, size, sgl->NumberOfElements));
        return FALSE;
    }
    RtlZeroMemory(ents, size);

    for (UINT i = 0; i < sgl->NumberOfElements; i++)
    {
        ents[i].addr = sgl->Elements[i].Address.QuadPart;
        ents[i].length = sgl->Elements[i].Length;
        ents[i].padding = 0;
    }

    m_CtrlQueue.AttachBacking(res_id, ents, sgl->NumberOfElements);
    obj->SetId(res_id);
    DbgPrint(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}
