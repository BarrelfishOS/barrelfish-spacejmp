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

int main(int argc, char *argv[])
{
    errval_t err;

    debug_printf("## VAS TEST STARTED\n");

    struct vas *vas;

    struct capref frame;
    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    struct capref frame2;
    err = frame_alloc(&frame2, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    struct capref frame3;
    err = frame_alloc(&frame3, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    err = vas_create("first", VAS_PERM_WRITE, &vas);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to create a new VAS");
    }

    err = vas_attach(vas, 0);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }
    struct vas *vas2;
    err = vas_create("second", VAS_PERM_WRITE, &vas2);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to create a new VAS");
    }
    err = vas_attach(vas2, 0);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    void *buf1;
    err = vas_vspace_map_one_frame(vas, &buf1, frame, BASE_PAGE_SIZE);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }
    debug_printf("mapped vas1 @ %p\n", buf1);

    void *buf2;
    err = vas_vspace_map_one_frame(vas2, &buf2, frame2, BASE_PAGE_SIZE);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }

    debug_printf("mapped vas2 @ %p\n", buf2);

    void *buf3 = buf2;
    err = vspace_map_one_frame_fixed((lvaddr_t)buf2, BASE_PAGE_SIZE, frame3, NULL, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping frame");
    }
    debug_printf("mapped vas0 @ %p\n", buf3);

    uint64_t *data = buf3;
    debug_printf("orig: [%p] *data = %016lx\n", data, *data);
    *data = 0xcafe0000;
    debug_printf("orig: [%p] *data = %016lx\n", data, *data);

    err = vas_switch(vas);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    debug_printf("%s: [%p] *data = %016lx\n", "first", data, *data);
    *data = 0xbabe0000;
    debug_printf("%s: [%p] *data = %016lx\n", "first", data, *data);

    err = vas_switch(vas2);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    debug_printf("%s: [%p] *data = %016lx\n", "second", data, *data);
    *data = 0xbeef0000;
    debug_printf("%s: [%p] *data = %016lx\n", "second", data, *data);

    err = vas_switch(vas);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to attach");
    }

    debug_printf("%s: [%p] *data = %016lx\n", "first", data, *data);

    debug_printf("## VAS TEST TERMINATED\n");

    return 0;
}
