// SPDX-License-Identifier: MPL-2.0

//! \file nix_mounts.c
//! \brief This file contains Unix/Unix-like system specific implementations for mounting and unmounting and reading
//! mounted file system information. It has some OS specific code, but is meant to be generic enough to be used
//! across multiple Unix-like OS's such as Linux, FreeBSD, NetBSD, OpenBSD, etc. Not all may be covered!!!
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "nix_mounts.h"

#include "common_types.h"
#include "io_utils.h"
#include "memory_safety.h"
#include "sleep.h"
#include "string_utils.h"

// sys/mount needed for almost all OS's.
#if !defined(_AIX) && !defined(__hpux)
#    include <sys/mount.h>
#endif

#if defined(__sun)
#    include <sys/mnttab.h> // getmntent(3C), struct mnttab
#elif defined(__linux__)
#    include <mntent.h> // setmntent, getmntent, getmntent_r, endmntent
#    include <paths.h>
#elif defined(_AIX)
#    include <sys/mntctl.h>
#    include <sys/types.h>
#    include <sys/vmount.h>
#elif defined(__hpux)
#    include <mntent.h>
#else
#    if IS_NETBSD_VERSION(3, 0, 0)
#        include <sys/statvfs.h> // getvfsstat(2), struct statvfs
#        include <sys/types.h>
#    else
#        include <sys/types.h>
#    endif
#endif

void free_spartitionInfo(spartitionInfo* pi)
{
    safe_free_core(M_REINTERPRET_CAST(void**, &pi->fsName));
    safe_free_core(M_REINTERPRET_CAST(void**, &pi->mntPath));
    safe_free_core(M_REINTERPRET_CAST(void**, &pi->mntType));
}

void free_spartitionInfo_list(spartitionInfo** list, int count)
{
    if (list != M_NULLPTR && *list != M_NULLPTR)
    {
        for (int partiter = 0; partiter < count; ++partiter)
        {
            free_spartitionInfo(&(*list)[partiter]);
        }
    }
    safe_free_core(M_REINTERPRET_CAST(void**, list));
}

// ============================================================================
//                                ITERATOR BACKENDS
// ============================================================================

enum
{
    MNT_LINE_BUF_SIZE = 4096
};

#if defined(__sun)

struct MountIter
{
    FILE* fp;
};

int mount_iter_open(MountIter* it)
{
    // /etc/mnttab is a kernel-maintained pseudo-file providing current mounts.
    // [1](https://cis.temple.edu/~ingargio/software/glibc-2.1.2/misc/mntent.h)
    it->fp = fopen("/etc/mnttab", "r");
    return it->fp ? 0 : -1;
}

int mount_iter_next(MountIter* it, MountEntry* out)
{
    struct mnttab e;
    safe_memset(&e, sizeof(struct mnttab), 0, sizeof(struct mnttab));
    // getmntent() returns 0 on success, -1 on EOF.
    // [2](https://unix.stackexchange.com/questions/743036/should-the-use-of-etc-mtab-now-be-considered-deprecated)
    if (getmntent(it->fp, &e) != 0)
    {
        return -1;
    }
    out->fsname = e.mnt_special;
    out->dir    = e.mnt_mountp;
    out->type   = e.mnt_fstype;
    return 0;
}

void mount_iter_close(MountIter* it)
{
    if (it->fp != M_NULLPTR)
    {
        fclose(it->fp);
        it->fp = M_NULLPTR;
    }
}

#elif defined(__linux__)

struct MountIter
{
    FILE*          fp;
    char           lineBuf[MNT_LINE_BUF_SIZE];
#    if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) ||                                                               \
        !defined(NO_GETMNTENT_R)
    struct mntent entBuf;
#    endif
    struct mntent* ent;
};

// Preferred: per-process mount namespace view; robust in containers and with bind mounts.
// Fallback chain: /proc/mounts, then _PATH_MOUNTED (/etc/mtab).
// [3](https://itsfoss.gitlab.io/post/etc-mtab-is-bad-when-mounting-a-cifs-share/)[4](https://sources.debian.org/src/glibc/2.41-7/misc/mntent.h/)
static FILE* open_mounts_stream(void)
{
    FILE* fp = M_NULLPTR;
    fp       = setmntent("/proc/self/mounts", "r");
    if (fp == M_NULLPTR)
    {
        return fp;
    }

    fp = setmntent("/proc/mounts", "r");
    if (fp == M_NULLPTR)
    {
        return fp;
    }

#    if defined(_PATH_MOUNTED)
    fp = setmntent(_PATH_MOUNTED, "r");
#    elif defined(MOUNTED)
    fp = setmntent(MOUNTED, "r");
#    endif
    return fp;
}

