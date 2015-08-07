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
#if 0
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
#endif
    return SYS_ERR_OK;
}

errval_t vas_vspace_map_one_frame_fixed(struct vas *vas, lvaddr_t addr,
                                  struct capref frame, size_t size,
                                  vas_flags_t flags)
{
#if 0
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
#endif
    return SYS_ERR_OK;
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

static errval_t vas_vspace_add_segment(struct vas *vas, struct vas_vregion *seg)
{
    if (vas->segs == NULL) {
        vas->segs = seg;
        seg->next = NULL;
    }
    struct vas_vregion *walk = vas->segs;
    struct vas_vregion *prev = NULL;

    while (walk != NULL) {
        if (seg->seg->vaddr <= walk->seg->vaddr) {
            /* check for overlaps! */
            if (seg->seg->vaddr + seg->seg->length > walk->seg->vaddr
                || (prev != NULL && prev->seg->vaddr + prev->seg->length
                                > seg->seg->vaddr)) {
                return LIB_ERR_VSPACE_REGION_OVERLAP;
            }

            /* add here */
            if (prev == NULL) {
                seg->next = vas->segs;
                vas->segs = seg;
            } else {
                prev->next = seg;
                seg->next = walk;
            }
            return SYS_ERR_OK;
        }

        prev = walk;
        walk = walk->next;
    }

    /* add to end of list, checking for overlap with last item */
    assert(prev != NULL);
    if (prev->seg->vaddr + prev->seg->length > seg->seg->vaddr) {
        return LIB_ERR_VSPACE_REGION_OVERLAP;
    }
    prev->next = seg;
    seg->next = NULL;

    return SYS_ERR_OK;
}

#if 0
static errval_t vas_vspace_remove_segment(struct vas *vas, struct vas_vregion *seg)
{
    struct vas_vregion *walk = vas->seg;
    struct vas_vregion *prev = NULL;

    while (walk) {
        if (walk == seg) {
            if (prev) {
                assert(prev->next == walk);
                prev->next = walk->next;
            } else {
                assert(walk == vspace->head);
                vas->seg = walk->next;
            }
            return SYS_ERR_OK;
        }
        prev = walk;
        walk = walk->next;
    }

    return LIB_ERR_VREGION_NOT_FOUND;
}
#endif


struct range_slot_allocator rsa;
struct vas_vnode *cached_pdir_vnodes;
struct vas_vnode *cached_pt_vnodes;

#define VNODE_CACHE_REFILL_BITS 4

static errval_t vas_vspace_vnode_cached_alloc(enum objtype type,
                                              struct vas_vnode **vnode)
{
    errval_t err;

    uint8_t num_bits = VNODE_CACHE_REFILL_BITS;


    struct vas_vnode **cache = NULL;
    switch(type) {
        case ObjType_VNode_x86_64_pdir :
            cache = &cached_pdir_vnodes;
            break;
        case ObjType_VNode_x86_64_ptable :
            cache = &cached_pt_vnodes;
            break;
        default:
            debug_printf("unsupported type failed\n");
            return -1;
    }

    /* check if we have a cached vnode and return this */
    struct vas_vnode *newvnode = NULL;
    if (cache && *cache) {
        newvnode = *cache;
        *cache = newvnode->next;
        *vnode = newvnode;
        return SYS_ERR_OK;
    }

    struct vas_vnode *newlist = calloc((1<<num_bits), sizeof(struct vas_vnode));
    if (newlist == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    /* allocate a ram region for retyping */
    struct capref ram;
    size_t objbits_vnode = vnode_objbits(type);
    err = ram_alloc(&ram, objbits_vnode + num_bits);
    if (err_is_fail(err)) {
        return err;
    }


    /* allocate a range of slots in the slot allocator */
    struct capref vn_cap;
    err = range_slot_alloc(&rsa, (1<<num_bits), &vn_cap);
    if (err_is_fail(err)) {
        return err;
    }

    cslot_t vn_cap_slot = vn_cap.slot;

    /* allocate a new block of vnodes and set the cap accordingly*/

    uint32_t vnodes_created;
    struct vas_vnode *current = newlist;
    for (vnodes_created = 0; vnodes_created < (1 << num_bits); vnodes_created++) {
        current->next = (current + 1);
        current->vnode = vn_cap;

        current = (current + 1);
        vn_cap.slot++;
    }

    vn_cap.slot = vn_cap_slot;

    /* retype the ram cap into the range of allocated slots */
    err = cap_retype(vn_cap, ram, type, objbits_vnode + num_bits);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    err = cap_destroy(ram);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_DESTROY);
    }

    /* get the first one from the vnode list and remove it */
    *vnode = newlist;
    newlist = newlist->next;

    /* update the vnode cache */
    newvnode->next = *cache;
    *cache = newlist;



    return SYS_ERR_OK;
}

