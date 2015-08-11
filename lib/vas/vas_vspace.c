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

#define VAS_VSPACE_TAG_START 0x100

static uint16_t vas_vspace_tag_alloc = VAS_VSPACE_TAG_START;

#ifndef VAS_CONFIG_SEG_CACHE
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


#ifdef VAS_CONFIG_SEG_CACHE

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
errval_t vas_vspace_init(struct vas_vspace *vspace)
{
    errval_t err;

    struct vas *vas = (struct vas *)vspace;

    VAS_DEBUG_VSPACE("initializing new vspace for vas @ %p\n", vas);

    err = range_slot_alloc_init(&vspace->rsa, VAS_CONFIG_VSPACE_SLOTS, NULL);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC_INIT);
    }

    /* create a new page cn */
    VAS_DEBUG_VSPACE("creating new pagecn cap\n");
    err = cnode_create(&vspace->pagecn_cap, &vspace->pagecn, VAS_VSPACE_CNODE_SLOTS,
                       NULL);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_CREATE_PAGECN);
    }

    /* create a new vroot */
    VAS_DEBUG_VSPACE("creating vroot\n");
    vas->vroot = (struct capref){.cnode = vspace->pagecn, .slot = 0};
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
    struct capref pdpt = (struct capref){.cnode = vspace->pagecn,
                                         .slot = VAS_VSPACE_PML4_SLOT_MIN};

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
    cap_destroy(vspace->pagecn_cap);
    return err;

}

static errval_t vas_vspace_add_segment(struct vas_vspace *vspace,
                                       struct vas_vregion *segment)
{
    if (vspace->vregions == NULL) {
        VAS_DEBUG_VSPACE("%s first element setting head of list\n", __FUNCTION__);
        vspace->vregions = segment;
        segment->next = NULL;
        return SYS_ERR_OK;
    }
    struct vas_vregion *walk = vspace->vregions;
    struct vas_vregion *prev = NULL;

    struct vas_segment *seg = &segment->seg->seg;

    while (walk != NULL) {
        struct vas_segment *walk_seg = &walk->seg->seg;
        if (seg->vaddr <= walk_seg->vaddr) {
            /* check for overlaps! */
            if (seg->vaddr + seg->length > walk_seg->vaddr) {
                VAS_DEBUG_VSPACE("%s overlap with walk segment '%s' [%lx..%lx] [%lx %lx]\n",
                                 __FUNCTION__,
                                 walk_seg->name, seg->vaddr, seg->vaddr+seg->length,
                                 walk_seg->vaddr, walk_seg->vaddr+walk_seg->length);
                return LIB_ERR_VSPACE_REGION_OVERLAP;
            }
            if (prev != NULL && prev->seg->seg.vaddr + prev->seg->seg.length > seg->vaddr) {
                VAS_DEBUG_VSPACE("%s overlap with prev segment '%s' [%lx..%lx] [%lx %lx]\n",
                                 __FUNCTION__,
                                 prev->seg->seg.name, seg->vaddr, seg->vaddr+seg->length,
                                 prev->seg->seg.vaddr, prev->seg->seg.vaddr+prev->seg->seg.length);
                return LIB_ERR_VSPACE_REGION_OVERLAP;
            }

            /* add here */
            if (prev == NULL) {
                segment->next = vspace->vregions;
                vspace->vregions = segment;
            } else {
                prev->next = segment;
                segment->next = walk;
            }

            VAS_DEBUG_VSPACE("%s segment added in list.\n", __FUNCTION__);

            return SYS_ERR_OK;
        }

        prev = walk;
        walk = walk->next;
    }

    /* add to end of list, checking for overlap with last item */
    assert(prev != NULL);
    if (prev->seg->seg.vaddr + prev->seg->seg.length > seg->vaddr) {
        VAS_DEBUG_VSPACE("%s overlap with last segment '%s' [%lx..%lx] [%lx %lx]\n",
                         __FUNCTION__,
                         prev->seg->seg.name, seg->vaddr, seg->vaddr+seg->length,
                         prev->seg->seg.vaddr, prev->seg->seg.vaddr+prev->seg->seg.length);
        return LIB_ERR_VSPACE_REGION_OVERLAP;
    }

    VAS_DEBUG_VSPACE("%s segment added at the end.\n", __FUNCTION__);

    prev->next = segment;
    segment->next = NULL;

    return SYS_ERR_OK;
}


static errval_t vas_vspace_remove_segment(struct vas_vspace *vspace, struct vas_vregion *seg)
{
    struct vas_vregion *walk = vspace->vregions;
    struct vas_vregion *prev = NULL;

    while (walk) {
        if (walk == seg) {
            if (prev) {
                assert(prev->next == walk);
                prev->next = walk->next;
            } else {
                assert(walk == vspace->vregions);
                vspace->vregions = walk->next;
            }
            return SYS_ERR_OK;
        }
        prev = walk;
        walk = walk->next;
    }

    return LIB_ERR_VREGION_NOT_FOUND;
}