int mount_iter_open(MountIter* it)
{
    it->fp  = open_mounts_stream();
    it->ent = M_NULLPTR;
    return it->fp ? 0 : -1;
}

int mount_iter_next(MountIter* it, MountEntry* out)
{
#    if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) ||                                                               \
        !defined(NO_GETMNTENT_R) // feature test macros we're defining _BSD_SOURCE or _SVID_SOURCE in my testing, but we
                                 // want the reentrant version whenever possible. This can be defined if this function
                                 // is not identified. - TJE
    if (getmntent_r(it->fp, &it->entBuf, it->lineBuf, MNT_LINE_BUF_SIZE) == M_NULLPTR)
    {
        return -1;
    }
    it->ent = &it->entBuf;
#    else
    it->ent = getmntent(it->fp);
    if (it->ent == M_NULLPTR)
    {
        return -1;
    }
#    endif
    out->fsname = it->ent->mnt_fsname;
    out->dir    = it->ent->mnt_dir;
    out->type   = it->ent->mnt_type;
    return 0;
}

void mount_iter_close(MountIter* it)
{
    if (it->fp != M_NULLPTR)
    {
        endmntent(it->fp);
    } // close stream opened with setmntent() [4](https://sources.debian.org/src/glibc/2.41-7/misc/mntent.h/)
}

#elif defined(_AIX)

// AIX vmount strings are stored via offsets in vmt_data[];
// this helper returns a char* to the requested string.
static M_INLINE const char* vmt_get_string(const struct vmount* vm, int idx)
{
    if (vm == M_NULLPTR)
    {
        return M_NULLPTR;
    }
    // vmt_data[idx].vmt_off holds offset from start of struct vmount to the string.
    // vmt_data[idx].vmt_len holds the length (not strictly required if the string is NUL-terminated).
    const struct vmt_data* d = &vm->vmt_data[idx];
    if (d->vmt_off == 0)
    {
        return M_NULLPTR;
    }
    return M_REINTERPRET_CAST(const char*, M_REINTERPRET_CAST(const char*, vm) + d->vmt_off);
}

struct MountIter
{
    char*  buf;   // buffer returned by mntctl (array of variable-length vmount structs)
    size_t bytes; // size of buf in bytes
    int    count; // number of vmount entries
    int    idx;   // current index
};

int mount_iter_open(MountIter* it)
{
    it->buf   = M_NULLPTR;
    it->bytes = 0;
    it->count = 0;
    it->idx   = 0;

    // mntctl expects a buffer; if too small, it writes required size into the first word and returns 0.
    // [1](https://www.ibm.com/docs/en/aix/7.1.0?topic=m-mntctl-subroutine) Strategy: start with a tiny buffer, read
    // required size, allocate, then query again.
    int size = MNT_LINE_BUF_SIZE; // initial guess; will be resized
    it->buf  = M_REINTERPRET_CAST(char*, safe_malloc(int_to_sizet(size)));
    if (it->buf == M_NULLPTR)
    {
        return -1;
    }

    int mntctlresult = mntctl(MCTL_QUERY, size, it->buf);
    if (mntctlresult == 0)
    {
        // First word of buffer contains required size.
        // [1](https://www.ibm.com/docs/en/aix/7.1.0?topic=m-mntctl-subroutine)
        int   needed = *M_REINTERPRET_CAST(uint16_t*, it->buf);
        char* newbuf = M_REINTERPRET_CAST(char*, safe_realloc(it->buf, int_to_sizet(needed)));
        if (newbuf == M_NULLPTR)
        {
            safe_free_core(M_REINTERPRET_CAST(void**, &it->buf));
            it->buf = M_NULLPTR;
            return -1;
        }
        it->buf      = newbuf;
        size         = needed;
        mntctlresult = mntctl(MCTL_QUERY, size, it->buf);
    }
    if (mntctlresult < 0)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, &it->buf));
        it->buf = M_NULLPTR;
        return -1;
    }

    it->count = mntctlresult; // number of vmount structures copied into buffer
                              // [1](https://www.ibm.com/docs/en/aix/7.1.0?topic=m-mntctl-subroutine)
    it->bytes = int_to_sizet(size);
    it->idx   = 0;
    return 0;
}

