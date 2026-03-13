//
//  m3_api_esp_wasi.c
//
//  Created by Volodymyr Shymanskyy on 01/07/20.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_esp_wasi.h"

#include "m3_env.h"
#include "m3_exception.h"

#if defined(ESP32)

typedef uint32_t __wasi_size_t;
#include "extra/wasi_core.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static m3_wasi_context_t* wasi_context;

typedef struct wasi_iovec_t
{
    __wasi_size_t buf;
    __wasi_size_t buf_len;
} wasi_iovec_t;

#define PREOPEN_CNT   3

typedef struct Preopen {
    int         fd;
    char*       path;
} Preopen;

Preopen preopen[PREOPEN_CNT] = {
    {  0, "<stdin>"  },
    {  1, "<stdout>" },
    {  2, "<stderr>" },
};

#  define APE_SWITCH_BEG          switch (errnum) {
#  define APE_SWITCH_END          }
#  define APE_CASE_RET(e1,e2)     case e1:   return e2;   break;

static
__wasi_errno_t errno_to_wasi(int errnum) {
    APE_SWITCH_BEG
    APE_CASE_RET( EPERM   , __WASI_ERRNO_PERM   )
    APE_CASE_RET( ENOENT  , __WASI_ERRNO_NOENT  )
    APE_CASE_RET( ESRCH   , __WASI_ERRNO_SRCH   )
    APE_CASE_RET( EINTR   , __WASI_ERRNO_INTR   )
    APE_CASE_RET( EIO     , __WASI_ERRNO_IO     )
    APE_CASE_RET( ENXIO   , __WASI_ERRNO_NXIO   )
    APE_CASE_RET( E2BIG   , __WASI_ERRNO_2BIG   )
    APE_CASE_RET( ENOEXEC , __WASI_ERRNO_NOEXEC )
    APE_CASE_RET( EBADF   , __WASI_ERRNO_BADF   )
    APE_CASE_RET( ECHILD  , __WASI_ERRNO_CHILD  )
    APE_CASE_RET( EAGAIN  , __WASI_ERRNO_AGAIN  )
    APE_CASE_RET( ENOMEM  , __WASI_ERRNO_NOMEM  )
    APE_CASE_RET( EACCES  , __WASI_ERRNO_ACCES  )
    APE_CASE_RET( EFAULT  , __WASI_ERRNO_FAULT  )
    APE_CASE_RET( EBUSY   , __WASI_ERRNO_BUSY   )
    APE_CASE_RET( EEXIST  , __WASI_ERRNO_EXIST  )
    APE_CASE_RET( EXDEV   , __WASI_ERRNO_XDEV   )
    APE_CASE_RET( ENODEV  , __WASI_ERRNO_NODEV  )
    APE_CASE_RET( ENOTDIR , __WASI_ERRNO_NOTDIR )
    APE_CASE_RET( EISDIR  , __WASI_ERRNO_ISDIR  )
    APE_CASE_RET( EINVAL  , __WASI_ERRNO_INVAL  )
    APE_CASE_RET( ENFILE  , __WASI_ERRNO_NFILE  )
    APE_CASE_RET( EMFILE  , __WASI_ERRNO_MFILE  )
    APE_CASE_RET( ENOTTY  , __WASI_ERRNO_NOTTY  )
    APE_CASE_RET( ETXTBSY , __WASI_ERRNO_TXTBSY )
    APE_CASE_RET( EFBIG   , __WASI_ERRNO_FBIG   )
    APE_CASE_RET( ENOSPC  , __WASI_ERRNO_NOSPC  )
    APE_CASE_RET( ESPIPE  , __WASI_ERRNO_SPIPE  )
    APE_CASE_RET( EROFS   , __WASI_ERRNO_ROFS   )
    APE_CASE_RET( EMLINK  , __WASI_ERRNO_MLINK  )
    APE_CASE_RET( EPIPE   , __WASI_ERRNO_PIPE   )
    APE_CASE_RET( EDOM    , __WASI_ERRNO_DOM    )
    APE_CASE_RET( ERANGE  , __WASI_ERRNO_RANGE  )
    APE_SWITCH_END
    return __WASI_ERRNO_INVAL;
}

