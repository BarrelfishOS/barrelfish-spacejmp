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

#include <vas_internal.h>
#include <vas_vspace.h>


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
errval_t vas_create(const char* name, vas_perm_t perm, vas_handle_t *ret_vas)
{
    errval_t err;

    struct vas *vas = calloc(1, sizeof(struct vas));
    if (vas == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    /* TODO: register name with the vas service to get the VAS ID */

    /* create a new VSPACE */
    err = vas_vspace_init(vas);
    if (err_is_fail(err)) {
        return err;
    }

    /* */

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
errval_t vas_delete(vas_handle_t vas)
{
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
errval_t vas_lookup_by_name(const char *name, vas_handle_t *ret_vas)
{
    return VAS_ERR_NOT_FOUND;
}

/**
 * \brief Looks up the address space using a human readable name
 *
 * \param id        ID of the VAS to lookup
 * \param ret_vas   returns a handle to the vas
 *
 * \returns SYS_ERR_OK on success
 *          VAS_ERR_NOT_FOUND if there is no such address space
 *          VAS_ERR_NO_PERMISSIONS if the caller has too less permissions
 *
 */
errval_t vas_lookup_by_id(vas_id_t id, vas_handle_t *ret_vas)
{
    return VAS_ERR_NOT_FOUND;
}

/**
 * \brief attaches a virtual address space to the domain
 * \param vas
 * \param perm
 *
 * \return
 */
errval_t vas_attach(vas_handle_t vas, vas_perm_t perm)
{
    return VAS_ERR_NOT_SUPPORTED;
}

/**
 * \brief Detach a virtual address sapce from the calling process
 *
 * \param vas   handle to the VAS to detach
 *
 * \return
 */
errval_t vas_detach(vas_handle_t vas)
{
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
errval_t vas_switch(vas_handle_t vas)
{
    return VAS_ERR_NOT_SUPPORTED;
}


/**
 * \brief Switch to the address space and return handle of old address space
 *
 * \param vas       the virtual address space to switch to
 * \param rev_vas   the old virtual addres sapce
 *
 * \return
 */
errval_t vas_switchm(vas_handle_t vas, vas_handle_t *rev_vas)
{
    return VAS_ERR_NOT_SUPPORTED;
}


/*
 *
 */
errval_t vas_get_perm(vas_id_t id, vas_perm_t* perm) { return VAS_ERR_NOT_SUPPORTED; }
errval_t vas_set_perm(vas_id_t id, vas_perm_t perm) {return VAS_ERR_NOT_SUPPORTED; }
errval_t vas_get_life(vas_id_t id, vas_life_t* life) {return VAS_ERR_NOT_SUPPORTED;}
errval_t vas_set_life(vas_id_t id, vas_life_t lifetime) {return VAS_ERR_NOT_SUPPORTED;}

