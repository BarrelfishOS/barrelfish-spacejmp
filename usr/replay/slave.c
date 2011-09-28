#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifndef __linux__
#include <barrelfish/barrelfish.h>
#include <vfs/vfs.h>
#include <barrelfish/nameservice_client.h>
#include <if/replay_defs.h>
#include <barrelfish/bulk_transfer.h>
#else
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <errno.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include "defs.h"

#define MIN(x,y) (x < y ? x : y)

static char *defdir;
static struct {
    uint64_t op_ticks[TOPs_Total];
    uint64_t op_count[TOPs_Total];
    uint64_t total_ticks;
} Stats = {{0}, {0}, 0};

//static uint64_t total_ticks=0, open_ticks=0, read_ticks=0, unlink_ticks=0;

#ifndef __linux__
static void export_cb(void *st, errval_t err, iref_t iref)
{
    assert(err_is_ok(err));
    char name[256];
    snprintf(name, 256, "replay_slave.%u", disp_get_core_id());
    msg("%s:%s() :: === registering %s\n", __FILE__, __FUNCTION__, name);
    err = nameservice_register(name, iref);
    assert(err_is_ok(err));
}
#else
static int connsock = -1;
#endif

#define MAX_FD_CONV     256
#define MAX_DATA        (1 * 1024 * 1024)

//static int pidconv[MAX_PIDS] = { 0 };
//static FILE *fdconv[MAX_PIDS][MAX_FD_CONV];
//static int fnumconv[MAX_PIDS][MAX_FD_CONV];
//static bool writerconv[MAX_PIDS][MAX_FD_CONV];

static int openfiles = 0;
static FILE *fd2fptr[MAX_FD_CONV] = {0};
static int  fd2fname[MAX_FD_CONV] = {0};
static char data[MAX_DATA];
#ifndef __linux__
static struct bulk_transfer_slave bulk_slave;
static uint64_t tscperms;
#endif

#ifdef __linux__
static int
disp_get_core_id(void)
{
    return getpid();
}
static inline uint64_t rdtsc(void)
{
    uint32_t eax, edx;
    __asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
    return ((uint64_t)edx << 32) | eax;
}
#endif

#ifndef __linux__
static void handle_init(struct replay_binding *b, struct capref shared_mem, uint32_t size)
{
    errval_t err;
    vregion_flags_t vspace_fl;

    #ifdef __scc__
    vspace_fl = VREGION_FLAGS_READ_WRITE_MPB;
    #else
    vspace_fl = VREGION_FLAGS_READ_WRITE;
    #endif

    // Map the frame in local memory
    void *pool;
    err = vspace_map_one_frame_attr(&pool, size, shared_mem, vspace_fl, NULL, NULL);
    assert(pool != NULL);
    assert(err_is_ok(err));

    // Init receiver
    err = bulk_slave_init(pool, size, &bulk_slave);
    assert(err_is_ok(err));
    msg("%s:%s: done\n", __FILE__, __FUNCTION__);

    err = b->tx_vtbl.slave_init_reply(b, NOP_CONT);
    assert(err_is_ok(err));
}

static void handle_finish(struct replay_binding *b)
{
    errval_t err;
    err = b->tx_vtbl.slave_finish_reply(b, NOP_CONT);
    assert(err_is_ok(err));
}

void cache_print_stats(void);
static void handle_print_stats(struct replay_binding *b)
{
    errval_t err;
    msg("SLAVE[%u]: END took %" PRIu64 " ticks (%lf ms)\n", disp_get_core_id(), Stats.total_ticks, (double)Stats.total_ticks/(double)tscperms);
    for (int i=0; i<TOPs_Total; i++) {
        uint64_t op_cnt = Stats.op_count[i];
        double op_time = (double)Stats.op_ticks[i]/(double)tscperms;
        msg(" op:%d cnt:%" PRIu64  " time:%lf avg:%lf\n", i, op_cnt, op_time, op_time/(double)op_cnt);
    }
    msg("SLAVE[%u]: CACHE STATISTICS\n", disp_get_core_id());
    cache_print_stats();
    err = b->tx_vtbl.slave_print_stats_reply(b, NOP_CONT);
    assert(err_is_ok(err));
}
#endif

