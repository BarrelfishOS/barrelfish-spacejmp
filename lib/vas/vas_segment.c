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

#include <target/x86_64/barrelfish_kpi/paging_target.h>

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

static inline struct vas_segment *vas_seg_get_pointer(vas_seg_handle_t sh)
{
    return (struct vas_segment*)sh;
}

static inline vas_seg_handle_t vas_seg_get_handle(struct vas_segment *seg)
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

static inline bool is_same_pdir(genvaddr_t va1, genvaddr_t va2)
{
    return (va1>>X86_64_LARGE_PAGE_BITS) == ((va2-1)>>X86_64_LARGE_PAGE_BITS);
}
// returns whether va1 and va2 share a page directory pointer table entry
static inline bool is_same_pdpt(genvaddr_t va1, genvaddr_t va2)
{
    return (va1>>X86_64_HUGE_PAGE_BITS) == ((va2-1)>>X86_64_HUGE_PAGE_BITS);
}
// returns whether va1 and va2 share a page map level 4 entry
static inline bool is_same_pml4(genvaddr_t va1, genvaddr_t va2)
{
    // the base macros work here as we only have one pml4.
    return X86_64_PML4_BASE(va1) == X86_64_PML4_BASE(va2-1);
}

static errval_t vas_segment_verify_alignment(struct vas_segment *seg,
                                             lvaddr_t alignment)
{

    lvaddr_t alignmentmask = (alignment - 1);

    VAS_DEBUG_SEG("%s - verify alignment %lx mask = %lx\n", __FUNCTION__,
                          seg->vaddr, alignmentmask);

    if ((seg->vaddr & alignmentmask) || (seg->length & alignmentmask)) {
        return LIB_ERR_VREGION_BAD_ALIGNMENT;
    }
    return SYS_ERR_OK;
}


static errval_t vas_seg_create_ptables(enum objtype type,
                                       cslot_t num,
                                       struct capref dest)
{
    errval_t err;

    if (num == 0) {
        return SYS_ERR_OK;
    }

    VAS_DEBUG_SEG("%s - creating %u vnodes of type %u in slot %u\n", __FUNCTION__,
                  num, type, dest.slot);


    while (num > 0) {

        uint8_t num_bits = log2floor(num);

        VAS_DEBUG_SEG("%s - allocating %u vnodes in slot %u\n", __FUNCTION__,
                          1<<num_bits, dest.slot);

        /* allocate a ram region for retyping */
        struct capref ram;
        size_t objbits_vnode = vnode_objbits(type);
        err = ram_alloc(&ram, objbits_vnode + num_bits);
        if (err_is_fail(err)) {
            return err;
        }

        /* retype the ram cap into the range of allocated slots */
        err = cap_retype(dest, ram, type, objbits_vnode + num_bits);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_RETYPE);
        }

        err = cap_destroy(ram);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_DESTROY);
        }

        dest.slot += (1<<num_bits);
        num -= (1<<num_bits);

    }
    /* delete the uneeded caps */


    return SYS_ERR_OK;
}



static errval_t vas_segment_map_page_table(struct capref root,
                                           struct capref children,
                                           cslot_t start, cslot_t num)
{

    VAS_DEBUG_SEG("%s [%lu..%lu] dest=%u, src=%u\n", __FUNCTION__,
                  (uint64_t)start, (uint64_t)start+num,  root.slot, children.slot );

    return vnode_map(root, children, start, PTABLE_ACCESS_DEFAULT, 0, num);
}

static errval_t vas_segment_map_frames(struct capref root, struct capref frames,
                                       cslot_t start, cslot_t num, lvaddr_t offset,
                                       vregion_flags_t flags)
{
    paging_x86_64_flags_t pmap_flags =
        PTABLE_USER_SUPERVISOR | PTABLE_EXECUTE_DISABLE;

    if (!(flags & VREGION_FLAGS_GUARD)) {
        if (flags & VREGION_FLAGS_WRITE) {
            pmap_flags |= PTABLE_READ_WRITE;
        }
        if (flags & VREGION_FLAGS_EXECUTE) {
            pmap_flags &= ~PTABLE_EXECUTE_DISABLE;
        }
        if (flags & VREGION_FLAGS_NOCACHE) {
            pmap_flags |= PTABLE_CACHE_DISABLED;
        }
    }

    VAS_DEBUG_SEG("%s [%lu..%lu] dest=%u, src=%u \n", __FUNCTION__,
                  (uint64_t)start, (uint64_t)start+num, root.slot, frames.slot );

    return vnode_map(root, frames, start, pmap_flags, offset, num);
}

