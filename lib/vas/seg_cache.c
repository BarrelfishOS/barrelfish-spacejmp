/*
 * seg.c
 *
 *  Created on: Aug 4, 2015
 *      Author: acreto
 */


#include <barrelfish/barrelfish.h>

#define X86_64_PT_MAP_SIZE   (2UL * 1024 * 1024)
#define X86_64_PDIR_MAP_SIZE (512 * X86_64_PT_MAP_SIZE)
#define X86_64_PDPT_MAP_SIZE (512 * X86_64_PDPT_MAP_SIZE)
#define X86_64_PML4_MAP_SIZE (512 * X86_64_PML4_MAP_SIZE)

static inline bool is_same_pdir(genvaddr_t va1, genvaddr_t va2)
{
    return (va1>>X86_64_LARGE_PAGE_BITS) == ((va2-1)>>X86_64_LARGE_PAGE_BITS);
}
// returns whether va1 and va2 share a page directory pointer table entry
static inline bool is_same_pdpt(genvaddr_t va1, genvaddr_t va2)
{
    return (va1>>X86_64_HUGE_PAGE_BITS) == ((va2-1)>>X86_64_HUGE_PAGE_BITS);
}
// returns whether va1 and va2 share a page map level 4 entry
static inline bool is_same_pml4(genvaddr_t va1, genvaddr_t va2)
{
    // the base macros work here as we only have one pml4.
    return X86_64_PML4_BASE(va1) == X86_64_PML4_BASE(va2-1);
}



static errval_t verify_vaddr(lvaddr_t va, size_t size)
{
    if (size < LARGE_PAGE_SIZE) {
        /* need to be in the same leaf page table */
        if (!is_same_pdir(va, va + size )) {
            return LIB_ERR_VREGION_BAD_ALIGNMENT;
        }
    } else if (size < HUGE_PAGE_SIZE) {
        if (!is_same_pdpt(va, va + size)) {
            return LIB_ERR_VREGION_BAD_ALIGNMENT;
        }
    } else if (size < (512 * HUGE_PAGE_SIZE)) {
        if (!is_same_pml4(va, va + size )) {
            return LIB_ERR_VREGION_BAD_ALIGNMENT;
        }
    }

    return SYS_ERR_OK;
}


static uint32_t calculate_num_pt(lvaddr_t va, size_t size, size_t pagesize)
{

    uint16_t start_slot;

    if (size == 0) {
        return 0;
    }

    size = (size + pagesize - 1) & ~(pagesize-1);
    uint32_t num_pt = 1;

    if (va & (pagesize-1)) {
        num_pt += 1;
    }

    switch (pagesize) {
        case HUGE_PAGE_SIZE:

            start_slot = X86_64_PDPT_BASE(va);
            break;
        case LARGE_PAGE_SIZE:
            start_slot = X86_64_PDIR_BASE(va);

            break;
        case BASE_PAGE_SIZE:
            /* number of page tables = num_full_leaves + header + footer */
            start_slot = X86_64_PTABLE_BASE(va);
            break;
        default:
            printf("unsupported page size");
            return 0;
            break;
    }

    uint32_t num_entries = size / pagesize;


    printf("--  start slot=%u, num_entries = %u\n", start_slot, num_entries);

    if ((512 - start_slot) >= num_entries) {
        /* all fits into a single leaf page table */
        return num_pt;
    }

    /* subtract header the needed entries */
    num_entries -= (512 - start_slot);

    /* add full leaves */
    uint32_t full_leaves = (num_entries / 512);
    num_pt += full_leaves;



    /* deal with the trailer */
    if (num_entries - (full_leaves * 512)) {
        num_pt += 1;
    }


    return num_pt;
}

enum map_size {
    MAP_SIZE_BASE = 0,
    MAP_SIZE_LARGE = 1,
    MAP_SIZE_HUGE = 2,
    MAP_SIZE_MAX=3
};

