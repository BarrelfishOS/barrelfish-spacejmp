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
#include <barrelfish/nameservice_client.h>
#include <bench/bench.h>
#include <vas_internal.h>
#include <vas_vspace.h>

#include <if/vas_defs.h>
#include <flounder/flounder_txqueue.h>

extern errval_t vspace_add_vregion(struct vspace *vspace, struct vregion *region);

#ifdef NDEBUG
#define VAS_SERVICE_DEBUG(x...)
#else
#define VAS_SERVICE_DEBUG(x...) debug_printf(x);
#endif


#define VAS_ATTACHED_MAX 16

#define MIN(a,b)        ((a) < (b) ? (a) : (b))

#define EXPECT_SUCCESS(expr, msg...) \
    if (!(expr)) {USER_PANIC("expr" msg);}

union vas_name_arg {
    char namestring[32];
    uint64_t namefields[4];
};

struct vas_client
{
    struct vas_binding *b;
    struct tx_queue txq;
};

struct list_elem
{
    struct list_elem *next;
    struct list_elem *prev;
};

struct vas_attached
{
    struct capref vroot;
    struct vas_attached *next;
};

struct vas_info
{
    struct list_elem l;
    struct vas vas;
    struct vas_client *creator;
    struct vas_attached *attached;
    uint64_t map[512/64];
    struct vas_info *next;
    struct vas_info *prev;
};

struct list_elem *vas_registered;

struct seg_attached
{
    struct vregion vreg;
    struct capref frame;
    struct seg_attached *next;
};

// abstract as vregion ?
struct seg_info
{
    struct list_elem l;
    vas_seg_id_t id;
    struct vregion vreg;
    struct seg_attached *attached;
    struct memobj_one_frame mobj;
    struct vas_client *creator;
    char name [VAS_NAME_MAX_LEN];
};

struct list_elem *seg_registered;

/*
 * ------------------------------------------------------------------------------
 * VAS info management
 * ------------------------------------------------------------------------------
 */

static void elem_insert(struct list_elem **list, struct list_elem *elem)
{
    if (*list) {
        (*list)->prev = elem;
        elem->next = *list;
        *list = elem;
    } else {
        elem->next = elem->prev = NULL;
        *list = elem;
    }
}


typedef int (*elem_cmp_fn_t)(struct list_elem *list, void *arg);

static int elem_cmp_seg(struct list_elem *e, void *arg)
{
    struct seg_info *si = (struct seg_info *)e;

    debug_printf("elem_cmp_seg: %s %s  : %u\n", si->name, (char *)arg, strncmp(si->name, (char *)arg, VAS_NAME_MAX_LEN));

    return strncmp(si->name, (char *)arg, VAS_NAME_MAX_LEN);
}

static int elem_cmp_vas(struct list_elem *e, void *arg)
{
    struct vas_info *vi = (struct vas_info *)e;
    return strncmp(vi->vas.name, (char *)arg, VAS_NAME_MAX_LEN);
}


static struct list_elem *elem_lookup(struct list_elem *list, elem_cmp_fn_t cmp,
                                     void *arg)
{
    struct list_elem *vi = list;
    while(vi) {
        if (cmp(vi, arg) == 0) {
            return vi;
        }
        vi = vi->next;
    }
    return NULL;
}

/*
 * ------------------------------------------------------------------------------
 * id check helpers
 * ------------------------------------------------------------------------------
 */
static inline errval_t vas_verify_vas_id(uint64_t id, struct vas_info **ret_vi)
{
    if ((id & VAS_ID_MARK) != VAS_ID_MARK) {
        return VAS_ERR_NOT_FOUND;
    }

    struct vas_info *vi = (struct vas_info *)(VAS_ID_MASK & id);
    if (vi->vas.id != (VAS_ID_MASK & id)) {
        return VAS_ERR_NOT_FOUND;
    }

    *ret_vi = vi;

    return SYS_ERR_OK;
}

static inline errval_t vas_verify_seg_id(uint64_t id, struct seg_info **ret_si)
{
    if ((id & VAS_ID_MARK) != VAS_ID_MARK) {
        return VAS_ERR_NOT_FOUND;
    }

    struct seg_info *si = (struct seg_info *)(VAS_ID_MASK & id);
    if (si->id != (VAS_ID_MASK & id)) {
        return VAS_ERR_NOT_FOUND;
    }

    *ret_si = si;

    return SYS_ERR_OK;
}
/*
 * ------------------------------------------------------------------------------
 * Receive handlers
 * ------------------------------------------------------------------------------
 */