static void
do_handle_event(replay_eventrec_t *er)
{
    static int pid = 0;
    static int op_id = 0;

    /* pick a file for this operation */
    // (only needed for Open/Create/Unlink)
    char fname[256];
    snprintf(fname, 256, "%s/data/%u", defdir, er->fnumsize);


    /* protocol:
     * - master will send consecutive operations with the same pid
     * - the pid will change after an an Op_Exit, and a subsequent Open/Create
     */
    // sanity chacks
    if (pid == 0) { // client is not associated with a pid
        assert(er->op == TOP_Open || er->op == TOP_Create);
    } else {         // client is associated with a pid
        assert(er->pid == pid);
    }

    op_id++;
    enum top op = er->op;
    dmsg("SLAVE[%u]: REQ pid:%d op:%d [op_id:%d]\n", disp_get_core_id(), er->pid, op, op_id);
    switch(op) {
    case TOP_Open:
    case TOP_Create: {
        //uint64_t ticks = rdtsc();
        char *flags = NULL;

        if (pid == 0) {
            // new pid
            pid = er->pid;
            dmsg("SLAVE[%u]: got new pid:%d\n", disp_get_core_id(), pid);
        }

        /* set flags */
        switch(er->mode) {
        case FLAGS_RdOnly:
            flags = "r";
            break;

        case FLAGS_WrOnly:
            flags = "w";
            break;

        case FLAGS_RdWr:
            flags = "w+";
            break;
        }

        /* assert(fd2fptr[er->fd] == NULL);
         * the above assertion will fail, because some close() operations are
         * missing from the file:
         *  $ egrep -c -e 'close' <kernelcompile.trace.anon
         *  10779
         *  $ egrep -c -e '(open|creat)' <kernelcompile.trace.anon
         *  10974  */

        /* open the file */
        fd2fptr[er->fd] = fopen(fname, flags);
        fd2fname[er->fd] = er->fnumsize;
        if (fd2fptr[er->fd] != NULL) {
            openfiles++;
        } else {
            printf("Open file:%s (%s) failed\n", fname, flags);
            assert(0);
        }
        //ticks = rdtsc() - ticks;
        //msg("SLAVE[%d] OPEN %d took %lu ticks (%lf ms)\n", disp_get_core_id(), opencnt++, ticks, (double)ticks/(double)tscperms);
        break;
    }

    case TOP_Unlink: {
        int ret = unlink(fname);
        assert(ret != -1);
        break;
    }

    case TOP_Read: {
        //uint64_t ticks = rdtsc();
        if (er->fnumsize > MAX_DATA) {
            printf("er->fnumsize == %u\n", er->fnumsize);
            assert(0);
        }
        FILE *fptr = fd2fptr[er->fd];
        assert(fptr != NULL);
        int ret = fread(data, 1, er->fnumsize, fptr);
        if (ret != er->fnumsize) {
            msg("[R] op_id:%d er->fnumsize:%u, read:%d fname:%d pid:%d error:%d eof:%d pos:%ld\n", op_id, er->fnumsize, ret, fd2fname[er->fd], er->pid, ferror(fptr), feof(fptr), ftell(fptr));
        }
        //ticks = rdtsc() - ticks;
        //msg("SLAVE[%d] READ %d took %lu ticks (%lf ms)\n", disp_get_core_id(), rdcnt++, ticks, (double)ticks/(double)tscperms);
        break;
    }

    case TOP_Write: {
        if (er->fnumsize > MAX_DATA) {
            printf("er->fnumsize == %u\n", er->fnumsize);
            assert(0);
        }
        FILE *fptr = fd2fptr[er->fd];
        assert(fptr != NULL);
        int ret = fwrite(data, 1, er->fnumsize, fptr);
        if (ret != er->fnumsize) {
            msg("[W] op_id:%d er->fnumsize:%u, write:%d fname:%d pid:%d error:%d eof:%d pos:%ld\n", op_id, er->fnumsize, ret, fd2fname[er->fd], er->pid, ferror(fptr), feof(fptr), ftell(fptr));
        }
        break;
    }

    case TOP_Close: {
        FILE *fptr = fd2fptr[er->fd];
        assert(fptr != NULL);
        //uint64_t ticks = rdtsc();
        int ret = fclose(fptr);
        //ticks = rdtsc() - ticks;
        //msg("SLAVE[%d] CLOSE %d took %lu ticks (%lf ms)\n", disp_get_core_id(), closecnt++, ticks, (double)ticks/(double)tscperms);
        assert(ret == 0);
        openfiles--;
        fd2fptr[er->fd] = NULL;
        fd2fname[er->fd] = 0;
        break;
    }

    #if 0
    case TOP_End: {
        dmsg("SLAVE[%u]: END\n", disp_get_core_id());
        //total_ticks += (rdtsc() - handle_ticks);
        //msg("SLAVE[%u]: END took %lu ticks (%lf ms)\n", disp_get_core_id(), total_ticks, (double)total_ticks/(double)tscperms);
        errval_t err = b->tx_vtbl.finished(b, NOP_CONT);
        assert(err_is_ok(err));
        break;
    }
    #endif

    case TOP_Exit: {
        dmsg("SLAVE[%u]: TOP_Exit on %d\n", disp_get_core_id(), er->pid);
        pid = 0;
        break;
    }

    default:
        printf("Invalid request: %d\n", op);
        assert(0);
        break;
    }
}

