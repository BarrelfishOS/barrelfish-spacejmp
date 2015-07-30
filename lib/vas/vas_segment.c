/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <barrelfish/barrelfish.h>
#include <barrelfish/monitor_client.h>

#include <vas_internal.h>
#include <vas/vas_segment.h>
#include <vas_client.h>

/*
 * =============================================================================
 * Type definitions
 * =============================================================================
 */



/*
 * =============================================================================
 * Internal functions
 * =============================================================================
 */

static inline struct vas_seg *vas_seg_get_pointer(vas_seg_handle_t sh)
{
    return (struct vas_seg*)sh;
}

static inline vas_seg_handle_t vas_seg_get_handle(struct vas_seg *seg)
{
    return (vas_seg_handle_t)seg;
}

static errval_t vas_seg_check(lvaddr_t vaddr, size_t length)
{
    if (length > VAS_SEG_MAX_LEN) {

    }

    if (vaddr < 0x10) {

    }
    return SYS_ERR_OK;
}

/*
 * =============================================================================
 * Public interface
 * =============================================================================
 */

errval_t vas_seg_alloc(const char *name, vas_seg_type_t type, size_t length,
                       lvaddr_t vaddr,  vas_flags_t flags,
                       vas_seg_id_t *ret_seg)
{
    errval_t err;
    struct capref frame;

    err = vas_seg_check(vaddr, length);
    if (err_is_fail(err)) {
        return err;
    }

    err = frame_alloc(&frame, length, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    err = vas_seg_create(name, type, length, vaddr, frame, flags, ret_seg);
    if (err_is_fail(err)) {
        cap_destroy(frame);
    }
    return err;
}


errval_t vas_seg_create(const char *name, vas_seg_type_t type, size_t length,
                       lvaddr_t vaddr, struct capref frame, vas_flags_t flags,
                       vas_seg_id_t *ret_seg)
{
    errval_t err;

    err = vas_seg_check(vaddr, length);
    if (err_is_fail(err)) {
        return err;
    }

    struct frame_identity fi;
    err = invoke_frame_identify(frame, &fi);
    if (err_is_fail(err)) {
        return err;
    }

    if ((1UL << fi.bits) < length) {
        return -1;
    }

    struct vas_seg *seg = calloc(1, sizeof(struct vas_seg));
    if (seg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    seg->vaddr = vaddr;
    seg->length = length;
    seg->frame = frame;
    seg->type = type;
    seg->flags = flags;
    strncpy(seg->name, name, sizeof(seg->name));

    err = vas_client_seg_create(seg);
    if (err_is_fail(err)) {
        free(seg);
        return err;
    }

    *ret_seg = vas_seg_get_handle(seg);

    return SYS_ERR_OK;
}

errval_t vas_seg_free(vas_seg_handle_t sh)
{
 //   struct vas_seg *seg = vas_seg_get_pointer(sh);

    return VAS_ERR_NOT_SUPPORTED;
}

errval_t vas_seg_lookup(const char *name, vas_seg_handle_t *ret_seg)
{
    return VAS_ERR_NOT_SUPPORTED;
}

errval_t vas_seg_attach(vas_handle_t vh, vas_seg_handle_t sh)
{
    struct vas_seg *seg = vas_seg_get_pointer(sh);
    struct vas *vas = vas_get_vas_pointer(vh);

    return vas_client_seg_attach(vas->id, seg->id, seg->flags);
}

errval_t vas_seg_detach(vas_seg_handle_t ret_seg)
{
    return VAS_ERR_NOT_SUPPORTED;
}

size_t vas_seg_get_size(vas_seg_handle_t sh)
{
    return vas_seg_get_pointer(sh)->length;
}

lvaddr_t vas_seg_get_vaddr(vas_seg_handle_t sh)
{
    return vas_seg_get_pointer(sh)->vaddr;
}

vas_seg_id_t vas_seg_get_id(vas_seg_handle_t sh)
{
    return vas_seg_get_pointer(sh)->id;
}