static void vas_create_call__rx(struct vas_binding *_binding, uint64_t name0,
                                uint64_t name1, uint64_t name2, uint64_t name3)
{
    errval_t err;

    struct vas_info *vi = calloc(1, sizeof(struct vas_info));
    if (!vi) {
        err = _binding->tx_vtbl.create_response(_binding, NOP_CONT,
                                                LIB_ERR_MALLOC_FAIL, 0, 0);
    } else {
        uint64_t *nameptr = (uint64_t *)vi->vas.name;
        nameptr[0] = name0;
        nameptr[1] = name1;
        nameptr[2] = name2;
        nameptr[3] = name3;

        vi->vas.id = (uint64_t)vi;

        err = vas_vspace_init(&vi->vas);
        if (err_is_fail(err)) {
            err = _binding->tx_vtbl.create_response(_binding, NOP_CONT,err, 0,0);
            free(vi);
        } else {
            elem_insert(&vas_registered, &vi->l);
            err = _binding->tx_vtbl.create_response(_binding, NOP_CONT,err,
                                                    VAS_ID_MARK | vi->vas.id,
                                                    vi->vas.tag);
        }
    }

    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "should not happend...");
    }
}

static void vas_delete_call__rx(struct vas_binding *_binding, uint64_t sid)
{

}

static void vas_attach_call__rx(struct vas_binding *_binding, uint64_t id,
                                struct capref vroot)
{
    errval_t err;

    VAS_SERVICE_DEBUG("[request] attach: client=%p, vas=0x%016lx\n", _binding->st, id);

    struct vas_info *vi;
    err = vas_verify_vas_id(id, &vi);

    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] attach: client=%p, vas=0x%016lx, err='%s'\n",
                          _binding->st, id, err_getstring(err));

        err = _binding->tx_vtbl.attach_response(_binding, NOP_CONT, err);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "send reply");
            cap_delete(vroot);
        }
        return;
    }

    struct vas_attached *ai = calloc(1, sizeof(struct vas_attached));
    if (!ai) {
        err = _binding->tx_vtbl.attach_response(_binding, NOP_CONT, LIB_ERR_MALLOC_FAIL);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "send reply");
            cap_delete(vroot);
        }
        return;
    }

    ai->vroot = vroot;
    ai->next = vi->attached;
    vi->attached = ai;

    err = vas_vspace_inherit_regions(&vi->vas, vroot,
                                     VAS_VSPACE_PML4_SLOT_MIN,
                                     VAS_VSPACE_PML4_SLOT_MAX);
    err = _binding->tx_vtbl.attach_response(_binding, NOP_CONT, err);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "send reply");
        cap_delete(vroot);
    }

}

static void vas_detach_call__rx(struct vas_binding *_binding, uint64_t id)
{
}


static void vas_lookup_call__rx(struct vas_binding *_binding, uint64_t name0,
                                uint64_t name1, uint64_t name2, uint64_t name3)
{
    union vas_name_arg narg = { .namefields = {name0, name1, name2, name3}};

    VAS_SERVICE_DEBUG("[request] lookup: client=%p, name='%s'\n", _binding->st,
                      narg.namestring);

    struct vas_info *vi = (struct vas_info *)elem_lookup(vas_registered, elem_cmp_vas,
                                                         narg.namestring);
    if (vi) {
        _binding->tx_vtbl.lookup_response(_binding, NOP_CONT, VAS_ERR_NOT_FOUND,
                                          VAS_ID_MARK | vi->vas.id, vi->vas.tag);
    } else {
        _binding->tx_vtbl.lookup_response(_binding, NOP_CONT, VAS_ERR_NOT_FOUND, 0, 0);
    }

}