enum map_rights {
    MAP_RIGHTS_READ = 0,
    MAP_RIGHTS_WRITE = 1,
    MAP_RIGHTS_READ_WRITE = 2,
    MAP_RIGHTS_READ_EXECUTE = 3,
    MAP_RIGHTS_MAX = 4
};

struct ptcache
{
    struct capref root; ///< root of the sub tree
    cslot_t start;      ///< start slot in the subtree root
    cslot_t end;        ///< end slot in the subtree root
    vregion_flags_t flags; ///< flags to be used when mapping
};

struct ptcache *cache[MAP_SIZE_MAX][MAP_RIGHTS_MAX];


struct vas_segment
{
    struct ptcache *pt;
    lvaddr_t vaddr;
    size_t size;
    struct capref frame;
    vas_flags_t flags;
};


static errval_t create_pt_cache(struct vas_segment *seg)
{
    errval_t err;

    /* figure out how many page tables are needed */

    uint8_t start_level = 1;
    uint8_t num_leves = 1;
    size_t pagesize = BASE_PAGE_SIZE;

    cslot_t cslots = 0;

#define NUM_LEVEL_MAX 3
    cslot_t num_slots[NUM_LEVEL_MAX+2];

    num_slots[NUM_LEVEL_MAX] = 1;
    num_slots[2] = calculate_num_pt(seg->vaddr, seg->size, HUGE_PAGE_SIZE);

    if (seg->flags & VREGION_FLAGS_LARGE) {
        pagesize = LARGE_PAGE_SIZE;
        num_slots[NUM_LEVEL_MAX -1] = seg->size / LARGE_PAGE_SIZE;
        num_slots[1] = calculate_num_pt(seg->vaddr, seg->size, LARGE_PAGE_SIZE);
        num_slots[1] += num_slots[NUM_LEVEL_MAX -1];
    } else if (seg->flags & VREGION_FLAGS_HUGE) {
        pagesize = HUGE_PAGE_SIZE;
        num_slots[NUM_LEVEL_MAX -1] = seg->size / HUGE_PAGE_SIZE;
        num_slots[2] += num_slots[NUM_LEVEL_MAX -1];

    } else {
        num_slots[NUM_LEVEL_MAX -1] = seg->size / BASE_PAGE_SIZE;
        num_slots[0] = calculate_num_pt(seg->vaddr, seg->size, BASE_PAGE_SIZE);
        num_slots[0] += num_slots[NUM_LEVEL_MAX -1];
        num_slots[1] = calculate_num_pt(seg->vaddr, seg->size, LARGE_PAGE_SIZE);
    }

    for (int i = 0; i < NUM_LEVEL_MAX; ++i) {
        if (num_slots[i] == 1) {
            break;
        }
        num_slots[NUM_LEVEL_MAX] += num_slots[i];
    }

    /* create a cnode for the segment */
    struct capref cnode_cap;
    struct cnode_ref cnode;
    err = cnode_create(&cnode_cap, &cnode, num_slots[NUM_LEVEL_MAX], NULL);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    enum objtype type[4] = {ObjType_VNode_x86_64_ptable, ObjType_VNode_x86_64_pdir, ObjType_VNode_x86_64_pdpt, ObjType_VNode_x86_64_pml4};


    struct capref pt_cap = {.cnode = cnode, .slot = 0};

    /* allocate memory for the page tables */
    for (int i = 0; i < NUM_LEVEL_MAX; ++i) {
        if (num_slots[i]) {

            uint8_t num_bits = log2ceil(num_slots[i]);

            /* allocate */
            struct capref ram;
            size_t objbits_vnode = vnode_objbits(type[i]);
            err = ram_alloc(&ram, objbits_vnode + num_bits);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_RAM_ALLOC);
            }

            /* retype */
            err = cap_retype(pt_cap, ram, type, objbits_vnode + num_bits);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_CAP_RETYPE);
            }

            pt_cap.slot += num_slots[i];

            err = cap_destroy(ram);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_CAP_DESTROY);
            }
        }
    }

    /* do the mappings */
    for (int i = 0; i < NUM_LEVEL_MAX; ++i) {
        //invoke_vnode_map();

    }
}
