/**
 * \file
 * \brief
 */

/*
 * Copyright (c) 2007, 2008, 2009, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <barrelfish/ump_impl.h>
#include <bench/bench.h>
#include "ump_bench.h"

#define MAX_COUNT 256
#define DRYRUNS 32

static struct timestamps *timestamps;

#define EXPECT_SUCCESS(err, str) \
    if (err_is_fail(err)) {USER_PANIC_ERR(err, str);}

#define EXPECT_NONNULL(expr, str) \
    if (!expr) {USER_PANIC(str);}

static void *buf;

static void run(struct ump_chan_state *send, struct ump_chan_state *recv,
                uint64_t msglen, coreid_t core) {

    cycles_t t_elapsed;


    bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, MAX_COUNT);
    EXPECT_NONNULL(bench_ctl, "bench ctl was null");
    bench_ctl_dry_runs(bench_ctl, DRYRUNS);
    do {

        uint64_t received = 0;
        void *buf_run = buf;
        volatile struct ump_message *msg;
        struct ump_control ctrl;

        cycles_t t_start = bench_tsc();
        msg = ump_impl_get_next(send, &ctrl);
        msg->data[0] = msglen;
        msg->header.control = ctrl;
        while(received < msglen) {
            do{
                msg = ump_impl_recv(recv);
            } while (!msg);

            if (msglen < UMP_MSG_BYTES) {
                memcpy(buf_run + received, (void *)msg->data, msglen);
            }else{
                memcpy(buf_run + received, (void *)msg->data, UMP_MSG_BYTES);
            }
            received += UMP_MSG_BYTES;
        }
        cycles_t t_end = bench_tsc();
        t_elapsed = bench_time_diff(t_start, t_end);
    } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

    char label[32];
    snprintf(label, 32, "%u: get_ump(%lu)", core, msglen);

    bench_ctl_dump_analysis(bench_ctl, 0, label, bench_tsc_per_us());
    bench_ctl_destroy(bench_ctl);
}

void experiment(coreid_t idx)
{
    timestamps = malloc(sizeof(struct timestamps) * MAX_COUNT);
    assert(timestamps != NULL);

    struct bench_ump_binding *bu = (struct bench_ump_binding*)array[idx];
    struct flounder_ump_state *fus = &bu->ump_state;
    struct ump_chan *chan = &fus->chan;

    struct ump_chan_state *send = &chan->send_chan;
    struct ump_chan_state *recv = &chan->endpoint.chan;

    printf("Running latency between core %"PRIuCOREID" and core %"PRIuCOREID"\n", my_core_id, idx);

    buf = malloc(LARGE_PAGE_SIZE + 1);

    /* Run experiment */
    for (int sz = 4; sz <= LARGE_PAGE_SIZE; sz <<= 1) {
        run(send, recv, sz, idx);
    }
    free(buf);

}