static void vas_seg_create_call__rx(struct vas_binding *_binding, uint64_t name0,
                                    uint64_t name1, uint64_t name2, uint64_t name3,
                                    uint64_t vaddr, uint64_t size, struct capref frame)
{
    errval_t err;

    union vas_name_arg narg = { .namefields = {name0, name1, name2, name3}};

    /// todo: proper roundup
    size = ROUND_UP(size, BASE_PAGE_SIZE);

    /// todo: handling of flags
    vregion_flags_t flags = VREGION_FLAGS_MASK;

    struct seg_info *si = (struct seg_info *)elem_lookup(seg_registered, elem_cmp_seg,
                                                         narg.namestring);
    if (si) {
        err = VAS_ERR_CREATE_NAME_CONFLICT;
        VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', err='%s'\n",
                                  _binding->st, narg.namestring, err_getstring(err));
        goto err_out;
    }

    struct frame_identity id;
    err = invoke_frame_identify(frame, &id);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', err='%s'\n",
                          _binding->st, narg.namestring, err_getstring(err));
        goto err_out;
    }

    if ((size > (1UL << id.bits))) {
        err = LIB_ERR_PMAP_FRAME_SIZE;
        VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', err='%s'\n",
                          _binding->st, narg.namestring, err_getstring(err));
        goto err_out;
    }

    si = calloc(1, sizeof(*si));
    if (!si) {
        err = LIB_ERR_MALLOC_FAIL;
        VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', err='%s'\n",
                          _binding->st, narg.namestring, err_getstring(err));
        goto err_out;
    }

    si->id = (uint64_t)si;
    strncpy(si->name, narg.namestring, VAS_NAME_MAX_LEN);

    err = memobj_create_one_frame(&si->mobj, size, 0);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', err='%s'\n",
                          _binding->st, narg.namestring, err_getstring(err));
        goto err_out;
    }

    err = si->mobj.m.f.fill(&si->mobj.m, 0, frame, size);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', err='%s'\n",
                          _binding->st, narg.namestring, err_getstring(err));
        goto err_out;
    }

    VAS_SERVICE_DEBUG("[request] seg_create: client=%p, name='%s', id=0x%016lx\n",
                       _binding->st, narg.namestring, si->id);


    si->vreg.base = vaddr;
    si->vreg.flags = flags;
    si->vreg.size = size;
    si->creator = _binding->st;

    elem_insert(&seg_registered, &si->l);

    err_out :
    _binding->tx_vtbl.seg_create_response(_binding, NOP_CONT, err, VAS_ID_MARK | si->id);

    return;
}

static void vas_seg_delete_call__rx(struct vas_binding *_binding, uint64_t sid)
{
    errval_t err;

    struct seg_info *si;
    err = vas_verify_seg_id(sid, &si);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_delete: client=%p, seg=0x%016lx, err='%s'\n",
                                          _binding->st, sid, err_getstring(err));
        goto err_out;
    }

    err = VAS_ERR_NOT_SUPPORTED;
    err_out:

    _binding->tx_vtbl.seg_delete_response(_binding, NOP_CONT, err);

}

static void vas_seg_lookup_call__rx(struct vas_binding *_binding, uint64_t name0,
                                    uint64_t name1, uint64_t name2, uint64_t name3)
{
    errval_t err;

    uint64_t id = 0,  vaddr = 0,  length = 0;

    union vas_name_arg narg = { .namefields = {name0, name1, name2, name3}};

    struct seg_info *si = (struct seg_info *)elem_lookup(seg_registered,
                                                         elem_cmp_seg, narg.namestring);
    if (!si) {
        err = VAS_ERR_CREATE_NAME_CONFLICT;
        goto err_out;
    }

    id = si->id | VAS_ID_MARK;
    vaddr = vregion_get_base_addr(&si->vreg);
    length = vregion_get_size(&si->vreg);


    err_out :
    _binding->tx_vtbl.seg_lookup_response(_binding, NOP_CONT, err, id, vaddr, length);

    return;

}

static void vas_seg_attach_call__rx(struct vas_binding *_binding, uint64_t vid,
                                    uint64_t sid, uint32_t flags)
{
    VAS_SERVICE_DEBUG("[request] seg_attach: client=%p, vas=0x%016lx, seg=0x%016lx\n",
                      _binding->st, vid, sid);

    errval_t err;
    struct vas_info *vi;
    err = vas_verify_vas_id(vid, &vi);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_attach: client=%p, no vas=0x%016lx, err='%s'\n",
                                  _binding->st, vid, err_getstring(err));
        goto err_out;
    }

    struct vspace *vs = &vi->vas.vspace_state.vspace;

    struct seg_info *si;
    err = vas_verify_seg_id(sid, &si);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_attach: client=%p, seg=0x%016lx, err='%s'\n",
                                          _binding->st, sid, err_getstring(err));
        goto err_out;
    }

    flags = flags & si->vreg.flags;


    struct seg_attached *att = calloc(1, sizeof(struct seg_attached));
    if (!att) {
        err = LIB_ERR_MALLOC_FAIL;
        goto err_out;
    }

    struct vregion *vreg = &att->vreg;
    vreg->vspace = vs;
    vreg->memobj = &si->mobj.m;
    vreg->base   = si->vreg.base;
    vreg->offset = si->vreg.offset;
    vreg->size   = si->vreg.size;
    vreg->flags  = flags;

    err = vspace_add_vregion(vs, vreg);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_VSPACE_ADD_REGION);
        free(att);
        VAS_SERVICE_DEBUG("[request] seg_attach: vspace add client=%p, seg=0x%016lx, err='%s'\n",
                          _binding->st, sid, err_getstring(err));
        goto err_out;
    }

    err = si->mobj.m.f.map_region(&si->mobj.m, vreg);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_MEMOBJ_MAP_REGION);
        free(att);
        VAS_SERVICE_DEBUG("[request] seg_attach: map add client=%p, seg=0x%016lx, err='%s'\n",
                          _binding->st, sid, err_getstring(err));
        goto err_out;
    }

    err = si->mobj.m.f.pagefault(&si->mobj.m, vreg, 0, 0);
    if (err_is_fail(err)) {
        err =  err_push(err, LIB_ERR_MEMOBJ_MAP_REGION);
        free(att);
        VAS_SERVICE_DEBUG("[request] seg_attach: pagefault client=%p, seg=0x%016lx, err='%s'\n",
                                  _binding->st, sid, err_getstring(err));
        goto err_out;
    }

    att->next = si->attached;
    si->attached = att;

