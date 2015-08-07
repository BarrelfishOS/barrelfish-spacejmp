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

#define VAS_CONFIG_SEG_CACHE 1

#define VAS_CONFIG_VSPACE_SLOTS 512

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


#ifndef VAS_CONFIG_SEG_CACHE
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

#ifdef VAS_CONFIG_SEG_CACHE


///< internal representation of the VAS used at client ant server side
struct vas
{
    vas_id_t id;                        ///< the vas id
    uint16_t tag;                       ///< tag for the vas
    vas_state_t state;                  ///< the state of the vas
    vas_flags_t perms;                  ///< associated permissions
    char name[VAS_NAME_MAX_LEN];        ///< name of the vas
    struct capref   vroot;              ///< vroot cache
};

///< internal representation of the VAS segment used at client ant server side
struct vas_segment
{
    vas_seg_id_t id;
    char name [VAS_NAME_MAX_LEN];
    vas_seg_type_t type;
    vas_flags_t flags;
    lvaddr_t vaddr;
    size_t length;
    struct capref frame;
};


///< association of segments to vspaces
struct vas_vregion
{
    struct vas_vspace *vspace;
    struct vas_seg *seg;             ///< pointer to the backing segment
    struct capref *pt;               ///< pt covering this segment
    struct capref *pdir;             ///< pdir covering this segment
    struct vas_vregion *next;        ///< pointer to the next region
};

///< internal representation of page-tables not associated with segments
struct vas_vnode
{
    lvaddr_t vaddr;
    struct capref vnode;
    struct vas_vnode *next;
    struct vas_vnode *children;
};

///< representation of the vas used by the service
struct vas_vspace
{
    struct vas vas;
    struct vas_vregion *vregions;       ///< attached segments in this VAS
    struct vas_vnode *vnodes;           ///< vnodes for this vspace
    struct capref pagecn_cap;           ///< cap of the page cn
    struct cnoderef pagecn;             ///< pagecn cap for top level pagetables
    struct range_slot_allocator rsa;    ///< range slot allocator for ptables
};

struct vas_seg
{
    struct vas_segment seg;

    struct capref segcn_cap;           ///< cap of the page cn
    struct cnoderef segcn;             ///< pagecn caps

    struct capref root;                ///< capability of the root of the subtree
    enum objtype roottype;             ///< type of the root capability
    uint16_t slot_start;               ///< start slot in the root
    uint16_t slot_num;                 ///< end slot in the root
};

errval_t vas_segment_create(struct vas_seg *seg);

#endif

#endif /* __VAS_INTERNAL_H_ */

