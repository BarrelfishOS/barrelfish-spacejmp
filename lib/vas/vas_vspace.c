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


#define VAS_VSPACE_TAG_START 0x100

static uint16_t vas_vspace_tag_alloc = VAS_VSPACE_TAG_START;

#if 0
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

    if (vas_vspace_tag_alloc < 0x1000) {
        vas->tag = vas_vspace_tag_alloc++;
    } else {
        vas->tag = 0;
        debug_printf("warning: out of tags for this vas!\n");
    }


    VAS_DEBUG_VSPACE("vspace initialized successfully\n");

    return SYS_ERR_OK;

    out_err :
    cap_destroy(vas->pagecn_cap);
    return err;
}
#endif

errval_t vas_vspace_map_one_frame(struct vas *vas, void **retaddr,
                                  struct capref frame, size_t size,
                                  vas_flags_t flags)
{
    VAS_DEBUG_VSPACE("mapping new frame in vas %s\n", vas->name);

    errval_t err;
    struct vregion *vregion = NULL;
    struct memobj *memobj = NULL;

    flags &= VREGION_FLAGS_MASK;

    size_t alignment;
    if (flags & VREGION_FLAGS_HUGE) {
        size = ROUND_UP(size, HUGE_PAGE_SIZE);
        alignment = HUGE_PAGE_SIZE;
    } else if (flags & VREGION_FLAGS_LARGE) {
        size = ROUND_UP(size, LARGE_PAGE_SIZE);
        alignment = LARGE_PAGE_SIZE;
    } else {
        size = ROUND_UP(size, BASE_PAGE_SIZE);
        alignment = BASE_PAGE_SIZE;
    }

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

    err = memobj_create_one_frame((struct memobj_one_frame *)memobj, size, 0);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_CREATE_ANON);
        goto error;
    }
    err = memobj->f.fill(memobj, 0, frame, size);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_FILL);
        goto error;
    }

    err = vregion_map_aligned(vregion, &vas->vspace_state.vspace, memobj, 0, size,
                              flags, alignment);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_VSPACE_MAP);
        goto error;
    }
    err = memobj->f.pagefault(memobj, vregion, 0, 0);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_PAGEFAULT_HANDLER);
        goto error;
    }

    *retaddr = (void*)vspace_genvaddr_to_lvaddr(vregion_get_base_addr(vregion));

    VAS_DEBUG_VSPACE("mapped frame in vas %s @ %016lx\n", vas->name,
                     vregion_get_base_addr(vregion));

    return SYS_ERR_OK;

    error: // XXX: proper cleanup
    if (space) {
        free(space);
    }

    return err;
}

errval_t vas_vspace_map_one_frame_fixed(struct vas *vas, lvaddr_t addr,
                                  struct capref frame, size_t size,
                                  vas_flags_t flags)
{
    VAS_DEBUG_VSPACE("mapping new frame in vas %s\n", vas->name);

    errval_t err;
    struct vregion *vregion = NULL;
    struct memobj *memobj = NULL;

    flags &= VREGION_FLAGS_MASK;

    if (flags & VREGION_FLAGS_HUGE) {
        size = ROUND_UP(size, HUGE_PAGE_SIZE);
        if (addr & ~(HUGE_PAGE_SIZE)) {
            return LIB_ERR_VREGION_BAD_ALIGNMENT;
        }
    } else if (flags & VREGION_FLAGS_LARGE) {
        size = ROUND_UP(size, LARGE_PAGE_SIZE);
        if (addr & ~(LARGE_PAGE_SIZE)) {
            return LIB_ERR_VREGION_BAD_ALIGNMENT;
        }
    } else {
        size = ROUND_UP(size, BASE_PAGE_SIZE);
        if (addr & ~(BASE_PAGE_SIZE)) {
            return LIB_ERR_VREGION_BAD_ALIGNMENT;
        }
    }

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

    err = memobj_create_one_frame((struct memobj_one_frame *)memobj, size, 0);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_CREATE_ANON);
        goto error;
    }
    err = memobj->f.fill(memobj, 0, frame, size);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_FILL);
        goto error;
    }

    err = vregion_map_fixed(vregion, &vas->vspace_state.vspace, memobj, 0, size,
                            addr, flags);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_VSPACE_MAP);
        goto error;
    }
    err = memobj->f.pagefault(memobj, vregion, 0, 0);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_MEMOBJ_PAGEFAULT_HANDLER);
        goto error;
    }

    VAS_DEBUG_VSPACE("mapped frame in vas %s @ %016lx\n", vas->name,
                     vregion_get_base_addr(vregion));

    return SYS_ERR_OK;

    error: // XXX: proper cleanup
    if (space) {
        free(space);
    }

    return err;
}




