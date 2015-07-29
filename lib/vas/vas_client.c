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
#include <if/vas_rpcclient_defs.h>
#include <flounder/flounder_txqueue.h>



#define MIN(a,b)        ((a) < (b) ? (a) : (b))


#define VAS_RPC_START
#define VAS_RPC_SIGNAL_DONE
#define VAS_RPC_WAIT_DONE

static struct vas_binding *vas_service_client =  NULL;

struct vas_rpc_client vas_srv_rpc;


/*
 * ---------------------------------------------------------------------------
 * RPC management
 * ---------------------------------------------------------------------------
 */



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

    err = vas_rpc_client_init(&vas_srv_rpc, _binding);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "RPC client init failed");
    }
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

    err = vas_bind(iref, vas_bind_continuation, NULL, get_default_waitset(),
                   IDC_BIND_FLAG_RPC_CAP_TRANSFER);
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


errval_t vas_client_vas_create(struct vas *vas)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    errval_t err, msgerr;

    uint64_t *nameptr = (uint64_t*)vas->name;
    err = vas_srv_rpc.vtbl.create(&vas_srv_rpc, nameptr[0], nameptr[1], nameptr[2], nameptr[3] ,
                                  &msgerr, &vas->id, &vas->tag);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}

errval_t vas_client_vas_lookup(char *name, struct vas *vas)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }
    errval_t err, msgerr;

    uint64_t *nameptr = (uint64_t*)vas->name;
    err = vas_srv_rpc.vtbl.lookup(&vas_srv_rpc, nameptr[0], nameptr[1], nameptr[2],
                                  nameptr[3], &msgerr, &vas->id, &vas->tag);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}

errval_t vas_client_vas_attach(vas_id_t id, struct capref vroot)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    errval_t err, msgerr;

    err = vas_srv_rpc.vtbl.attach(&vas_srv_rpc, id, vroot, &msgerr);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}

errval_t vas_client_vas_detach(vas_id_t id)
{
    errval_t err, msgerr;

    err = vas_srv_rpc.vtbl.detach(&vas_srv_rpc, id, &msgerr);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}

errval_t vas_client_seg_map(vas_id_t id, struct capref frame, size_t size,
                            vregion_flags_t flags, lvaddr_t *ret_vaddr)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }

    errval_t err, msgerr;

    err = vas_srv_rpc.vtbl.map(&vas_srv_rpc, id, frame, size, flags, &msgerr, ret_vaddr);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}

errval_t vas_client_seg_map_fixed(vas_id_t id, lvaddr_t vaddr, struct capref frame,
                                  size_t size, vregion_flags_t flags)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }
    errval_t err, msgerr;

    err = vas_srv_rpc.vtbl.map_fixed(&vas_srv_rpc, id, frame, size, vaddr, flags, &msgerr);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}

errval_t vas_client_seg_unmap(vas_id_t id, lvaddr_t vaddr)
{
    if (vas_service_client == NULL) {
        return VAS_ERR_SERVICE_NOT_ENABLED;
    }
    errval_t err, msgerr;

    err = vas_srv_rpc.vtbl.unmap(&vas_srv_rpc, id, vaddr, &msgerr);
    if (err_is_fail(err)) {
        return err;
    }

    return msgerr;
}


