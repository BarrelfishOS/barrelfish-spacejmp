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
#include <barrelfish/nameservice_client.h>
#include <vas/vas.h>
#include <vas_client.h>

#include <if/vas_defs.h>
#include <flounder/flounder_txqueue.h>



#define MIN(a,b)        ((a) < (b) ? (a) : (b))


#define VAS_RPC_START
#define VAS_RPC_SIGNAL_DONE
#define VAS_RPC_WAIT_DONE

static struct vas_binding *vas_service =  NULL;

static struct tx_queue vas_txq;


/*
 * ------------------------------------------------------------------------------
 * Send handlers
 * ------------------------------------------------------------------------------
 */

struct vas_msg_st
{
    struct txq_msg_st mst;
    union {
        struct {
            uint8_t *name;
            size_t length;
        } create;
        struct {
            uint64_t id;
            struct capref vroot;
        } attach;
        struct {
            uint64_t id;
        } detach;
        struct {
            uint64_t id;
            struct capref frame;
            uint64_t offset;
            uint64_t size;
        } map;
        struct {
            uint64_t id;
            uint64_t vaddr;
        } unmap;
        struct {
            uint8_t *name;
            size_t length;
        } lookup;
    };
};

static errval_t vas_create_call_tx(struct txq_msg_st *st)
{
    return vas_create_call__tx(st->queue->binding, TXQCONT(st),
                               ((struct vas_msg_st *)st)->create.name,
                               ((struct vas_msg_st *)st)->create.length);
}

static errval_t vas_attach_call_tx(struct txq_msg_st *st)
{
    return vas_attach_call__tx(st->queue->binding, TXQCONT(st),
                               ((struct vas_msg_st *)st)->attach.id,
                               ((struct vas_msg_st *)st)->attach.vroot);
}

static errval_t vas_detach_call_tx(struct txq_msg_st *st)
{
    return vas_detach_call__tx(st->queue->binding, TXQCONT(st),
                               ((struct vas_msg_st *)st)->detach.id);
}

static errval_t vas_map_call_tx(struct txq_msg_st *st)
{
    return vas_map_call__tx(st->queue->binding, TXQCONT(st),
                            ((struct vas_msg_st *)st)->map.id,
                            ((struct vas_msg_st *)st)->map.frame,
                            ((struct vas_msg_st *)st)->map.offset,
                            ((struct vas_msg_st *)st)->map.size);

}

static errval_t vas_unmap_call_tx(struct txq_msg_st *st)
{
    return vas_unmap_call__tx(st->queue->binding, TXQCONT(st),
                              ((struct vas_msg_st *)st)->unmap.id,
                              ((struct vas_msg_st *)st)->unmap.vaddr);
}

static errval_t vas_lookup_call_tx(struct txq_msg_st *st)
{
    return vas_lookup_call__tx(st->queue->binding, TXQCONT(st),
                               ((struct vas_msg_st *)st)->lookup.name,
                               ((struct vas_msg_st *)st)->lookup.length);
}


/*
 * ------------------------------------------------------------------------------
 * Receive handlers
 * ------------------------------------------------------------------------------
 */
static void vas_create_response_rx(struct vas_binding *_binding, vas_errval_t msgerr, uint64_t id)
{

}

static void vas_attach_response_rx(struct vas_binding *_binding, vas_errval_t msgerr)
{

}

static void vas_detach_response_rx(struct vas_binding *_binding, vas_errval_t msgerr)
{

}

static void vas_map_response_rx(struct vas_binding *_binding, vas_errval_t msgerr, uint64_t vaddr)
{

}

static void vas_unmap_response_rx(struct vas_binding *_binding, vas_errval_t msgerr)
{

}

static void vas_lookup_response_rx(struct vas_binding *_binding, vas_errval_t msgerr, uint64_t id)
{

}


static struct vas_rx_vtbl vas_rx_vtbl= {
    .create_response =vas_create_response_rx,
    .attach_response =vas_attach_response_rx,
    .detach_response =vas_detach_response_rx,
    .map_response =vas_map_response_rx,
    .unmap_response =vas_unmap_response_rx,
    .lookup_response =vas_lookup_response_rx
};

/*
 * ------------------------------------------------------------------------------
 * Connection management
 * ------------------------------------------------------------------------------
 */


static void vas_bind_continuation(void *st, errval_t err, struct vas_binding *_binding)
{
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "bind failed");
    }

    vas_service = _binding;
    _binding->rx_vtbl = vas_rx_vtbl;

    txq_init(&vas_txq, _binding, get_default_waitset(),
             (txq_register_fn_t)_binding->register_send, sizeof(struct vas_msg_st));
}


errval_t vas_client_connect(void)
{
    errval_t err;

    iref_t iref;
    err = nameservice_blocking_lookup(VAS_SERVICE_NAME, &iref);
    if (err_is_fail(err)) {
        return err;
    }

    uint32_t bound = 0;
    err = vas_bind(iref, vas_bind_continuation, &bound, get_default_waitset(), 0);
    if (err_is_fail(err)) {
        return err;
    }

    while (vas_service == NULL) {
        messages_wait_and_handle_next();
    }

    return SYS_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------
 * Connection management
 * ------------------------------------------------------------------------------
 */


errval_t vas_client_vas_create(char *name, vas_perm_t perms, vas_id_t *id)
{
    if (vas_service == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);

    mst->mst.send = vas_create_call_tx;

    return SYS_ERR_OK;
}

errval_t vas_client_vas_lookup(char *name, vas_id_t *id)
{
    if (vas_service == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);

    mst->mst.send = vas_lookup_call_tx;

    return SYS_ERR_OK;
}

errval_t vas_client_vas_attach(vas_id_t id, struct capref vroot)
{
    if (vas_service == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);

    mst->mst.send = vas_attach_call_tx;

    return SYS_ERR_OK;
}

errval_t vas_client_vas_detach(vas_id_t id)
{
    if (vas_service == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);

    mst->mst.send = vas_detach_call_tx;

    return SYS_ERR_OK;
}

errval_t vas_client_seg_map(vas_id_t id, struct capref frame, lpaddr_t offset,
                            size_t size, vas_perm_t perms, lvaddr_t *ret_vaddr)
{
    if (vas_service == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);

    mst->mst.send = vas_map_call_tx;

    return SYS_ERR_OK;
}

errval_t vas_client_seg_unmap(vas_id_t id, lvaddr_t vaddr)
{
    if (vas_service == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);

    mst->mst.send = vas_unmap_call_tx;

    return SYS_ERR_OK;
}


