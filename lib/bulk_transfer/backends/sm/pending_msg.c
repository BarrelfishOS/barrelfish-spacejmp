/*
 * Copyright (c) 2009, 2010, 2011, 2012, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 *
 * A simple sorted linked list for pending flounder RPC messages in the sm backend.
 * Because there is no obvious correspondence between RPC calls and replies,
 * but we have continuations to call after we get the reply,
 * we keep track of them ourselves.
 * All our flounder RPC calls and replies contain a transaction id, which is used
 * to look up the correct callback on receiving a RPC reply.
 *
 * using a linked list should be more than enough, since we don't expect many concurrent
 * pending messages.
 */

#include "bulk_sm_impl.h"
#include <stdlib.h>

#define USE_LOCKS 0

/**
 * dump pending message TID's for given channel
 */
static void pending_msg_dump(struct bulk_channel *channel)
{
    assert(channel);
#if USE_LOCKS
    thread_mutex_lock_nested(&CHANNEL_DATA(channel)->mutex);
#endif

    struct bulk_sm_pending_msg *node = CHANNEL_DATA(channel)->root;
    debug_printf("Dumping pending message TID's for channel %p.\n", channel);
    while (node) {
        debug_printf("  %u\n", node->tid);
        node = node->next;
    }
#if USE_LOCKS
    thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
}


static uint32_t current_tid = 0;

/**
 * add the data to the list of pending messages in channel
 * generates tid automatically
 *
 * @param channel:  Channel this message belongs to
 * @param tid:      will be filled in with transaction id
 * @param data:     payload for this message
 */
errval_t pending_msg_add(struct bulk_channel* channel,
                         uint32_t *tid,
                         union pending_msg_data data)
{
    assert(channel);
    struct bulk_sm_pending_msg *p =alloc_bulk_sm_pending_msg();
    assert(p);

    p->data = data;
    p->next = NULL;
    p->previous = NULL;

    //seperate variable declared for easier compiler optimization
    //uint32_t thistid = (uint32_t) rand();
    p->tid = current_tid++;

    // DEBUG_MALLOC("PENDING MSG: [new tid=%u]\n", thistid);
    // DEBUG_MALLOC("before:\n");
    // pending_msg_dump(channel);
#if USE_LOCKS
    thread_mutex_lock(&CHANNEL_DATA(channel)->mutex);
#endif

    struct bulk_sm_pending_msg *node = CHANNEL_DATA(channel)->root;

    if (node == NULL) {
        CHANNEL_DATA(channel)->root = p;
        *tid = p->tid;
#if USE_LOCKS
        thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
        // DEBUG_MALLOC("after:\n");
        // pending_msg_dump(channel);
        return SYS_ERR_OK;
    }

    /* add it to the end */
    if (node->previous == NULL) {
        /* only one in the setting */
        node->next = p;
        node->previous = p;
        p->previous = node;
        p->next = node;
        *tid = p->tid;
#if USE_LOCKS
        thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
        // DEBUG_MALLOC("after:\n");
        // pending_msg_dump(channel);
        return SYS_ERR_OK;
    }

    struct bulk_sm_pending_msg *last = node->previous;
    last->next = p;
    p->previous = last;
    p->next = node;
    node->previous = p;

    *tid = p->tid;
    thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
    // DEBUG_MALLOC("after:\n");
    // pending_msg_dump(channel);
    return SYS_ERR_OK;


    uint32_t thistid = 0;
    if (node == NULL){    //no other entries
        CHANNEL_DATA(channel)->root = p;
        *tid = thistid;
#if USE_LOCKS
        thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
        // DEBUG_MALLOC("after:\n");
        // pending_msg_dump(channel);
        return SYS_ERR_OK;
    }  else {
        while(true){
            if (node->tid < thistid){
                if (node->next){
                    node = node->next;
                } else {    //end of list reached
                    p->previous = node;
                    node->next = p;
                    *tid = thistid;
#if USE_LOCKS
        thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
                    // DEBUG_MALLOC("after:\n");
                    // pending_msg_dump(channel);
                    return SYS_ERR_OK;
                }
            } else if (node->tid > thistid) {
                p->next = node;
                p->previous = node->previous;

                node->previous = p;

                if (p->previous) {
                    p->previous->next = p;
                } else {
                    //become new root
                    CHANNEL_DATA(channel)->root = p;
                }

                *tid = thistid;
#if USE_LOCKS
        thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
                // DEBUG_MALLOC("after:\n");
                // pending_msg_dump(channel);
                return SYS_ERR_OK;
            } else {
                // //tid already taken -> try again with different tid
                // thistid = (uint32_t) rand();
                // p->tid = thistid;
                // node = CHANNEL_DATA(channel)->root; // XXX WRONG. root could be NULL -- jb
#if USE_LOCKS
                thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
                free_bulk_sm_pending_msg(p);
                pending_msg_add(channel, tid, data); // XXX does copy of data recursively :-(
            }
        }
    }
    assert(!"should not be reached");
}

/**
 * reads pending message
 *
 * @param channel:  Channel this message belongs to
 * @param tid:      transaction id to look up
 * @param data:     will be filled in with payload for this message
 * @param remove:   whether item is to be removed from list
 */
errval_t pending_msg_get(struct bulk_channel     *channel,
                         uint32_t                tid,
                         union pending_msg_data  *data,
                         bool                     do_remove)
{
    assert(channel);

    #if USE_LOCKS
    thread_mutex_lock(&CHANNEL_DATA(channel)->mutex);
#endif

    struct bulk_sm_pending_msg *p = CHANNEL_DATA(channel)->root;

    // DEBUG_MALLOC("PENDING MSG: [remove tid=%u]\n", tid);
    if (0) pending_msg_dump(channel); // defined but not used :-(

    while(p != NULL){
        if(p->tid == tid) {
            break;
        }

        if (p->tid > tid) {
            return BULK_TRANSFER_SM_NO_PENDING_MSG;
        }

        if (p->tid < tid) {
            p = p->next;
        }

        if (p == CHANNEL_DATA(channel)->root) {
            /* we've been once around the look */
            return BULK_TRANSFER_SM_NO_PENDING_MSG;
        }
    }
    //tid matches -> found
    *data = p->data;

    if (do_remove) {

        /* update root if needed */
        if (CHANNEL_DATA(channel)->root == p) {
            CHANNEL_DATA(channel)->root = p->next;
        }

        /* either 1 or two elements in the list */
        if (p->next == p->previous) {
            if (p->next) {
                /* we have 2 elements */
                CHANNEL_DATA(channel)->root->next = NULL;
                CHANNEL_DATA(channel)->root->previous = NULL;
            }
        } else {
            p->next->previous = p->previous;
            p->previous->next = p->next;
        }
        p->previous = NULL;
        p->next = NULL;

        free_bulk_sm_pending_msg(p);
    }

#if USE_LOCKS
        thread_mutex_unlock(&CHANNEL_DATA(channel)->mutex);
#endif
    return SYS_ERR_OK;
}
