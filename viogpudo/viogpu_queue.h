#pragma once
#include "helper.h"

#pragma pack(1)
typedef struct virtio_gpu_config {
    u32 events_read;
    u32 events_clear;
    u32 num_scanouts;
    u32 num_capsets;
}GPU_CONFIG, *PGPU_CONFIG;
#pragma pack()

//#pragma pack(1)
typedef struct virtio_gpu_vbuffer {
    char *buf;
    int size;

    void *data_buf;
    u32 data_size;

    char *resp_buf;
    int resp_size;
    PKEVENT event;
    LIST_ENTRY list_entry;
}GPU_VBUFFER, *PGPU_VBUFFER;
//#pragma pack()

#define MAX_INLINE_CMD_SIZE   96
#define MAX_INLINE_RESP_SIZE  24
#define VBUFFER_SIZE          (sizeof(struct virtio_gpu_vbuffer) \
                               + MAX_INLINE_CMD_SIZE \
                               + MAX_INLINE_RESP_SIZE)

class VioGpuBuf
{
public:
    VioGpuBuf();
    ~VioGpuBuf();
    PGPU_VBUFFER GetBuf(
        _In_ int size,
        _In_ int resp_size,
        _In_ void *resp_buf);
    void FreeBuf(
        _In_ PGPU_VBUFFER pbuf);
    BOOLEAN Init(_In_ UINT cnt);
private:
    void Close(void);
private:
    LIST_ENTRY   m_FreeBufs;
    KSPIN_LOCK   m_SpinLock;
    UINT         m_uCount;
};

class VioGpuMemSegment
{
public:
    VioGpuMemSegment(void);
    ~VioGpuMemSegment(void);
    SIZE_T GetSize(void) { return m_Size; }
    PVOID GetVirtualAddress(void) { return m_pVAddr; }
    PHYSICAL_ADDRESS GetPhysicalAddress(void);
    PSCATTER_GATHER_LIST GetSGList(void) { return m_pSGList; }
    BOOLEAN Init(_In_ UINT size, _In_ PPHYSICAL_ADDRESS pPAddr);
    BOOLEAN IsSystemMemory(void) { return m_bSystemMemory; }
    void Close(void);
private:
    BOOLEAN m_bSystemMemory;
    PSCATTER_GATHER_LIST m_pSGList;
    PVOID m_pVAddr;
    PMDL    m_pMdl;
    SIZE_T m_Size;
};

class VioGpuObj
{
public:
    VioGpuObj(void);
    ~VioGpuObj(void);
    void SetId(_In_ UINT id) { m_uiHwRes = id; }
    UINT GetId(void) { return m_uiHwRes; }
    BOOLEAN Init(_In_ UINT size, VioGpuMemSegment *pSegment);
    SIZE_T GetSize(void) { return m_Size; }
    PSCATTER_GATHER_LIST GetSGList(void) { return m_pSegment ? m_pSegment->GetSGList() : NULL; }
    PHYSICAL_ADDRESS GetPhysicalAddress(void) { PHYSICAL_ADDRESS pa = { 0 }; return m_pSegment ? m_pSegment->GetPhysicalAddress() : pa; }
    PVOID GetVirtualAddress(void) { return m_pSegment ? m_pSegment->GetVirtualAddress() : NULL; }
private:
    UINT m_uiHwRes;
    SIZE_T m_Size;
    VioGpuMemSegment *m_pSegment;
};

class VioGpuQueue
{
public:
    VioGpuQueue();
    ~VioGpuQueue();
    BOOLEAN Init(
        _In_ VirtIODevice* pVIODevice, 
        _In_ struct virtqueue* pVirtQueue,
        _In_ UINT index) {
                m_pVIODevice = pVIODevice; 
                m_pVirtQueue = pVirtQueue; 
                m_Index = index; 
                return ((m_pVIODevice != NULL) && (m_pVirtQueue != NULL)); }
    void Close(void);
    int AddBuf(_In_ struct VirtIOBufferDescriptor sg[],
        _In_ UINT out_num,
        _In_ UINT in_num,
        _In_ void* data,
        _In_ void* va_indirect,
        _In_ ULONGLONG phys_indirect)
    { return virtqueue_add_buf(m_pVirtQueue, sg, out_num, in_num, data,
          va_indirect, phys_indirect); }
    UINT NumFree(void) { return m_pVirtQueue->num_free; }
    void* GetBuf(_Out_ UINT* len)
    { return virtqueue_get_buf(m_pVirtQueue, len); }
    void Kick()
    { virtqueue_kick_always(m_pVirtQueue); }
    BOOLEAN EnableInterrupt(void) { return (virtqueue_enable_cb(m_pVirtQueue) ? TRUE : FALSE); }
    VOID DisableInterrupt(void) { virtqueue_disable_cb(m_pVirtQueue); }
    BOOLEAN InterruptEnabled(void) { return virtqueue_is_interrupt_enabled(m_pVirtQueue); }
    UINT QueryAllocation();
    void SetGpuBuf(_In_ VioGpuBuf* pbuf) { m_pBuf = pbuf;}
    void ReleaseBuffer(PGPU_VBUFFER buf);
protected:
    void Lock(KIRQL* Irql);
    void Unlock(KIRQL Irql);
private:
    struct virtqueue* m_pVirtQueue;
    VirtIODevice* m_pVIODevice;
    UINT m_Index;
    KSPIN_LOCK m_SpinLock;
protected:
    VioGpuBuf* m_pBuf;
};

class CtrlQueue : public VioGpuQueue
{
public:
    PVOID AllocCmd(PGPU_VBUFFER* buf, int sz);
    PVOID AllocCmdResp(PGPU_VBUFFER* buf, int cmd_sz, PVOID resp_buf, int resp_sz);

    UINT QueueBuffer(PGPU_VBUFFER buf);
    PGPU_VBUFFER DequeueBuffer(_Out_ UINT* len);

    void CreateResource(UINT res_id, UINT format, UINT width, UINT height);
    void UnrefResource(UINT id);
    void InvalBacking(UINT id);
    void SetScanout(UINT scan_id, UINT res_id, UINT width, UINT height, UINT x, UINT y);
    void ResFlush(UINT res_id, UINT width, UINT height, UINT x, UINT y);
    void TransferToHost2D(UINT res_id, ULONG offset, UINT width, UINT height, UINT x, UINT y, PUINT fence_id);
    void AttachBacking(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents);
    void SubmitCmd(VOID *data, UINT32 size);
    BOOLEAN GetDisplayInfo(PGPU_VBUFFER buf, UINT id, PULONG xres, PULONG yres);
    BOOLEAN AskDisplayInfo(PGPU_VBUFFER* buf);
};

class CrsrQueue : public VioGpuQueue
{
public:
    PVOID AllocCursor(PGPU_VBUFFER* buf);
    UINT QueueCursor(PGPU_VBUFFER buf);
    PGPU_VBUFFER DequeueCursor(_Out_ UINT* len);
};

