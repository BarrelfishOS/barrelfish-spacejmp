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

#include <if/vas_defs.h>
#include <flounder/flounder_txqueue.h>

#define VAS_SERVICE_DEBUG(x...) debug_printf(x);

#define MIN(a,b)        ((a) < (b) ? (a) : (b))

struct vas_client
{
    struct vas_binding *b;
    struct tx_queue txq;
};


struct vas_info
{
    vas_id_t id;
    uint16_t slot;
    char name[VAS_ID_MAX_LEN];
    struct capref pagecn;
    struct vas_client *creator;
    struct vas_info *next;
    struct vas_info *prev;

};

struct vas_info *vas_registered;

/*
 * ------------------------------------------------------------------------------
 * VAS info management
 * ------------------------------------------------------------------------------
 */

static void vas_info_insert(struct vas_info *vi)
{
    if (vas_registered) {
        vas_registered->prev = vi;
        vi->next = vas_registered;
        vas_registered = vi;
    } else {
        vi->next = vi->prev = NULL;
        vas_registered = vi;
    }
}

static struct vas_info *vas_info_lookup(const char *name)
{
    struct vas_info *vi = vas_registered;
    while(vi) {
        if (strcmp(name, vi->name) == 0) {
            return vi;
        }
        vi = vi->next;
    }
    return NULL;
}

#if 0
static void vas_info_remove(struct vas_info *vi)
{

    if (vi->prev == NULL) {
        /* remove the first in the queue */
        vas_register = vi->next;
    } else {
        /* remove in the middle of the queue in the queue */
        vi->prev->next = vi->next;
    }

    if (vi->next) {
        vi->next->prev = vi->prev;
    }

    vi->next = NULL;
    vi->prev = NULL;
}
#endif


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
            uint16_t slot;
        } slot_alloc;
        struct {
            struct capref pagecn;
            uint64_t id;
            uint16_t slot;
        } lookup;
        struct {
            uint64_t id;
        } share;
    };
};

static errval_t vas_share_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_share_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                  vst->share.id);

}

static errval_t vas_lookup_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_lookup_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                   vst->lookup.pagecn, vst->lookup.id,
                                   vst->lookup.slot);
}

static errval_t vas_slot_alloc_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_slot_alloc_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                       vst->slot_alloc.slot);
}


/*
 * ------------------------------------------------------------------------------
 * Receive handlers
 * ------------------------------------------------------------------------------
 */
static void vas_share_call__rx(struct vas_binding *_binding, uint8_t *name,
                               size_t size, struct capref pagecn)
{
    struct vas_client *client = _binding->st;
    struct txq_msg_st *mst = txq_msg_st_alloc(&client->txq);

    mst->send = vas_share_response_tx;

    struct vas_info *vi = calloc(1, sizeof(struct vas_info));
    if (vi == NULL) {
        mst->err = LIB_ERR_MALLOC_FAIL;
        txq_send(mst);
        return;
    }

    mst->err = SYS_ERR_OK;

    vi->id = (vas_id_t)vi;
    vi->creator = client;
    size_t len = MIN(size, VAS_ID_MAX_LEN);
    memcpy(vi->name, name, len);
    vi->name[len] = 0;

    vi->pagecn = pagecn;
    vi->slot = 127; // todo: have a define

    ((struct vas_msg_st *)mst)->share.id = vi->id;

    vas_info_insert(vi);

    txq_send(mst);
}

static void vas_lookup_call__rx(struct vas_binding *_binding, uint8_t *name,
                                size_t size)
{
    struct vas_client *client = _binding->st;
    struct txq_msg_st *mst = txq_msg_st_alloc(&client->txq);


    mst->send = vas_lookup_response_tx;

    struct vas_info *vi = vas_info_lookup((char *)name);
    if (vi) {
        if (vi->slot == 512) {
            mst->err = VAS_ERR_OUT_OF_HANDLES;
        } else {
            mst->err = SYS_ERR_OK;
            ((struct vas_msg_st *)mst)->lookup.id = vi->id;
            ((struct vas_msg_st *)mst)->lookup.pagecn = vi->pagecn;
            ((struct vas_msg_st *)mst)->lookup.slot = vi->slot++;
        }
    } else {
        mst->err = VAS_ERR_NOT_FOUND;
    }

    txq_send(mst);
}

static  void vas_slot_alloc_call__rx(struct vas_binding *_binding,
                                     uint64_t id)
{
    struct vas_client *client = _binding->st;
    struct txq_msg_st *mst = txq_msg_st_alloc(&client->txq);


    mst->send = vas_slot_alloc_response_tx;

    struct vas_info *vi = (struct vas_info *)id;
    if (vi->id == id) {
        if (vi->slot ==512) {
            ((struct vas_msg_st *)mst)->slot_alloc.slot = 0;
            mst->err = VAS_ERR_OUT_OF_HANDLES;
        } else {
            ((struct vas_msg_st *)mst)->slot_alloc.slot = vi->slot++;
            mst->err = SYS_ERR_OK;
        }
    } else {
        mst->err = VAS_ERR_NOT_FOUND;
    }

    txq_send(mst);
}


static struct vas_rx_vtbl rx_vtbl= {
    .share_call = vas_share_call__rx,
    .lookup_call = vas_lookup_call__rx,
    .slot_alloc_call = vas_slot_alloc_call__rx
};

/*
 * ------------------------------------------------------------------------------
 * Connection management
 * ------------------------------------------------------------------------------
 */
static errval_t vas_connect_handler(void *st, struct vas_binding *binding)
{
    VAS_SERVICE_DEBUG("new connection from client\n");

    struct vas_client *client = calloc(1, sizeof(struct vas_client));
    if (client == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    client->b = binding;
    txq_init(&client->txq, binding, get_default_waitset(),
             (txq_register_fn_t)binding->register_send, sizeof(struct vas_msg_st));

    binding->st = client;
    binding->rx_vtbl = rx_vtbl;

    return SYS_ERR_OK;
}

static void export_callback_fn(void *st, errval_t err, iref_t iref)
{
    VAS_SERVICE_DEBUG("service exported: %s\n", err_getstring(err));

    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "export failed");
    }


#define VAS_SERVICE_NAME "vas"

    err = nameservice_register(VAS_SERVICE_NAME, iref);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "export failed");
    }

    VAS_SERVICE_DEBUG("service registered: [%s] -> [%" PRIxIREF "]\n",
                      VAS_SERVICE_NAME, iref);
}


int main(int argc, char *argv[])
{
    errval_t err;

    VAS_SERVICE_DEBUG("vas service started\n");

    err =  vas_export(NULL, export_callback_fn, vas_connect_handler,
                      get_default_waitset(), 0);

    while(1) {
        messages_wait_and_handle_next();
    }

    return 0;
}
