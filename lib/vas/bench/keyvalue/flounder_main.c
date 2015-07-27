/**
 * \file
 * \brief UMP benchmarks
 */

/*
 * Copyright (c) 2007, 2008, 2009, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include "ump_bench.h"
#include <string.h>
#include <barrelfish/barrelfish.h>
#include <barrelfish/nameservice_client.h>
#include <barrelfish/spawn_client.h>
#include <if/bench_defs.h>
#include <if/bench_rpcclient_defs.h>
#include <barrelfish/ump_impl.h>


#define MAX_COUNT 1000

static struct timestamps *timestamps;

coreid_t my_core_id;

void *buf;

static void get_call_rx(struct bench_binding *b, uint64_t key)
{
    errval_t err;

    // struct event_closure _continuation
    err = b->tx_vtbl.get_response(b, NOP_CONT, (char *)buf, key);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to send response");
    }
}

static struct bench_rx_vtbl rx_vtbl = {
    .get_call = get_call_rx
};



static void bind_cb(void *st, errval_t binderr, struct bench_binding *b)
{
    errval_t err;

    struct bench_rpc_client rpc_b;
    err = bench_rpc_client_init(&rpc_b,b);
    assert(err_is_ok(err));

    timestamps = malloc(sizeof(struct timestamps) * MAX_COUNT);


    for (int sz = 4; sz <= LARGE_PAGE_SIZE; sz <<= 1) {
        for (int i = 0; i < MAX_COUNT; i++) {
            timestamps[i].time0 = bench_tsc();
            uint64_t length;
            void *inbuf;
            rpc_b.vtbl.get(&rpc_b, sz, (char **)&inbuf, &length);
            timestamps[i].time1 = bench_tsc();
            free(inbuf);
        }
        cycles_t elapsed = 0;
        uint32_t count = 0;
        for (int i = MAX_COUNT / 10; i < MAX_COUNT; i++) {
            if (timestamps[i].time1 > timestamps[i].time0) {
                elapsed += timestamps[i].time1 - timestamps[i].time0;
                count++;
            }
        }
        printf("get(%i): %" PRIuCYCLES" cycles (avg)\n", sz, elapsed / count);
    }

    printf("done.\n");


}

static void export_cb(void *st, errval_t err, iref_t iref)
{
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "export failed");
        abort();
    }

    // register this iref with the name service
    err = nameservice_register("ump_server", iref);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "nameservice_register failed");
        abort();
    }

    printf("BSP core... ready\n");
}

static errval_t connect_cb(void *st, struct bench_binding *b)
{
    printf("BSP core... new client\n");

    // copy my message receive handler vtable to the binding
    b->rx_vtbl = rx_vtbl;

    // accept the connection
    return SYS_ERR_OK;
}

int main(int argc, char *argv[])
{
    errval_t err;

    /* Set my core id */
    my_core_id = disp_get_core_id();

    buf = malloc(LARGE_PAGE_SIZE);

    bench_init();

    if (argc == 1) { /* bsp core */
        printf("BSP core... starting server\n");
        /* 1. spawn domains,
           2. setup a server,
           3. wait for all clients to connect,
           4. run experiments
        */
        // Spawn domains
        char *xargv[] = {"benchmarks/kv_fl_lat", "dummy", NULL};
        //err = spawn_program_on_all_cores(false, xargv[0], xargv, NULL,
        //                                 SPAWN_FLAGS_DEFAULT, NULL, &num_cores);
        err = spawn_program(my_core_id + 2, xargv[0], xargv, NULL,0, NULL);

        assert(err_is_ok(err));

        printf("BSP core... export\n");

        /* Setup a server */
        err = bench_export(NULL, export_cb, connect_cb, get_default_waitset(),
                           IDC_BIND_FLAGS_DEFAULT);
        assert(err_is_ok(err));
    } else {
        /* Connect to the server */
        iref_t iref;

        err = nameservice_blocking_lookup("ump_server", &iref);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "nameservice_blocking_lookup failed");
            abort();
        }

        err = bench_bind(iref, bind_cb, NULL,
                         get_default_waitset(), IDC_BIND_FLAGS_DEFAULT);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "bind failed");
            abort();
        }
    }

    messages_handler_loop();
}