static errval_t vas_vspace_get_pdir(struct vas *vas, struct vas_vregion *reg,
                                    struct vas_vnode **vnode)
{
    errval_t err;

    lvaddr_t vaddr = reg->seg->vaddr & ~(X86_64_LARGE_PAGE_MASK);

    struct vas_vnode *walk = vas->vnodes;
    struct vas_vnode *prev = NULL;

    if (walk) {
        /* look for the vnode */

        while(walk) {
            if (walk->vaddr == vaddr) {
                *vnode = walk;
                return SYS_ERR_OK;
            }
            if (walk->vaddr > vaddr) {
                break;
            }
            prev = walk;
            walk = walk->next;
        }
    }


    /* need to allocate a new vnode */

    struct vas_vnode *v = NULL;

    /* check vnode chache */
    err = vas_vspace_vnode_cached_alloc(ObjType_VNode_x86_64_pdir, &v);
    if (err_is_fail(err)) {
        return err;
    }
    v->vaddr = vaddr;

    struct capref dest = {
        .cnode = vas->pagecn,
        .slot = X86_64_PML4_BASE(vaddr)
    };

    err = vnode_map(dest, v->vnode, X86_64_PDPT_BASE(vaddr),
                    X86_64_PTABLE_ACCESS_DEFAULT, 0, 1);
    if (err_is_fail(err)) {
        return err;
    }

    /* */

    if (prev) {
        v->next = prev->next;
        prev->next = v;
    } else {
        v->next = vas->vnodes;
        vas->vnodes = v;
    }

    *vnode = v;

    return SYS_ERR_OK;
}


static errval_t vas_vspace_get_pt(struct vas *vas, struct vas_vregion *reg,
                                  struct vas_vnode **vnode)
{
    errval_t err;

    lvaddr_t vaddr = reg->seg->vaddr & ~(X86_64_BASE_PAGE_MASK);

    struct vas_vnode *pdir;
    err = vas_vspace_get_pdir(vas, reg, &pdir);
    if (err_is_fail(err)) {
        return err;
    }

    struct vas_vnode *walk = pdir->children;
    struct vas_vnode *prev = NULL;

    if (walk) {
        /* look for the vnode */
        while(walk) {
            if (walk->vaddr == vaddr) {
                *vnode = walk;
                return SYS_ERR_OK;
            }
            if (walk->vaddr > vaddr) {
                break;
            }
            prev = walk;
            walk = walk->next;
        }
    }

    struct vas_vnode *v = NULL;

    /* check vnode chache */
    err = vas_vspace_vnode_cached_alloc(ObjType_VNode_x86_64_ptable, &v);
    if (err_is_fail(err)) {
        return err;
    }
    v->vaddr = vaddr;

    err = vnode_map(pdir->vnode, v->vnode, X86_64_PDIR_BASE(vaddr),
                    X86_64_PTABLE_ACCESS_DEFAULT, 0, 1);
    if (err_is_fail(err)) {
        return err;
    }

    /* */

    if (prev) {
        v->next = prev->next;
        prev->next = v;
    } else {
        v->next = vas->vnodes;
        vas->vnodes = v;
    }

    *vnode = v;

    return SYS_ERR_OK;
}

errval_t vas_vspace_attach_segment(struct vas *vas, struct vas_seg*seg)
{
    errval_t err;


    struct vas_vregion *vreg = calloc(1, sizeof(*vreg));
    if (!vreg) {
        return LIB_ERR_MALLOC_FAIL;
    }

    vreg->seg = seg;

    err = vas_vspace_add_segment(vas, vreg);
    if (err_is_fail(err)) {
        free(vreg);
        return err_push(err, LIB_ERR_VSPACE_ADD_REGION);
    }

    struct capref dest;
    struct vas_vnode *vnode;

    switch (seg->roottype) {
        case ObjType_VNode_x86_64_ptable :
            err = vas_vspace_get_pt(vas, vreg, &vnode);
            if (err_is_fail(err)) {
                 return err;
             }
             dest = vnode->vnode;
            break;
        case ObjType_VNode_x86_64_pdir :
            /* may need to create pdir */
            err = vas_vspace_get_pdir(vas, vreg, &vnode);
            if (err_is_fail(err)) {
                return err;
            }
            dest = vnode->vnode;
            break;
        case ObjType_VNode_x86_64_pdpt :
            /* we already have a pdpt -> use this */
            dest.cnode = vas->pagecn;
            dest.slot = X86_64_PML4_BASE(seg->vaddr);
            break;
        case ObjType_VNode_x86_64_pml4 :
            dest = vas->vroot;
            break;
        default:
            return SYS_ERR_VNODE_TYPE;
            break;
    }

    if (err_is_fail(err)) {
        return err;
    }

    return vnode_inherit(dest, seg->root, seg->start, seg->end);

}

errval_t vas_vspace_detach_segment(struct vas *vas, struct vas_seg *seg)
{
    return SYS_ERR_OK;
}
