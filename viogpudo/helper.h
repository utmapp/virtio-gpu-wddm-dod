#pragma once

extern "C" {

#define __CPLUSPLUS

    // Standard C-runtime headers
    #include <stddef.h>
    #include <string.h>
    #include <stdarg.h>
    #include <stdio.h>
    #include <stdlib.h>

    #include <initguid.h>

    // NTOS headers
    #include <ntddk.h>

    #ifndef FAR
    #define FAR
    #endif

    // Windows headers
    #include <windef.h>
    #include <winerror.h>

    // Windows GDI headers
    #include <wingdi.h>

    // Windows DDI headers
    #include <winddi.h>
    #include <ntddvdeo.h>

    #include <d3dkmddi.h>
    #include <d3dkmthk.h>

    #include <ntstrsafe.h>
    #include <ntintsafe.h>

    #include <dispmprt.h>

    #include "osdep.h"
    #include "virtio_pci.h"
    #include "virtio.h"
    #include "virtio_ring.h"
    #include "kdebugprint.h"
    #include "viogpu_pci.h"
    #include "viogpu.h"
    #include "viogpu_queue.h"
    #include "viogpu_idr.h"
}

#define MIN(x, y) (((x) <= (y)) ? (x) : (y))
#define MAX(x, y) (((x) >= (y)) ? (x) : (y))
#define ALIGN(a, b) (((a) + ((b) - 1)) & ~((b) - 1))

#define MAX_CHILDREN               1
#define MAX_VIEWS                  1
#define BITS_PER_BYTE              8

#define POINTER_SIZE               64
#define MIN_WIDTH_SIZE             1024
#define MIN_HEIGHT_SIZE            768
#define VGPU_BPP                   32
#define VGA_BPP                    32

#define VIOGPUTAG                  'OIVg'

extern VirtIOSystemOps VioGpuSystemOps;

#define DBG 1

#ifdef DBG
#define PRINT_DEBUG 1
//#define COM_DEBUG 1

extern int nDebugLevel;
void DebugPrintFuncSerial(const char *format, ...);
void DebugPrintFuncKdPrint(const char *format, ...);

#define DbgPrint(level, line) \
    if (level > nDebugLevel) {} \
    else VirtioDebugPrintProc line
#else
#define DbgPrint(level, line) 
#endif


#define VioGpuDbgBreak()\
    if (KD_DEBUGGER_ENABLED && !KD_DEBUGGER_NOT_PRESENT) DbgBreakPoint(); \


#ifndef TRACE_LEVEL_INFORMATION
#define TRACE_LEVEL_NONE        0   // Tracing is not on
#define TRACE_LEVEL_FATAL       1   // Abnormal exit or termination
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Includes non-error cases(e.g.,Entry-Exit)
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps
#define TRACE_LEVEL_RESERVED6   6
#define TRACE_LEVEL_RESERVED7   7
#define TRACE_LEVEL_RESERVED8   8
#define TRACE_LEVEL_RESERVED9   9
#endif // TRACE_LEVEL_INFORMATION

#define VIOGPU_LOG_ASSERTION0(Msg) NT_ASSERT(FALSE)
#define VIOGPU_LOG_ASSERTION1(Msg,Param1) NT_ASSERT(FALSE)
#define VIOGPU_LOG_ASSERTION2(Msg,Param1,Param2) NT_ASSERT(FALSE)
#define VIOGPU_LOG_ASSERTION3(Msg,Param1,Param2,Param3) NT_ASSERT(FALSE)
#define VIOGPU_LOG_ASSERTION4(Msg,Param1,Param2,Param3,Param4) NT_ASSERT(FALSE)
#define VIOGPU_LOG_ASSERTION5(Msg,Param1,Param2,Param3,Param4,Param5) NT_ASSERT(FALSE)
#define VIOGPU_ASSERT(exp) {if (!(exp)) {VIOGPU_LOG_ASSERTION0(#exp);}}


#if DBG
#define VIOGPU_ASSERT_CHK(exp) VIOGPU_ASSERT(exp)
#else
#define VIOGPU_ASSERT_CHK(exp) {}
#endif
