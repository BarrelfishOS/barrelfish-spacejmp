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
#include <bench/bench.h>
#include <vas_internal.h>
#include <vas_vspace.h>

#include <if/vas_defs.h>
#include <flounder/flounder_txqueue.h>

#ifdef NDEBUG
#define VAS_SERVICE_DEBUG(x...)
#else
#define VAS_SERVICE_DEBUG(x...) debug_printf(x);
#endif

#define VAS_SERVICE_USE_RPC 1

#define VAS_SERVICE_BENCH 0

#define VAS_SERVICE_USE_RPC 1

#if VAS_SERVICE_BENCH
#define VAS_BENCH_START(varname) \
    cycles_t varname = bench_tsc();

#define VAS_BENCH_END(varname) \
    varname = bench_tsc() - varname;

#define STRINGIFY2( x) #x
#define STRINGIFY(x) STRINGIFY2(x)
#define VAS_BENCH_PRINT(__varname) \
    debug_printf("[bench] " STRINGIFY(__varname) " %" PRIuCYCLES " cycles\n", __varname);
#else
#define VAS_BENCH_START(varname)
#define VAS_BENCH_END(varname)
#define VAS_BENCH_PRINT(varname)
#endif


#define VAS_ATTACHED_MAX 16

#define MIN(a,b)        ((a) < (b) ? (a) : (b))

#define EXPECT_SUCCESS(expr, msg...) \
    if (!(expr)) {USER_PANIC("expr" msg);}

union vas_name_arg {
    char namestring[32];
    uint64_t namefields[4];
};

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
 * id check helpers
 * ------------------------------------------------------------------------------
 */
static inline errval_t vas_verify_id(uint64_t id, struct vas_info **ret_vi)
{
    if ((id & VAS_ID_MARK) != VAS_ID_MARK) {
        return VAS_ERR_NOT_FOUND;
    }

    struct vas_info *vi = (struct vas_info *)(VAS_ID_MASK & id);
    if (vi->vas.id != (VAS_ID_MASK & id)) {
        return VAS_ERR_NOT_FOUND;
    }

    *ret_vi = vi;

    return SYS_ERR_OK;
}


/*
 * ------------------------------------------------------------------------------
 * Send handlers
 * ------------------------------------------------------------------------------
 */

#ifndef VAS_SERVICE_USE_RPC

struct vas_msg_st
{
    struct txq_msg_st mst;
    union {
        struct {
            uint64_t id;
            uint16_t tag;
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
            uint16_t tag;
        } lookup;
    };
};

static errval_t vas_create_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_create_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                   vst->create.id, vst->create.tag);
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

static errval_t vas_map_fixed_response_tx(struct txq_msg_st *st)
{
    return vas_map_fixed_response__tx(st->queue->binding, TXQCONT(st), st->err);
}

static errval_t vas_unmap_response_tx(struct txq_msg_st *st)
{
    return vas_unmap_response__tx(st->queue->binding, TXQCONT(st), st->err);
}

static errval_t vas_lookup_response_tx(struct txq_msg_st *st)
{
    struct vas_msg_st *vst = (struct vas_msg_st *)st;
    return vas_lookup_response__tx(st->queue->binding, TXQCONT(st), st->err,
                                   vst->lookup.id, vst->lookup.tag);
}

#endif
/*
 * ------------------------------------------------------------------------------
 * Receive handlers
 * ------------------------------------------------------------------------------
 */

static void vas_create_call__rx(struct vas_binding *_binding, uint64_t name0,
                                uint64_t name1, uint64_t name2, uint64_t name3)
{
    VAS_BENCH_START(vas_create_total);

#ifdef VAS_SERVICE_USE_RPC
    errval_t err;

    struct vas_info *vi = calloc(1, sizeof(struct vas_info));
    if (!vi) {
        err = _binding->tx_vtbl.create_response(_binding, NOP_CONT,
                                                LIB_ERR_MALLOC_FAIL, 0, 0);
    } else {
        uint64_t *nameptr = (uint64_t *)vi->vas.name;
        nameptr[0] = name0;
        nameptr[1] = name1;
        nameptr[2] = name2;
        nameptr[3] = name3;

        vi->vas.id = (uint64_t)vi;

        err = vas_vspace_init(&vi->vas);
        if (err_is_fail(err)) {
            err = _binding->tx_vtbl.create_response(_binding, NOP_CONT,err, 0,0);
            free(vi);
        } else {
            vas_info_insert(vi);
            err = _binding->tx_vtbl.create_response(_binding, NOP_CONT,err,
                                                    VAS_ID_MARK | vi->vas.id,
                                                    vi->vas.tag);
        }
    }

    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "should not happend...");
    }