static void
handle_event(replay_eventrec_t *er)
{
    uint64_t handle_ticks = rdtsc();
    enum top op = er->op;

    do_handle_event(er);

    /* update stats */
    handle_ticks = (rdtsc() - handle_ticks);
    Stats.total_ticks += handle_ticks;
    Stats.op_count[op]++;
    Stats.op_ticks[op] += handle_ticks;
}
#ifndef __linux__

static void handle_new_task(struct replay_binding *b, uint64_t bulk_id, uint32_t tes_size)
{
    errval_t err;
    replay_eventrec_t *tes;
    size_t tes_nr;
    int pid;

    tes = bulk_slave_buf_get_mem(&bulk_slave, bulk_id, NULL);
    tes_nr = tes_size / sizeof(replay_eventrec_t);
    assert(tes_size % sizeof(replay_eventrec_t) == 0);

    assert(tes->op == TOP_Open);
    pid = tes->pid;
    for (size_t i=0; i<tes_nr; i++) {
        replay_eventrec_t *er = tes + i;
        handle_event(er);
    }

    err = b->tx_vtbl.task_completed(b, NOP_CONT, pid, bulk_id);
    assert(err_is_ok(err));
}


static struct replay_rx_vtbl replay_vtbl = {
    .new_task          = handle_new_task,
    .slave_init        = handle_init,
    .slave_finish      = handle_finish,
    .slave_print_stats = handle_print_stats
};

static errval_t connect_cb(void *st, struct replay_binding *b)
{
    b->rx_vtbl = replay_vtbl;
    return SYS_ERR_OK;
}
#endif

int main(int argc, char *argv[])
{
#ifndef __linux__
    assert(argc >= 4);
#else
    const int default_port = 1234;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <data_dir> [port (default:%d)]\n", argv[0], default_port);
        exit(1);
    }
#endif
    defdir = argv[1];

    msg("===================> replay slave up\n");
