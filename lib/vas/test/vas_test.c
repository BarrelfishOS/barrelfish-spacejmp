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
    err = vas_create("foobar", VAS_PERM_WRITE, &vas);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to create a new VAS");
    }
    debug_printf("## VAS TEST TERMINATED\n");

    return 0;
}