int mount_iter_next(MountIter* it, MountEntry* out)
{
    if (it->buf == M_NULLPTR || it->idx >= it->count)
    {
        return -1;
    }
    // Walk variable-length vmount array: each entry's length is vm->vmt_length.
    // [1](https://www.ibm.com/docs/en/aix/7.1.0?topic=m-mntctl-subroutine) To compute the address of the Nth vmount
    // entry, iterate from the start and sum lengths. To keep iteration O(1) per step, we maintain a running pointer.
    static const struct vmount* cur            = M_NULLPTR;
    static size_t               current_offset = 0;

    if (it->idx == 0)
    {
        cur            = M_REINTERPRET_CAST(const struct vmount*, it->buf);
        current_offset = 0;
    }
    else
    {
        current_offset += cur->vmt_length;
        cur = M_REINTERPRET_CAST(const struct vmount*, it->buf + current_offset);
    }

    if (cur == M_NULLPTR)
    {
        return -1;
    }

    // Map AIX vmount strings into our normalized entry
    const char* obj    = vmt_get_string(cur, VMT_OBJECT); // e.g., /dev/hd4 or host:path
    const char* stub   = vmt_get_string(cur, VMT_STUB);   // mount point (directory)
    const char* fstype = vmt_get_string(cur, VMT_FSTYPE); // filesystem type name

    out->fsname = obj;
    out->dir    = stub;
    out->type   = fstype;

    ++it->idx;
    return 0;
}

void mount_iter_close(MountIter* it)
{
    if (it->buf != M_NULLPTR)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, &it->buf));
        it->buf = M_NULLPTR;
    }
    it->bytes = 0;
    it->count = 0;
    it->idx   = 0;
}

#elif defined(__hpux)

struct MountIter
{
    FILE* fp;
    char  lineBuf[MNT_LINE_BUF_SIZE]; // HP-UX manpage recommends ~1025; use a bit larger.
                                      // [3](https://docstore.mik.ua/manuals/hp-ux/en/B2355-60130/getmntent.3X.html)
    struct mntent* ent;
};

static FILE* open_hpux_mnttab(void)
{
    // HP-UX mount table pseudo-file. Do NOT write to it.
    // [1](https://docstore.mik.ua/manuals/hp-ux/en/B2355-60130/mnttab.4.html)
    return setmntent("/etc/mnttab", "r");
}

int mount_iter_open(MountIter* it)
{
    it->fp  = open_hpux_mnttab();
    it->ent = M_NULLPTR;
    return it->fp ? 0 : -1;
}

int mount_iter_next(MountIter* it, MountEntry* out)
{
#    if defined(HAVE_HPUX_GETMNTENT_R) || defined(HAVE_GETMNTENT_R)
    // If your build system detects getmntent_r on HP-UX, use it.
    struct mntent result;
    if (getmntent_r(it->fp, &result, it->lineBuf, MNT_LINE_BUF_SIZE) != 0)
    {
        return -1;
    }
    it->ent = &result;
#    else
    // Portable fallback: non-reentrant (document thread-safety caveat).
    it->ent = getmntent(it->fp);
    if (it->ent == M_NULLPTR)
    {
        return -1;
    }
#    endif
    out->fsname = it->ent->mnt_fsname;
    out->dir    = it->ent->mnt_dir;
    out->type   = it->ent->mnt_type;
    return 0;
}

void mount_iter_close(MountIter* it)
{
    if (it->fp != M_NULLPTR)
    {
        endmntent(it->fp);
    }
}

#else
// -------------------- BSDs (FreeBSD / OpenBSD / macOS / NetBSD) --------------------

struct MountIter
{
#    if IS_NETBSD_VERSION(3, 0, 0)
    struct statvfs* buf;
#    else
    struct statfs* buf;
#    endif
    int count;
    int idx;
};

