/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich.
 * Attn: Systems Group.
 */

#ifndef __LIBVAS_SEG_H
#define __LIBVAS_SEG_H 1

#include <vas/vas.h>


/*
 * ==============================================================================
 * Typedefs and defines
 * ==============================================================================
 */


///< segment identifier
typedef uint64_t vas_seg_id_t;

///< segment handle
typedef uintptr_t vas_seg_handle_t;



/**
 * Represents the value of vaddr to seg_alloc which indicates the
 * kernel should / pick the offset to use. It is an addresss 'too
 * large' to use as a valid value anyway.
 */
#define VAS_SEG_VADDR_MAX   (0UL-1)

/**
 * Represents the value of vaddr to seg_alloc which indicates the
 * kernel should / pick the offset to use. It is an addresss 'too
 * large' to use as a valid value anyway.
 */
#define VAS_SEG_VADDR_MAX   (0UL-1)

///< maximum length of a segment
#define VAS_SEG_MAX_LEN (1UL<<40)

///< location of the segment
typedef enum vas_seg_type
{
    VAS_SEG_TYPE_RELOC = 0,  ///< the segment can be mapped at any address
    VAS_SEG_TYPE_FIXED = 1   ///< the segment is only mappable at fixed faddr
} vas_seg_type_t;


/*
 * =============================================================================
 * Public interface
 * =============================================================================
 */

errval_t vas_seg_alloc(const char *name, vas_seg_type_t type, size_t length,
                       lvaddr_t vaddr,  vas_flags_t flags,
                       vas_seg_id_t *ret_seg);
errval_t vas_seg_create(const char *name, vas_seg_type_t type, size_t length,
                       lvaddr_t vaddr, struct capref frame, vas_flags_t flags,
                       vas_seg_id_t *ret_seg);
errval_t vas_seg_free   (vas_seg_handle_t seg);
errval_t gas_seg_lookup(const char *name, vas_seg_handle_t *ret_seg);

errval_t vas_seg_attach(vas_handle_t vas, vas_seg_id_t seg, vas_seg_handle_t *ret_seg);
errval_t vas_seg_detach(vas_seg_handle_t ret_seg);

lvaddr_t vas_seg_get_vaddr(vas_seg_handle_t);
vas_seg_id_t vas_seg_get_id(vas_seg_handle_t);


#endif /* __LIBVAS_H */
