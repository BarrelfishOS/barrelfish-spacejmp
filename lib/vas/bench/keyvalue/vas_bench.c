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
#include <vas/vas.h>
#include <bench/bench.h>


#define MAX_COUNT 1000
#define DRYRUNS 32
#define EXPECT_SUCCESS(err, str) \
    if (err_is_fail(err)) {USER_PANIC_ERR(err, str);}

#define EXPECT_NONNULL(expr, str) \
    if (!expr) {USER_PANIC(str);}

static uint64_t request_benchmark(void)
{
    errval_t err, err2;
    vas_handle_t vas;

    struct capref frame;
    err = frame_alloc(&frame, LARGE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "frame alloc failed");
    }

    err = vas_create("server://vas", 0, &vas);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "create failed");
    }

    void *addr;
    err = vas_map(vas, &addr, frame, LARGE_PAGE_SIZE, VREGION_FLAGS_READ_WRITE);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "map failed");
    }

    err = vas_attach(vas, 0);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "attach failed");
    }

    void *buf = malloc(LARGE_PAGE_SIZE);

    // starting benchmarks
    printf("get requests\n");
    vas_handle_t prev;


    for (int sz = 4; sz <= LARGE_PAGE_SIZE; sz <<=1) {
        cycles_t t_elapsed;

        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, MAX_COUNT);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {
            cycles_t t_start = bench_tsc();
            err = vas_switchm(vas, &prev);
            memcpy(buf, addr, sz);
            err2 = vas_switch(prev);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(err, "vas_switchm");
            EXPECT_SUCCESS(err2, "vas_switchm");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        char label[32];
        snprintf(label, 32, "get_mvas(%u)", sz);

        bench_ctl_dump_analysis(bench_ctl, 0, label, bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

    err = vas_tagging_enable();



    for (int sz = 4; sz <= LARGE_PAGE_SIZE; sz <<=1) {
        cycles_t t_elapsed;


        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, MAX_COUNT);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {
            cycles_t t_start = bench_tsc();
            err = vas_switchm(vas, &prev);
            memcpy(buf, addr, sz);
            err2 = vas_switch(prev);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(err, "vas_switchm");
            EXPECT_SUCCESS(err2, "vas_switchm");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        char label[32];
        snprintf(label, 32, "get_mvas_tag(%u)", sz);

        bench_ctl_dump_analysis(bench_ctl, 0, label, bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

    return 0;
}

int main(int argc, char *argv[])
{

    bench_init();

    vas_enable();

    request_benchmark();


    return 0;
}
