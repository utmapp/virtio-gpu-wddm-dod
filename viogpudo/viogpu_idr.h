#pragma once
#include "helper.h"

class VioGpuIdr
{
public:
    VioGpuIdr();
    ~VioGpuIdr();
    BOOLEAN Init(_In_ ULONG start);
    ULONG GetId(void);
    void PutId(_In_ ULONG id);
private:
    void Close(void);
    void Lock(void);
    void Unlock(void);
private:
    ULONG m_uStartIndex;
    RTL_BITMAP m_IdBitMap;
    FAST_MUTEX m_IdBitMapMutex;
};
