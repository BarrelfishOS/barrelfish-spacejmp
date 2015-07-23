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
#include <vas_vspace.h>

#include <if/vas_defs.h>
#include <flounder/flounder_txqueue.h>

#define VAS_SERVICE_DEBUG(x...) debug_printf(x);

#define VAS_ID_MASK 0x00ffffffffffffffUL
#define VAS_ID_MARK 0x1D00000000000000UL

#define VAS_ATTACHED_MAX 16

#define MIN(a,b)        ((a) < (b) ? (a) : (b))

#define EXPECT_SUCCESS(expr, msg...) \
    if (!(expr)) {USER_PANIC("expr" msg);}

struct vas_client
{
    struct vas_binding *b;
    struct tx_queue txq;
};


struct vas_attached
{
    struct capref vroot;
    struct vas_attached *next;
};

struct vas_info
{
    struct vas vas;
    struct vas_client *creator;
    struct vas_attached *attached;
    uint64_t map[512/64];
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
        if (strcmp(name, vi->vas.name) == 0) {
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
};

static errval_t vas_create_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_create_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                   vst->create.id);
}

static errval_t vas_attach_response_tx(struct txq_msg_st *st)
{
    return vas_attach_response__tx(st->queue->binding, TXQCONT(st), st->err);
}

static errval_t vas_detach_response_tx(struct txq_msg_st *st)
{
    return vas_detach_response__tx(st->queue->binding, TXQCONT(st), st->err);
}

static errval_t vas_map_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_map_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                vst->map.vaddr);
}

static errval_t vas_unmap_response_tx(struct txq_msg_st *st)
{
    return vas_unmap_response__tx(st->queue->binding, TXQCONT(st), st->err);
}

static errval_t vas_lookup_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_lookup_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                   vst->lookup.id);
}



/*
 * ------------------------------------------------------------------------------
 * Receive handlers
 * ------------------------------------------------------------------------------
 */

static void vas_create_call__rx(struct vas_binding *_binding, uint8_t *name,
                                size_t size)
{
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] create: client=%p, name='%s'\n", client,
                      (char *)name);

    mst->mst.send =  vas_create_response_tx;

    struct vas_info *vi = calloc(1, sizeof(struct vas_info));
    if (vi == NULL) {
        mst->mst.err = LIB_ERR_MALLOC_FAIL;
    } else {
        vi->vas.id = (uint64_t)vi;
        strncpy(vi->vas.name, (char *)name, size);
        mst->mst.err = vas_vspace_init(&vi->vas);
        mst->create.id = VAS_ID_MARK | (uint64_t)vi;
        vas_info_insert(vi);
    }

    txq_send(&mst->mst);
}

static void vas_attach_call__rx(struct vas_binding *_binding, uint64_t id,
                                struct capref vroot)
{
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] attach: client=%p, vas=0x%016lx\n", client, id);

    mst->mst.send =  vas_attach_response_tx;

    if ((id & VAS_ID_MARK) != VAS_ID_MARK) {
        mst->mst.err = VAS_ERR_NOT_FOUND;
        txq_send(&mst->mst);
        VAS_SERVICE_DEBUG("[request] attach: client=%p, not found vas=0x%016lx", client, id);
        return;
    }

    struct vas_info *vi = (struct vas_info *)(VAS_ID_MASK & id);
    if (vi->vas.id != (VAS_ID_MASK & id)) {
        mst->mst.err = VAS_ERR_NOT_FOUND;
        txq_send(&mst->mst);

        VAS_SERVICE_DEBUG("[request] attach: client=%p, map error %s\n", client,
                                  err_getstring(mst->mst.err));
        return;
    }

    struct vas_attached *ai = calloc(1, sizeof(struct vas_attached));
    if (!ai) {
        mst->mst.err = LIB_ERR_MALLOC_FAIL;
        txq_send(&mst->mst);
        return;
    }

    ai->vroot = vroot;
    ai->next = vi->attached;
    vi->attached = ai;

    mst->mst.err = vas_vspace_inherit_regions(&vi->vas, vroot,
                                              VAS_VSPACE_PML4_SLOT_MIN,
                                              VAS_VSPACE_PML4_SLOT_MAX);

    txq_send(&mst->mst);
}

static void vas_detach_call__rx(struct vas_binding *_binding, uint64_t id)
{
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] detach: client=%p, vas=0x%016lx\n", client, id);

    mst->mst.send =  vas_detach_response_tx;
    txq_send(&mst->mst);
}

