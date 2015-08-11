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
#include <vas/vas_segment.h>
#include <bench/bench.h>

#define CHECK(v,str,expr) \
    do { \
        (v) = (expr); \
        if (err_is_fail(v)) { \
            USER_PANIC_ERR(v, "error: %s:\n", str); \
        } \
    } while (0)

#define EXPECT_SUCCESS(err, str) \
    if (err_is_fail(err)) {USER_PANIC_ERR(err, str);}

#define EXPECT_NONNULL(expr, str) \
    if (!expr) {USER_PANIC(str);}

//#define ITERATIONS 256ULL
#define ITERATIONS 512ULL
#define DRYRUNS 16
#define TOTAL_RUNS (ITERATIONS + DRYRUNS)


#define SEGMENT_SIZE_MIN_BITS 12
#define SEGMENT_SIZE_MAX_BITS 25


#define SEGMENT_SIZE (4UL * HUGE_PAGE_SIZE)

#if 0
static void shuffle(size_t *array, size_t n) {
    srand48(0xdeaf);

    if (n > 1) {
        size_t i;
        for (i = n - 1; i > 0; i--) {
            size_t j = (unsigned int) (drand48()*(i+1));
            int t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}
#endif


vas_handle_t vas[TOTAL_RUNS];
vas_seg_handle_t seg[TOTAL_RUNS];

static uint64_t micro_benchmarks(void) {


    errval_t r;



    cycles_t t_elapsed;

    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {

            cycles_t t_start = bench_tsc();
            r = vas_bench_cap_invoke_nop(VAS_HANDLE_PROCESS);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "cap_invoke", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }


    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        int iter = 0;
        do {
            char str[10];
            snprintf(str, 10, "/vas/%u", iter);
            cycles_t t_start = bench_tsc();
            r = vas_create(str, 0, &vas[iter++]);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));
        bench_ctl_dump_analysis(bench_ctl, 0, "vas_create", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        int iter = 0;
        do {
            cycles_t t_start = bench_tsc();
            r = vas_attach(vas[iter++], 0);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "vas attach");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_attach", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

    {
        struct capref frame;
        r = frame_alloc(&frame, SEGMENT_SIZE, NULL);
        EXPECT_SUCCESS(r, "frame alloc");

        int iter = 0;
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTAL_RUNS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");

        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {
            lvaddr_t addr = (VAS_SEG_VADDR_MIN + iter * SEGMENT_SIZE);
            char str[10];
            snprintf(str, 10, "/seg/%u", iter);
            cycles_t t_start = bench_tsc();

            r =  vas_seg_create(str, VAS_SEG_TYPE_FIXED, SEGMENT_SIZE,
                               addr, frame, VREGION_FLAGS_READ_WRITE,
                               &seg[iter++]);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping in  vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_seg_alloc", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

#if 0
    {
        struct capref frame;
        r = frame_alloc(&frame, LARGE_PAGE_SIZE, NULL);
        EXPECT_SUCCESS(r, "frame alloc");

        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {
            void *addr;
            cycles_t t_start = bench_tsc();
            r = vas_map(vas[0], &addr, frame, LARGE_PAGE_SIZE, VREGION_FLAGS_READ_WRITE);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping in  vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_map", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }
#endif


    {
        int iter = 0;
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTAL_RUNS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");

        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {
            cycles_t t_start = bench_tsc();
            r = vas_seg_attach(vas[0], seg[iter++], 0);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping in  vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_seg_attach", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 2, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        cycles_t t_elapsed2[2];
        do {
            cycles_t t_start = bench_tsc();
            r = vas_switch(vas[0]);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed2[0] = bench_time_diff(t_start, t_end);
            t_start = bench_tsc();
            r = vas_switch(VAS_HANDLE_PROCESS);
            t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed2[1] = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, t_elapsed2));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_switch(vas)", bench_tsc_per_us());
        bench_ctl_dump_analysis(bench_ctl, 1, "vas_switch(proc)", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }



    r = vas_tagging_enable();
    EXPECT_SUCCESS(r, "tagging enable");


    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 2, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        cycles_t t_elapsed2[2];
        do {
            cycles_t t_start = bench_tsc();
            r = vas_switch(vas[0]);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed2[0] = bench_time_diff(t_start, t_end);
            t_start = bench_tsc();
            r = vas_switch(VAS_HANDLE_PROCESS);
            t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed2[1] = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, t_elapsed2));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_tagged_switch(vas)", bench_tsc_per_us());
        bench_ctl_dump_analysis(bench_ctl, 1, "vas_tagged_switch(proc)", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }


#if 0
    /// Context switch (i.e. flush TLB) before access
    {
        cycles_t start = bench_tsc();
        for (size_t iter=0; iter<ITERATIONS; iter++) {
            vas_debug_reload_cr3();
        }
        cycles_t end = bench_tsc();
        printf("RELOAD CR3 syscall tagged = %llu\n", (end - start) / ITERATIONS);
    }
#endif
    //CHECK(r, "disable tags", vas_tagging_disable());

    printf("microbenchmarks done\n");

    return 0;

}



static uint64_t seg_attach_detach(void) {


    errval_t r;



    cycles_t t_elapsed;

    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTAL_RUNS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        int iter = 0;
        do {
            char str[10];
            snprintf(str, 10, "/vas/%u", iter);
            cycles_t t_start = bench_tsc();
            r = vas_create(str, 0, &vas[iter++]);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));
        bench_ctl_dump_analysis(bench_ctl, 0, "vas_create", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }


    struct capref frame;
    r = frame_alloc(&frame, (1UL << SEGMENT_SIZE_MAX_BITS), NULL);
    EXPECT_SUCCESS(r, "frame alloc");


    for (uint8_t bits = SEGMENT_SIZE_MIN_BITS; bits <= SEGMENT_SIZE_MAX_BITS; ++bits) {
        char str[10];
        snprintf(str, 10, "/seg/%u", bits);
        r =  vas_seg_create(str, VAS_SEG_TYPE_FIXED, (1UL << bits),
                            VAS_SEG_VADDR_MIN, frame, VREGION_FLAGS_READ_WRITE,
                            &seg[bits]);
        EXPECT_SUCCESS(r, "create segment");
    }

    r = vas_attach(vas[0], 0);
        EXPECT_SUCCESS(r, "creating vas");

    r = vas_switch(vas[0]);
    EXPECT_SUCCESS(r, "creating vas");

    for (uint8_t bits = SEGMENT_SIZE_MIN_BITS; bits <= SEGMENT_SIZE_MAX_BITS; ++bits) {
        bench_ctl_t *bench_ctl_m = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTAL_RUNS);
        bench_ctl_t *bench_ctl_u = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTAL_RUNS);

        EXPECT_NONNULL(bench_ctl_m, "bench ctl was null");
        EXPECT_NONNULL(bench_ctl_u, "bench ctl was null");

        bench_ctl_dry_runs(bench_ctl_m, DRYRUNS);
        bench_ctl_dry_runs(bench_ctl_u, DRYRUNS);
        do {
            cycles_t t_start = bench_tsc();
            r = vas_seg_attach(vas[0], seg[bits], 0);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping in  vas");
            t_elapsed = bench_time_diff(t_start, t_end);
            bench_ctl_add_run(bench_ctl_m, &t_elapsed);

            uint64_t *ptr = (uint64_t *)( VAS_SEG_VADDR_MIN + (1UL << (bits -1)));
            *ptr = 0xcafebabe;

            t_start = bench_tsc();
            r = vas_seg_detach(vas[0], seg[bits]);
            t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping in  vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl_u, &t_elapsed));

        char label[30];
        snprintf(label, 30, "seg_attach(%u)", bits);

        bench_ctl_dump_analysis(bench_ctl_m, 0, label, bench_tsc_per_us());
        snprintf(label, 30, "seg_detach(%u)", bits);
        bench_ctl_dump_analysis(bench_ctl_u, 0, label, bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl_m);
        bench_ctl_destroy(bench_ctl_u);
    }


    debug_printf("TSC per MS: %lu\n", bench_tsc_per_ms());


    r = vas_tagging_enable();
    EXPECT_SUCCESS(r, "tagging enable");

    //CHECK(r, "disable tags", vas_tagging_disable());

    printf("microbenchmarks done\n");

    return 0;

}

int main(int argc, char *argv[])
{

    bench_init();

    vas_enable();


    if (0) {
        micro_benchmarks();
    }
    seg_attach_detach();

    while(1)
        ;

    return 0;
}
