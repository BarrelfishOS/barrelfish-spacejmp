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
#include <vas_debug.h>



///< internal representation of the VAS
struct vas
{
    vas_id_t id;                        ///< the vas id
    uint16_t tag;                       ///< tag for the vas
    vas_state_t state;                  ///< the state of the vas
    vas_perm_t perms;                   ///< associated permissions
    char name[VAS_ID_MAX_LEN];          ///< name of the vas
    struct vspace_state vspace_state;   ///< vspace state
    struct capref pagecn_cap;           ///< cap of the page cn
    struct cnoderef pagecn;             ///< pagecn cap
    struct capref   vroot;              ///< vroot
    struct single_slot_allocator pagecn_slot_alloc;
};

#endif /* __VAS_INTERNAL_H_ */
