#include "helper.h"
#include "baseobj.h"

VioGpuIdr::VioGpuIdr()
{
    m_uStartIndex = 0;
    m_IdBitMap.SizeOfBitMap = 0;
    m_IdBitMap.Buffer = NULL;
}

VioGpuIdr::~VioGpuIdr()
{
    Close();
}

BOOLEAN VioGpuIdr::Init(
    _In_ ULONG start)
{
    PUCHAR buf = NULL;
    m_uStartIndex = start;
    ExInitializeFastMutex(&m_IdBitMapMutex);

    VIOGPU_ASSERT(m_IdBitMap.Buffer == NULL);
    Lock();
    {
        buf = new (NonPagedPoolNx) UCHAR[PAGE_SIZE];
        RtlInitializeBitMap( &m_IdBitMap,
                             (PULONG)buf,
                             CHAR_BIT * PAGE_SIZE);
        RtlClearAllBits( &m_IdBitMap );
        RtlSetBits(&m_IdBitMap, 0, m_uStartIndex);
    }
    Unlock();
    return TRUE; 
}

void VioGpuIdr::Lock(void)
{
    ExAcquireFastMutex( &m_IdBitMapMutex );
}

void VioGpuIdr::Unlock(void)
{
    ExReleaseFastMutex( &m_IdBitMapMutex );
}

ULONG VioGpuIdr::GetId(void)
{
    ULONG id = 0;
    Lock();
    if (m_IdBitMap.Buffer != NULL)
    {
        id = RtlFindClearBitsAndSet(&m_IdBitMap, 1, 0);
    }
    Unlock();
    DbgPrint(TRACE_LEVEL_INFORMATION, ("[%s] id = %d\n", __FUNCTION__, id));
    ASSERT(id < USHORT_MAX);
    return id;
}

void VioGpuIdr::PutId(ULONG id)
{
    ASSERT(id >= m_uStartIndex);
    ASSERT(id <= (CHAR_BIT * PAGE_SIZE));
    DbgPrint(TRACE_LEVEL_INFORMATION, ("[%s] bit %d\n", __FUNCTION__, id));
    Lock();
    if (m_IdBitMap.Buffer != NULL)
    {
        if (!RtlAreBitsSet (&m_IdBitMap, id, 1))
        {
            DbgPrint(TRACE_LEVEL_FATAL, ("[%s] bit %d is not set\n", __FUNCTION__, id - m_uStartIndex));
        }
        RtlClearBits(&m_IdBitMap, id, 1);
    }
    Unlock();
}

void VioGpuIdr::Close(void)
{
    Lock();
    if (m_IdBitMap.Buffer != NULL)
    {
        delete [] m_IdBitMap.Buffer;
        m_IdBitMap.Buffer = NULL;
    }
    m_uStartIndex = 0;
    Unlock();
}
