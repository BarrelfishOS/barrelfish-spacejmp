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

#define ITERATIONS 256ULL

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

static char *randstring(size_t length) {

    static char charset[] = "abcdefghijklmnopqrstuvwxyz";
    char *randomString = NULL;

    if (length) {
        randomString = malloc(sizeof(char) * (length +1));

        if (randomString) {
            for (size_t n = 0; n < length; n++) {
                size_t key = rand() % (sizeof(charset) -1);
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }

    return randomString;
}


static uint64_t micro_benchmarks(void) {

    vas_handle_t vas[ITERATIONS];
    errval_t r;

    static cycles_t overhead = 0;
    //// Loop overhead:
    {
        cycles_t start = bench_tsc();
        for (size_t iter=0; iter<ITERATIONS; iter++) {randstring(0);}
        cycles_t end = bench_tsc();
        overhead = (end - start) / ITERATIONS;
        printf("Loop overhead is = %lu\n", overhead);
    }


    /// Context switch (i.e. flush TLB) before access
    {
        char random_strings[ITERATIONS][10];
        for (size_t iter=0; iter<ITERATIONS; iter++) {
            char* str = randstring(7);
            strncpy(random_strings[iter], str, 10);
            free(str);
            //printf("%s:%s:%d: random_strings[iter] = %s\n",
            //       __FILE__, __FUNCTION__, __LINE__, random_strings[iter]);
        }

        cycles_t start = bench_tsc();
        for (size_t iter=0; iter<ITERATIONS; iter++) {
            r = vas_create(random_strings[iter], 0, &vas[iter]);
            assert(err_is_ok(r));
        }
        cycles_t end = bench_tsc();
        printf("Create VAS time = %llu\n", (end - start) / ITERATIONS);
    }

    {
        cycles_t start = bench_tsc();
        for (size_t iter=0; iter<ITERATIONS; iter++) {
            vas_attach(vas[iter], 0);
        }
        cycles_t end = bench_tsc();
        printf("vas_create (attach) syscall time = %llu\n", (end - start) / ITERATIONS);
    }

    /// Switching:
    printf("%s:%s:%d: \n", __FILE__, __FUNCTION__, __LINE__);
    {
        cycles_t start = bench_tsc();
        for (size_t iter=0; iter<ITERATIONS; iter++) {

            CHECK(r, "switch in", vas_switch(vas[iter]));
            CHECK(r, "switch back", vas_switch(NULL));
        }
        cycles_t end = bench_tsc();
        printf("vas_switch = %llu\n", (end - start) / ITERATIONS / 2);
    }


#if 0
    /// Swiching with tags:
    CHECK(r, "enable tagging", vas_tagging_enable());
    CHECK(r, "tag process", vas_tagging_tag(0));
    CHECK(r, "tag id1", vas_tagging_tag(id1));

    {
        cycles_t start = bench_tsc();
        for (size_t iter=0; iter<ITERATIONS; iter++) {

            CHECK(r, "switch in", vas_switch(id1));
            CHECK(r, "switch back", vas_switch(0));
        }
        cycles_t end = bench_tsc();
        printf("vas_switch with tags = %llu\n", (end - start) / ITERATIONS / 2);
    }


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

    return 0;

}


int main(int argc, char *argv[])
{

    bench_init();

    micro_benchmarks();

    return 0;
}
