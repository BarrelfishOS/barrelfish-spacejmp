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

///< name of the vas coordinator service
#define VAS_SERVICE_NAME "vas"

///<Maximum length of a VAS and segment identifier string.
#define VAS_NAME_MAX_LEN  32

///< virtual address space identifier
typedef uint64_t vas_id_t;

#define VAS_ID_PROCESS 0xcafebabecafebabe

///< Identifier for a currently attached VAS inside a process
typedef uintptr_t vas_handle_t;

#define VAS_HANDLE_PROCESS vas_get_proc_handle()

///< state of the virtual address space
typedef enum vas_state {
    VAS_STATE_INVALID = 0,      ///< the VAS is invalid
    VAS_STATE_DESTROIED = 1,    ///< the vas has been destroyed
    VAS_STATE_DESTROYING = 2,   ///< the vas in the process of being destroyed
    VAS_STATE_DETACHED = 3,     ///< vas is not attached to the calling process
    VAS_STATE_DETACHING = 4,    ///< the vas is being detached
    VAS_STATE_ATTACHED = 5,     ///< the vas is attached to the process
    VAS_STATE_ACTIVE = 6        ///< the vas is currently active
} vas_state_t;


///< permissions associated with a virtual address space
typedef uint32_t vas_flags_t;
#define VAS_FLAGS_PERM_READ       VREGION_FLAGS_READ
#define VAS_FLAGS_PERM_WRITE      VREGION_FLAGS_WRITE
#define VAS_FLAGS_PERM_EXEC       VREGION_FLAGS_EXECUTE
#define VAS_FLAGS_MAP_HUGE        VREGION_FLAGS_HUGE
#define VAS_FLAGS_MAP_LARGE       VREGION_FLAGS_LARGE
#define VAS_FLAGS_PERM_ALLOC      (1 << 9)
#define VAS_FLAGS_PERM_LOCAL      (1 << 10)


///< Lifetime of an address space
typedef enum vas_life_t {
    VAS_LIFE_TRANSIENT  = 0,    ///< VAS is dependent to a process
    VAS_LIFE_PERSISTENT = 1     ///< VAS lives independent of processes
} vas_life_t;


/*
 * ==============================================================================
 * Public interface
 * ==============================================================================
 */

errval_t vas_enable(void);
///> directly create a vas handle
errval_t vas_create(const char* name, vas_flags_t flags, vas_handle_t *ret_vas);
///> directly use a vas handle
errval_t vas_delete(vas_handle_t vas);
///> directly retrn a vas handle
errval_t vas_lookup(const char* name, vas_handle_t *ret_vas);
vas_handle_t vas_get_current(void);

errval_t vas_get_perm(vas_handle_t id, vas_flags_t* perm);
errval_t vas_set_perm(vas_handle_t id, vas_flags_t perm);
errval_t vas_get_life(vas_handle_t id, vas_life_t* life);
errval_t vas_set_life(vas_handle_t id, vas_life_t lifetime);

errval_t vas_attach(vas_handle_t vas, vas_flags_t flags);
errval_t vas_detach(vas_handle_t vas);
errval_t vas_switch(vas_handle_t vas);
errval_t vas_switchm(vas_handle_t vas, vas_handle_t *prev_id);

vas_state_t vas_get_state(vas_handle_t vas);
vas_id_t vas_get_id(vas_handle_t vas);

void* vas_get_root(char* root_name);
errval_t vas_set_root(char* root_name, void* value);
errval_t vas_share(vas_handle_t vas, unsigned user_id, vas_flags_t flags);

errval_t vas_tagging_enable(void);
errval_t vas_tagging_disable(void);
errval_t vas_tagging_tag(vas_handle_t vas);

vas_handle_t vas_get_current_handle(void);
vas_handle_t vas_get_proc_handle(void);

/*
 *
 */
errval_t vas_map(vas_handle_t vas, void **retaddr, struct capref frame, size_t size,
                 vregion_flags_t flags);
errval_t vas_unmap(vas_handle_t vas, void *addr);

errval_t vas_bench_cap_invoke_nop(vas_handle_t vh);

#endif /* __LIBVAS_H */
