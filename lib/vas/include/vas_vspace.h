/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __VAS_VSPACE_H_
#define __VAS_VSPACE_H_ 1

// Size of virtual region mapped by a single PML4 entry
#define PML4_MAPPING_SIZE ((genvaddr_t)512*512*512*BASE_PAGE_SIZE)


#define VAS_VSPACE_PML4_SLOTS 256
#define VAS_VSPACE_PML4_SLOT_MAX 500
#define VAS_VSPACE_PML4_SLOT_MIN VAS_VSPACE_PML4_SLOT_MAX - VAS_VSPACE_PML4_SLOTS
///<
#define VAS_VSPACE_MIN_MAPPABLE (PML4_MAPPING_SIZE * (VAS_VSPACE_PML4_SLOT_MIN))

#define VAS_VSPACE_META_RESERVED_SIZE (BASE_PAGE_SIZE * 80000)

///<
#define VAS_VSPACE_MAX_MAPPABLE (PML4_MAPPING_SIZE * (VAS_PML4_SLOT_MAX))


/*
 *
 */

errval_t vas_vspace_init(struct vas *vas);
errval_t vas_vspace_create_vroot(struct capref vroot);


errval_t vas_vspace_map_one_frame(struct vas *vas, void **retaddr,
                                  struct capref frame, size_t size,
                                  vas_flags_t flags);
errval_t vas_vspace_map_one_frame_fixed(struct vas *vas, lvaddr_t addr,
                                  struct capref frame, size_t size,
                                  vas_flags_t flags);
errval_t vas_vspace_unmap(void *addr);

/**
 * \brief inherits the text and data segment regions from the domain
 *
 * \param vas   the VAS to set the segments
 *
 * \returns SYS_ERR_OK on success
 *          errval or error
 */
static inline errval_t vas_vspace_inherit_segments(struct vas *vas)
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
static inline errval_t vas_vspace_inherit_heap(struct vas *vas)
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
static inline errval_t vas_vspace_inherit_regions(struct vas *vas,
                                                  struct capref vroot,
                                                  uint32_t start, uint32_t end)
{
    return vnode_inherit(vroot, vas->vroot, start, end);
}

#endif /* __VAS_VSPACE_H_ */
