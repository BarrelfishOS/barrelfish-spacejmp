/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich.
 * Attn: Systems Group.
 */

#ifndef __LIBVAS_H
#define __LIBVAS_H 1


/*
 * ==============================================================================
 * Typedefs and defines
 * ==============================================================================
 */

struct vas;

typedef struct vas * vas_handle_t;

///< virtual address space identifier
typedef uint32_t vas_id_t;

///< segment identifier
typedef uint64_t vas_seg_id_t;

typedef enum vas_state {
    VAS_STATE_INVALID = 0,
    VAS_STATE_DETACHED = 1,
    VAS_STATE_ATTACHED = 2
} vas_state_t;

///< identifier for a currently attached VAS inside a process

///< permissions associated with a virtual address space
typedef uint32_t vas_perm_t;
#define VAS_PERM_READ       (1 << 0)
#define VAS_PERM_WRITE      (1 << 1)
#define VAS_PERM_ALLOC      (1 << 2)
#define VAS_PERM_EXEC       (1 << 3)

///< Lifetime of an address space
typedef enum vas_life_t {
    VAS_LIFE_TRANSIENT  = 0,    ///< VAS is dependent to a process
    VAS_LIFE_PERSISTENT = 1     ///< VAS lives independent of processes
} vas_life_t;

///<Maximum length of a VAS and segment identifier string.
#define VAS_ID_MAX_LEN  32

/**
 * Represents the value of vaddr to seg_alloc which indicates the
 * kernel should / pick the offset to use. It is an addresss 'too
 * large' to use as a valid value anyway.
 */
#define VAS_SEG_VADDR_MAX   (0UL-1)

///< maximum length of a segment
#define VAS_SEG_MAX_LEN (1UL<<40)


/**
 * Information pertaining to a segment, returned by the kernel.
 */
struct vas_seg_info
{
    char name[VAS_ID_MAX_LEN];
    lvaddr_t vaddr;
    size_t len;
    vas_seg_id_t id;
};


/*
 * ==============================================================================
 * Public interface
 * ==============================================================================
 */

errval_t vas_create(const char* name, vas_perm_t perm, vas_handle_t *ret_vas);
errval_t vas_delete(vas_handle_t id);


errval_t vas_lookup_by_name(const char* name, vas_handle_t *ret_vas);
errval_t vas_lookup_by_id(vas_id_t id, vas_handle_t *ret_vas);

errval_t vas_get_perm(vas_id_t id, vas_perm_t* perm);
errval_t vas_set_perm(vas_id_t id, vas_perm_t perm);
errval_t vas_get_life(vas_id_t id, vas_life_t* life);
errval_t vas_set_life(vas_id_t id, vas_life_t lifetime);

vas_state_t vas_get_state(struct vas *vas);

errval_t vas_attach(vas_handle_t vas, vas_perm_t perm);
errval_t vas_detach(vas_handle_t vas);
errval_t vas_switch(vas_handle_t vas);
errval_t vas_switchm(vas_handle_t vas, vas_handle_t *prev_id);

void* vas_get_root(char* root_name);
errval_t vas_set_root(char* root_name, void* value);
errval_t vas_share(vas_id_t id, unsigned user_id, vas_perm_t perm);
vas_id_t vas_get_current(void);

errval_t vas_tagging_enable(void);
errval_t vas_tagging_disable(void);
errval_t vas_tagging_tag(vas_id_t id);

/*
 *
 */
errval_t vas_vspace_map_one_frame(struct vas *vas, void *retaddr,
                                  struct capref frame, size_t size);
errval_t vas_vspace_map_one_frame_fixed(struct vas *vas, lvaddr_t addr,
                                        struct capref frame, size_t size);

errval_t vas_vspace_unmap(void *addr);


#if 0

static inline long
__seg_alloc(const char *seg_name, ulong vaddr, size_t len, sid_t *sid)
{
    return ((long)__syscall((quad_t)SYS_seg_alloc,
                seg_name, vaddr, len, sid));
}

static inline long
__seg_free(sid_t sid)
{
    return ((long)__syscall((quad_t)SYS_seg_free, sid));
}

static inline long
__seg_map(sid_t sid, ulong *vaddr, int prot)
{
    return ((long)__syscall((quad_t)SYS_seg_map,
                sid, vaddr, prot));
}

static inline long
__seg_attach(sid_t sid, vid_t vid)
{
    return ((long)__syscall((quad_t)SYS_seg_attach, sid, vid));
}

static inline long
__vas_create(const char *name, vid_t *vid)
{
    return ((long)__syscall((quad_t)SYS_vas_create, name, vid));
}

static inline long
__vas_ctl(vid_t vid, int cmd)
{
    return ((long)__syscall((quad_t)SYS_vas_ctl, vid, cmd));
}

#endif

#endif /* __LIBVAS_H */
