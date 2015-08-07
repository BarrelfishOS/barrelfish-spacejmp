/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __VAS_INTERNAL_H_
#define __VAS_INTERNAL_H_ 1

#include <barrelfish/barrelfish.h>
#include <barrelfish/core_state_arch.h>
#include <vas/vas.h>
#include <vas/vas_segment.h>
#include <vas_debug.h>

static inline struct vas *vas_get_vas_pointer(vas_handle_t vashandle)
{
    return (struct vas *)vashandle;
}

static inline vas_handle_t vas_get_handle(struct vas* vas)
{
    return (vas_handle_t)vas;
}

#define VAS_ID_MASK 0x0000ffffffffffffUL
#define VAS_ID_TAG_MASK 0x0fff000000000000UL
#define VAS_ID_MASK 0x0000ffffffffffffUL
#define VAS_ID_MARK 0xA000000000000000UL


#if 0
///< internal representation of the VAS
struct vas
{
    vas_id_t id;                        ///< the vas id
    uint16_t tag;                       ///< tag for the vas
    vas_state_t state;                  ///< the state of the vas
    vas_flags_t perms;                  ///< associated permissions
    char name[VAS_NAME_MAX_LEN];        ///< name of the vas
    struct vspace_state vspace_state;   ///< vspace state
    struct capref pagecn_cap;           ///< cap of the page cn
    struct cnoderef pagecn;             ///< pagecn cap
    struct capref   vroot;              ///< vroot
    struct single_slot_allocator pagecn_slot_alloc;
};

struct vas_seg
{
    vas_seg_id_t id;
    char name [VAS_NAME_MAX_LEN];
    vas_seg_type_t type;
    vas_flags_t flags;
    lvaddr_t vaddr;
    size_t length;
    struct capref frame;
};
#endif

struct vas_vregion
{
    struct capref *pt;               ///< pt covering this segment
    struct capref *pdir;             ///< pdir covering this segment
    struct vas_seg *seg;                ///< pointer to the backing segment
    struct vas_vregion *next;       ///< pointer to the next region
};

struct vas_vspace
{

};

struct vas_vnode
{
    lvaddr_t vaddr;
    struct capref vnode;
    struct vas_vnode *next;
    struct vas_vnode *children;
};

///< internal representation of the VAS
struct vas
{
    vas_id_t id;                        ///< the vas id
    uint16_t tag;                       ///< tag for the vas
    vas_state_t state;                  ///< the state of the vas
    vas_flags_t perms;                  ///< associated permissions
    char name[VAS_NAME_MAX_LEN];        ///< name of the vas
    struct capref pagecn_cap;           ///< cap of the page cn
    struct cnoderef pagecn;             ///< pagecn cap
    struct capref   vroot;              ///< vroot
    struct vas_vregion *segs;           ///< attached segments in this VAS
    struct vas_vnode *vnodes;
};

struct vas_seg
{
    vas_seg_id_t id;
    char name [VAS_NAME_MAX_LEN];
    vas_seg_type_t type;
    vas_flags_t flags;
    lvaddr_t vaddr;
    size_t length;

    struct capref segcn_cap;           ///< cap of the page cn
    struct cnoderef segcn;             ///< pagecn caps

    struct capref root;             ///< capability of the root of the subtree
    enum objtype roottype;              ///< type of the root capability
    uint16_t slot_start;                 ///< start slot in the root
    uint16_t slot_num;                   ///< end slot in the root
    struct capref frame;
};

errval_t vas_segment_create(struct vas_seg *seg);

#endif /* __VAS_INTERNAL_H_ */

