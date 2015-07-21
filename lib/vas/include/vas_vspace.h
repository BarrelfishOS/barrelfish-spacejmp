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


#define VAS_VSPACE_PML4_SLOT_MIN 128
#define VAS_VSPACE_PML4_SLOT_MAX 512

///<
#define VAS_VSPACE_MIN_MAPPABLE (PML4_MAPPING_SIZE * (VAS_VSPACE_PML4_SLOT_MIN - 1))

#define VAS_VSPACE_META_RESERVED_SIZE (BASE_PAGE_SIZE * 80000)

///<
#define VAS_VSPACE_MAX_MAPPABLE (PML4_MAPPING_SIZE * (VAS_PML4_SLOT_MAX - 1))


/*
 *
 */

errval_t vas_vspace_init(struct vas *vas);
errval_t vas_vspace_create_vroot(struct capref vroot);



#endif /* __VAS_VSPACE_H_ */
