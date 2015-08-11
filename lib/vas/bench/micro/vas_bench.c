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
#define ITERATIONS_SYSCALL 10000
#define DRYRUNS_SYSCALL 100
#define TOTALRUNS_SYSCALL (ITERATIONS_SYSCALL + DRYRUNS_SYSCALL)
#define ITERATIONS 256ULL
#define DRYRUNS 5

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


static uint64_t micro_benchmarks(void) {

    vas_handle_t vas[ITERATIONS];
    errval_t r;



    cycles_t t_elapsed;

    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTALRUNS_SYSCALL);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS_SYSCALL);
        do {

            cycles_t t_start = bench_tsc();
            r = sys_nop();
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "nopsyscall");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "nop_syscall", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }

    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, TOTALRUNS_SYSCALL);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS_SYSCALL);
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
            EXPECT_SUCCESS(r, "creating vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_attach", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }


    {
        struct capref frame;
        r = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
        EXPECT_SUCCESS(r, "frame alloc");

        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 1, ITERATIONS);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS);
        do {
            void *addr;
            cycles_t t_start = bench_tsc();
            r = vas_map(vas[0], &addr, frame, BASE_PAGE_SIZE, VREGION_FLAGS_READ_WRITE);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping in  vas");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_map", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }


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
            r = vas_map(vas[1], &addr, frame, LARGE_PAGE_SIZE, VREGION_FLAGS_READ_WRITE | VREGION_FLAGS_LARGE);
            cycles_t t_end = bench_tsc();
            EXPECT_SUCCESS(r, "mapping");
            t_elapsed = bench_time_diff(t_start, t_end);
        } while(!bench_ctl_add_run(bench_ctl, &t_elapsed));

        bench_ctl_dump_analysis(bench_ctl, 0, "vas_map_large", bench_tsc_per_us());
        bench_ctl_destroy(bench_ctl);
    }


    {
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 2, TOTALRUNS_SYSCALL);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS_SYSCALL);
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
        bench_ctl_t *bench_ctl = bench_ctl_init(BENCH_MODE_FIXEDRUNS, 2, TOTALRUNS_SYSCALL);
        EXPECT_NONNULL(bench_ctl, "bench ctl was null");
        bench_ctl_dry_runs(bench_ctl, DRYRUNS_SYSCALL);
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

int main(int argc, char *argv[])
{

    bench_init();

    vas_enable();


    micro_benchmarks();

    return 0;
}
