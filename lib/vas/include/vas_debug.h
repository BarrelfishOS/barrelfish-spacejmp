/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __VAS_DEBUG_H_
#define __VAS_DEBUG_H_ 1

#define VAS_DEBUG_GLOBAL_ENABLE 1
#define VAS_DEBUG_VAS_ENABLE 1
#define VAS_DEBUG_SEGMENT_ENABLE 1
#define VAS_DEBUG_VSPACE_ENABLE 1
#define VAS_DEBUG_CLIENT_ENABLE 1

#ifdef NDEBUG
#undef VAS_DEBUG_GLOBAL_ENABLE
#define VAS_DEBUG_GLOBAL_ENABLE 0
#endif

#if VAS_DEBUG_GLOBAL_ENABLE
#define VAS_DEBUG_PRINTF(x...) debug_printf("[vas] " x);
#else
#define VAS_DEBUG_PRINTF(x...)
#endif

#if VAS_DEBUG_LIBVAS_ENABLE
#define VAS_DEBUG_LIBVAS(x...) VAS_DEBUG_PRINTF("[libvas] "x)
#else
#define VAS_DEBUG_LIBVAS(x...)
#endif

#if VAS_DEBUG_SEGMENT_ENABLE
#define VAS_DEBUG_SEG(x...) VAS_DEBUG_PRINTF("[segment] "x)
#else
#define VAS_DEBUG_SEGS(x...)
#endif

#if VAS_DEBUG_VSPACE_ENABLE
#define VAS_DEBUG_VSPACE(x...) VAS_DEBUG_PRINTF("[vspace] "x)
#else
#define VAS_DEBUG_VSPACE(x...)
#endif

#if VAS_DEBUG_CLIENT_ENABLE
#define VAS_DEBUG_CLIENT(x...) VAS_DEBUG_PRINTF("[client] "x)
#else
#define VAS_DEBUG_CLIENT(x...)
#endif

#endif /* __VAS_DEBUG_H_ */