int mount_iter_open(MountIter* it)
{
#    if IS_NETBSD_VERSION(3, 0, 0)
    // Two-step pattern: query count with M_NULLPTR, then allocate and fetch.
    // [5](https://unix.stackexchange.com/questions/69286/is-it-possible-to-tell-df-to-use-proc-mounts-instead-of-etc-mtab)
    int count = getvfsstat(M_NULLPTR, 0, ST_WAIT);
    if (count <= 0)
    {
        return -1;
    }
    size_t size = int_to_sizet(count) * sizeof(struct statvfs);
    it->buf     = M_REINTERPRET_CAST(struct statvfs*, safe_malloc(size));
    if (!it->buf)
    {
        return -1;
    }

    int got = getvfsstat(it->buf, M_STATIC_CAST(int, size), ST_WAIT);
    if (got < 0)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, &it->buf));
        it->buf = M_NULLPTR;
        return -1;
    }

    it->count = got;
#    else
    // FreeBSD/OpenBSD/macOS: getfsstat(2) + struct statfs (caller-owned buffer).
    // [6](https://uw714doc.xinuos.com/en/man/html.3G/getmntent.3G.html)[7](https://github.com/lattera/glibc/blob/master/misc/mntent.h)[8](https://www.gnu.org/software/gnulib/manual/html_node/mntent_002eh.html)
    int count = getfsstat(M_NULLPTR, 0, MNT_WAIT);
    if (count <= 0)
    {
        return -1;
    }
    size_t size = int_to_sizet(count) * sizeof(struct statfs);
    it->buf     = M_REINTERPRET_CAST(struct statfs*, safe_malloc(size));
    if (!it->buf)
    {
        return -1;
    }

    int got = getfsstat(it->buf, M_STATIC_CAST(int, size), MNT_WAIT);
    if (got < 0)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, &it->buf));
        it->buf = M_NULLPTR;
        return -1;
    }

    it->count = got;
#    endif

    it->idx = 0;
    return 0;
}

int mount_iter_next(MountIter* it, MountEntry* out)
{
    if (it->idx >= it->count)
    {
        return -1;
    }
#    if IS_NETBSD_VERSION(3, 0, 0)
    struct statvfs* s = &it->buf[it->idx++];
    out->fsname       = s->f_mntfromname;
    out->dir          = s->f_mntonname;
    out->type         = s->f_fstypename;
#    else
    struct statfs* s = &it->buf[it->idx++];
    out->fsname      = s->f_mntfromname;
    out->dir         = s->f_mntonname;
    out->type        = s->f_fstypename;
#    endif
    return 0;
}

void mount_iter_close(MountIter* it)
{
    // getfsstat()/getvfsstat() buffers are caller-owned; free them here.
    // [6](https://uw714doc.xinuos.com/en/man/html.3G/getmntent.3G.html)
    if (it->buf)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, &it->buf));
        it->buf = M_NULLPTR;
    }
}

#endif // platform selectors

// ============================================================================
//                            HIGH-LEVEL HELPERS
// ============================================================================

int get_Partition_Count(const char* blockDeviceName)
{
    int       count = 0;
    MountIter mntit;
    safe_memset(&mntit, sizeof(MountIter), 0, sizeof(MountIter));
    if (mount_iter_open(&mntit) != 0)
    {
        return -1;
    }

    MountEntry mntentry;
    safe_memset(&mntentry, sizeof(MountEntry), 0, sizeof(MountEntry));
    while (mount_iter_next(&mntit, &mntentry) == 0)
    {
        if (mntentry.fsname && blockDeviceName && strstr(mntentry.fsname, blockDeviceName))
        {
            ++count;
        }
    }

    mount_iter_close(&mntit);
    return count;
}

