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
#include <octopus/octopus.h>
#include <vas/vas.h>

#define VAS_IDENTIFIER "/vas/test/"

static struct capref frame, frame2, frame3;

static void alloc_frames(void)
{
    errval_t err;

    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    err = frame_alloc(&frame2, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    err = frame_alloc(&frame3, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }
}

int main(int argc, char *argv[])
{
    errval_t err;

    debug_printf("## VAS TEST STARTED\n");

    oct_init();
    vas_enable();

    size_t proc_id = 0;
    size_t proc_total = 1;
    if (argc > 1) {
        proc_total = atoi(argv[1]);
        proc_id = atoi(argv[2]);
    }


    debug_printf("### num processes: %lu / %lu\n", proc_id, proc_total);

    char* record = NULL;

    char name[32];
    snprintf(name, 32, VAS_IDENTIFIER "%lu", proc_id);

    vas_handle_t vas[proc_total];

    alloc_frames();

    debug_printf("[%lu] ### creating VAS\n", proc_id);

    err = vas_create(name, VAS_PERM_WRITE, &vas[proc_id]);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to create a new VAS");
    }

    err = oct_barrier_enter("vas_test_barrier", &record, proc_total);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "enterint barrier");
    }

    debug_printf("[%lu] ### proceed with lookup VAS\n", proc_id);

    for (size_t i = 0; i < proc_total; ++i) {
        if (i != proc_id) {
            snprintf(name, 32, VAS_IDENTIFIER "%lu", i);
            debug_printf("### lookup vas '%s'\n", name);
            err = vas_lookup(name, &vas[i]);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "could not lookup AS");
            }
        }

        debug_printf("[%lu] ### attaching vas[%lu]\n", proc_id, i);

        err = vas_attach(vas[i], 0);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "failed to attach");
        }
    }

    void *buf1, *buf2;
    debug_printf("[%lu] ### mapping frame vas[%lu]\n", proc_id, proc_id);
    err = vas_map(vas[proc_id], &buf1, frame, BASE_PAGE_SIZE);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }
    err = vas_map(vas[proc_id], &buf2, frame2, BASE_PAGE_SIZE);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }

    debug_printf("[%lu] ### buffer mapped: %p %p\n", proc_id, buf1, buf2);

    err = vspace_map_one_frame_fixed((lvaddr_t)buf1, BASE_PAGE_SIZE, frame3, NULL, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }

    memset(buf1, 0xAA, BASE_PAGE_SIZE);

    debug_printf("[%lu] ### switching to created vas\n", proc_id);
    err = vas_switch(vas[proc_id]);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    memset(buf1, proc_id + 1, BASE_PAGE_SIZE);
    memset(buf2, proc_id + 1, BASE_PAGE_SIZE);

    err = oct_barrier_leave(record);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "oct barrier leave failed");
    }

    debug_printf("switching back go original address space\n");
    err = vas_switch(NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to switch to original");
    }
    uint64_t *data = buf1;
    debug_printf("[%lu] orig: [%p] *data = %016lx\n", proc_id, data, *data);



    for (size_t i = 0; i < proc_total; ++i) {
        debug_printf("proc[%lu] vas[%lu]: switch to vas %p\n", proc_id, i, vas[i]);
        err = vas_switch(vas[i]);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "failed to attach");
        }
        debug_printf("proc[%lu] vas[%lu]: [%p] *data = %016lx\n", proc_id, i, data, *data);
    }

    err = vas_switch(NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to switch to original");
    }
    debug_printf("[%lu] orig: [%p] *data = %016lx\n", proc_id, data, *data);


    debug_printf("## VAS TEST TERMINATED\n");

    return 0;
}
