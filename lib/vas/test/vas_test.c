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
#include <vas/vas_segment.h>

#define VAS_IDENTIFIER "/vas/test/"

static struct capref frame, frame2, frame3;

static void alloc_frames(void)
{
    errval_t err;

    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    err = frame_alloc(&frame2, LARGE_PAGE_SIZE, NULL);
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

    vas_tagging_enable();

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

    err = vas_create(name, VAS_FLAGS_PERM_WRITE, &vas[proc_id]);
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


    /* segment test */
    debug_printf("## VAS SEGMENT TEST\n");

#define VAS_TEST_NUM_SEG 4

#define VAS_VSPACE_PML4_SLOTS 256UL
#define VAS_VSPACE_PML4_SLOT_MAX 500UL
#define VAS_VSPACE_PML4_SLOT_MIN (VAS_VSPACE_PML4_SLOT_MAX - VAS_VSPACE_PML4_SLOTS)
#define BASE_ADDR (VAS_VSPACE_PML4_SLOT_MIN * 512UL * HUGE_PAGE_SIZE)

    void *bufs[VAS_TEST_NUM_SEG];
    vas_seg_handle_t seg[VAS_TEST_NUM_SEG];
    for (int i = 0; i < VAS_TEST_NUM_SEG; ++i) {
        snprintf(name, 32, "/seg/test/" "%u", i);
        bufs[i] = (void *)(BASE_ADDR + 16*i*LARGE_PAGE_SIZE);
        err = vas_seg_alloc(name, 0, LARGE_PAGE_SIZE + 2*i*LARGE_PAGE_SIZE, (lvaddr_t)bufs[i] , VAS_FLAGS_PERM_READ, &seg[i]);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "failed to create segment");
        }
    }

    for (int i = 0; i < VAS_TEST_NUM_SEG; ++i) {
        err = vas_seg_attach(vas[0], seg[i], VAS_FLAGS_PERM_READ);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "failed to switch to original");
        }
    }

    err = vas_switch(vas[0]);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to switch to original");
    }

    for (int i = 0; i < VAS_TEST_NUM_SEG; ++i) {
        uint64_t *data = bufs[i];
        debug_printf("[%lu] seg[%i]: [%p] *data = %016lx\n", proc_id, i, data, *data);
    }


    err = oct_barrier_leave(record);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "oct barrier leave failed");
    }

    debug_printf("switching back go original address space\n");
    err = vas_switch(VAS_HANDLE_PROCESS);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to switch to original");
    }

    debug_printf("about to page fault!\n");
    for (int i = 0; i < VAS_TEST_NUM_SEG; ++i) {
            uint64_t *data = bufs[i];
            debug_printf("[%lu] seg[%i]: [%p] *data = %016lx\n", proc_id, i, data, *data);
        }

    debug_printf("## VAS TEST TERMINATED\n");

    while(1);


    void *buf1, *buf2;
    uint64_t *data = buf1;
    debug_printf("[%lu] orig: [%p] *data = %016lx\n", proc_id, data, *data);


    for (size_t i = 0; i < proc_total; ++i) {
        debug_printf("proc[%lu] vas[%lu]: switch to vas %lx\n", proc_id, i, vas[i]);
        err = vas_switch(vas[i]);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "failed to attach");
        }
        debug_printf("proc[%lu] vas[%lu]: [%p] *data = %016lx\n", proc_id, i, data, *data);
    }

    err = vas_switch(VAS_HANDLE_PROCESS);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to switch to original");
    }
    debug_printf("[%lu] orig: [%p] *data = %016lx\n", proc_id, data, *data);


    debug_printf("%lx \n", *((uint64_t*)0x80000000000));

    *((uint64_t*)0x80000000000) = 0x1;


    debug_printf("[%lu] ### mapping frame vas[%lu]\n", proc_id, proc_id);
    err = vas_map(vas[proc_id], &buf1, frame, BASE_PAGE_SIZE, VREGION_FLAGS_READ_WRITE);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }
    err = vas_map(vas[proc_id], &buf2, frame2, LARGE_PAGE_SIZE, VREGION_FLAGS_READ_WRITE | VREGION_FLAGS_LARGE);
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

    debug_printf("## VAS TEST TERMINATED\n");

    return 0;
}