eReturnValues get_Partition_List(const char* blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount)
{
    if (listCount <= 0 || partitionInfoList == M_NULLPTR)
    {
        return SUCCESS;
    }

    eReturnValues ret     = SUCCESS;
    int           matches = 0;

    MountIter mntit;
    safe_memset(&mntit, sizeof(MountIter), 0, sizeof(MountIter));
    if (mount_iter_open(&mntit) != 0)
    {
        return FAILURE;
    }

    MountEntry mntentry;
    safe_memset(&mntentry, sizeof(MountEntry), 0, sizeof(MountEntry));
    while (mount_iter_next(&mntit, &mntentry) == 0)
    {
        if (mntentry.fsname && blockDeviceName && strstr(mntentry.fsname, blockDeviceName))
        {
            if (matches >= listCount)
            {
                ret = MEMORY_FAILURE;
                break;
            }

            // Initialize to M_NULLPTR to handle partial failure rollback cleanly
            partitionInfoList[matches].fsName  = M_NULLPTR;
            partitionInfoList[matches].mntPath = M_NULLPTR;
            partitionInfoList[matches].mntType = M_NULLPTR;

            errno_t err = 0;
            err |= safe_strdup(&partitionInfoList[matches].fsName, mntentry.fsname ? mntentry.fsname : "");
            err |= safe_strdup(&partitionInfoList[matches].mntPath, mntentry.dir ? mntentry.dir : "");
            err |= safe_strdup(&partitionInfoList[matches].mntType, mntentry.type ? mntentry.type : "");

            if (err != 0)
            {
                // Roll back this element to keep the list consistent
                free_spartitionInfo(&partitionInfoList[matches]);
                ret = MEMORY_FAILURE;
                break;
            }
            ++matches;
        }
    }

    mount_iter_close(&mntit);
    return ret;
}

M_PARAM_RW(1)
M_NULL_TERM_STRING(2)
M_PARAM_RO(2)
eReturnValues set_Device_Partition_Info(fileSystemInfo* fsInfo, const char* blockDevice)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;

    if (fsInfo == M_NULLPTR || blockDevice == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    partitionCount = get_Partition_Count(blockDevice);
#if defined(_DEBUG)
    printf("Partition count for %s = %d\n", blockDevice, partitionCount);
#endif

    // Initialize file system info state regardless of count
    fsInfo->fileSystemInfoValid = true;
    fsInfo->hasActiveFileSystem = false;
    fsInfo->isSystemDisk        = false;

    if (partitionCount > 0)
    {
        // Allocate list of pointer-based partition info structs
        ptrsPartitionInfo parts =
            M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts)
        {
            if (SUCCESS == get_Partition_List(blockDevice, parts, partitionCount))
            {
                for (int iter = 0; iter < partitionCount; ++iter)
                {
                    // We have at least one mount → active FS present
                    fsInfo->hasActiveFileSystem = true;

#if defined(_DEBUG)
                    printf("Found mounted file system: %s - %s (%s)\n",
                           parts[iter].fsName ? parts[iter].fsName : "(null)",
                           parts[iter].mntPath ? parts[iter].mntPath : "(null)",
                           parts[iter].mntType ? parts[iter].mntType : "(null)");
#endif
                    // Detect system disk by /boot (you can also consider "/" if desired)
                    if (parts[iter].mntPath && strncmp(parts[iter].mntPath, "/boot", 5) == 0)
                    {
                        fsInfo->isSystemDisk = true;
#if defined(_DEBUG)
                        print_str("found system disk\n");
#endif
                    }
                }
            }

            // NEW: free nested strings + array (replaces safe_free_spartition_info)
            free_spartitionInfo_list(&parts, partitionCount);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }

    return ret;
}

#define UMNT_MAX_ATTEMPTS (3)
#define UMNT_INITIAL_MS   UINT32_C(250)

static int os_unmount_normal(const char* mntPath)
{
    if (!mntPath || !*mntPath)
    {
        errno = EINVAL;
        return -1;
    }

#if defined(__linux__)
    return umount(mntPath);

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return unmount(mntPath, 0);

#elif defined(__sun)
    return umount(mntPath);

#elif defined(__hpux)
    return umount(mntPath);
#elif defined(_AIX)
    return umount(mntPath);
#else
    errno = ENOSYS;
    return -1;
#endif
}

