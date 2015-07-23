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
#include <vas_internal.h>
#include <vas_debug.h>
#include <vas_client.h>

#include <if/vas_defs.h>
#include <flounder/flounder_txqueue.h>



#define MIN(a,b)        ((a) < (b) ? (a) : (b))


#define VAS_RPC_START
#define VAS_RPC_SIGNAL_DONE
#define VAS_RPC_WAIT_DONE

static struct vas_binding *vas_service_client =  NULL;

static struct tx_queue vas_txq;

struct vas_client_rpc_st
{
    errval_t err;
    struct vas_msg_st *mst;
#if VAS_DEBUG_CLIENT_ENABLE
    uint64_t callseq;
#endif
    union {
        struct {
            uint64_t id;
        } create;
        struct {
            /* nothing */
        } attach;
        struct {
            /* nothing */
        } detach;
        struct {
            uint64_t vaddr;
        } map;
        struct {
            /* nothing */
        } unmap;
        struct {
            uint64_t id;
        } lookup;
    };
    uint8_t rpc_wait_reply;
};

#if VAS_DEBUG_CLIENT_ENABLE
#define VAS_DEBUG_CLIENT_RPC_INC(rpc) \
    (rpc)->callseq++;
#else
#define VAS_DEBUG_CLIENT_RPC_INC(rpc)
#endif

/*
 * ---------------------------------------------------------------------------
 * RPC management
 * ---------------------------------------------------------------------------
 */

/**
 * \brief starts a new RPC to the DMA service
 *
 * \param chan  the DMA channel to start the RPC on
 *
 * \returns 1 if the RPC transaction could be started
 *          0 if there was already a transaction in process
 */
static inline int rpc_start(struct vas_client_rpc_st *cl)
{
    if (!cl->rpc_wait_reply) {
#if VAS_DEBUG_CLIENT_ENABLE
        cl->callseq++;
#endif
        cl->rpc_wait_reply = 0x1;
        return 1;
    }
    return 0;
}

/**
 * \brief waits until the started transaction is finished
 *
 * \param chan  the DMA channel
 */
static inline void rpc_wait_done(struct vas_client_rpc_st *cl)
{
    while (cl->rpc_wait_reply == 0x1) {
        messages_wait_and_handle_next();
    }
}

/**
 * \brief signals the completion of the RPC
 *
 * \param chan  the DMA channel
 */
static inline void rpc_done(struct vas_client_rpc_st *cl)
{
    cl->rpc_wait_reply = 0x2;
}

/**
 * \brief signals the completion of the RPC
 *
 * \param chan  the DMA channel
 */
static inline errval_t rpc_clear(struct vas_client_rpc_st *cl)
{
    errval_t err = cl->err;
    cl->rpc_wait_reply = 0x0;
    return err;
}

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
static void vas_create_response_rx(struct vas_binding *_binding, vas_errval_t msgerr,
                                   uint64_t id)
{
    struct vas_client_rpc_st *rpc_st = vas_service_client->st;

    VAS_DEBUG_CLIENT("[response] create seq=%lu, id=%016lx\n",rpc_st->callseq, id);

    rpc_st->err = msgerr;
    rpc_st->create.id = id;

    rpc_done(rpc_st);
}

static void vas_attach_response_rx(struct vas_binding *_binding, vas_errval_t msgerr)
{
    struct vas_client_rpc_st *rpc_st = vas_service_client->st;

    VAS_DEBUG_CLIENT("[response] attach seq=%lu\n",rpc_st->callseq);

    rpc_done(rpc_st);
}

static void vas_detach_response_rx(struct vas_binding *_binding, vas_errval_t msgerr)
{
    struct vas_client_rpc_st *rpc_st = vas_service_client->st;

    VAS_DEBUG_CLIENT("[response] detach seq=%lu\n",rpc_st->callseq);

    rpc_done(rpc_st);
}

static void vas_map_response_rx(struct vas_binding *_binding, vas_errval_t msgerr,
                                uint64_t vaddr)
{
    struct vas_client_rpc_st *rpc_st = vas_service_client->st;

    VAS_DEBUG_CLIENT("[response] map seq=%lu, vaddr=%016lx\n",rpc_st->callseq, vaddr);

    rpc_st->map.vaddr = vaddr;
    rpc_st->err = msgerr;

    rpc_done(rpc_st);
}

static void vas_unmap_response_rx(struct vas_binding *_binding, vas_errval_t msgerr)
{
    struct vas_client_rpc_st *rpc_st = vas_service_client->st;

    VAS_DEBUG_CLIENT("[response] unmap seq=%lu\n",rpc_st->callseq);

    rpc_done(rpc_st);
}

static void vas_lookup_response_rx(struct vas_binding *_binding, vas_errval_t msgerr,
                                   uint64_t id)
{
    struct vas_client_rpc_st *rpc_st = vas_service_client->st;

    VAS_DEBUG_CLIENT("[response] lookup seq=%lu, id=%016lx\n",rpc_st->callseq, id);

    rpc_st->lookup.id = id;
    rpc_st->err = msgerr;

    rpc_done(rpc_st);
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
    VAS_DEBUG_CLIENT("[connect] bind callback: %s\n", err_getstring(err));

    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "bind failed");
    }

    vas_service_client = _binding;
    _binding->rx_vtbl = vas_rx_vtbl;
    _binding->st = calloc(1, sizeof(struct vas_client_rpc_st));
    if (_binding->st == NULL) {
        USER_PANIC("malloc failed");
    }

    txq_init(&vas_txq, _binding, get_default_waitset(),
             (txq_register_fn_t)_binding->register_send, sizeof(struct vas_msg_st));
}


