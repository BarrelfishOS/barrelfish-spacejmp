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

#include "ump_bench.h"

#define MAX_COUNT 1000
static struct timestamps *timestamps;

static void *buf;

static void run(struct ump_chan_state *send, struct ump_chan_state *recv,
                uint64_t msglen) {

    for (int i = 0; i < MAX_COUNT; i++) {
        uint64_t received = 0;
        void *buf_run = buf;
        volatile struct ump_message *msg;
        struct ump_control ctrl;
        timestamps[i].time0 = bench_tsc();
        msg = ump_impl_get_next(send, &ctrl);
        msg->data[0] = msglen;
        msg->header.control = ctrl;
        while(received < msglen) {
            while (!(msg = ump_impl_recv(recv)));
            if ((msglen - received) < sizeof(msg->data)) {
                memcpy(buf_run + received, (void *)msg->data, msglen - received);
            } else {
                memcpy(buf_run + received, (void *)msg->data, UMP_MSG_BYTES);
            }
            received += UMP_MSG_BYTES;
        }

        timestamps[i].time1 = bench_tsc();
    }
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
        run(send, recv, sz);
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
    free(buf);

}
