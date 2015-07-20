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

#include <vas_internal.h>
#include <vas_vspace.h>

#include <barrelfish/vspace_layout.h>
#include <barrelfish/pmap_arch.h>
#include <barrelfish_kpi/init.h>



/**
 * \brief initializes the VSPACE structure of the VAS
 *
 * \param vas   the VAS to initialize
 *
 * \returns SYS_ERR_OK on sucecss
 *          errval on failure
 */
errval_t vas_vspace_init(struct vas *vas)
{
    errval_t err;

    VAS_DEBUG_VSPACE("initializing new vspace for vas @ %p\n", vas);


    struct vspace *vspace = &vas->vspace_state.vspace;

    vspace->head = NULL;

    VAS_DEBUG_VSPACE("initializing vspace layout\n");
    err = vspace_layout_init(&vspace->layout);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_LAYOUT_INIT);
    }

    /* create a new page cn */
    VAS_DEBUG_VSPACE("creating new pagecn cap\n");
    err = cnode_create(&vas->pagecn_cap, &vas->pagecn, PAGE_CNODE_SLOTS, NULL);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_CREATE_PAGECN);
    }

    VAS_DEBUG_VSPACE("initializing pmap\n");

    /* initialize the pmap */
    struct capref cap = {
        .cnode = vas->pagecn,
        .slot  = 0,
    };

    /* XXX: Maybe use a optional slot allocator? */
    err = pmap_init((struct pmap *)&vas->vspace_state.pmap, vspace, cap, NULL);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_PMAP_INIT);
    }

    VAS_DEBUG_VSPACE("setting reserved memory region for pmap\n");

    err = pmap_region_init((struct pmap *)&vas->vspace_state.pmap, 0,
                           VAS_VSPACE_MIN_MAPPABLE, VAS_VSPACE_META_RESERVED_SIZE);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_PMAP_CURRENT_INIT);
    }

    VAS_DEBUG_VSPACE("vspace initialized successfully\n");

    return SYS_ERR_OK;
}