static inline
int convert_clockid(__wasi_clockid_t in) {
    switch (in) {
    case __WASI_CLOCKID_MONOTONIC:            return CLOCK_MONOTONIC;
    //case __WASI_CLOCKID_PROCESS_CPUTIME_ID:   return CLOCK_PROCESS_CPUTIME_ID;
    case __WASI_CLOCKID_REALTIME:             return CLOCK_REALTIME;
    //case __WASI_CLOCKID_THREAD_CPUTIME_ID:    return CLOCK_THREAD_CPUTIME_ID;
    default: return -1;
    }
}

static inline
__wasi_timestamp_t convert_timespec(const struct timespec *ts) {
    if (ts->tv_sec < 0)
        return 0;
    if ((__wasi_timestamp_t)ts->tv_sec >= UINT64_MAX / 1000000000)
        return UINT64_MAX;
    return (__wasi_timestamp_t)ts->tv_sec * 1000000000 + ts->tv_nsec;
}


/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_generic_args_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t             , argv_offset)
    m3ApiGetArgMem   (uint32_t             , argv_buf_offset)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(__WASI_ERRNO_INVAL); }

    m3ApiCheckMem(argv_offset, context->argc * sizeof(uint32_t));

    uint32_t buf_cursor = argv_buf_offset;
    for (u32 i = 0; i < context->argc; ++i)
    {
        m3ApiWriteMem32(argv_offset + i * sizeof(uint32_t), buf_cursor);

        size_t len = strlen (context->argv[i]);

        m3ApiCheckMem(buf_cursor, len + 1);
        m3ApiMemStore(buf_cursor, context->argv[i], len);
        buf_cursor += len;
        uint8_t zero = 0;
        m3ApiMemStore(buf_cursor, &zero, 1);
        buf_cursor++;
    }

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_args_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t             , argc_offset)
    m3ApiGetArgMem   (uint32_t             , argv_buf_size_offset)

    m3ApiCheckMem(argc_offset,             sizeof(__wasi_size_t));
    m3ApiCheckMem(argv_buf_size_offset,    sizeof(__wasi_size_t));

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context == NULL) { m3ApiReturn(__WASI_ERRNO_INVAL); }

    __wasi_size_t buf_len = 0;
    for (u32 i = 0; i < context->argc; ++i)
    {
        buf_len += strlen (context->argv[i]) + 1;
    }

    m3ApiWriteMem32(argc_offset, context->argc);
    m3ApiWriteMem32(argv_buf_size_offset, buf_len);

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_environ_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t             , env_offset)
    m3ApiGetArgMem   (uint32_t             , env_buf_offset)

    // TODO
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_environ_sizes_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t             , env_count_offset)
    m3ApiGetArgMem   (uint32_t             , env_buf_size_offset)

    m3ApiCheckMem(env_count_offset,    sizeof(__wasi_size_t));
    m3ApiCheckMem(env_buf_size_offset, sizeof(__wasi_size_t));

    // TODO
    m3ApiWriteMem32(env_count_offset,    0);
    m3ApiWriteMem32(env_buf_size_offset, 0);

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_dir_name)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t             , path_offset)
    m3ApiGetArg      (__wasi_size_t        , path_len)

    m3ApiCheckMem(path_offset, path_len);

    if (fd < 3 || fd >= PREOPEN_CNT) { m3ApiReturn(__WASI_ERRNO_BADF); }
    size_t slen = strlen(preopen[fd].path) + 1;
    size_t copy_len = M3_MIN(slen, path_len);
    m3ApiMemStore(path_offset, preopen[fd].path, copy_len);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_prestat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t             , buf_offset)

    m3ApiCheckMem(buf_offset, 8);

    if (fd < 3 || fd >= PREOPEN_CNT) { m3ApiReturn(__WASI_ERRNO_BADF); }

    m3ApiWriteMem32(buf_offset+0, __WASI_PREOPENTYPE_DIR);
    m3ApiWriteMem32(buf_offset+4, strlen(preopen[fd].path) + 1);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t             , fdstat_offset)

    m3ApiCheckMem(fdstat_offset, sizeof(__wasi_fdstat_t));

    __wasi_fdstat_t fdstat;
    memset(&fdstat, 0, sizeof(fdstat));

    struct stat fd_stat;
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) { m3ApiReturn(errno_to_wasi(errno)); }
    fstat(fd, &fd_stat);
    int mode = fd_stat.st_mode;
    fdstat.fs_filetype = (S_ISBLK(mode)   ? __WASI_FILETYPE_BLOCK_DEVICE     : 0) |
                          (S_ISCHR(mode)   ? __WASI_FILETYPE_CHARACTER_DEVICE : 0) |
                          (S_ISDIR(mode)   ? __WASI_FILETYPE_DIRECTORY        : 0) |
                          (S_ISREG(mode)   ? __WASI_FILETYPE_REGULAR_FILE     : 0) |
                          (S_ISLNK(mode)   ? __WASI_FILETYPE_SYMBOLIC_LINK    : 0);
    fdstat.fs_flags =  ((fl & O_APPEND)    ? __WASI_FDFLAGS_APPEND    : 0) |
                       ((fl & O_NONBLOCK)  ? __WASI_FDFLAGS_NONBLOCK  : 0) |
                       ((fl & O_SYNC)      ? __WASI_FDFLAGS_SYNC      : 0);

    fdstat.fs_rights_base = (uint64_t)-1;
    if (fd <= 2) {
        fdstat.fs_rights_base &= ~(__WASI_RIGHTS_FD_SEEK | __WASI_RIGHTS_FD_TELL);
    }
    fdstat.fs_rights_inheriting = (uint64_t)-1;
    m3ApiMemStore(fdstat_offset, &fdstat, sizeof(fdstat));
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_fdstat_set_flags)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_fdflags_t     , flags)

    // TODO

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_filedelta_t   , offset)
    m3ApiGetArg      (uint32_t             , wasi_whence)
    m3ApiGetArgMem   (uint32_t             , result_offset)

    m3ApiCheckMem(result_offset, sizeof(__wasi_filesize_t));

    int whence;

    switch (wasi_whence) {
    case 0: whence = SEEK_CUR; break;
    case 1: whence = SEEK_END; break;
    case 2: whence = SEEK_SET; break;
    default:                m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    int64_t ret;
    ret = lseek(fd, offset, whence);
    if (ret < 0) { m3ApiReturn(errno_to_wasi(errno)); }
    m3ApiWriteMem64(result_offset, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_snapshot_preview1_fd_seek)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArg      (__wasi_filedelta_t   , offset)
    m3ApiGetArg      (uint32_t             , wasi_whence)
    m3ApiGetArgMem   (uint32_t             , result_offset)

    m3ApiCheckMem(result_offset, sizeof(__wasi_filesize_t));

    int whence;

    switch (wasi_whence) {
    case 0: whence = SEEK_SET; break;
    case 1: whence = SEEK_CUR; break;
    case 2: whence = SEEK_END; break;
    default:                m3ApiReturn(__WASI_ERRNO_INVAL);
    }

    int64_t ret;
    ret = lseek(fd, offset, whence);
    if (ret < 0) { m3ApiReturn(errno_to_wasi(errno)); }
    m3ApiWriteMem64(result_offset, ret);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}