#define VAS_VSPACE_CNODE_SLOTS_BITS 9
#define VAS_VSPACE_CNODE_SLOTS (1 << VAS_VSPACE_CNODE_SLOTS_BITS)

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

    /* create a new page cn */
    VAS_DEBUG_VSPACE("creating new pagecn cap\n");
    err = cnode_create(&vas->pagecn_cap, &vas->pagecn, VAS_VSPACE_CNODE_SLOTS, NULL);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_CREATE_PAGECN);
    }

    /* create a new vroot */
    VAS_DEBUG_VSPACE("creating vroot\n");
    vas->vroot = (struct capref){.cnode = vas->pagecn, .slot = 0};
    err = vas_vspace_create_vroot(vas->vroot);
    if (err_is_fail(err)) {
        goto out_err;
    }

    size_t num_bits = vnode_objbits(ObjType_VNode_x86_64_pdpt) + VAS_VSPACE_CNODE_SLOTS_BITS - 1;

    VAS_DEBUG_VSPACE("allocating ram for pdir, size = %lu\n", (1UL << num_bits));
    /* create pdpt */
    struct capref ram;
    err = ram_alloc(&ram, num_bits);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_RAM_ALLOC);
        goto out_err;
    }

    VAS_DEBUG_VSPACE("retyping into ObjType_VNode_x86_64_pdpt\n");
    struct capref pdpt = (struct capref){.cnode = vas->pagecn, .slot = VAS_VSPACE_PML4_SLOT_MIN};

    err = cap_retype(pdpt, ram, ObjType_VNode_x86_64_pdpt, num_bits);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_CAP_RETYPE);
        goto out_err;
    }

    err = cap_destroy(ram);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_CAP_DESTROY);
        goto out_err;
    }

    VAS_DEBUG_VSPACE("mapping pdpt into vroot\n");
    pdpt.slot = VAS_VSPACE_PML4_SLOT_MIN;
    err = vnode_map(vas->vroot, pdpt, VAS_VSPACE_PML4_SLOT_MIN, PTABLE_ACCESS_DEFAULT, 0, VAS_VSPACE_PML4_SLOTS);

/*
    for (int i = VAS_VSPACE_PML4_SLOT_MIN; i <  VAS_VSPACE_PML4_SLOT_MAX; ++i) {
        pdpt.slot = i;

        VAS_DEBUG_VSPACE("slot %i\n", i);

        err = vnode_map(vas->vroot, pdpt, i, PTABLE_ACCESS_DEFAULT, 0, 1);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "vnode map failed");
            goto out_err;
        }
    }
*/

    if (vas_vspace_tag_alloc < 0x1000) {
        vas->tag = vas_vspace_tag_alloc++;
    } else {
        vas->tag = 0;
        debug_printf("warning: out of tags for this vas!\n");
    }


    VAS_DEBUG_VSPACE("vspace initialized successfully\n");

    return SYS_ERR_OK;

    out_err :
    cap_destroy(vas->pagecn_cap);
    return err;
}

errval_t vas_vspace_map_segment(struct vas *vas, struct vas_segment *seg)
{

}

errval_t vas_vspace_unmap_segment(struct vas *vas, struct vas_segment *seg)
{

}
