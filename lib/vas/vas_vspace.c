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

#ifdef VAS_CONFIG_MULTI_DOMAIN_META

#define VAS_VSPACE_MAX_REGIONS 256

#define VAS_VSPACE_BLOCK_SIZE \
                (sizeof(struct vregion) + sizeof(struct memobj_one_frame))

struct vspace_mmu_aware *vas_vspace_current_vm;

static errval_t vas_vspace_mmu_aware_init(struct vas *vas, size_t size)
{
    errval_t err;

    struct vspace_mmu_aware *state = &vas->vspace_vm;

    state->size = size;
    state->consumed = 0;
    state->alignment = 0;
    state->offset = 0;
    state->mapoffset = 0;

    size = ROUND_UP(size, BASE_PAGE_SIZE);
    err = memobj_create_anon(&state->memobj, size, 0);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_MEMOBJ_CREATE_ANON);
    }

    err = vregion_map_aligned(&state->vregion, &vas->vspace_state.vspace,
                              &state->memobj.m, 0, size,
                              VREGION_FLAGS_READ_WRITE, state->alignment);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VREGION_MAP);
    }

    return SYS_ERR_OK;
}

static errval_t vas_vspace_refill_slabs(struct slab_allocator *slabs)
{
    struct capref frame;
    size_t size;
    void *buf;
    errval_t err;

    err = slot_alloc(&frame);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    err = vspace_mmu_aware_map(vas_vspace_current_vm, frame, VAS_VSPACE_BLOCK_SIZE,
                               &buf, &size);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_MMU_AWARE_MAP);
    }

    slab_grow(slabs, buf, size);

    return SYS_ERR_OK;
}

#endif /* VAS_CONFIG_MULTI_DOMAIN_META */

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
    struct pmap *pmap = (struct pmap *)&vas->vspace_state.pmap;

    /* create a new page cn */
    VAS_DEBUG_VSPACE("creating new pagecn cap\n");
    err = cnode_create(&vas->pagecn_cap, &vas->pagecn, PAGE_CNODE_SLOTS, NULL);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_CREATE_PAGECN);
    }

    /* create a new vroot */

    VAS_DEBUG_VSPACE("initializing slot allocator \n");
    size_t bufsize = SINGLE_SLOT_ALLOC_BUFLEN(PAGE_CNODE_SLOTS);
    void *buf = calloc(1, bufsize);
    if (buf == NULL) {
        err =  LIB_ERR_MALLOC_FAIL;
        goto out_err;
    }

    err = single_slot_alloc_init_raw(&vas->pagecn_slot_alloc, vas->pagecn_cap,
                                     vas->pagecn, PAGE_CNODE_SLOTS, buf, bufsize);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_SINGLE_SLOT_ALLOC_INIT_RAW);
        goto out_err;
    }

    VAS_DEBUG_VSPACE("creating the root page table\n");

    // Create root of pagetable
    err = vas->pagecn_slot_alloc.a.alloc(&vas->pagecn_slot_alloc.a, &vas->vroot);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_SLOT_ALLOC);
        goto out_err;
    }

    assert(vas->vroot.slot == 0);
    err = vas_vspace_create_vroot(vas->vroot);
    if (err_is_fail(err)) {
        goto out_err;
    }
    VAS_DEBUG_VSPACE("initializing pmap\n");

    err = pmap_init(pmap, vspace, vas->vroot, &vas->pagecn_slot_alloc.a);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_PMAP_INIT);
        goto out_err;
    }

    VAS_DEBUG_VSPACE("initializing vspace\n");
    err = vspace_init(vspace, pmap);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_VSPACE_LAYOUT_INIT);
        goto out_err;
    }

    VAS_DEBUG_VSPACE("setting reserved memory region for pmap\n");

    err = pmap_region_init(pmap, 0, VAS_VSPACE_MIN_MAPPABLE,
                           VAS_VSPACE_META_RESERVED_SIZE);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_PMAP_CURRENT_INIT);
        goto out_err;
    }