#else
    struct vas_client *client = _binding->st;

    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    union vas_name_arg narg = { .namefields = {name0, name1, name2, name3}};

    VAS_SERVICE_DEBUG("[request] create: client=%p, name='%s'\n", client,
                      narg.namestring);

    mst->mst.send =  vas_create_response_tx;

    struct vas_info *vi = calloc(1, sizeof(struct vas_info));
    if (vi == NULL) {
        mst->mst.err = LIB_ERR_MALLOC_FAIL;
    } else {
        vi->vas.id = (uint64_t)vi;
        strncpy(vi->vas.name, (char *)narg.namestring, 32);
        mst->mst.err = vas_vspace_init(&vi->vas);
        mst->create.id = VAS_ID_MARK | (uint64_t)vi;
        mst->create.tag = vi->vas.tag;
        vas_info_insert(vi);
    }

    txq_send(&mst->mst);
#endif
    VAS_BENCH_END(vas_create_total);
    VAS_BENCH_PRINT(vas_create_total);
}

static void vas_delete_call__rx(struct vas_binding *_binding, uint64_t sid)
{

}

static void vas_attach_call__rx(struct vas_binding *_binding, uint64_t id,
                                struct capref vroot)
{
    errval_t err;

    VAS_BENCH_START(vas_attach_total);

#ifndef VAS_SERVICE_USE_RPC
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
        EXPECT_SUCCESS(mst, "could not allocate msg st");
#endif

    VAS_SERVICE_DEBUG("[request] attach: client=%p, vas=0x%016lx\n", _binding->st, id);

    struct vas_info *vi;
    err = vas_verify_id(id, &vi);

    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] attach: client=%p, vas=0x%016lx, err='%s'\n",
                          _binding->st, id, err_getstring(err));
#ifdef VAS_SERVICE_USE_RPC
        err = _binding->tx_vtbl.attach_response(_binding, NOP_CONT, err);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "send reply");
            cap_delete(vroot);
        }
#else
        mst->mst.err = err;
        txq_send(&mst->mst);
#endif
        return;
    }

    struct vas_attached *ai = calloc(1, sizeof(struct vas_attached));
    if (!ai) {
#ifdef VAS_SERVICE_USE_RPC
        err = _binding->tx_vtbl.attach_response(_binding, NOP_CONT, LIB_ERR_MALLOC_FAIL);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "send reply");
            cap_delete(vroot);
        }
#else
        mst->mst.err = LIB_ERR_MALLOC_FAIL;
        txq_send(&mst->mst);
#endif
        return;
    }

    ai->vroot = vroot;
    ai->next = vi->attached;
    vi->attached = ai;

    VAS_BENCH_START(vas_attach_inherit);
    err = vas_vspace_inherit_regions(&vi->vas, vroot,
                                     VAS_VSPACE_PML4_SLOT_MIN,
                                     VAS_VSPACE_PML4_SLOT_MAX);
    VAS_BENCH_END(vas_attach_inherit);

#ifdef VAS_SERVICE_USE_RPC
    err = _binding->tx_vtbl.attach_response(_binding, NOP_CONT, err);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "send reply");
        cap_delete(vroot);
    }
#else
    mst->mst.err = err;
    txq_send(&mst->mst);
#endif
    VAS_BENCH_END(vas_attach_total);
    VAS_BENCH_PRINT(vas_attach_total);
    VAS_BENCH_PRINT(vas_attach_inherit);
}

static void vas_detach_call__rx(struct vas_binding *_binding, uint64_t id)
{

#ifndef VAS_SERVICE_USE_RPC
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");
    mst->mst.send =  vas_detach_response_tx;
#endif



#ifdef VAS_SERVICE_USE_RPC

#else


    struct vas_info *vi;
    mst->mst.err = vas_verify_id(id, &vi);
    if (err_is_fail(mst->mst.err)) {
        VAS_SERVICE_DEBUG("[request] detach: client=%p, vas=0x%016lx, err='%s'\n",
                          client, id, err_getstring(mst->mst.err));
        txq_send(&mst->mst);
        return;
    }

    VAS_SERVICE_DEBUG("[request] detach: client=%p, vas=0x%016lx\n", client, id);


    txq_send(&mst->mst);
#endif
}

