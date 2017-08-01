#pragma once
#include "helper.h"


// For the following macros, c must be a UCHAR.
#define UPPER_6_BITS(c)   (((c) & rMaskTable[6 - 1]) >> 2)
#define UPPER_5_BITS(c)   (((c) & rMaskTable[5 - 1]) >> 3)
#define LOWER_6_BITS(c)   (((BYTE)(c)) & lMaskTable[BITS_PER_BYTE - 6])
#define LOWER_5_BITS(c)   (((BYTE)(c)) & lMaskTable[BITS_PER_BYTE - 5])

#define SHIFT_FOR_UPPER_5_IN_565   (6 + 5)
#define SHIFT_FOR_MIDDLE_6_IN_565  (5)
#define SHIFT_UPPER_5_IN_565_BACK  ((BITS_PER_BYTE * 2) + (BITS_PER_BYTE - 5))
#define SHIFT_MIDDLE_6_IN_565_BACK ((BITS_PER_BYTE * 1) + (BITS_PER_BYTE - 6))
#define SHIFT_LOWER_5_IN_565_BACK  ((BITS_PER_BYTE * 0) + (BITS_PER_BYTE - 5))


typedef struct _BLT_INFO {
    PVOID pBits;
    UINT Pitch;
    UINT BitsPerPel;
    POINT Offset; // To unrotated top-left of dirty rects
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    UINT Width; // For the unrotated image
    UINT Height; // For the unrotated image
} BLT_INFO;


// HW specific code

VOID GetPitches(
    _In_ CONST BLT_INFO* pBltInfo,
    _Out_ LONG* pPixelPitch,
    _Out_ LONG* pRowPitch);

BYTE* GetRowStart(
    _In_ CONST BLT_INFO* pBltInfo, 
    _In_ CONST RECT* pRect);

VOID CopyBitsGeneric(
    _Out_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects);

VOID CopyBits32_32(
    _Out_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects);

VOID BltBits (
    _Out_ BLT_INFO* pDst,
    _In_ CONST BLT_INFO* pSrc,
    _In_ UINT  NumRects,
    _In_reads_(NumRects) CONST RECT *pRects);


UINT BPPFromPixelFormat(D3DDDIFORMAT Format);
