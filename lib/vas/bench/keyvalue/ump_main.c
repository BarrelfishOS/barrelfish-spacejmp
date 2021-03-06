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

#include <barrelfish/ump_impl.h>

static char my_name[100];

struct bench_binding *array[MAX_CPUS] = {NULL};
coreid_t my_core_id;

static void ump_init_msg(struct bench_binding *b, coreid_t id)
{
    static int count = 0;

    array[count] = b;
    experiment(count);

    count++;

    printf("client done\n");
}

static struct bench_rx_vtbl rx_vtbl = {
    .ump_init_msg   = ump_init_msg,
};



static void bind_cb(void *st, errval_t binderr, struct bench_binding *b)
{
    errval_t err;
    err = b->tx_vtbl.ump_init_msg(b, NOP_CONT, my_core_id);
    assert(err_is_ok(err));

    void *buf = malloc(LARGE_PAGE_SIZE);

    struct bench_ump_binding *bu = (struct bench_ump_binding*)b;
    struct flounder_ump_state *fus = &bu->ump_state;
    struct ump_chan *chan = &fus->chan;

    struct ump_chan_state *send = &chan->send_chan;
    struct ump_chan_state *recv = &chan->endpoint.chan;

    /* Wait for and reply to msgs */
    while (1) {
        volatile struct ump_message *msg;
        struct ump_control ctrl;
        while (!(msg = ump_impl_recv(recv)));

        uint64_t reqsize = msg->data[0];
        uint64_t sent = 0;
        while(sent < reqsize) {
            while(!(msg = ump_impl_get_next(send, &ctrl)));
            if ((reqsize - sent) < sizeof(msg->data)) {
                memcpy((void *)msg->data, buf + sent, reqsize - sent);
            } else {
                memcpy((void *)msg->data, buf + sent, UMP_MSG_BYTES);
            }
            sent += UMP_MSG_BYTES;
            msg->header.control = ctrl;
        }
    }
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
    strcpy(my_name, argv[0]);

    bench_init();

    if (argc == 1) { /* bsp core */
        printf("BSP core... starting server\n");
        /* 1. spawn domains,
           2. setup a server,
           3. wait for all clients to connect,
           4. run experiments
        */
        // Spawn domains
        char *xargv[] = {"benchmarks/kv_ump_lat", "dummy", NULL};
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