static void vas_map_call__rx(struct vas_binding *_binding, uint64_t id,
                             struct capref frame, uint64_t offset, uint64_t size)
{
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] map: client=%p, vas=0x%016lx, size=0x%016lx\n",
                          client, id, size);

    mst->mst.send =  vas_map_response_tx;

    if ((id & VAS_ID_MARK) != VAS_ID_MARK) {
        mst->mst.err = VAS_ERR_NOT_FOUND;
        txq_send(&mst->mst);
        VAS_SERVICE_DEBUG("[request] map: client=%p, invalid vas=0x%016lx", client, id);
        return;
    }

    struct vas_info *vi = (struct vas_info *)(VAS_ID_MASK & id);
    if (vi->vas.id != (VAS_ID_MASK & id)) {
        mst->mst.err = VAS_ERR_NOT_FOUND;
        txq_send(&mst->mst);
        VAS_SERVICE_DEBUG("[request] map: client=%p, not found vas=0x%016lx", client, id);
        return;
    }

    void *addr;
    mst->mst.err = vas_vspace_map_one_frame(&vi->vas, &addr, frame, size);
    if (err_is_fail(mst->mst.err)) {
        txq_send(&mst->mst);
        VAS_SERVICE_DEBUG("[request] map: client=%p, map error %s\n", client,
                          err_getstring(mst->mst.err));
        return;
    }

#define X86_64_PML4_BASE(base)         (((uint64_t)(base) >> 39) & X86_64_PTABLE_MASK)

    uint32_t entry = X86_64_PML4_BASE(addr);
    uint32_t slot = entry / 64;
    uint32_t bit = entry % 64;

    if (!(vi->map[slot] & (1UL << bit))) {
        struct vas_attached *at = vi->attached;
        while(at) {
            mst->mst.err = vas_vspace_inherit_regions(&vi->vas, at->vroot, entry, entry);
            if (err_is_fail(mst->mst.err)) {
                USER_PANIC_ERR(mst->mst.err, "could not inherit region");
            }
            at = at->next;
        }
        vi->map[slot] |= (1UL << bit);
    }

    mst->map.vaddr = (lvaddr_t)addr;

    txq_send(&mst->mst);
}

static void vas_unmap_call__rx(struct vas_binding *_binding, uint64_t id,
                               uint64_t vaddr)
{
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] unmap: client=%p, bas=0x%016lx, vaddr=0x%016lx\n",
                          client, id, vaddr);

    mst->mst.send =  vas_unmap_response_tx;
    txq_send(&mst->mst);
}

static void vas_lookup_call__rx(struct vas_binding *_binding, uint8_t *name,
                                size_t size)
{
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] lookup: client=%p, name='%s'\n", client, (char *)name);

    mst->mst.send =  vas_lookup_response_tx;

    struct vas_info *vi = vas_info_lookup((char *)name);
    if (vi) {
        mst->lookup.id = VAS_ID_MARK | vi->vas.id;
    } else {
        mst->mst.err = VAS_ERR_NOT_FOUND;
    }

    txq_send(&mst->mst);
}


static struct vas_rx_vtbl rx_vtbl = {
    .create_call = vas_create_call__rx,
    .attach_call = vas_attach_call__rx,
    .detach_call = vas_detach_call__rx,
    .map_call = vas_map_call__rx,
    .unmap_call = vas_unmap_call__rx,
    .lookup_call = vas_lookup_call__rx
};

/*
 * ------------------------------------------------------------------------------
 * Connection management
 * ------------------------------------------------------------------------------
 */

static errval_t vas_connect_handler(void *st, struct vas_binding *binding)
{
    struct vas_client *client = calloc(1, sizeof(struct vas_client));
    if (client == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    VAS_SERVICE_DEBUG("[connect] new client %p\n", client);

    client->b = binding;
    txq_init(&client->txq, binding, get_default_waitset(),
             (txq_register_fn_t)binding->register_send, sizeof(struct vas_msg_st));

    binding->st = client;
    binding->rx_vtbl = rx_vtbl;

    return SYS_ERR_OK;
}

static void export_callback_fn(void *st, errval_t err, iref_t iref)
{
    VAS_SERVICE_DEBUG("[service] export callback: %s\n", err_getstring(err));

    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "export failed");
    }


#define VAS_SERVICE_NAME "vas"

    err = nameservice_register(VAS_SERVICE_NAME, iref);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "export failed");
    }

    VAS_SERVICE_DEBUG("[service] registered with name '%s'::%" PRIxIREF "]\n",
                      VAS_SERVICE_NAME, iref);
}


int main(int argc, char *argv[])
{
    errval_t err;

    VAS_SERVICE_DEBUG("[service] vas service started\n");

    err =  vas_export(NULL, export_callback_fn, vas_connect_handler,
                      get_default_waitset(), 0);

    while(1) {
        messages_wait_and_handle_next();
    }

    VAS_SERVICE_DEBUG("[service] vas service terminated\n");

    return 0;
}