struct vas_vnode *cached_pdir_vnodes;
struct vas_vnode *cached_pt_vnodes;

#define VNODE_CACHE_REFILL_BITS 4

static errval_t vas_vspace_vnode_cached_alloc(struct vas_vspace *vspace,
                                              enum objtype type,
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
        VAS_DEBUG_VSPACE("%s serving request from cache\n", __FUNCTION__);
        newvnode = *cache;
        *cache = newvnode->next;
        *vnode = newvnode;
        return SYS_ERR_OK;
    }

    VAS_DEBUG_VSPACE("%s allocating new vnode list\n", __FUNCTION__);
    struct vas_vnode *newlist = calloc((1<<num_bits), sizeof(struct vas_vnode));
    if (newlist == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }


    /* allocate a ram region for retyping */
    VAS_DEBUG_VSPACE("%s allocating ram for %u vnodes\n", __FUNCTION__, 1 << num_bits);
    struct capref ram;
    size_t objbits_vnode = vnode_objbits(type);
    err = ram_alloc(&ram, objbits_vnode + num_bits);
    if (err_is_fail(err)) {
        free(newlist);
        return err;
    }

    /* allocate a range of slots in the slot allocator */
    struct capref vn_cap;
    err = range_slot_alloc(&vspace->rsa, (1<<num_bits), &vn_cap);
    if (err_is_fail(err)) {
        return err;
    }

    cslot_t vn_cap_slot = vn_cap.slot;

    /* allocate a new block of vnodes and set the cap accordingly*/

    VAS_DEBUG_VSPACE("%s linking list and setting caps\n", __FUNCTION__);
    uint32_t vnodes_created;
    struct vas_vnode *current = newlist;
    for (vnodes_created = 0; vnodes_created < (1 << num_bits); vnodes_created++) {
        current->next = (current + 1);
        current->vnode = vn_cap;

        current = (current + 1);
        vn_cap.slot++;
    }

    newlist[(1 << num_bits) - 1].next = NULL;

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


    VAS_DEBUG_VSPACE("%s updating cache and returing element\n", __FUNCTION__);
    /* get the first one from the vnode list and remove it */
    *vnode = newlist;
    newlist = newlist->next;

    /* update the vnode cache */
    newlist->next = *cache;
    *cache = newlist;

    return SYS_ERR_OK;
}

static errval_t vas_vspace_get_pdir(struct vas_vspace *vspace, struct vas_vregion *reg,
                                    struct vas_vnode **vnode)
{
    errval_t err;

    lvaddr_t vaddr = reg->seg->seg.vaddr & ~(X86_64_HUGE_PAGE_MASK);

    VAS_DEBUG_VSPACE("%s getting pdir for vaddr=0x%lx\n", __FUNCTION__,
                         vaddr);

    struct vas_vnode *walk = vspace->vnodes;
    struct vas_vnode *prev = NULL;

    if (walk) {
        /* look for the vnode */
        while(walk) {
            if (walk->vaddr == vaddr) {
                *vnode = walk;
                VAS_DEBUG_VSPACE("%s found vnode @ %p\n", __FUNCTION__, walk);
                return SYS_ERR_OK;
            }
            if (walk->vaddr > vaddr) {
                break;
            }
            prev = walk;
            walk = walk->next;
        }
    }


    VAS_DEBUG_VSPACE("%s allocating new pdir for vaddr=0x%lx\n", __FUNCTION__, vaddr);

    /* need to allocate a new vnode */

    struct vas_vnode *v = NULL;

    /* check vnode chache */
    err = vas_vspace_vnode_cached_alloc(vspace, ObjType_VNode_x86_64_pdir, &v);
    if (err_is_fail(err)) {
        return err;
    }
    v->vaddr = vaddr;

    struct capref dest = {
        .cnode = vspace->pagecn,
        .slot = X86_64_PML4_BASE(vaddr)
    };

    VAS_DEBUG_VSPACE("%s mapping vnode in pdpt=%u in slot=%lu\n", __FUNCTION__,
                     dest.slot, X86_64_PDPT_BASE(vaddr));

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
        v->next = vspace->vnodes;
        vspace->vnodes = v;
    }

    *vnode = v;

    return SYS_ERR_OK;
}


static errval_t vas_vspace_get_pt(struct vas_vspace *vspace, struct vas_vregion *reg,
                                  struct vas_vnode **vnode)
{
    errval_t err;

    lvaddr_t vaddr = reg->seg->seg.vaddr & ~(X86_64_LARGE_PAGE_MASK);

    VAS_DEBUG_VSPACE("%s getting ptable for vaddr=0x%lx\n", __FUNCTION__, vaddr);

    struct vas_vnode *pdir;
    err = vas_vspace_get_pdir(vspace, reg, &pdir);
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

    VAS_DEBUG_VSPACE("%s allocating new ptable for vaddr=0x%lx\n", __FUNCTION__, vaddr);