static cslot_t vas_segment_num_slots(enum objtype type,  size_t length,
                                     size_t pagesize)
{
    cslot_t num_slots = 2 + ((length/pagesize)/512);

    VAS_DEBUG_SEG("%s calculating slots %lu bytes, pagesize = %lu\n",
                  __FUNCTION__, length, pagesize);

    switch(type) {
        case ObjType_VNode_x86_64_pml4 :
            /* we have multiple pml4 entries which we span */
            num_slots += (length / (512UL*HUGE_PAGE_SIZE));
            /* no break */
        case ObjType_VNode_x86_64_pdpt :
            /* we have multiple full pdirs that we span */
            if (pagesize < HUGE_PAGE_SIZE) {
                num_slots += (length / (HUGE_PAGE_SIZE));
            }
            /* no break */
        case ObjType_VNode_x86_64_pdir :
            if (pagesize < LARGE_PAGE_SIZE) {
                num_slots += (length / (LARGE_PAGE_SIZE));
            }
            /* no break */
        case ObjType_VNode_x86_64_ptable :
            break;
        default:
            USER_PANIC("should not be reached");
            break;
    }

    return num_slots;
}



errval_t vas_segment_create(struct vas_seg *segment)
{
    errval_t err = SYS_ERR_OK;

    struct vas_segment *seg = (struct vas_segment *)segment;


    VAS_DEBUG_SEG("%s vaddr=0x%lx, size=%lu\n", __FUNCTION__, seg->vaddr, seg->length);

    /* work out root level */

    if (seg->length <= X86_64_LARGE_PAGE_SIZE) {
        segment->roottype = ObjType_VNode_x86_64_ptable;
        /* check if in the same pdir slot */
        if (!is_same_pdir(seg->vaddr, seg->vaddr+seg->length)) {
            return LIB_ERR_VSPACE_REGION_OVERLAP;
        }
        err = vas_segment_verify_alignment(seg, BASE_PAGE_SIZE);
    } else if (seg->length <= X86_64_HUGE_PAGE_SIZE) {
        segment->roottype = ObjType_VNode_x86_64_pdir;
        /* check if in the same pdpt slot */
        if (!is_same_pdpt(seg->vaddr, seg->vaddr+seg->length)) {
            return LIB_ERR_VSPACE_REGION_OVERLAP;
        }
        err = vas_segment_verify_alignment(seg, LARGE_PAGE_SIZE);
    } else if (seg->length <= 512UL * X86_64_HUGE_PAGE_SIZE){
        segment->roottype = ObjType_VNode_x86_64_pdpt;
        /* check if in the same pml4 slot */
        if (!is_same_pml4(seg->vaddr, seg->vaddr+seg->length)) {
            return LIB_ERR_VSPACE_REGION_OVERLAP;
        }
        err = vas_segment_verify_alignment(seg, HUGE_PAGE_SIZE);
    } else {
        segment->roottype = ObjType_VNode_x86_64_pml4;
        err = vas_segment_verify_alignment(seg, 512UL*HUGE_PAGE_SIZE);
    }

    if (err_is_fail(err)) {
        VAS_DEBUG_SEG("%s resulted in error '%s'\n", __FUNCTION__,
                      err_getstring(err));
        return err;
    }

    struct frame_identity fi;
    err = invoke_frame_identify(seg->frame, &fi);
    if (err_is_fail(err)) {
        return err;
    }

    if ((1UL << fi.bits) < seg->length) {
        return -1;
    }

    size_t pagesize = BASE_PAGE_SIZE;
    uint8_t pagesize_bits = BASE_PAGE_BITS;
    if (seg->flags & VREGION_FLAGS_HUGE) {
        pagesize = HUGE_PAGE_SIZE;
        pagesize_bits = HUGE_PAGE_BITS;
    } else if (seg->flags & VREGION_FLAGS_LARGE) {
        pagesize = LARGE_PAGE_SIZE;
        pagesize_bits = LARGE_PAGE_BITS;
    }

    cslot_t num_slots = vas_segment_num_slots(segment->roottype, seg->length, pagesize);


    VAS_DEBUG_SEG("%s creating cnode with %u slots\n", __FUNCTION__,
                         num_slots);

    err = cnode_create(&segment->segcn_cap, &segment->segcn, num_slots, NULL);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    struct capref dest = {.cnode = segment->segcn, .slot = 0};


    /* we need one subtree root table */
    VAS_DEBUG_SEG("%s creating subtree vroot of type %u\n", __FUNCTION__,
                   segment->roottype);
    err = vas_seg_create_ptables(segment->roottype, 1, dest);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VNODE_CREATE);
    }

    segment->root = dest;

    dest.slot += 1;


    switch(segment->roottype) {
        case ObjType_VNode_x86_64_pml4 :
            /* we have multiple pml4 entries which we span */
            num_slots = (seg->length / (512UL*HUGE_PAGE_SIZE));
            VAS_DEBUG_SEG("%s creating %u pdpt in slot %u\n", __FUNCTION__,
                               num_slots, dest.slot);
            err = vas_seg_create_ptables(ObjType_VNode_x86_64_pdpt, num_slots, dest);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_VNODE_CREATE);
            }
            dest.slot += num_slots;
            /* no break */
        case ObjType_VNode_x86_64_pdpt :
            /* we have multiple full pdirs that we span */
            num_slots = (seg->length / (HUGE_PAGE_SIZE));
            assert(!(seg->flags & VREGION_FLAGS_HUGE) == !!num_slots);
            VAS_DEBUG_SEG("%s creating %u pdir in slot %u\n", __FUNCTION__,
                               num_slots, dest.slot);
            err = vas_seg_create_ptables(ObjType_VNode_x86_64_pdir, num_slots, dest);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_VNODE_CREATE);
            }
            dest.slot += num_slots;
            /* no break */
        case ObjType_VNode_x86_64_pdir :
            num_slots = (seg->length / (LARGE_PAGE_SIZE));
            assert(!(seg->flags & VREGION_FLAGS_LARGE) == !!num_slots);
            VAS_DEBUG_SEG("%s creating %u ptables in slot %u\n", __FUNCTION__,
                               num_slots, dest.slot);
            err = vas_seg_create_ptables(ObjType_VNode_x86_64_ptable, num_slots, dest);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_VNODE_CREATE);
            }
            dest.slot += num_slots;
            /* no break */
        case ObjType_VNode_x86_64_ptable :
            /* nothig to do single root pt is enough */
            break;
        default:
            USER_PANIC("should not be reached");
            break;
    }

    /* we have the page tables now and we can do the mapping */

    VAS_DEBUG_SEG("%s copying caps\n", __FUNCTION__);

    cslot_t num_leave_pt = 1 + ((seg->length / pagesize) / 512);

    VAS_DEBUG_SEG("%s copying caps into slots [%" PRIuCSLOT "..%" PRIuCSLOT "]\n",
                  __FUNCTION__, dest.slot, dest.slot+num_leave_pt);
    for (cslot_t i = 0; i < num_leave_pt; ++i) {
        err = cap_copy(dest, seg->frame);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_COPY_FAIL);
        }
        dest.slot++;
    }


    dest.slot = 0;
    struct capref src = {.cnode = segment->segcn, .slot = 1};

    switch (segment->roottype) {
        case ObjType_VNode_x86_64_pml4:
            /* we have multiple pml4 entries which we span */
            num_slots = (seg->length / (512UL * HUGE_PAGE_SIZE));
            segment->slot_num = num_slots;
            segment->slot_start = X86_64_PML4_BASE(seg->vaddr);

            VAS_DEBUG_SEG("%s mapping pml4[%lu] -> %u pdpt\n", __FUNCTION__,
                          X86_64_PML4_BASE(seg->vaddr), num_slots);

            err = vas_segment_map_page_table(dest, src,
                                             X86_64_PML4_BASE(seg->vaddr),
                                             num_slots);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_VNODE_MAP);
            }
            src.slot += num_slots;
            dest.slot++;
            break;
        case ObjType_VNode_x86_64_pdpt:
            /* we have multiple full pdirs that we span */
            num_slots = (seg->length / (HUGE_PAGE_SIZE));
            segment->slot_num = num_slots;
            segment->slot_start = X86_64_PDPT_BASE(seg->vaddr);
            if (pagesize < HUGE_PAGE_SIZE) {
                VAS_DEBUG_SEG("%s mapping pdpt[%lu] -> %u pdir dst=%u, src=%u\n", __FUNCTION__,
                              X86_64_PDPT_BASE(seg->vaddr), num_slots, dest.slot, src.slot);

                err = vas_segment_map_page_table(dest, src,
                                                 X86_64_PDPT_BASE(seg->vaddr),
                                                 num_slots);
                if (err_is_fail(err)) {
                    return err_push(err, LIB_ERR_VNODE_MAP);
                }
                src.slot += num_slots;
                dest.slot++;
            }
            if (pagesize < LARGE_PAGE_SIZE) {
                //     num_slots = (seg->length / (LARGE_PAGE_SIZE));
                VAS_DEBUG_SEG("%s mapping pdir[%lu..%lu] -> pt dst=%u, src=%u\n", __FUNCTION__,
                              X86_64_PDPT_BASE(seg->vaddr), X86_64_PDPT_BASE(seg->vaddr)+ num_slots, dest.slot, src.slot);

                for (cslot_t slot = 0; slot < num_slots; ++slot) {
                    err = vas_segment_map_page_table(dest, src, 0, 512);
                    if (err_is_fail(err)) {
                        return err_push(err, LIB_ERR_VNODE_MAP);
                    }
                    src.slot += 512;
                    dest.slot++;
                }
            }
            break;
        case ObjType_VNode_x86_64_pdir:
            num_slots = (seg->length / (LARGE_PAGE_SIZE));
            segment->slot_num = num_slots;
            segment->slot_start = X86_64_PDIR_BASE(seg->vaddr);
            if (pagesize < LARGE_PAGE_SIZE) {
                VAS_DEBUG_SEG("%s mapping pdir[%lu] -> %u ptables, dst=%u, src=%u\n",
                              __FUNCTION__, X86_64_PDIR_BASE(seg->vaddr),
                              num_slots, dest.slot, src.slot);
                err = vas_segment_map_page_table(dest, src,
                                                 X86_64_PDIR_BASE(seg->vaddr),
                                                 num_slots);
                if (err_is_fail(err)) {
                    return err_push(err, LIB_ERR_VNODE_MAP);
                }
                src.slot += num_slots;
                dest.slot++;
            }
            break;
        case ObjType_VNode_x86_64_ptable:
            segment->slot_num = (seg->length / BASE_PAGE_SIZE);
            segment->slot_start = X86_64_PTABLE_BASE(seg->vaddr);
            break;
        default:
            USER_PANIC("should not be reached")
            ;
            break;
    }

    /* now we can start with mapping other levels */
    switch (segment->roottype) {
        case ObjType_VNode_x86_64_pml4:
            USER_PANIC("NOT YET IMPLEMENTED");
            if (pagesize == HUGE_PAGE_SIZE) {
                /* install frames */

            } else if (pagesize == LARGE_PAGE_SIZE) {
                /* need further mappings */

            } else {

            }
            break;
        case ObjType_VNode_x86_64_pdpt:
            if (pagesize == HUGE_PAGE_SIZE) {
                /* install frames */
                VAS_DEBUG_SEG("%s mapping large frames in pdir[%lu..%u] \n",
                              __FUNCTION__, X86_64_PDIR_BASE(seg->vaddr),
                              num_slots);
                err = vas_segment_map_frames(dest, src,
                                             X86_64_PDIR_BASE(seg->vaddr),
                                             num_slots, 0, seg->flags);
                if (err_is_fail(err)) {
                    return err;
                }
            } else if (pagesize == LARGE_PAGE_SIZE) {

                /* need further mappings */
                USER_PANIC("NOT YET IMPLEMENTED");
            } else {
                num_slots = seg->length / LARGE_PAGE_SIZE;
                size_t offset = 0;

                VAS_DEBUG_SEG("%s mapping base frames in pts[%u..%lu] \n",
                                              __FUNCTION__, 0, (uint64_t)num_slots);
                for (cslot_t slot = 0; slot < num_slots; slot++) {
                    err = vas_segment_map_frames(dest, src,
                                                 0, 512, offset, seg->flags);
                    dest.slot++;
                    src.slot++;
                    offset += LARGE_PAGE_SIZE;
                }
            }
            break;
        case ObjType_VNode_x86_64_pdir:
            num_slots = (seg->length / (LARGE_PAGE_SIZE));
            if (pagesize == LARGE_PAGE_SIZE) {
                VAS_DEBUG_SEG("%s mapping large frames in pdir[%lu..%u] \n",
                              __FUNCTION__, X86_64_PDIR_BASE(seg->vaddr),
                              num_slots);
                err = vas_segment_map_frames(dest, src,
                                             X86_64_PDIR_BASE(seg->vaddr),
                                             num_slots, 0, seg->flags);
                if (err_is_fail(err)) {
                    return err;
                }
            } else {
                lvaddr_t offset = 0;
                for (cslot_t s = 0; s < num_slots; ++s) {
                    err = vas_segment_map_frames(dest, src,
                                                 X86_64_PTABLE_BASE(seg->vaddr),
                                                 512, offset, seg->flags);
                    if (err_is_fail(err)) {
                        return err;
                    }
                    dest.slot++;
                    src.slot++;
                }
            }

            break;
        case ObjType_VNode_x86_64_ptable:
            num_slots = (seg->length / (BASE_PAGE_SIZE));
            VAS_DEBUG_SEG("%s mapping base frames in ptable[%lu..%u] \n",
                          __FUNCTION__, X86_64_PTABLE_BASE(seg->vaddr), num_slots);
            err = vas_segment_map_frames(dest, src, X86_64_PTABLE_BASE(seg->vaddr),
                                         num_slots, 0, seg->flags);
            if (err_is_fail(err)) {
                return err;
            }
            break;
        default:
            USER_PANIC("should not be reached")
            ;
            break;
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
                       vas_seg_handle_t *ret_seg)
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
                       vas_seg_handle_t *ret_seg)
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

    struct vas_segment *seg = calloc(1, sizeof(struct vas_seg));
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
    errval_t err;

    struct vas_segment *seg = vas_seg_get_pointer(sh);

    err = vas_client_seg_delete(seg->id);
    if (err_is_fail(err)) {
        return err;
    }

    free(seg);

    return SYS_ERR_OK;
}

errval_t vas_seg_lookup(const char *name, vas_seg_handle_t *ret_seg)
{
    errval_t err;

    struct vas_segment *seg = calloc(1, sizeof(struct vas_segment));
    if (seg == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    strncpy(seg->name, name, sizeof(seg->name));


    err = vas_client_seg_lookup(seg);
    if (err_is_fail(err)) {
        free(seg);
        return err;
    }

    *ret_seg = vas_seg_get_handle(seg);

    return SYS_ERR_OK;
}

errval_t vas_seg_attach(vas_handle_t vh, vas_seg_handle_t sh, vas_flags_t flags)
{
    struct vas_segment *seg = vas_seg_get_pointer(sh);
    struct vas *vas = vas_get_vas_pointer(vh);

    return vas_client_seg_attach(vas->id, seg->id, flags);
}

errval_t vas_seg_detach(vas_handle_t vh, vas_seg_handle_t sh)
{
    struct vas_segment *seg = vas_seg_get_pointer(sh);
    struct vas *vas = vas_get_vas_pointer(vh);

    return vas_client_seg_detach(vas->id, seg->id);
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
