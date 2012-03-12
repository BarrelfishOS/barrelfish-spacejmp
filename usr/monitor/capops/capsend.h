/*
 * Copyright (c) 2012 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/types.h>
#include <barrelfish/caddr.h>
#include <if/intermon_defs.h>
#include "queue.h"

#ifndef CAPOPS_TRANSPORT_H
#define CAPOPS_TRANSPORT_H

struct capsend_mc_msg_st;
struct capsend_mc_st;

typedef void (*capsend_send_fn)(struct intermon_binding*, intermon_caprep_t*, struct capsend_mc_st*); /* binding, caprep, user_st */

bool capsend_handle_mc_reply(genvaddr_t mc_st); /* returns true if was last reply */

struct capsend_mc_st {
    struct capsend_mc_msg_st *msg_st_arr;
    int num_pending;
    int num_queued;
    bool do_send;
    intermon_caprep_t caprep;
    capsend_send_fn send_fn;
};

errval_t capsend_target(coreid_t dest, struct msg_queue_elem *queue_elem);

errval_t capsend_owner(struct capref capref, struct msg_queue_elem *queue_elem);

errval_t capsend_update_owner(struct capref capref, struct event_closure continuation);

errval_t capsend_copies(struct capability *cap, capsend_send_fn send_fn, struct capsend_mc_st *mc_st);

errval_t capsend_descendants(struct capability *cap, capsend_send_fn send_fn, struct capsend_mc_st *mc_st);

typedef void (*capsend_find_cap_result_fn)(errval_t, coreid_t, void*);

errval_t capsend_find_cap(struct capability *cap, capsend_find_cap_result_fn result_fn, void *st);

typedef void (*capsend_result_fn)(errval_t, void*);

errval_t capsend_find_descendants(struct capref src, capsend_result_fn result_fn, void *st);

#endif