#ifndef __linux__
    assert(err_is_ok(sys_debug_get_tsc_per_ms(&tscperms)));

    errval_t err = vfs_mkdir(argv[2]);
    if(err_is_fail(err) && err_no(err) != FS_ERR_EXISTS) {
        DEBUG_ERR(err, "vfs_mkdir");
    }
    /* assert(err_is_ok(err)); */

    err = vfs_mount(argv[2], argv[3]);
    assert(err_is_ok(err));

    err = replay_export(NULL, export_cb, connect_cb,
                        get_default_waitset(),
                        IDC_EXPORT_FLAGS_DEFAULT);
    assert(err_is_ok(err));

    msg("%s:%s() :: slave starts servicing requests\n", __FILE__, __FUNCTION__);
    for(;;) {
        err = event_dispatch(get_default_waitset());
        assert(err_is_ok(err));
    }
#else
    /* { */
    /*     struct rlimit rl; */
    /*     rl.rlim_cur = 2048; */
    /*     rl.rlim_max = 2050; */
    /*     int r = setrlimit(RLIMIT_NOFILE, &rl); */
    /*     if(r == -1) { */
    /*         printf("setrlimit errno = %s\n", strerror(errno)); */
    /*     } */
    /*     assert(r == 0); */
    /* } */

    int port = argc > 2 ? atoi(argv[2]) : default_port;
    // Listen on port 1234
    int listensock = socket(AF_INET, SOCK_STREAM, 0);
    assert(listensock != -1);
    struct sockaddr_in a = {
        .sin_family = PF_INET,
        .sin_port = htons(port),
        .sin_addr = {
            .s_addr = htonl(INADDR_ANY)
        }
    };
    int r = bind(listensock, (struct sockaddr *)&a, sizeof(a));
    if(r == -1) {
        printf("bind: %s\n", strerror(errno));
    }
    assert(r == 0);
    r = listen(listensock, 5);
    assert(r == 0);

    socklen_t sizea = sizeof(a);
    connsock = accept(listensock, (struct sockaddr *)&a, &sizea);
    assert(connsock != -1);
    assert(sizea == sizeof(a));

    int from = (ntohl(a.sin_addr.s_addr) & 0xff) - 1;
    printf("got connection from %d\n", from);


    /* circular buffer for events */
    const size_t er_size_elems = 2<<12; /* size in elements */
    replay_eventrec_t ers[er_size_elems];
    const size_t er_size = sizeof(ers);  /* size in bytes */
    uint64_t er_r, er_w;                /* full indices (in bytes) */

    er_r = er_w = 0;

    for(;;) {

        dmsg("r:%zd w:%zd er_size:%zd\n", er_r, er_w, er_size);
        size_t er_avail = er_size - (er_w - er_r);
        size_t er_w_idx = er_w % er_size;
        char *xfer_start = (char *)ers + er_w_idx;
        size_t xfer_len = MIN(er_avail, er_size - er_w_idx);

        /* fetch events */
        dmsg("RECV: from:%zd len:%zd\n", er_w, xfer_len);
        if (xfer_len == 0) {
            continue;
        }
        ssize_t ret = recv(connsock, xfer_start, xfer_len, 0);
        if(ret == -1) {
            perror("recv");
            exit(1);
        } else if (ret == 0) {
            printf("end of session\n");
            break;
        }
        dmsg("GOT DATA=%zd!\n", ret);
        er_w += ret;

        /* handle events */
        assert(er_r % sizeof(replay_eventrec_t) == 0);
        assert(er_w > er_r);
        while (er_w - er_r >= sizeof(replay_eventrec_t)) {
            size_t er_r_items = er_r / sizeof(replay_eventrec_t);
            replay_eventrec_t *er = ers + (er_r_items % er_size_elems);
            handle_event(er);

            // notify server that we are done with a task
            if (er->op == TOP_Exit) {
                uint16_t pid = er->pid;
                dmsg("SENDING PID: %d\n", pid);
                if (send(connsock, &pid, sizeof(pid), 0) != sizeof(pid)) {
                    perror("send");
                    exit(1);
                }
            }

            // increase read pointer
            er_r += sizeof(replay_eventrec_t);
        }
    }
#endif

    return 0;
}