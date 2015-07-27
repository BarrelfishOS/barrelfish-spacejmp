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

#define ITERATIONS 256ULL


static uint64_t request_benchmark(void)
{
    errval_t err;
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

    cycles_t start = bench_tsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        err = vas_switchm(vas, &prev);
        *((uint32_t *)buf) = *((uint32_t *)addr);
        err = vas_switch(prev);
    }
    cycles_t end = bench_tsc();

    printf("get(4): %llu cycles (avg)\n", (end - start) / ITERATIONS);

    start = bench_tsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        err = vas_switchm(vas, &prev);
        *((uint64_t *)buf) = *((uint64_t *)addr);
        err = vas_switch(prev);
    }
    end = bench_tsc();

    printf("get(8): %llu cycles (avg)\n", (end - start) / ITERATIONS);

    for (int sz = 16; sz <= LARGE_PAGE_SIZE; sz <<=1) {
        start = bench_tsc();
        for (int i = 0; i < ITERATIONS; ++i) {
            err = vas_switchm(vas, &prev);
            memcpy(buf, addr, sz);
            err = vas_switch(prev);
        }
        end = bench_tsc();
        printf("get(%u): %llu cycles (avg)\n", sz, (end - start) / ITERATIONS);
    }

    err = vas_tagging_enable();

    start = bench_tsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        err = vas_switchm(vas, &prev);
        *((uint32_t *)buf) = *((uint32_t *)addr);
        err = vas_switch(prev);
    }
    end = bench_tsc();

    printf("tag get(4): %llu cycles (avg)\n", (end - start) / ITERATIONS);

    start = bench_tsc();
    for (int i = 0; i < ITERATIONS; ++i) {
        err = vas_switchm(vas, &prev);
        *((uint64_t *)buf) = *((uint64_t *)addr);
        err = vas_switch(prev);
    }
    end = bench_tsc();

    printf("tag get(8): %llu cycles (avg)\n", (end - start) / ITERATIONS);

    for (int sz = 16; sz <= LARGE_PAGE_SIZE; sz <<=1) {
        start = bench_tsc();
        for (int i = 0; i < ITERATIONS; ++i) {
            err = vas_switchm(vas, &prev);
            memcpy(buf, addr, sz);
            err = vas_switch(prev);
        }
        end = bench_tsc();
        printf("tag get(%u): %llu cycles (avg)\n", sz, (end - start) / ITERATIONS);
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
