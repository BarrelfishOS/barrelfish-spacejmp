/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __VAS_CLIENT_H_
#define __VAS_CLIENT_H_ 1

union vas_name_arg {
    char namestring[32];
    uint64_t namefields[4];
};

errval_t vas_client_connect(void);

errval_t vas_client_vas_create(struct vas *vas);

errval_t vas_client_vas_lookup(char *name, struct vas *vas);

errval_t vas_client_vas_attach(vas_id_t id, struct capref vroot);

errval_t vas_client_vas_detach(vas_id_t id);

errval_t vas_client_seg_map(vas_id_t id, struct capref frame, size_t size,
                            vas_flags_t flags, lvaddr_t *ret_vaddr);
errval_t vas_client_seg_map_fixed(vas_id_t id, lvaddr_t vaddr, struct capref frame,
                                  size_t size, vas_flags_t flags);
errval_t vas_client_seg_unmap(vas_id_t id, lvaddr_t vaddr);

errval_t vas_client_seg_create(struct vas_segment *seg);
errval_t vas_client_seg_lookup(struct vas_segment *seg);
errval_t vas_client_seg_delete(vas_seg_id_t sid);
errval_t vas_client_seg_attach(vas_id_t vid, vas_seg_id_t sid, vas_flags_t flags);
errval_t vas_client_seg_detach(vas_id_t vid, vas_seg_id_t sid);


#endif /* __VAS_CLIENT_H_ */