errval_t vas_client_connect(void)
{
    errval_t err;

    VAS_DEBUG_CLIENT("[connect] nslookup '%s'\n", VAS_SERVICE_NAME);

    iref_t iref;
    err = nameservice_blocking_lookup(VAS_SERVICE_NAME, &iref);
    if (err_is_fail(err)) {
        return err;
    }

    VAS_DEBUG_CLIENT("[connect] bind to service %s::%"PRIxIREF "\n",
                     VAS_SERVICE_NAME, iref);

    uint32_t bound = 0;
    err = vas_bind(iref, vas_bind_continuation, &bound, get_default_waitset(), 0);
    if (err_is_fail(err)) {
        return err;
    }

    while (vas_service_client == NULL) {
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
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_client_rpc_st *rpc_st = vas_service_client->st;
    if (!rpc_start(rpc_st)) {
        return FLOUNDER_ERR_TX_BUSY;
    }

    VAS_DEBUG_CLIENT("[call] create seq=%lu, name=%s\n",rpc_st->callseq, name)
    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);
    if (!mst) {
        return LIB_ERR_MALLOC_FAIL;
    }

    mst->mst.send = vas_create_call_tx;

    size_t len = strlen(name);
    assert(len < VAS_ID_MAX_LEN);

    mst->create.name = (uint8_t*)name;
    mst->create.length = len + 1;

    /*
     * RPC START SEQUENCE
     */

    txq_send(&mst->mst);
    rpc_wait_done(rpc_st);

    if (id) {
        *id = rpc_st->create.id;
    }

    return rpc_clear(rpc_st);;
}

errval_t vas_client_vas_lookup(char *name, vas_id_t *id)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_client_rpc_st *rpc_st = vas_service_client->st;
    if (!rpc_start(rpc_st)) {
        return FLOUNDER_ERR_TX_BUSY;
    }

    VAS_DEBUG_CLIENT("[call] lookup seq=%lu, name=%s\n",rpc_st->callseq, name);

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);
    if (!mst) {
        return LIB_ERR_MALLOC_FAIL;
    }
    mst->mst.send = vas_lookup_call_tx;

    mst->lookup.length = strlen(name) + 1;
    mst->lookup.name = (uint8_t*)name;
    /*
     * RPC START SEQUENCE
     */

    txq_send(&mst->mst);
    rpc_wait_done(rpc_st);

    if (id) {
        *id = rpc_st->lookup.id;
    }

    return rpc_clear(rpc_st);;
}

errval_t vas_client_vas_attach(vas_id_t id, struct capref vroot)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_client_rpc_st *rpc_st = vas_service_client->st;
    if (!rpc_start(rpc_st)) {
        return FLOUNDER_ERR_TX_BUSY;
    }

    VAS_DEBUG_CLIENT("[call] attach seq=%lu, id=%016lx\n",rpc_st->callseq, id);

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);
    if (!mst) {
        return LIB_ERR_MALLOC_FAIL;
    }

    mst->attach.id = id;
    mst->attach.vroot = vroot;
    mst->mst.send = vas_attach_call_tx;

    /*
     * RPC START SEQUENCE
     */
    txq_send(&mst->mst);
    rpc_wait_done(rpc_st);

    return rpc_clear(rpc_st);;
}

errval_t vas_client_vas_detach(vas_id_t id)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_client_rpc_st *rpc_st = vas_service_client->st;
    if (!rpc_start(rpc_st)) {
        return FLOUNDER_ERR_TX_BUSY;
    }

    VAS_DEBUG_CLIENT("[call] detach seq=%lu, id=%016lx\n",rpc_st->callseq, id);

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);
    if (!mst) {
        return LIB_ERR_MALLOC_FAIL;
    }
    mst->mst.send = vas_detach_call_tx;

    /*
     * RPC START SEQUENCE
     */
    txq_send(&mst->mst);
    rpc_wait_done(rpc_st);

    return rpc_clear(rpc_st);;
}

errval_t vas_client_seg_map(vas_id_t id, struct capref frame, lpaddr_t offset,
                            size_t size, vas_perm_t perms, lvaddr_t *ret_vaddr)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_client_rpc_st *rpc_st = vas_service_client->st;
    if (!rpc_start(rpc_st)) {
        return FLOUNDER_ERR_TX_BUSY;
    }

    VAS_DEBUG_CLIENT("[call] map seq=%lu, id=%016lx, size=%016lx\n",rpc_st->callseq,
                     id, size);

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);
    if (!mst) {
        return LIB_ERR_MALLOC_FAIL;
    }
    mst->mst.send = vas_map_call_tx;
    mst->map.frame = frame;
    mst->map.id = id;
    mst->map.offset = offset;
    mst->map.size = size;

    /*
     * RPC START SEQUENCE
     */
    txq_send(&mst->mst);
    rpc_wait_done(rpc_st);

    if (ret_vaddr) {
        *ret_vaddr = rpc_st->map.vaddr;
    }

    return rpc_clear(rpc_st);
}

errval_t vas_client_seg_unmap(vas_id_t id, lvaddr_t vaddr)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    struct vas_client_rpc_st *rpc_st = vas_service_client->st;
    if (!rpc_start(rpc_st)) {
        return FLOUNDER_ERR_TX_BUSY;
    }

    VAS_DEBUG_CLIENT("[call] unmap seq=%lu, id=%016lx, vaddr=%016lx\n",
                     rpc_st->callseq, id, vaddr);

    struct vas_msg_st *mst = (struct vas_msg_st *)txq_msg_st_alloc(&vas_txq);
    if (!mst) {
        return LIB_ERR_MALLOC_FAIL;
    }

    mst->mst.send = vas_unmap_call_tx;

    /*
     * RPC START SEQUENCE
     */
    txq_send(&mst->mst);
    rpc_wait_done(rpc_st);



    return rpc_clear(rpc_st);
}