m3ApiRawFunction(m3_wasi_generic_path_open)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , dirfd)
    m3ApiGetArg      (__wasi_lookupflags_t , dirflags)
    m3ApiGetArgMem   (uint32_t             , path_offset)
    m3ApiGetArg      (__wasi_size_t        , path_len)
    m3ApiGetArg      (__wasi_oflags_t      , oflags)
    m3ApiGetArg      (__wasi_rights_t      , fs_rights_base)
    m3ApiGetArg      (__wasi_rights_t      , fs_rights_inheriting)
    m3ApiGetArg      (__wasi_fdflags_t     , fs_flags)
    m3ApiGetArgMem   (uint32_t             , fd_offset)

    m3ApiCheckMem(path_offset, path_len);
    m3ApiCheckMem(fd_offset,   sizeof(__wasi_fd_t));

    if (path_len >= 512)
        m3ApiReturn(__WASI_ERRNO_INVAL);

    char host_path[path_len+1];
    m3ApiMemLoad(host_path, path_offset, path_len);
    host_path[path_len] = '\0';

    // TODO
    m3ApiReturn(__WASI_ERRNO_NOSYS);
}

m3ApiRawFunction(m3_wasi_generic_fd_read)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t             , wasi_iovs_offset)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (uint32_t             , nread_offset)

    m3ApiCheckMem(wasi_iovs_offset, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread_offset,     sizeof(__wasi_size_t));

    ssize_t res = 0;
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        uint32_t iov_off = wasi_iovs_offset + i * sizeof(wasi_iovec_t);
        uint32_t buf_off = m3ApiReadMem32(iov_off);
        uint32_t buf_len = m3ApiReadMem32(iov_off + sizeof(uint32_t));
        if (buf_len == 0) continue;
        m3ApiCheckMem(buf_off, buf_len);

        uint8_t* tmp = (uint8_t*)alloca(buf_len);
        int ret = read(fd, tmp, buf_len);
        if (ret < 0) m3ApiReturn(errno_to_wasi(errno));
        m3ApiMemStore(buf_off, tmp, ret);
        res += ret;
        if ((size_t)ret < buf_len) break;
    }
    m3ApiWriteMem32(nread_offset, res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_write)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t          , fd)
    m3ApiGetArgMem   (uint32_t             , wasi_iovs_offset)
    m3ApiGetArg      (__wasi_size_t        , iovs_len)
    m3ApiGetArgMem   (uint32_t             , nwritten_offset)

    m3ApiCheckMem(wasi_iovs_offset, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten_offset,  sizeof(__wasi_size_t));

    ssize_t res = 0;
    for (__wasi_size_t i = 0; i < iovs_len; i++) {
        uint32_t iov_off = wasi_iovs_offset + i * sizeof(wasi_iovec_t);
        uint32_t buf_off = m3ApiReadMem32(iov_off);
        uint32_t buf_len = m3ApiReadMem32(iov_off + sizeof(uint32_t));
        if (buf_len == 0) continue;
        m3ApiCheckMem(buf_off, buf_len);

        uint8_t* tmp = (uint8_t*)alloca(buf_len);
        m3ApiMemLoad(tmp, buf_off, buf_len);
        int ret = write(fd, tmp, buf_len);
        if (ret < 0) m3ApiReturn(errno_to_wasi(errno));
        res += ret;
        if ((size_t)ret < buf_len) break;
    }
    m3ApiWriteMem32(nwritten_offset, res);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_fd_close)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    int ret = close(fd);
    m3ApiReturn(ret == 0 ? __WASI_ERRNO_SUCCESS : ret);
}