#define X86_64_PML4_BASE(base)         (((uint64_t)(base) >> 39) & X86_64_PTABLE_MASK)

    uint32_t entry = X86_64_PML4_BASE(vreg->base);
    uint32_t slot = entry / 64;
    uint32_t bit = entry % 64;


    if (!(vi->map[slot] & (1UL << bit))) {
        struct vas_attached *at = vi->attached;
        while(at) {
            err = vas_vspace_inherit_regions(&vi->vas, at->vroot, entry, entry);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "could not inherit region");
            }
            at = at->next;
        }
        vi->map[slot] |= (1UL << bit);
    }

    err_out:
    err = _binding->tx_vtbl.seg_attach_response(_binding, NOP_CONT, err);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "oops");
    }
}

static void vas_seg_detach_call__rx(struct vas_binding *_binding, uint64_t vid,
                                    uint64_t sid)
{
    errval_t err;
    struct vas_info *vi;
    err = vas_verify_vas_id(vid, &vi);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_attach: client=%p, vas=0x%016lx, err='%s'\n",
                                  _binding->st, vid, err_getstring(err));
        goto err_out;
    }

    struct seg_info *si;
    err = vas_verify_seg_id(sid, &si);
    if (err_is_fail(err)) {
        VAS_SERVICE_DEBUG("[request] seg_attach: client=%p, seg=0x%016lx, err='%s'\n",
                                          _binding->st, sid, err_getstring(err));
        goto err_out;
    }

    err = VAS_ERR_NOT_SUPPORTED;

    err_out:
    err = _binding->tx_vtbl.seg_attach_response(_binding, NOP_CONT, err);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "oops");
    }
}


static struct vas_rx_vtbl rx_vtbl = {
    .create_call = vas_create_call__rx,
    .delete_call = vas_delete_call__rx,
    .attach_call = vas_attach_call__rx,
    .detach_call = vas_detach_call__rx,
    .lookup_call = vas_lookup_call__rx,

    .seg_create_call = vas_seg_create_call__rx,
    .seg_delete_call = vas_seg_delete_call__rx,
    .seg_attach_call = vas_seg_attach_call__rx,
    .seg_detach_call = vas_seg_detach_call__rx,
    .seg_lookup_call = vas_seg_lookup_call__rx
};

/*
 * ------------------------------------------------------------------------------
 * Connection management
 * ------------------------------------------------------------------------------
 */

static errval_t vas_connect_handler(void *st, struct vas_binding *binding)
{
    struct vas_client *client = calloc(1, sizeof(struct vas_client));
    if (client == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    VAS_SERVICE_DEBUG("[connect] new client %p\n", client);

    client->b = binding;
    binding->st = client;
    binding->rx_vtbl = rx_vtbl;

    return SYS_ERR_OK;
}

static void export_callback_fn(void *st, errval_t err, iref_t iref)
{
    VAS_SERVICE_DEBUG("[service] export callback: %s\n", err_getstring(err));

    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "export failed");
    }


#define VAS_SERVICE_NAME "vas"

    err = nameservice_register(VAS_SERVICE_NAME, iref);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "export failed");
    }

    VAS_SERVICE_DEBUG("[service] registered with name '%s'::%" PRIxIREF "]\n",
                      VAS_SERVICE_NAME, iref);
}


int main(int argc, char *argv[])
{
    errval_t err;

    VAS_SERVICE_DEBUG("[service] vas service started\n");

    err =  vas_export(NULL, export_callback_fn, vas_connect_handler,
                      get_default_waitset(), 0);

    while(1) {
        messages_wait_and_handle_next();
    }

    VAS_SERVICE_DEBUG("[service] vas service terminated\n");

    return 0;
}
