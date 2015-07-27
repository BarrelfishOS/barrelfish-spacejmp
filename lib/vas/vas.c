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
#include <barrelfish/monitor_client.h>

#include <vas_internal.h>
#include <vas_vspace.h>
#include <vas_client.h>

/**
 * \brief enables the support for multiple virtual address spaces on this dispatcher
 *
 * \returns SYS_ERR_OK on success
 *          errval on failure
 */
errval_t vas_enable(void)
{
    VAS_DEBUG_LIBVAS("enabling mvas support for domain.\n");

    return vas_client_connect();
}

/**
 * \brief creates a new virtual address space (VAS)
 *
 * Initializes the permissions to the given permissions and the lifetime to
 * VAS_LIFE_TRANSIENT.
 *
 * It currently creates a new process local VSPACE and attaches to it
 *
 * \param name      name of the newly created virtual address space
 * \param perm      associated permissions for the address space
 * \param ret_vas   pointer to the VAS handle
 *
 * \returns SYS_ERR_OK success
 *          errval on error
 */
errval_t vas_create(const char* name, vas_flags_t perm, vas_handle_t *ret_vas)
{
    errval_t err;

    assert(ret_vas);

    struct vas *vas = calloc(1, sizeof(struct vas));
    if (vas == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    vas->perms = perm;

    strncpy(vas->name, name, VAS_NAME_MAX_LEN);

    size_t namelen = strlen(name);
    if (namelen > VAS_NAME_MAX_LEN) {
        vas->name[VAS_NAME_MAX_LEN - 1] = 0;
    }

    if (perm & VAS_FLAGS_PERM_LOCAL) {
        VAS_DEBUG_LIBVAS("creating local vas '%s'\n", name);
        /* create a new VSPACE */
        err = vas_vspace_init(vas);
        if (err_is_fail(err)) {
            free(vas);
            return err;
        }
    } else {
        VAS_DEBUG_LIBVAS("creating vas '%s'\n", name);
        err = slot_alloc(&vas->vroot);
        if(err_is_fail(err)) {
            free(vas);
            return err;
        }
        err = vas_vspace_create_vroot(vas->vroot);
        if (err_is_fail(err)) {
            free(vas);
            return err;
        }

        err = vas_client_vas_create(vas->name, perm, &vas->id);
        if (err_is_fail(err)) {
            cap_destroy(vas->vroot);
            slot_free(vas->vroot);
            free(vas);
            return err;
        }
    }

    /* TODO: register name with the vas service to get the VAS ID */

    vas->state = VAS_STATE_DETACHED;
    vas->tag = (vas->id + 2);

    *ret_vas = vas_get_handle(vas);

    return SYS_ERR_OK;
}

/**
 * \brief deletes a virtual address space
 *
 *  If the address space is attached to others, it becomes invisible to
 *  future attachers, and is deleted once all have detached
 *
 * \return SYS_ERR_OK on success
 *         VAS_ERR_* on failure
 */
errval_t vas_delete(vas_handle_t vh)
{
    //struct vas *vas = vas_get_vas_pointer(vh);

    return VAS_ERR_NOT_SUPPORTED;
}

/**
 * \brief Looks up the address space using a human readable name
 *
 * \param name      name of the address space to look up
 * \param ret_vas   returns a handle to the vas
 *
 * \returns SYS_ERR_OK on success
 *          VAS_ERR_NOT_FOUND if there is no such address space
 *          VAS_ERR_NO_PERMISSIONS if the caller has too less permissions
 *
 */
errval_t vas_lookup(const char *name, vas_handle_t *ret_vas)
{
    errval_t err;



    assert(ret_vas);

    struct vas *vas = calloc(1, sizeof(struct vas));
    if (vas == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    strncpy(vas->name, name, VAS_NAME_MAX_LEN);

    size_t namelen = strlen(name);
    if (namelen > VAS_NAME_MAX_LEN) {
        vas->name[VAS_NAME_MAX_LEN - 1] = 0;
    }


    VAS_DEBUG_LIBVAS("looking up vas '%s'\n", name);
    err = slot_alloc(&vas->vroot);
    if(err_is_fail(err)) {
        free(vas);
        return err;
    }
    err = vas_vspace_create_vroot(vas->vroot);
    if (err_is_fail(err)) {
        free(vas);
        return err;
    }

    err = vas_client_vas_lookup(vas->name, &vas->id);
    if (err_is_fail(err)) {
        cap_destroy(vas->vroot);
        slot_free(vas->vroot);
        free(vas);
        return err;
    }

    vas->state = VAS_STATE_DETACHED;

    *ret_vas = vas_get_handle(vas);

    return SYS_ERR_OK;
}


/**
 * \brief attaches a virtual address space to the domain
 * \param vas
 * \param perm
 *
 * \return
 */
errval_t vas_attach(vas_handle_t vh, vas_flags_t perm)
{
    errval_t err;

    struct vas *vas = vas_get_vas_pointer(vh);

    VAS_DEBUG_LIBVAS("attaching vas '%s'\n", vas->name);

    /* if the state is already attached don't do anything*/
    if (vas->state == VAS_STATE_ATTACHED) {
        return SYS_ERR_OK;
    }

    /* state change is undergoing / vas is not valid this is an error*/
    if (vas->state != VAS_STATE_DETACHED) {
        return VAS_ERR_ATTACH_STATE;
    }

    err = vas_vspace_inherit_segments(vas);
    if (err_is_fail(err)) {
        return err;
    }

    err = vas_vspace_inherit_heap(vas);
    if (err_is_fail(err)) {
        return err;
    }

    if (!(vas->perms & VAS_FLAGS_PERM_LOCAL)) {
        err = vas_client_vas_attach(vas->id, vas->vroot);
        if (err_is_fail(err)) {
            return err;
        }
    }
    vas->state = VAS_STATE_ATTACHED;

    return SYS_ERR_OK;
}

/**
 * \brief Detach a virtual address sapce from the calling process
 *
 * \param vas   handle to the VAS to detach
 *
 * \return
 */
errval_t vas_detach(vas_handle_t vh)
{
    //struct vas *vas = vas_get_vas_pointer(vh);

    return VAS_ERR_NOT_SUPPORTED;
}

/**
 * \brief Switch to the address space.
 *
 * \param vas   the virtual address space to switch to
 *
 * \return SYS_ERR_OK on success
 *         VAS_ERR_SWITCH_NOT_ATTACHED
 */
errval_t vas_switch(vas_handle_t vh)
{
    VAS_DEBUG_LIBVAS("switching to vas '%s'\n", vas->name);

    struct vas *vas = vas_get_vas_pointer(vh);

    errval_t err;

    if (vas) {
        if (vas->state != VAS_STATE_ATTACHED) {
            return VAS_ERR_SWITCH_NOT_ATTACHED;
        }
        err = vnode_vroot_switch(vas->vroot, vas->tag);
    } else {
        struct capref vroot = {
            .cnode = cnode_page,
            .slot = 0
        };
        err = vnode_vroot_switch(vroot, 0);
    }
    if (err_is_fail(err)) {
        return err;
    }

   // vas->state = VAS_STATE_ACTIVE;

    VAS_DEBUG_LIBVAS("switched to vas '%s'\n", vas->name);

    return SYS_ERR_OK;
}


/**
 * \brief Switch to the address space and return handle of old address space
 *
 * \param vas       the virtual address space to switch to
 * \param rev_vas   the old virtual addres sapce
 *
 * \return
 */
errval_t vas_switchm(vas_handle_t vh, vas_handle_t *rev_vas)
{
    //struct vas *vas = vas_get_vas_pointer(vh);
    return VAS_ERR_NOT_SUPPORTED;
}


/*
 *
 */
errval_t vas_get_perm(vas_id_t id, vas_flags_t* perm) { return VAS_ERR_NOT_SUPPORTED; }
errval_t vas_set_perm(vas_id_t id, vas_flags_t perm) {return VAS_ERR_NOT_SUPPORTED; }
errval_t vas_get_life(vas_id_t id, vas_life_t* life) {return VAS_ERR_NOT_SUPPORTED;}
errval_t vas_set_life(vas_id_t id, vas_life_t lifetime) {return VAS_ERR_NOT_SUPPORTED;}

vas_state_t vas_get_state(vas_handle_t vh)
{
    return vas_get_vas_pointer(vh)->state;
}


errval_t vas_map(vas_handle_t vh, void **retaddr, struct capref frame, size_t size, vregion_flags_t flags)
{
    struct vas *vas = vas_get_vas_pointer(vh);

    if (!(vas->perms & VAS_FLAGS_PERM_LOCAL)) {
        return vas_client_seg_map(vas->id, frame, size, flags, (lvaddr_t *) retaddr);
    } else {
        return vas_vspace_map_one_frame(vas, retaddr, frame, size, flags);
    }
}

errval_t vas_unmap(vas_handle_t vh, void *addr)
{
    //struct vas *vas = vas_get_vas_pointer(vh);

    return VAS_ERR_NOT_SUPPORTED;
}

errval_t vas_tagging_enable(void)
{
    return monitor_tlb_tag_toggle(1);
}

errval_t vas_tagging_disable(void)
{
    return monitor_tlb_tag_toggle(0);
}

errval_t vas_tagging_tag(vas_handle_t vh)
{
    //struct vas *vas = vas_get_vas_pointer(vh);

    return VAS_ERR_NOT_SUPPORTED;
}