#ifdef VAS_CONFIG_MULTI_DOMAIN_META
    VAS_DEBUG_VSPACE("initializing meta-storage\n");

    err = vas_vspace_mmu_aware_init(vas, VAS_VSPACE_MAX_REGIONS * VAS_VSPACE_BLOCK_SIZE);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "vspace_mmu_aware_init for thread region failed\n");
    }
    slab_init(&vas->vspace_slabs, VAS_VSPACE_BLOCK_SIZE, vas_vspace_refill_slabs);
#endif
    /*
     * XXX:
     *  - maybe reserve a region of memory for the vregion/memobjs
     *  - create CNODE for the backing frames?
     */


    VAS_DEBUG_VSPACE("vspace initialized successfully\n");

    return SYS_ERR_OK;

    out_err :
    cap_destroy(vas->pagecn_cap);
    return err;
}


/**
 * \brief inherits the text and data segment regions from the domain
 *
 * \param vas   the VAS to set the segments
 *
 * \returns SYS_ERR_OK on success
 *          errval or error
 */
errval_t vas_vspace_inherit_segments(struct vas *vas)
{
    struct capref vroot = {
        .cnode = cnode_page,
        .slot = 0
    };

    return vnode_inherit(vas->vroot, vroot, 0, 1);
}

/**
 * \brief inherits the heap segment regions from the domain
 *
 * \param vas   the VAS to set the segments
 *
 * \returns SYS_ERR_OK on success
 *          errval or error
 */
errval_t vas_vspace_inherit_heap(struct vas *vas)
{
    struct capref vroot = {
        .cnode = cnode_page,
        .slot = 0
    };

    return vnode_inherit(vas->vroot, vroot, 1, 32);
}

/**
 * \brief inherits the vas segment regions from the VAS
 *
 * \param vas   the VAS to set the segments
 * \param vroot
 *
 * \returns SYS_ERR_OK on success
 *          errval or error
 */
errval_t vas_vspace_inherit_regions(struct vas *vas, struct capref vroot)
{
    return vnode_inherit(vroot, vas->vroot, VAS_VSPACE_PML4_SLOT_MIN,
                         VAS_VSPACE_PML4_SLOT_MAX);
}

errval_t vas_vspace_map_one_frame(struct vas *vas, void **retaddr,
                                  struct capref frame, size_t size)
{
    VAS_DEBUG_VSPACE("mapping new frame in vas %s\n", vas->name);

    errval_t err;
    struct vregion *vregion = NULL;
    struct memobj_one_frame *memobj = NULL;

#ifdef VAS_CONFIG_MULTI_DOMAIN_META

    void *space = slab_alloc(&vas->vspace_slabs);
    if (!space) {
        return LIB_ERR_MALLOC_FAIL;
    }
#else
    void *space = calloc(1, sizeof(struct vregion) + sizeof(struct memobj_one_frame));
#endif

    vregion = space;
    memobj = space + sizeof(struct vregion);


    err = memobj_create_one_frame(memobj, size, 0);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_CREATE_ANON);
        goto error;
    }
    err = memobj->m.f.fill(&memobj->m, 0, frame, size);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_FILL);
        goto error;
    }
    err = vregion_map(vregion, &vas->vspace_state.vspace, &memobj->m, 0, size,
                      VREGION_FLAGS_READ_WRITE);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_VSPACE_MAP);
        goto error;
    }
    err = memobj->m.f.pagefault(&memobj->m, vregion, 0, 0);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_PAGEFAULT_HANDLER);
        goto error;
    }

    *retaddr = (void *)vregion_get_base_addr(vregion);

    VAS_DEBUG_VSPACE("mapped frame in vas %s @ %016lx\n", vas->name,
                     vregion_get_base_addr(vregion));

    return SYS_ERR_OK;

    error: // XXX: proper cleanup
    if (vregion) {
        free(vregion);
    }
    if (memobj) {
        free(memobj);
    }
    return err;
}