static void vas_map_call__rx(struct vas_binding *_binding, uint64_t id,
                             struct capref frame, uint64_t size, uint32_t flags)
{
    struct vas_info *vi = NULL;
    errval_t err;

    VAS_BENCH_START(vas_map_total);

    VAS_SERVICE_DEBUG("[request] map: client=%p, vas=0x%016lx, size=0x%016lx\n",
                      _binding->st, id, size);

    err = vas_verify_id(id, &vi);

#ifndef VAS_SERVICE_USE_RPC
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");
    mst->mst.send =  vas_map_response_tx;
    mst->mst.err err;
    if (err_is_fail(mst->mst.err)) {
        VAS_SERVICE_DEBUG("[request] map: client=%p, vas=0x%016lx, err='%s'\n",
                          _binding->st, id, err_getstring(mst->mst.err));
        txq_send(&mst->mst);
        return;
    }
#else
    if (err_is_fail(err)) {
        err = _binding->tx_vtbl.map_response(_binding, NOP_CONT, err, 0);
        EXPECT_SUCCESS(err_is_ok(err), "sending reply");
        cap_delete(frame);
    }
#endif

    VAS_BENCH_START(vas_map_map);
    void *addr;
    err = vas_vspace_map_one_frame(&vi->vas, &addr, frame, size, flags);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] map: client=%p, map error %s\n", _binding->st,
                                  err_getstring(err));
#ifdef VAS_SERVICE_USE_RPC
        err = _binding->tx_vtbl.map_response(_binding, NOP_CONT, err, 0);
        EXPECT_SUCCESS(err_is_ok(err), "sending reply");
        cap_delete(frame);
#else
        txq_send(&mst->mst);

#endif
        return;
    }
    VAS_BENCH_END(vas_map_map)


#define X86_64_PML4_BASE(base)         (((uint64_t)(base) >> 39) & X86_64_PTABLE_MASK)

    uint32_t entry = X86_64_PML4_BASE(addr);
    uint32_t slot = entry / 64;
    uint32_t bit = entry % 64;

    VAS_BENCH_START(vas_map_inherit);
    if (!(vi->map[slot] & (1UL << bit))) {
        struct vas_attached *at = vi->attached;
        while(at) {
            err = vas_vspace_inherit_regions(&vi->vas, at->vroot, entry, entry);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "could not inherit region");
            }
            at = at->next;
        }
        vi->map[slot] |= (1UL << bit);
    }
    VAS_BENCH_END(vas_map_inherit);

#ifdef VAS_SERVICE_USE_RPC
    err = _binding->tx_vtbl.map_response(_binding, NOP_CONT, err, (lvaddr_t)addr);
    EXPECT_SUCCESS(err_is_ok(err), "sending reply");
#else
    mst->map.vaddr = (lvaddr_t)addr;

    txq_send(&mst->mst);
    VAS_BENCH_END(vas_map_total);

    VAS_BENCH_PRINT(vas_map_total);
    VAS_BENCH_PRINT(vas_map_map);
    VAS_BENCH_PRINT(vas_map_inherit);

#endif
}

static void vas_map_fixed_call__rx(struct vas_binding *_binding, uint64_t id,
                                   struct capref frame, uint64_t size, uint64_t vaddr,
                                   uint32_t flags)
{
#ifdef VAS_SERVICE_USE_RPC

#else
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] map: client=%p, vas=0x%016lx, size=0x%016lx\n",
                          client, id, size);

    mst->mst.send =  vas_map_fixed_response_tx;

    struct vas_info *vi;
    mst->mst.err = vas_verify_id(id, &vi);
    if (err_is_fail(mst->mst.err)) {
        VAS_SERVICE_DEBUG("[request] map_fixed: client=%p, vas=0x%016lx, err='%s'\n",
                          client, id, err_getstring(mst->mst.err));
        txq_send(&mst->mst);
        return;
    }

    mst->mst.err = vas_vspace_map_one_frame_fixed(&vi->vas, vaddr, frame, size, flags);
    if (err_is_fail(mst->mst.err)) {
        VAS_SERVICE_DEBUG("[request] map: client=%p, map error %s\n", client,
                          err_getstring(mst->mst.err));
        txq_send(&mst->mst);
        return;
    }

#define X86_64_PML4_BASE(base)         (((uint64_t)(base) >> 39) & X86_64_PTABLE_MASK)

    uint32_t entry = X86_64_PML4_BASE(vaddr);
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

    txq_send(&mst->mst);