m3ApiRawFunction(m3_wasi_generic_fd_datasync)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_fd_t, fd)

    // TODO
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_random_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArgMem   (uint32_t             , buf_offset)
    m3ApiGetArg      (__wasi_size_t        , buf_len)

    m3ApiCheckMem(buf_offset, buf_len);

    uint8_t* host_buf = (uint8_t*)alloca(buf_len);
    // Fill with pseudo-random for ESP32
    for (__wasi_size_t i = 0; i < buf_len; i++) {
        host_buf[i] = (uint8_t)(esp_random() & 0xFF);
    }
    m3ApiMemStore(buf_offset, host_buf, buf_len);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_clock_res_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArgMem   (uint32_t             , resolution_offset)

    m3ApiCheckMem(resolution_offset, sizeof(__wasi_timestamp_t));

    int clk = convert_clockid(wasi_clk_id);
    if (clk < 0) m3ApiReturn(__WASI_ERRNO_INVAL);

    struct timespec tp;
    if (clock_getres(clk, &tp) != 0) {
        m3ApiWriteMem64(resolution_offset, 1000000);
    } else {
        m3ApiWriteMem64(resolution_offset, convert_timespec(&tp));
    }

    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_clock_time_get)
{
    m3ApiReturnType  (uint32_t)
    m3ApiGetArg      (__wasi_clockid_t     , wasi_clk_id)
    m3ApiGetArg      (__wasi_timestamp_t   , precision)
    m3ApiGetArgMem   (uint32_t             , time_offset)

    m3ApiCheckMem(time_offset, sizeof(__wasi_timestamp_t));

    int clk = convert_clockid(wasi_clk_id);
    if (clk < 0) m3ApiReturn(__WASI_ERRNO_INVAL);

    struct timespec tp;
    if (clock_gettime(clk, &tp) != 0) {
        m3ApiReturn(errno_to_wasi(errno));
    }

    m3ApiWriteMem64(time_offset, convert_timespec(&tp));
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_generic_proc_exit)
{
    m3ApiGetArg      (uint32_t, code)

    m3_wasi_context_t* context = (m3_wasi_context_t*)(_ctx->userdata);

    if (context) {
        context->exit_code = code;
    }

    m3ApiTrap(m3Err_trapExit);
}