    /* check vnode chache */
    err = vas_vspace_vnode_cached_alloc(vspace, ObjType_VNode_x86_64_ptable, &v);
    if (err_is_fail(err)) {
        return err;
    }
    v->vaddr = vaddr;

    VAS_DEBUG_VSPACE("%s mapping vnode in pdir=%p in slot=%lu\n", __FUNCTION__,
                     pdir, X86_64_PDPT_BASE(vaddr));

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
        v->next = vspace->vnodes;
        pdir->children = v;
    }

    *vnode = v;

    return SYS_ERR_OK;
}

errval_t vas_vspace_attach_segment(struct vas_vspace *vspace, struct vas_seg *segment,
                                   struct vas_vregion *vreg)
{
    errval_t err;

    VAS_DEBUG_VSPACE("%s, attaching segment '%s' to vas '%s'\n", __FUNCTION__,
                     segment->seg.name, vspace->vas.name);

    struct vas *vas = (struct vas *)vspace;

    vreg->seg = segment;
    vreg->vspace = vspace;

    err = vas_vspace_add_segment(vspace, vreg);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_ADD_REGION);
    }

    struct capref dest;
    struct vas_vnode *vnode;

    switch (segment->roottype) {
        case ObjType_VNode_x86_64_ptable :
            err = vas_vspace_get_pt(vspace, vreg, &vnode);
            if (err_is_fail(err)) {
                 return err;
             }
            dest = vnode->vnode;
            VAS_DEBUG_VSPACE("%s, attaching at ptable %p\n", __FUNCTION__, vnode);
            break;
        case ObjType_VNode_x86_64_pdir :
            /* may need to create pdir */
            err = vas_vspace_get_pdir(vspace, vreg, &vnode);
            if (err_is_fail(err)) {
                return err;
            }
            dest = vnode->vnode;
            VAS_DEBUG_VSPACE("%s, attaching at pdir %p\n", __FUNCTION__, vnode);
            break;
        case ObjType_VNode_x86_64_pdpt :
            /* we already have a pdpt -> use this */
            VAS_DEBUG_VSPACE("%s, attaching at pdpt\n", __FUNCTION__);
            dest.cnode = vspace->pagecn;
            dest.slot = X86_64_PML4_BASE(segment->seg.vaddr);
            break;
        case ObjType_VNode_x86_64_pml4 :
            VAS_DEBUG_VSPACE("%s, attaching at pml4\n", __FUNCTION__);
            dest = vas->vroot;
            break;
        default:
            return SYS_ERR_VNODE_TYPE;
            break;
    }

    if (err_is_fail(err)) {
        return err;
    }

    VAS_DEBUG_VSPACE("%s, inheriting subtree root slots [%u..%u] dest=%u\n", __FUNCTION__,
                     segment->slot_start, segment->slot_start + segment->slot_num,
                     dest.slot);

    return vnode_inherit(dest, segment->root, segment->slot_start, segment->slot_num);

}

errval_t vas_vspace_detach_segment(struct vas_vregion *vreg)
{
    errval_t err;

    struct vas_vspace *vspace = vreg->vspace;

    err = vas_vspace_remove_segment(vspace, vreg);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_REMOVE_REGION);
    }

    struct capref dest;
    struct vas_vnode *vnode;

    switch (vreg->seg->roottype) {
        case ObjType_VNode_x86_64_ptable :
            err = vas_vspace_get_pt(vspace, vreg, &vnode);
            if (err_is_fail(err)) {
                return err;
            }
            dest = vnode->vnode;
            VAS_DEBUG_VSPACE("%s, detaching at ptable %p\n", __FUNCTION__, vnode);
            break;
        case ObjType_VNode_x86_64_pdir :
            /* may need to create pdir */
            err = vas_vspace_get_pdir(vspace, vreg, &vnode);
            if (err_is_fail(err)) {
                return err;
            }
            dest = vnode->vnode;
            VAS_DEBUG_VSPACE("%s, detaching at pdir %p\n", __FUNCTION__, vnode);
            break;
        case ObjType_VNode_x86_64_pdpt :
            /* we already have a pdpt -> use this */
            VAS_DEBUG_VSPACE("%s, detaching at pdpt\n", __FUNCTION__);
            dest.cnode = vspace->pagecn;
            dest.slot = X86_64_PML4_BASE(vreg->seg->seg.vaddr);
            break;
        case ObjType_VNode_x86_64_pml4 :
            VAS_DEBUG_VSPACE("%s, detaching at pml4\n", __FUNCTION__);
            dest = vspace->vas.vroot;
            break;
        default:
            return SYS_ERR_VNODE_TYPE;
            break;
    }

    if (err_is_fail(err)) {
        return err;
    }

    VAS_DEBUG_VSPACE("%s, clearing subtree root slots [%u..%u] dest=%u\n", __FUNCTION__,
                     vreg->seg->slot_start, vreg->seg->slot_start + vreg->seg->slot_num,
                     dest.slot);

    return vnode_clear(dest, vreg->seg->slot_start, vreg->seg->slot_num);
}

#endif