#endif
}


static void vas_unmap_call__rx(struct vas_binding *_binding, uint64_t id,
                               uint64_t vaddr)
{
#ifdef VAS_SERVICE_USE_RPC

#else
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    VAS_SERVICE_DEBUG("[request] unmap: client=%p, bas=0x%016lx, vaddr=0x%016lx\n",
                          client, id, vaddr);
    mst->mst.send =  vas_unmap_response_tx;

    struct vas_info *vi;
    mst->mst.err = vas_verify_id(id, &vi);
    if (err_is_fail(mst->mst.err)) {
        VAS_SERVICE_DEBUG("[request] attach: client=%p, vas=0x%016lx, err='%s'\n",
                          client, id, err_getstring(mst->mst.err));
        txq_send(&mst->mst);
        return;
    }


    txq_send(&mst->mst);
#endif
}

static void vas_lookup_call__rx(struct vas_binding *_binding, uint64_t name0,
                                uint64_t name1, uint64_t name2, uint64_t name3)
{
    union vas_name_arg narg = { .namefields = {name0, name1, name2, name3}};

    VAS_SERVICE_DEBUG("[request] lookup: client=%p, name='%s'\n", _binding->st, narg.namestring);

    struct vas_info *vi = vas_info_lookup(narg.namestring);

#ifdef VAS_SERVICE_USE_RPC
    if (vi) {
        _binding->tx_vtbl.lookup_response(_binding, NOP_CONT, VAS_ERR_NOT_FOUND,
                                          VAS_ID_MARK | vi->vas.id, vi->vas.tag);
    } else {
        _binding->tx_vtbl.lookup_response(_binding, NOP_CONT, VAS_ERR_NOT_FOUND, 0, 0);
    }

#else
    struct vas_client *client = _binding->st;
    struct vas_msg_st *mst = (struct vas_msg_st*)txq_msg_st_alloc(&client->txq);
    EXPECT_SUCCESS(mst, "could not allocate msg st");

    mst->mst.send =  vas_lookup_response_tx;

    if (vi) {
        mst->lookup.id = VAS_ID_MARK | vi->vas.id;
    } else {
        mst->mst.err = VAS_ERR_NOT_FOUND;
    }

    txq_send(&mst->mst);
#endif
}

static void vas_seg_create_call__rx(struct vas_binding *_binding, uint64_t name0,
                                    uint64_t name1, uint64_t name2, uint64_t name3,
                                    uint64_t vaddr, uint64_t size, struct capref frame)
{

}

static void vas_seg_delete_call__rx(struct vas_binding *_binding, uint64_t sid)
{

}

static void vas_seg_lookup_call__rx(struct vas_binding *_binding, uint64_t name0,
                                    uint64_t name1, uint64_t name2, uint64_t name3)
{

}

static void vas_seg_attach_call__rx(struct vas_binding *_binding, uint64_t vid,
                                    uint64_t sid, uint32_t flags)
{

}

static void vas_seg_detach_call__rx(struct vas_binding *_binding, uint64_t vid,
                                    uint64_t sid)
{

}


static struct vas_rx_vtbl rx_vtbl = {
    .create_call = vas_create_call__rx,
    .delete_call = vas_delete_call__rx,
    .attach_call = vas_attach_call__rx,
    .detach_call = vas_detach_call__rx,
    .map_call = vas_map_call__rx,
    .map_fixed_call = vas_map_fixed_call__rx,
    .unmap_call = vas_unmap_call__rx,
    .lookup_call = vas_lookup_call__rx,

    .seg_create_call = vas_seg_create_call__rx,
    .seg_delete_call = vas_seg_delete_call__rx,
    .seg_attach_call = vas_seg_attach_call__rx,
    .seg_detach_call = vas_seg_detach_call__rx,
    .seg_lookup_call = vas_seg_lookup_call__rx
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
#ifndef VAS_SERVICE_USE_RPC
    txq_init(&client->txq, binding, get_default_waitset(),
             (txq_register_fn_t)binding->register_send, sizeof(struct vas_msg_st));
#endif
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

#if VAS_SERVICE_BENCH
    bench_init();
#endif

    err =  vas_export(NULL, export_callback_fn, vas_connect_handler,
                      get_default_waitset(), 0);

    while(1) {
        messages_wait_and_handle_next();
    }

    VAS_SERVICE_DEBUG("[service] vas service terminated\n");

    return 0;
}