static
M3Result SuppressLookupFailure(M3Result i_result)
{
    if (i_result == m3Err_functionLookupFailed)
        return m3Err_none;
    else
        return i_result;
}

m3_wasi_context_t* m3_GetWasiContext()
{
    return wasi_context;
}


M3Result  m3_LinkEspWASI  (IM3Module module)
{
    M3Result result = m3Err_none;

    // TODO: Preopen dirs

    if (!wasi_context) {
        wasi_context = (m3_wasi_context_t*)malloc(sizeof(m3_wasi_context_t));
        wasi_context->exit_code = 0;
        wasi_context->argc = 0;
        wasi_context->argv = 0;
    }

    static const char* namespaces[2] = { "wasi_unstable", "wasi_snapshot_preview1" };

    // fd_seek is incompatible
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_unstable",          "fd_seek",     "i(iIi*)", &m3_wasi_unstable_fd_seek)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, "wasi_snapshot_preview1", "fd_seek",     "i(iIi*)", &m3_wasi_snapshot_preview1_fd_seek)));

    for (int i=0; i<2; i++)
    {
        const char* wasi = namespaces[i];

_       (SuppressLookupFailure (m3_LinkRawFunctionEx (module, wasi, "args_get",           "i(**)",   &m3_wasi_generic_args_get, wasi_context)));
_       (SuppressLookupFailure (m3_LinkRawFunctionEx (module, wasi, "args_sizes_get",     "i(**)",   &m3_wasi_generic_args_sizes_get, wasi_context)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_res_get",        "i(i*)",   &m3_wasi_generic_clock_res_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "clock_time_get",       "i(iI*)",  &m3_wasi_generic_clock_time_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_get",          "i(**)",   &m3_wasi_generic_environ_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "environ_sizes_get",    "i(**)",   &m3_wasi_generic_environ_sizes_get)));

//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_advise",            "i(iIIi)", )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_allocate",          "i(iII)",  )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_close",             "i(i)",    &m3_wasi_generic_fd_close)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_datasync",          "i(i)",    &m3_wasi_generic_fd_datasync)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_get",        "i(i*)",   &m3_wasi_generic_fd_fdstat_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_set_flags",  "i(ii)",   &m3_wasi_generic_fd_fdstat_set_flags)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_fdstat_set_rights", "i(iII)",  )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_filestat_get",      "i(i*)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_filestat_set_size", "i(iI)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_filestat_set_times","i(iIIi)", )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_pread",             "i(i*iI*)",)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_get",       "i(i*)",   &m3_wasi_generic_fd_prestat_get)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_prestat_dir_name",  "i(i*i)",  &m3_wasi_generic_fd_prestat_dir_name)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_pwrite",            "i(i*iI*)",)));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_read",              "i(i*i*)", &m3_wasi_generic_fd_read)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_readdir",           "i(i*iI*)",)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_renumber",          "i(ii)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_sync",              "i(i)",    )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_tell",              "i(i*)",   )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_write",             "i(i*i*)", &m3_wasi_generic_fd_write)));

//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_create_directory",    "i(i*i)",       )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_filestat_get",        "i(ii*i*)",     &m3_wasi_generic_path_filestat_get)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_filestat_set_times",  "i(ii*iIIi)",   )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_link",                "i(ii*ii*i)",   )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_open",                "i(ii*iiIIi*)", &m3_wasi_generic_path_open)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_readlink",            "i(i*i*i*)",    )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_remove_directory",    "i(i*i)",       )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_rename",              "i(i*ii*i)",    )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_symlink",             "i(*ii*i)",     )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "path_unlink_file",         "i(i*i)",       )));

//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "poll_oneoff",          "i(**i*)", &m3_wasi_generic_poll_oneoff)));
_       (SuppressLookupFailure (m3_LinkRawFunctionEx (module, wasi, "proc_exit",          "v(i)",    &m3_wasi_generic_proc_exit, wasi_context)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "proc_raise",           "i(i)",    )));
_       (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "random_get",           "i(*i)",   &m3_wasi_generic_random_get)));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sched_yield",          "i()",     )));

//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_recv",            "i(i*ii**)",        )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_send",            "i(i*ii*)",         )));
//_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "sock_shutdown",        "i(ii)",            )));
    }

_catch:
    return result;
}

#endif // ESP32