static int os_unmount_force_then_retry_normal(const char* mntPath)
{
    int rc = -1;
    if (!mntPath || !*mntPath)
    {
        errno = EINVAL;
        return -1;
    }

#if defined(__linux__)
    rc = umount2(mntPath, MNT_FORCE);

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    rc = unmount(mntPath, MNT_FORCE);

#elif defined(__sun)
    rc = umount2(mntPath, MS_FORCE);

#elif defined(__hpux)
    rc = umount2(mntPath, MS_FORCE);
#elif defined(_AIX)
    // TODO: Use uvmount which allows a force flag. Needs a different ID to specify it though -TJE
    errno = ENOSYS;
#else
    errno = ENOSYS;
    return -1;
#endif

    if (rc == 0)
    {
        return 0;
    }

    // Save the errno after force failure
    errno_t savederr = errno;

    // Retry plain unmount if force failed due to "unsupported or busy or not-a-mount" conditions:
    // - Linux: MNT_FORCE unsupported fs usually yield EBUSY; retry plain unmount is reasonable.
    // [1](https://www.unix.com/man_page/opensolaris/3c/getmntany/)
    // - Illumos/Solaris: ENOTSUP for MS_FORCE; retry plain umount2().
    // [2](http://ibgwww.colorado.edu/~lessem/psyc5112/usail/man/solaris/mount.1.html)
    // - *BSD/macOS: EBUSY may still succeed after short delay with a normal unmount.
    // [3](https://www.redhat.com/en/blog/deprecated-linux-command-replacements)
    if (savederr == EBUSY || savederr == EINVAL
#if defined(__sun)
        || savederr == ENOTSUP
#endif
    )
    {
        uint32_t backoff_ms = UMNT_INITIAL_MS;
        for (int attempt = 0; attempt < UMNT_MAX_ATTEMPTS; ++attempt)
        {
            sleepms(backoff_ms);

            // Try plain unmount
            rc = os_unmount_normal(mntPath);
            if (rc == 0)
            {
                return 0;
            }

            // If it’s not busy/not-a-mount now, break early and return failure with latest errno
            if (errno != EBUSY && errno != EINVAL)
            {
                return -1;
            }

            backoff_ms <<= 1; // exponential backoff
        }
        // Final failure after retries; errno is from last attempt
        return -1;
    }

    // Non-retryable failure; return with original errno
    errno = savederr;
    return -1;
}

eReturnValues unmount_Partitions_From_Device(const char* blockDevice)
{
    if (!blockDevice || !*blockDevice)
    {
        return FAILURE;
    }

    MountIter mntit;
    safe_memset(&mntit, sizeof(MountIter), 0, sizeof(MountIter));
    if (mount_iter_open(&mntit) != 0)
    {
        return FAILURE;
    }

    ptrsPartitionInfo list = NULL;
    int               size = 0;
    int               cap  = 8;
    list = M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(cap), sizeof(spartitionInfo)));
    if (list == M_NULLPTR)
    {
        mount_iter_close(&mntit);
        return MEMORY_FAILURE;
    }

    MountEntry mntentry;
    safe_memset(&mntentry, sizeof(MountEntry), 0, sizeof(MountEntry));
    while (mount_iter_next(&mntit, &mntentry) == 0)
    {
        if (mntentry.fsname && strstr(mntentry.fsname, blockDevice))
        {
            if (size == cap)
            {
                cap *= 2;
                ptrsPartitionInfo temp = M_REINTERPRET_CAST(
                    ptrsPartitionInfo, safe_realloc(list, int_to_sizet(cap) * sizeof(spartitionInfo)));
                if (temp == M_NULLPTR)
                {
                    free_spartitionInfo_list(&list, size);
                    mount_iter_close(&mntit);
                    return MEMORY_FAILURE;
                }
                size_t newmemlen = int_to_sizet(cap - size) * sizeof(spartitionInfo);
                safe_memset(temp + size, newmemlen, 0, newmemlen);
                list = temp;
            }
            errno_t err = 0;
            err |= safe_strdup(&list[size].fsName, mntentry.fsname ? mntentry.fsname : "");
            err |= safe_strdup(&list[size].mntPath, mntentry.dir ? mntentry.dir : "");
            err |= safe_strdup(&list[size].mntType, mntentry.type ? mntentry.type : "");
            if (err != 0)
            {
                free_spartitionInfo_list(&list, size);
                mount_iter_close(&mntit);
                return MEMORY_FAILURE;
            }
            ++size;
        }
    }
    mount_iter_close(&mntit);

    if (size == 0)
    {
        free_spartitionInfo_list(&list, size);
        return SUCCESS;
    }

    eReturnValues ret = SUCCESS;
    for (int pathIter = 0; pathIter < size; ++pathIter)
    {
        const char* path = list[pathIter].mntPath;
        if (path == M_NULLPTR || *path == 0)
        {
            continue;
        }

        if (os_unmount_force_then_retry_normal(path) < 0)
        {
            ret = FAILURE;
#if defined(_DEBUG)
            fprintf(stderr, "unmount failed: %s (errno=%d)\n", path, errno);
#endif
        }
    }

    free_spartitionInfo_list(&list, size);
    return ret;
}
