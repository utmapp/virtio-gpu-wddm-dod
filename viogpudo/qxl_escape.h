/*
 * Copyright 2013-2021 Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef _H_QXL_ESCAPE
#define _H_QXL_ESCAPE

#include <basetsd.h>

enum {
    QXL_ESCAPE_SET_CUSTOM_DISPLAY = 0x10001,
    QXL_ESCAPE_MONITOR_CONFIG
};

#pragma pack(1)
typedef struct QXLEscapeSetCustomDisplay {
    UINT32 xres;
    UINT32 yres;
    UINT32 bpp;
} QXLEscapeSetCustomDisplay;

#if 0
/* A QXLHead is a single monitor output backed by a QXLSurface.
 * x and y offsets are unsigned since they are used in relation to
 * the given surface, not the same as the x, y coordinates in the guest
 * screen reference frame. */
typedef struct QXLHead {
    UINT32 id;
    UINT32 surface_id;
    UINT32 width;
    UINT32 height;
    UINT32 x;
    UINT32 y;
    UINT32 flags;
} QXLHead;

typedef struct QXLMonitorsConfig {
    UINT16 count;
    UINT16 max_allowed; /* If it is 0 no fixed limit is given by the driver */
    QXLHead heads[0];
} QXLMonitorsConfig;
#endif
#pragma pack()

#endif /* _H_QXL_ESCAPE */
