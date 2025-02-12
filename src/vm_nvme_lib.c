// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "sort_and_search.h"
#include "string_utils.h"
#include "type_conversion.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vmkapi.h>

#include "vm_nvme_lib.h"

/*****************************************************************************
 * Global Variables
 ****************************************************************************/

struct nvme_adapter_list adapterList;

/*****************************************************************************
 * NVMe Management Ops
 ****************************************************************************/

/**
 * Open a handle to the specified vmhba device
 *
 * @param [in] name name of the vmhba
 *
 * @return pointer to the device handle if successful; M_NULLPTR specified vmhba
 *         is not a valid NVM Express device.
 */
struct nvme_handle* Nvme_Open(struct nvme_adapter_list* adapters, const char* name)
{
    struct nvme_handle*     handle;
    struct nvmeAdapterInfo* adapter;
    vmk_MgmtApiSignature    signature;
    int                     i;
    int                     rc;

    assert(adapters != M_NULLPTR);
    assert(name != M_NULLPTR);

    adapter = M_NULLPTR;
    for (i = 0; i < adapters->count; i++)
    {
        if (strcmp(name, adapters->adapters[i].name) == 0)
        {
            adapter = &adapters->adapters[i];
            break;
        }
    }
    if (adapter == M_NULLPTR)
    {
        return M_NULLPTR;
    }

    handle = C_CAST(struct nvme_handle*, safe_malloc(sizeof(struct nvme_handle)));
    if (!handle)
    {
        return M_NULLPTR;
    }

    snprintf_err_handle(handle->name, VMK_MISC_NAME_MAX, "%s", name);

    signature.version = VMK_REVISION_FROM_NUMBERS(NVME_MGMT_MAJOR, NVME_MGMT_MINOR, NVME_MGMT_UPDATE, NVME_MGMT_PATCH);
    snprintf_err_handle(signature.name.string, sizeof(signature.name.string), "%s", adapter->signature);
    snprintf_err_handle(signature.vendor.string, sizeof(signature.vendor.string), NVME_MGMT_VENDOR);
    signature.numCallbacks = NVME_MGMT_CTRLR_NUM_CALLBACKS;
    signature.callbacks    = nvmeCallbacks;

    rc = vmk_MgmtUserInit(&signature, 0LL, &handle->handle);
    if (rc)
    {
        free(handle);
        return M_NULLPTR;
    }

    return handle;
}

/**
 * Close a handle
 *
 * @param [in] handle pointer to the device handle
 */
void Nvme_Close(struct nvme_handle* handle)
{
    assert(handle);

    if (!handle || handle->handle == M_NULLPTR)
    {
        return;
    }

    vmk_MgmtUserDestroy(handle->handle);
    free(handle);
}

/**
 * Get device management signature
 *
 * @param [in] vmhba name of the vmhba
 * @param [out] signature buffer to hold the signature name
 * @param [in] length signature buffer length
 *
 * @return 0 if successful
 */
int Nvme_GetAdapterList(struct nvme_adapter_list* list)
{
    vmk_MgmtUserHandle driverHandle;
    int                rc;

    assert(list != M_NULLPTR);

    rc = vmk_MgmtUserInit(&globalSignature, 0LL, &driverHandle);
    if (rc)
    {
        return rc;
    }

    rc = vmk_MgmtUserCallbackInvoke(driverHandle, 0LL, NVME_MGMT_GLOBAL_CB_LISTADAPTERS, &list->count, &list->adapters);
    if (rc)
    {
        vmk_MgmtUserDestroy(driverHandle);
        return rc;
    }

    vmk_MgmtUserDestroy(driverHandle);
    return 0;
}

/**
 * Set driver parameter: nvme_log_level and nvme_dbg.
 *
 * @param [in] log level
 * @param [in] debug level
 *
 * @return 0 if successful
 */
int Nvme_SetLogLevel(int loglevel, int debuglevel)
{
    vmk_MgmtUserHandle driverHandle;
    int                rc = 0;

    rc = vmk_MgmtUserInit(&globalSignature, 0LL, &driverHandle);
    if (rc)
    {
        return rc;
    }

    rc = vmk_MgmtUserCallbackInvoke(driverHandle, 0LL, NVME_MGMT_GLOBAL_CB_SETLOGLEVEL, &loglevel, &debuglevel);

    vmk_MgmtUserDestroy(driverHandle);
    return rc;
}

/**
 * Issue Ioctl command to a device
 *
 * @param [in] handle handle to a device
 * @param [in] cmd Ioctl command to be executed.
 * @param [inout] uio pointer to uio data structure
 *
 * @return 0 if successful
 */
int Nvme_Ioctl(struct nvme_handle* handle, int cmd, struct usr_io* uio)
{
    int ioctlCmd;

    assert(handle);
    assert(uio);

    ioctlCmd = cmd;

    return vmk_MgmtUserCallbackInvoke(handle->handle, 0LL, NVME_MGMT_CB_IOCTL, &ioctlCmd, uio);
}

/**
 * Issue admin passthru command to a device
 *
 * @param [in] handle handle to a device
 * @param [inout] uio pointer to uio data structure
 *
 * @return 0 if successful
 */
int Nvme_AdminPassthru(struct nvme_handle* handle, struct usr_io* uio)
{
    int rc;

    rc = Nvme_Ioctl(handle, NVME_IOCTL_ADMIN_CMD, uio);

    /**
     * If the command has been successfully submitted to driver, the actual
     * return code for the admin command is returned in uio->status field.
     */
    if (!rc)
    {
        rc = uio->status;
    }

    return rc;
}

/**
 * Issue error admin passthru command to a device
 *
 * @param [in] handle handle to a device
 * @param [inout] uio pointer to uio data structure
 *
 * @return 0 if successful
 */
int Nvme_AdminPassthru_error(struct nvme_handle* handle, int cmd, struct usr_io* uio)
{
    return Nvme_Ioctl(handle, cmd, uio);
}

#if !defined(OK)
#    define OK 0
#endif
// clang-format off
/*
 * The return statuses are part of the VMkernel public API. To avoid breaking
 * 3rd party software built on top of this API, any change to the table must
 * maintain backward source level as well as binary compatibility i.e. a status
 * cannot be moved in the table or removed from the table, and new statuses must
 * be added at the end (before VMK_GENERIC_LINUX_ERROR).
 *
 *                VMK error code name                        Description Unix name
 *                ===================                        =========== =========
 */
/** \cond nodoc */
#define VMK_ERROR_CODES \
   DEFINE_VMK_ERR_AT(VMK_OK,                                 "Success", 0,                                                          OK             )\
   DEFINE_VMK_ERR_AT(VMK_FAILURE,                            "Failure", 0x0bad0001,                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_WOULD_BLOCK,                           "Would block",                                                         EAGAIN         )\
   DEFINE_VMK_ERR(VMK_NOT_FOUND,                             "Not found",                                                           ENOENT         )\
   DEFINE_VMK_ERR(VMK_BUSY,                                  "Busy",                                                                EBUSY          )\
   DEFINE_VMK_ERR(VMK_EXISTS,                                "Already exists",                                                      EEXIST         )\
   DEFINE_VMK_ERR(VMK_LIMIT_EXCEEDED,                        "Limit exceeded",                                                      EFBIG          )\
   DEFINE_VMK_ERR(VMK_BAD_PARAM,                             "Bad parameter",                                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_METADATA_READ_ERROR,                   "Metadata read error",                                                 EIO            )\
   DEFINE_VMK_ERR(VMK_METADATA_WRITE_ERROR,                  "Metadata write error",                                                EIO            )\
   DEFINE_VMK_ERR(VMK_IO_ERROR,                              "I/O error",                                                           EIO            )\
   DEFINE_VMK_ERR(VMK_READ_ERROR,                            "Read error",                                                          EIO            )\
   DEFINE_VMK_ERR(VMK_WRITE_ERROR,                           "Write error",                                                         EIO            )\
   DEFINE_VMK_ERR(VMK_INVALID_NAME,                          "Invalid name",                                                        ENAMETOOLONG   )\
   DEFINE_VMK_ERR(VMK_INVALID_HANDLE,                        "Invalid handle",                                                      EBADF          )\
   DEFINE_VMK_ERR(VMK_INVALID_ADAPTER,                       "No such SCSI adapter",                                                ENODEV         )\
   DEFINE_VMK_ERR(VMK_INVALID_TARGET,                        "No such target on adapter",                                           ENODEV         )\
   DEFINE_VMK_ERR(VMK_INVALID_PARTITION,                     "No such partition on target",                                         ENXIO          )\
   DEFINE_VMK_ERR(VMK_INVALID_FS,                            "No filesystem on the device",                                         ENXIO          )\
   DEFINE_VMK_ERR(VMK_INVALID_MEMMAP,                        "Memory map mismatch",                                                 EFAULT         )\
   DEFINE_VMK_ERR(VMK_NO_MEMORY,                             "Out of memory",                                                       ENOMEM         )\
   DEFINE_VMK_ERR(VMK_NO_MEMORY_RETRY,                       "Out of memory (ok to retry)",                                         ENOMEM         )\
   DEFINE_VMK_ERR(VMK_NO_LPAGE_MEMORY,                       "Out of large pages",                                                  ENOMEM         )\
   DEFINE_VMK_ERR(VMK_NO_RESOURCES,                          "Out of resources",                                                    ENOMEM         )\
   DEFINE_VMK_ERR(VMK_NO_FREE_HANDLES,                       "No free handles",                                                     EMFILE         )\
   DEFINE_VMK_ERR(VMK_NUM_HANDLES_EXCEEDED,                  "Exceeded maximum number of allowed handles",                          ENFILE         )\
   DEFINE_VMK_ERR(VMK_DEPRECATED_NO_FREE_PTR_BLOCKS,         "No free pointer blocks (deprecated)",                                 ENOSPC         )\
   DEFINE_VMK_ERR(VMK_DEPRECATED_NO_FREE_DATA_BLOCKS,        "No free data blocks (deprecated)",                                    ENOSPC         )\
   DEFINE_VMK_ERR(VMK_CORRUPT_REDOLOG,                       "Corrupt RedoLog",                                                     EBADF          )\
   DEFINE_VMK_ERR(VMK_STATUS_PENDING,                        "Status pending",                                                      EAGAIN         )\
   DEFINE_VMK_ERR(VMK_STATUS_FREE,                           "Status free",                                                         EAGAIN         )\
   DEFINE_VMK_ERR(VMK_UNSUPPORTED_CPU,                       "Unsupported CPU",                                                     ENODEV         )\
   DEFINE_VMK_ERR(VMK_NOT_SUPPORTED,                         "Not supported",                                                       ENOSYS         )\
   DEFINE_VMK_ERR(VMK_TIMEOUT,                               "Timeout",                                                             ETIMEDOUT      )\
   DEFINE_VMK_ERR(VMK_READ_ONLY,                             "Read only",                                                           EROFS          )\
   DEFINE_VMK_ERR(VMK_RESERVATION_CONFLICT,                  "SCSI reservation conflict",                                           EAGAIN         )\
   DEFINE_VMK_ERR(VMK_FS_LOCKED,                             "File system locked",                                                  EADDRINUSE     )\
   DEFINE_VMK_ERR(VMK_NOT_ENOUGH_SLOTS,                      "Out of slots",                                                        ENFILE         )\
   DEFINE_VMK_ERR(VMK_INVALID_ADDRESS,                       "Invalid address",                                                     EFAULT         )\
   DEFINE_VMK_ERR(VMK_NOT_SHARED,                            "Not shared",                                                          ENOMEM         )\
   DEFINE_VMK_ERR(VMK_SHARED,                                "Page is shared",                                                      ENOMEM         )\
   DEFINE_VMK_ERR(VMK_KSEG_PAIR_FLUSHED,                     "Kseg pair flushed",                                                   ENOMEM         )\
   DEFINE_VMK_ERR(VMK_MAX_ASYNCIO_PENDING,                   "Max async I/O requests pending",                                      ENOMEM         )\
   DEFINE_VMK_ERR(VMK_VERSION_MISMATCH_MINOR,                "Minor version mismatch",                                              ENOSYS         )\
   DEFINE_VMK_ERR(VMK_VERSION_MISMATCH_MAJOR,                "Major version mismatch",                                              ENOSYS         )\
   DEFINE_VMK_ERR(VMK_IS_CONNECTED,                          "Already connected",                                                   EINVAL         )\
   DEFINE_VMK_ERR(VMK_IS_DISCONNECTED,                       "Already disconnected",                                                ENOTCONN       )\
   DEFINE_VMK_ERR(VMK_IS_ENABLED,                            "Already enabled",                                                     EINVAL         )\
   DEFINE_VMK_ERR(VMK_IS_DISABLED,                           "Already disabled",                                                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_NOT_INITIALIZED,                       "Not initialized",                                                     EINVAL         )\
   DEFINE_VMK_ERR(VMK_WAIT_INTERRUPTED,                      "Wait interrupted",                                                    EINTR          )\
   DEFINE_VMK_ERR(VMK_NAME_TOO_LONG,                         "Name too long",                                                       ENAMETOOLONG   )\
   DEFINE_VMK_ERR(VMK_MISSING_FS_PES,                        "VMFS volume missing physical extents",                                ENODEV         )\
   DEFINE_VMK_ERR(VMK_NICTEAMING_VALID_MASTER,               "NIC teaming master valid",                                            EINVAL         )\
   DEFINE_VMK_ERR(VMK_NICTEAMING_SLAVE,                      "NIC teaming slave",                                                   EEXIST         )\
   DEFINE_VMK_ERR(VMK_NICTEAMING_REGULAR_VMNIC,              "NIC teaming regular VMNIC",                                           EINVAL         )\
   DEFINE_VMK_ERR(VMK_ABORT_NOT_RUNNING,                     "Abort not running",                                                   ECANCELED      )\
   DEFINE_VMK_ERR(VMK_NOT_READY,                             "Not ready",                                                           EIO            )\
   DEFINE_VMK_ERR(VMK_CHECKSUM_MISMATCH,                     "Checksum mismatch",                                                   EIO            )\
   DEFINE_VMK_ERR(VMK_VLAN_NO_HW_ACCEL,                      "VLan HW Acceleration not supported",                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_VLAN_SUPPORT,                       "VLan is not supported in vmkernel",                                   EOPNOTSUPP     )\
   DEFINE_VMK_ERR(VMK_NOT_VLAN_HANDLE,                       "Not a VLan handle",                                                   EINVAL         )\
   DEFINE_VMK_ERR(VMK_BAD_VLANID,                            "Couldn't retrieve VLan id",                                           EBADF          )\
   DEFINE_VMK_ERR(VMK_MIG_CONN_CLOSED,                       "Connection closed by remote host, possibly due to timeout",           EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_CONNECT,                            "No connection",                                                       EIO            )\
   DEFINE_VMK_ERR(VMK_SEGMENT_OVERLAP,                       "Segment overlap",                                                     EINVAL         )\
   DEFINE_VMK_ERR(VMK_BAD_MPS,                               "Error parsing MPS Table",                                             EIO            )\
   DEFINE_VMK_ERR(VMK_BAD_ACPI,                              "Error parsing ACPI Table",                                            EIO            )\
   DEFINE_VMK_ERR(VMK_RESUME_ERROR,                          "Failed to resume virtual machine",                                    EIO            )\
   DEFINE_VMK_ERR(VMK_NO_ADDRESS_SPACE,                      "Insufficient address space for operation",                            ENOMEM         )\
   DEFINE_VMK_ERR(VMK_BAD_ADDR_RANGE,                        "Bad address range",                                                   EINVAL         )\
   DEFINE_VMK_ERR(VMK_ENETDOWN,                              "Network is down",                                                     ENETDOWN       )\
   DEFINE_VMK_ERR(VMK_ENETUNREACH,                           "Network unreachable",                                                 ENETUNREACH    )\
   DEFINE_VMK_ERR(VMK_ENETRESET,                             "Network dropped connection on reset",                                 ENETRESET      )\
   DEFINE_VMK_ERR(VMK_ECONNABORTED,                          "Software caused connection abort",                                    ECONNABORTED   )\
   DEFINE_VMK_ERR(VMK_ECONNRESET,                            "Connection reset by peer",                                            ECONNRESET     )\
   DEFINE_VMK_ERR(VMK_ENOTCONN,                              "Socket is not connected",                                             ENOTCONN       )\
   DEFINE_VMK_ERR(VMK_ESHUTDOWN,                             "Cannot send after socket shutdown",                                   ESHUTDOWN      )\
   DEFINE_VMK_ERR(VMK_ETOOMANYREFS,                          "Too many references: cannot splice",                                  ETOOMANYREFS   )\
   DEFINE_VMK_ERR(VMK_ECONNREFUSED,                          "Connection refused",                                                  ECONNREFUSED   )\
   DEFINE_VMK_ERR(VMK_EHOSTDOWN,                             "Host is down",                                                        EHOSTDOWN      )\
   DEFINE_VMK_ERR(VMK_EHOSTUNREACH,                          "No route to host",                                                    EHOSTUNREACH   )\
   DEFINE_VMK_ERR(VMK_EADDRINUSE,                            "Address already in use",                                              EADDRINUSE     )\
   DEFINE_VMK_ERR(VMK_BROKEN_PIPE,                           "Broken pipe",                                                         EPIPE          )\
   DEFINE_VMK_ERR(VMK_NOT_A_DIRECTORY,                       "Not a directory",                                                     ENOTDIR        )\
   DEFINE_VMK_ERR(VMK_IS_A_DIRECTORY,                        "Is a directory",                                                      EISDIR         )\
   DEFINE_VMK_ERR(VMK_NOT_EMPTY,                             "Directory not empty",                                                 ENOTEMPTY      )\
   DEFINE_VMK_ERR(VMK_NOT_IMPLEMENTED,                       "Not implemented",                                                     ENOSYS         )\
   DEFINE_VMK_ERR(VMK_NO_SIGNAL_HANDLER,                     "No signal handler",                                                   EINVAL         )\
   DEFINE_VMK_ERR(VMK_FATAL_SIGNAL_BLOCKED,                  "Fatal signal blocked",                                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_ACCESS,                             "Permission denied",                                                   EACCES         )\
   DEFINE_VMK_ERR(VMK_NO_PERMISSION,                         "Operation not permitted",                                             EPERM          )\
   DEFINE_VMK_ERR(VMK_UNDEFINED_SYSCALL,                     "Undefined syscall",                                                   ENOSYS         )\
   DEFINE_VMK_ERR(VMK_RESULT_TOO_LARGE,                      "Result too large",                                                    ERANGE         )\
   DEFINE_VMK_ERR(VMK_VLAN_FILTERED,                         "Pkts dropped because of VLAN (support) mismatch",                     ERANGE         )\
   DEFINE_VMK_ERR(VMK_BAD_EXCFRAME,                          "Unsafe exception frame",                                              EFAULT         )\
   DEFINE_VMK_ERR(VMK_MODULE_NOT_LOADED,                     "Necessary module isn't loaded",                                       ENODEV         )\
   DEFINE_VMK_ERR(VMK_NO_SUCH_ZOMBIE,                        "No dead world by that name",                                          ECHILD         )\
   DEFINE_VMK_ERR(VMK_NO_SUCH_CARTEL,                        "No cartel by that name",                                              ESRCH          )\
   DEFINE_VMK_ERR(VMK_IS_A_SYMLINK,                          "Is a symbolic link",                                                  ELOOP          )\
   DEFINE_VMK_ERR(VMK_CROSS_DEVICE_LINK,                     "Cross-device link" ,                                                  EXDEV          )\
   DEFINE_VMK_ERR(VMK_NOT_A_SOCKET,                          "Not a socket",                                                        ENOTSOCK       )\
   DEFINE_VMK_ERR(VMK_ILLEGAL_SEEK,                          "Illegal seek",                                                        ESPIPE         )\
   DEFINE_VMK_ERR(VMK_ADDRFAM_UNSUPP,                        "Unsupported address family",                                          EAFNOSUPPORT   )\
   DEFINE_VMK_ERR(VMK_ALREADY_CONNECTED,                     "Already connected",                                                   EISCONN        )\
   DEFINE_VMK_ERR(VMK_DEATH_PENDING,                         "World is marked for death",                                           ENOENT         )\
   DEFINE_VMK_ERR(VMK_NO_CPU_ASSIGNMENT,                     "No valid scheduler assignment",                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MIN_INVALID,                       "Invalid cpu min",                                                     EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MINLIMIT_INVALID,                  "Invalid cpu minLimit",                                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MAX_INVALID,                       "Invalid cpu max",                                                     EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_SHARES_INVALID,                    "Invalid cpu shares",                                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MIN_OVERFLOW,                      "CPU min outside valid range",                                         EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MINLIMIT_OVERFLOW,                 "CPU minLimit outside valid range",                                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MAX_OVERFLOW,                      "CPU max outside valid range" ,                                        EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MIN_GT_MINLIMIT,                   "CPU min exceeds minLimit",                                            EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MIN_GT_MAX,                        "CPU min exceeds max",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_CPU_MINLIMIT_LT_RESERVED,              "CPU minLimit less than cpu already reserved by children",             ENOSPC         )\
   DEFINE_VMK_ERR(VMK_CPU_MAX_LT_RESERVED,                   "CPU max less than cpu already reserved by children",                  ENOSPC         )\
   DEFINE_VMK_ERR(VMK_CPU_ADMIT_FAILED,                      "Admission check failed for cpu resource",                             ENOSPC         )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_INVALID,                       "Invalid memory min",                                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MINLIMIT_INVALID,                  "Invalid memory minLimit",                                             EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MAX_INVALID,                       "Invalid memory max",                                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_OVERFLOW,                      "Memory min outside valid range",                                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MINLIMIT_OVERFLOW,                 "Memory minLimit outside valid range",                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MAX_OVERFLOW,                      "Memory max outside valid range",                                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_GT_MINLIMIT,                   "Memory min exceeds minLimit",                                         EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_GT_MAX,                        "Memory min exceeds max",                                              EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MINLIMIT_LT_RESERVED,              "Memory minLimit less than memory already reserved by children",       ENOSPC         )\
   DEFINE_VMK_ERR(VMK_MEM_MAX_LT_RESERVED,                   "Memory max less than memory already reserved by children",            ENOSPC         )\
   DEFINE_VMK_ERR(VMK_MEM_ADMIT_FAILED,                      "Admission check failed for memory resource",                          ENOSPC         )\
   DEFINE_VMK_ERR(VMK_NO_SWAP_FILE,                          "No swap file",                                                        ENOENT         )\
   DEFINE_VMK_ERR(VMK_BAD_PARAM_COUNT,                       "Bad parameter count",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_BAD_PARAM_TYPE,                        "Bad parameter type",                                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_UNMAP_RETRY,                           "Dueling unmaps (ok to retry)",                                        ENOMEM         )\
   DEFINE_VMK_ERR(VMK_INVALID_IOCTL,                         "Inappropriate ioctl for device",                                      ENOTTY         )\
   DEFINE_VMK_ERR(VMK_MAPFAULT_RETRY,                        "Mmap changed under page fault (ok to retry)",                         EBUSY          )\
   DEFINE_VMK_ERR(VMK_EINPROGRESS,                           "Operation now in progress",                                           EINPROGRESS    )\
   DEFINE_VMK_ERR(VMK_ADDR_UNMAPPED,                         "Address temporarily unmapped",                                        EFAULT         )\
   DEFINE_VMK_ERR(VMK_INVALID_BUDDY_TYPE,                    "Invalid buddy type",                                                  ENOMEM         )\
   DEFINE_VMK_ERR(VMK_LPAGE_INFO_NOT_FOUND,                  "Large page info not found",                                           ENOMEM         )\
   DEFINE_VMK_ERR(VMK_LPAGE_INFO_INVALID,                    "Invalid large page info",                                             EINVAL         )\
   DEFINE_VMK_ERR(VMK_SNAPSHOT_DEV,                          "SCSI LUN is in snapshot state",                                       EIO            )\
   DEFINE_VMK_ERR(VMK_IN_TRANSITION,                         "SCSI LUN is in transition",                                           EIO            )\
   DEFINE_VMK_ERR(VMK_TXN_FULL,                              "Transaction ran out of lock space or log space",                      ENOSPC         )\
   DEFINE_VMK_ERR(VMK_LOCK_NOT_FREE,                         "Lock was not free",                                                   EBUSY          )\
   DEFINE_VMK_ERR(VMK_NUM_FILES_EXCEEDED,                    "Exceed maximum number of files on the filesystem",                    ENOSPC         )\
   DEFINE_VMK_ERR(VMK_MIGRATE_VMX_FAILURE,                   "Migration determined a failure by the VMX",                           EINVAL         )\
   DEFINE_VMK_ERR(VMK_VSI_LIST_OVERFLOW,                     "VSI GetList handler overflow",                                        EFBIG          )\
   DEFINE_VMK_ERR(VMK_INVALID_WORLD,                         "Invalid world",                                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_VMM,                           "Invalid vmm",                                                         EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_TXN,                           "Invalid transaction",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_FS_RETRY_OPERATION,                    "Transient file system condition, suggest retry",                      EAGAIN         )\
   DEFINE_VMK_ERR(VMK_VCPU_LIMIT_EXCEEDED,                   "Number of running VCPUs limit exceeded",                              EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_METADATA,                      "Invalid metadata",                                                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_PAGE_NUMBER,                   "Invalid page number",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_NOT_EXEC,                              "Not in executable format",                                            ENOEXEC        )\
   DEFINE_VMK_ERR(VMK_NFS_CONNECT_FAILURE,                   "Unable to connect to NFS server",                                     EHOSTDOWN      )\
   DEFINE_VMK_ERR(VMK_NFS_MOUNT_NOT_SUPPORTED,               "The NFS server does not support MOUNT version 3 over TCP",            EINVAL         )\
   DEFINE_VMK_ERR(VMK_NFS_NFS_NOT_SUPPORTED,                 "The NFS server does not support requested NFS version over TCP",              EINVAL         )\
   DEFINE_VMK_ERR(VMK_NFS_MOUNT_DENIED,                      "The NFS server denied the mount request",                             EPERM          )\
   DEFINE_VMK_ERR(VMK_NFS_MOUNT_NOT_DIR,                     "The specified mount path was not a directory",                        ENOTDIR        )\
   DEFINE_VMK_ERR(VMK_NFS_BAD_FSINFO,                        "Unable to query remote mount point's attributes",                     EACCES         )\
   DEFINE_VMK_ERR(VMK_NFS_VOLUME_LIMIT_EXCEEDED,             "NFS has reached the maximum number of supported volumes",             EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_MEMORY_NICE,                        "Out of nice memory",                                                  ENOMEM         )\
   DEFINE_VMK_ERR(VMK_MIGRATE_PREEMPTIVE_FAIL,               "Migration failed to start due to lack of CPU or memory resources",    ENOMEM         )\
   DEFINE_VMK_ERR(VMK_CACHE_MISS,                            "Cache miss",                                                          EFAULT         )\
   DEFINE_VMK_ERR(VMK_STRESS_INDUCED_ERROR,                  "Error induced when stress options are enabled",                       EIO            )\
   DEFINE_VMK_ERR(VMK_TOO_MANY_LOCK_HOLDERS,                 "Maximum number of concurrent hosts are already accessing this resource", EUSERS      )\
   DEFINE_VMK_ERR(VMK_NO_JOURNAL,                            "Host doesn't have a journal",                                         EIO            )\
   DEFINE_VMK_ERR(VMK_RANK_VIOLATION,                        "Lock rank violation detected",                                        EDEADLK        )\
   DEFINE_VMK_ERR(VMK_MODULE_FAILED,                         "Module failed",                                                       ENODEV         )\
   DEFINE_VMK_ERR(VMK_NO_MASTER_PTY,                         "Unable to open slave if no master pty",                               ENXIO          )\
   DEFINE_VMK_ERR(VMK_NOT_IOABLE,                            "Not IOAble",                                                          EFAULT         )\
   DEFINE_VMK_ERR(VMK_NO_FREE_INODES,                        "No free inodes",                                                      ENOSPC         )\
   DEFINE_VMK_ERR(VMK_NO_MEMORY_FOR_FILEDATA,                "No free memory for file data",                                        ENOSPC         )\
   DEFINE_VMK_ERR(VMK_NO_TAR_SPACE,                          "No free space to expand file or meta data",                           ENOSPC         )\
   DEFINE_VMK_ERR(VMK_NO_FIFO_READER,                        "Unable to open writer if no fifo reader",                             ENXIO          )\
   DEFINE_VMK_ERR(VMK_NO_SUCH_DEVICE,                        "No underlying device for major,minor",                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_GT_MEMSIZE,                    "Memory min exceeds memSize",                                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_SUCH_VT,                            "No virtual terminal for number",                                      ENXIO          )\
   DEFINE_VMK_ERR(VMK_TOO_MANY_ELEMENTS,                     "Too many elements for list",                                          E2BIG          )\
   DEFINE_VMK_ERR(VMK_SHAREDAREA_MISMATCH,                   "VMM<->VMK shared area mismatch",                                      ENOSYS         )\
   DEFINE_VMK_ERR(VMK_EXEC_FAILURE,                          "Failure during exec while original state already lost",               ESRCH          )\
   DEFINE_VMK_ERR(VMK_INVALID_MODULE,                        "Invalid module",                                                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_UNALIGNED_ADDRESS,                     "Address is not aligned on required boundary",                         EINVAL         )\
   DEFINE_VMK_ERR(VMK_NOT_MAPPED,                            "Address is not mapped in address space",                              ENOMEM         )\
   DEFINE_VMK_ERR(VMK_NO_MESSAGE_SPACE,                      "No space to record a message",                                        ENOMEM         )\
   DEFINE_VMK_ERR(VMK_EXCEPTION_HANDLER_INVALID,             "Invalid exception handler",                                           EINVAL         )\
   DEFINE_VMK_ERR(VMK_EXCEPTION_NOT_HANDLED,                 "Exception not handled by exception handler",                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_MULTIWRITER_OBJECT,            "Cannot open sparse/TBZ files in multiwriter mode",                    EDEADLK        )\
   DEFINE_VMK_ERR(VMK_STORAGE_RETRY_OPERATION,               "Transient storage condition, suggest retry",                          EAGAIN         )\
   DEFINE_VMK_ERR(VMK_HBA_ERROR,                             "Storage initiator error",                                             EIO            )\
   DEFINE_VMK_ERR(VMK_TIMER_INIT_FAILED,                     "Timer initialization failed",                                         EINVAL         )\
   DEFINE_VMK_ERR(VMK_MODULE_NOT_FOUND,                      "Module not found",                                                    ENOENT         )\
   DEFINE_VMK_ERR(VMK_NOT_SOCKET_OWNER,                      "Socket not owned by cartel",                                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_VSI_HANDLER_NOT_FOUND,                 "No VSI handler found for the requested node",                         ENOENT         )\
   DEFINE_VMK_ERR(VMK_INVALID_MMAPPROTFLAGS,                 "Invalid mmap protection flags",                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_MAPCONTIG_SIZE,                "Invalid chunk size for contiguous mmap ",                             EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_MAPCONTIG_MAX,                 "Invalid MPN max for contiguous mmap ",                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_INVALID_MAPCONTIG_FLAG,                "Invalid mmap flag on contiguous mmap ",                               EINVAL         )\
   DEFINE_VMK_ERR(VMK_NOT_LAZY_MMINFO,                       "Unexpected fault on pre-faulted memory region",                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_MMINFO_WONT_SPLIT,                     "Memory region cannot be split (remap/unmap)",                         EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_CACHE_INFO,                         "Cache Information not available",                                     ENOENT         )\
   DEFINE_VMK_ERR(VMK_CANNOT_REMAP_PINNED_MEMORY,            "Cannot remap pinned memory",                                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_SUCH_CARTELGROUP,                   "No cartel group by that name",                                        ESRCH          )\
   DEFINE_VMK_ERR(VMK_SPLOCKSTATS_DISABLED,                  "SPLock stats collection disabled",                                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_BAD_TAR_IMAGE,                         "Boot image is corrupted",                                             EINVAL         )\
   DEFINE_VMK_ERR(VMK_BRANCHED_ALREADY,                      "Branched file cannot be modified",                                    EPERM          )\
   DEFINE_VMK_ERR(VMK_NAME_RESERVED_FOR_BRANCH,              "Name is reserved for branched file",                                  EPERM          )\
   DEFINE_VMK_ERR(VMK_CANNOT_BRANCH_UNLINKED,                "Unlinked file cannot be branched",                                    EPERM          )\
   DEFINE_VMK_ERR(VMK_MAX_RETRIES_EXCEEDED,                  "Maximum kernel-level retries exceeded",                               EAGAIN         )\
   DEFINE_VMK_ERR(VMK_OPTLOCK_STOLEN,                        "Optimistic lock acquired by another host",                            EAGAIN         )\
   DEFINE_VMK_ERR(VMK_NOT_MMAPABLE,                          "Object cannot be mmapped",                                            ENODEV         )\
   DEFINE_VMK_ERR(VMK_INVALID_CPU_AFFINITY,                  "Invalid cpu affinity",                                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_DEVICE_NOT_PARTOF_LV,                  "Device does not contain a logical volume",                            ENXIO          )\
   DEFINE_VMK_ERR(VMK_NO_SPACE,                              "No space left on device",                                             ENOSPC         )\
   DEFINE_VMK_ERR(VMK_VSI_INVALID_NODE_ID,                   "Invalid vsi node ID",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_TOO_MANY_USERS,                        "Too many users accessing this resource",                              EUSERS         )\
   DEFINE_VMK_ERR(VMK_EALREADY,                              "Operation already in progress",                                       EALREADY       )\
   DEFINE_VMK_ERR(VMK_BUF_TOO_SMALL,                         "Buffer too small to complete the operation",                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_SNAPSHOT_DEV_DISALLOWED,               "Snapshot device disallowed",                                          EACCES         )\
   DEFINE_VMK_ERR(VMK_LVM_DEVICE_UNREACHABLE,                "LVM device unreachable",                                              EIO            )\
   DEFINE_VMK_ERR(VMK_CPU_INVALID_RESOURCE_UNITS,            "Invalid cpu resource units",                                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_MEM_INVALID_RESOURCE_UNITS,            "Invalid memory resource units",                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_ABORTED,                               "IO was aborted",                                                      ECANCELED      )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_LT_RESERVED,                   "Memory min less than memory already reserved by children",            ENOSPC         )\
   DEFINE_VMK_ERR(VMK_MEM_MIN_LT_CONSUMED,                   "Memory min less than memory required to support current consumption", ENOSPC         )\
   DEFINE_VMK_ERR(VMK_MEM_MAX_LT_CONSUMED,                   "Memory max less than memory required to support current consumption", ENOSPC         )\
   DEFINE_VMK_ERR(VMK_TIMEOUT_RETRY_DEPRECATED,              "Timeout (ok to retry) DEPRECATED",                                    ETIMEDOUT      )\
   DEFINE_VMK_ERR(VMK_RESERVATION_LOST,                      "Reservation Lost",                                                    EBUSY          )\
   DEFINE_VMK_ERR(VMK_FS_STALE_METADATA,                     "Cached metadata is stale",                                            ENOENT         )\
   DEFINE_VMK_ERR(VMK_NO_FCNTL_LOCK,                         "No fcntl lock slot left",                                             ENOLCK         )\
   DEFINE_VMK_ERR(VMK_NO_FCNTL_LOCK_HOLDER,                  "No fcntl lock holder slot left",                                      ENOLCK         )\
   DEFINE_VMK_ERR(VMK_NO_LICENSE,                            "Not licensed to access VMFS volumes",                                 EACCES         )\
   DEFINE_VMK_ERR(VMK_VSI_MODULE_NOT_FOUND,                  "Vmkernel module necessary for this vsi call not loaded",              ENOENT         )\
   DEFINE_VMK_ERR(VMK_LVM_RETRY_OPERATION,                   "Transient LVM device condition, suggest retry",                       EAGAIN         )\
   DEFINE_VMK_ERR(VMK_SNAPSHOT_LV_INCOMPLETE,                "Snapshot LV incomplete",                                              EAGAIN         )\
   DEFINE_VMK_ERR(VMK_MEDIUM_NOT_FOUND,                      "Medium not found",                                                    EIO            )\
   DEFINE_VMK_ERR(VMK_MAX_PATHS_CLAIMED,                     "Maximum allowed SCSI paths have already been claimed",                ENOMEM         )\
   DEFINE_VMK_ERR(VMK_NOT_MOUNTABLE,                         "Filesystem is not mountable",                                         ENODEV         )\
   DEFINE_VMK_ERR(VMK_MEMSIZE_GT_MEMSIZELIMIT,               "Memory size exceeds memSizeLimit",                                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_RECORD_WRITE_ERROR,                    "An error occurred trying to write to the log",                        EIO            )\
   DEFINE_VMK_ERR(VMK_REPLAY_READ_ERROR,                     "An error occurred trying to read from the log",                       EIO            )\
   DEFINE_VMK_ERR(VMK_REPLAY_TYPE_MISMATCH,                  "There was a type mismatch while reading from the log",                EIO            )\
   DEFINE_VMK_ERR(VMK_REPLAY_DIVERGENCE,                     "A divergence was detected during replay",                             EIO            )\
   DEFINE_VMK_ERR(VMK_FT_NOT_RESPONDING,                     "The remote side of an FT pair isn't responding",                      ENOTCONN       )\
   DEFINE_VMK_ERR(VMK_NET_REPLAY_ERROR,                      "An error occurred during replay of networking.",                      EIO            )\
   DEFINE_VMK_ERR(VMK_VOBERR_INVALID_VOBID,                  "Vob ID invalid",                                                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_VOBERR_FMT_LIMIT_EXCEEDED,             "Vob format string too long",                                          EFBIG          )\
   DEFINE_VMK_ERR(VMK_VOBERR_INVALID_FMT_STRING,             "Invalid format specifier in VOB format string",                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_VOBERR_INVALID_ATTR,                   "Invalid attribute specifier in VOB format string",                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_ELF_CORRUPT,                           "ELF file is corrupt.",                                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_EADDRNOTAVAIL,                         "Address not available",                                               EADDRNOTAVAIL  )\
   DEFINE_VMK_ERR(VMK_EDESTADDRREQ,                          "Destination address required",                                        EDESTADDRREQ   )\
   DEFINE_VMK_ERR(VMK_LVM_STALE_METADATA,                    "Cached LVM metadata is stale.",                                       EPERM          )\
   DEFINE_VMK_ERR(VMK_NO_RPC_TABLE,                          "RPC table does not exist",                                            ENOENT         )\
   DEFINE_VMK_ERR(VMK_DUPLICATE_UID,                         "Device already has UID",                                              EEXIST         )\
   DEFINE_VMK_ERR(VMK_UNRESOLVED_SYMBOL,                     "Unresolved symbol",                                                   ENOENT         )\
   DEFINE_VMK_ERR(VMK_DEVICE_NOT_OWNED,                      "VMkernel does not own the device",                                    EINVAL         )\
   DEFINE_VMK_ERR(VMK_DEVICE_NOT_NAMED,                      "Device has no name",                                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_EPROTONOSUPPORT,                       "Protocol not supported",                                              EPROTONOSUPPORT)\
   DEFINE_VMK_ERR(VMK_EOPNOTSUPP,                            "Operation not supported",                                             EOPNOTSUPP     )\
   DEFINE_VMK_ERR(VMK_UNDEFINED_VMKCALL,                     "Undefined VMKCall",                                                   ENOSYS         )\
   DEFINE_VMK_ERR(VMK_MIGRATE_MAX_DOWNTIME_EXCEEDED,         "Maximum switchover time for migration exceeded",                      ETIMEDOUT      )\
   DEFINE_VMK_ERR(VMK_LOCK_EXISTS,                           "Multiple RO/MW locks held by the same host",                          EEXIST         )\
   DEFINE_VMK_ERR(VMK_MIGRATE_PRECOPY_NO_FORWARD_PROGRESS,   "Migration failed due to lack of pre-copy forward progress",           EINVAL         )\
   DEFINE_VMK_ERR(VMK_UID_CHANGED,                           "Device UID changed",                                                  EEXIST         )\
   DEFINE_VMK_ERR(VMK_VMOTION_CONNECT_FAILED,                "The ESX hosts failed to connect over the VMotion network",            ENOTCONN       )\
   DEFINE_VMK_ERR(VMK_NO_MIGRATION_IN_PROGRESS,              "No migration in progress",                                            ENOENT         )\
   DEFINE_VMK_ERR(VMK_EXEC_FILE_BUSY,                        "File is being executed, write access denied",                         ETXTBSY        )\
   DEFINE_VMK_ERR(VMK_FS_TIMEOUT_RETRY,                      "File system timeout (Ok to retry)",                                   ETIMEDOUT      )\
   DEFINE_VMK_ERR(VMK_COW_TIMEOUT_RETRY,                     "COW timeout (Ok to retry)",                                           ETIMEDOUT      )\
   DEFINE_VMK_ERR(VMK_FS_LOCKSTATE_IN_TRANSITION_DEPRECATED, "Lock state is in transition (ok to retry) DEPRECATED",                EBUSY          )\
   DEFINE_VMK_ERR(VMK_FS_LOCK_LOST,                          "Lost previously held disk lock",                                      EIO            )\
   DEFINE_VMK_ERR(VMK_NO_SPACE_ON_DEVICE,                    "Underlying device has no free space",                                 ENOSPC         )\
   DEFINE_VMK_ERR(VMK_EOVERFLOW,                             "Value too large for defined data type",                               EOVERFLOW      )\
   DEFINE_VMK_ERR(VMK_MEM_SHARES_INVALID,                    "Invalid memory shares",                                               EINVAL         )\
   DEFINE_VMK_ERR(VMK_LVM_INCONSISTENT_LOCKLESSOP,           "LVM lockless op reads in an inconsistent state",                      EAGAIN         )\
   DEFINE_VMK_ERR(VMK_INVALID_SECURITY_LABEL,                "Invalid security label",                                              EINVAL         )\
   DEFINE_VMK_ERR(VMK_ACCESS_DENIED,                         "Access denied by vmkernel access control policy",                     EPERM          )\
   DEFINE_VMK_ERR(VMK_WORK_COMPLETED,                        "Work has already completed",                                          EALREADY       )\
   DEFINE_VMK_ERR(VMK_WORK_RUNNING,                          "Work is currently running",                                           EINPROGRESS    )\
   DEFINE_VMK_ERR(VMK_WORK_PENDING,                          "Work is already pending",                                             EEXIST         )\
   DEFINE_VMK_ERR(VMK_WORK_INVALID,                          "Work or properties provided invalid",                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_VOBERR_OVERFLOW,                       "VOB context overflow",                                                EFBIG          )\
   DEFINE_VMK_ERR(VMK_VOBERR_INVALID_CONTEXT,                "VOB context invalid",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_VOBERR_LOCK_CONFLICT,                  "VOB context conflict for lock",                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_RETRY,                                 "Retry the operation",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_MODULE_HEAP,                        "Module has no heap to allocate from",                                 ENOMEM         )\
   DEFINE_VMK_ERR(VMK_REMOTE_PAGE_FAULT_FAILURE,             "Remote page fault failure",                                           ENOMEM         )\
   DEFINE_VMK_ERR(VMK_VSI_DATA_LENGTH_MISMATCH,              "VSI data length mismatch",                                            EIO            )\
   DEFINE_VMK_ERR(VMK_MAPPING_FAILED,                        "Mapping operation failed",                                            EFAULT         )\
   DEFINE_VMK_ERR(VMK_ATS_MISCOMPARE,                        "Atomic test and set of disk block returned false for equality",       EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_BUFFERSPACE,                        "No buffer space available",                                           ENOBUFS        )\
   DEFINE_VMK_ERR(VMK_FT_NOT_RUNNING,                        "FT vm is not enabled",                                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_LICENSE_MISMATCH,                      "Incompatible licenses detected",                                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_ELF_UNKNOWN_RELOCATIONS,               "ELF file contains invalid relocation types",                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_MESSAGE_TOO_LONG,                      "Message too long",                                                    EMSGSIZE       )\
   DEFINE_VMK_ERR(VMK_INVALID_NAMESPACE,                     "Invalid or missing namespace",                                        ENOENT         )\
   DEFINE_VMK_ERR(VMK_SHUTTING_DOWN,                         "Operation not allowed because the VMKernel is shutting down",         EINVAL         )\
   DEFINE_VMK_ERR(VMK_SKIPPED_FREE,                          "Skipped freeing of resource with no reference",                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_VMFS_ABORTED,                          "IO was aborted by VMFS via a virt-reset on the device",               ECANCELED      )\
   DEFINE_VMK_ERR(VMK_NO_WRITE_ON_TARDISKS,                  "Write not allowed on tardisks",                                       EPERM          )\
   DEFINE_VMK_ERR(VMK_SVM_IO_RETRY,                          "Re-issue IO at a later time",                                         EBUSY          )\
   DEFINE_VMK_ERR(VMK_MODULE_NO_LICENSE,                     "Module does not provide a license tag",                               ENOENT         )\
   DEFINE_VMK_ERR(VMK_MODULE_UNKNOWN_LICENSE,                "Module provides an unknown license tag",                              ENOENT         )\
   DEFINE_VMK_ERR(VMK_PERM_DEV_LOSS,                         "Device is permanently unavailable",                                   EIO            )\
   DEFINE_VMK_ERR(VMK_SE_IO_RETRY,                           "Reissue IO at a later time for SE disks",                             EBUSY          )\
   DEFINE_VMK_ERR(VMK_BAD_ADDR_SPACE,                        "Address space type is not supported for operation",                   EINVAL         )\
   DEFINE_VMK_ERR(VMK_DMA_MAPPING_FAILED,                    "DMA mapping could not be completed",                                  EINVAL         )\
   DEFINE_VMK_ERR(VMK_RESERVATION_GT_LIMIT,                  "Memory pool reservation is greater than limit" ,                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_MODULE_NONAMESPACE,                    "Module tried to export a symbol but didn't provide a name space",     ENOENT         )\
   DEFINE_VMK_ERR(VMK_FS_OBJECT_UNLINKED,                    "File system object is unlinked",                                      EINVAL         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_INSTANCE_ABORTED,             "Replication instance was aborted",                                    ECANCELED      )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_NEED_FULL_SYNC,               "Replicated disk needs full synchronization",                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_DISK_SET_MISMATCH,            "The set of disks on the replication server doesn't match",            EINVAL         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_REQUEST_CHECKSUM_MISMATCH,    "The checksum for the replication request was invalid",                EINVAL         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_RESPONSE_CHECKSUM_MISMATCH,   "The checksum for the replication response was invalid",               EINVAL         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_GROUP_REMOVED,                "The replication group was removed on the server side",                ENOENT         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_GROUP_SESSION_REVOKED,        "A newer client for this group is connected to the replication server",EEXIST         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_PROTOCOL_CORRUPTED,           "Corrupt response from replication server",                            EINVAL         )\
   DEFINE_VMK_ERR(VMK_PORTSET_HANDLE_NOT_MUTABLE,            "Portset handle is not mutable",                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_SUSPEND_IO,                            "Suspend the IO in question",                                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_NO_WORKING_PATHS,                      "No working paths to select",                                          EINVAL         )\
   DEFINE_VMK_ERR(VMK_EPROTOTYPE,                            "Invalid protocol for connection",                                     EPROTOTYPE     )\
   DEFINE_VMK_ERR(VMK_MODULE_CONSUMED_RESOURCE_COUNT_NOT_ZERO, "Consumed resource count of module is not zero",                     EBUSY          )\
   DEFINE_VMK_ERR(VMK_HBR_SERVER_DOES_NOT_SUPPORT_REQUEST,   "vSphere Replication Server does not support request",                 EOPNOTSUPP     )\
   DEFINE_VMK_ERR(VMK_STALE_FILEHANDLE,                      "Stale file handle",                                                   ESTALE         )\
   DEFINE_VMK_ERR(VMK_VVOL_UNBOUND,                          "Virtual volume is not bound",                                         ENODEV         )\
   DEFINE_VMK_ERR(VMK_DEVICE_NOT_READY_FAIL_OPEN,            "Device open failed with no-retry",                                    EPERM          )\
   DEFINE_VMK_ERR(VMK_NOT_THIS_DEVICE,                       "Not for this device",                                                 EINVAL         )\
   DEFINE_VMK_ERR(VMK_IGNORE,                                "Ignore",                                                              EINVAL         )\
   DEFINE_VMK_ERR(VMK_OBJECT_DESTROYED,                      "Object is being or has been destroyed",                               EINVAL         )\
   DEFINE_VMK_ERR(VMK_VVOL_PE_NOT_READY,                     "Protocol Endpoint not ready for I/O to given secondary level ID",     EAGAIN         )\
   DEFINE_VMK_ERR(VMK_SCSI_PI_GUARD_ERROR,                   "T10 PI GUARD tag check failed",                                       EIO            )\
   DEFINE_VMK_ERR(VMK_SCSI_PI_REF_ERROR,                     "T10 PI REF tag check failed",                                         EIO            )\
   DEFINE_VMK_ERR(VMK_RES_META_STALE,                        "Cached resource metadata is stale",                                   EAGAIN         )\
   DEFINE_VMK_ERR(VMK_NOT_PINNED,                            "Page is not pinned",                                                  ENOENT         )\
   DEFINE_VMK_ERR(VMK_BAD_SWAP_SCOPE,                        "Incorrect swap scope",                                                EINVAL         )\
   DEFINE_VMK_ERR(VMK_CONSUMED_GT_ZERO,                      "Consumed memory is more than zero",                                   EINVAL         )\
   DEFINE_VMK_ERR(VMK_LOCK_HELD_BY_ZOMBIE_TXN,               "Lock held by a transaction in progress",                              EBUSY          )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_FILE_IDENTICAL,               "The file being sent already exists and is identical",                 EEXIST         )\
   DEFINE_VMK_ERR(VMK_VOL_ALREADY_MOUNTED,                   "The volume is already mounted",                                       EBUSY          )\
   DEFINE_VMK_ERR(VMK_NO_VOLUMES,                            "Out of volumes",                                                      ENOMEM         )\
   DEFINE_VMK_ERR(VMK_SB_NOT_FOUND,                          "Super block not found",                                               ENOENT         )\
   DEFINE_VMK_ERR(VMK_NO_PMEM,                               "Out of persistent memory",                                            ENOMEM         )\
   DEFINE_VMK_ERR(VMK_OTHER,                                 "Another operation is in progress",                                    EINPROGRESS    )\
   DEFINE_VMK_ERR(VMK_CANNOT_SHRINK,                         "Shrinking is not allowed",                                            EPERM          )\
   DEFINE_VMK_ERR(VMK_NOT_A_BASE,                            "Not a base disk",                                                     EPERM          )\
   DEFINE_VMK_ERR(VMK_HAS_SNAPSHOTS,                         "Disk has snapshots",                                                  ECHILD         )\
   DEFINE_VMK_ERR(VMK_LOCK_DROPPED,                          "Lock was dropped during function call",                               EAGAIN         )\
   DEFINE_VMK_ERR(VMK_PMEM_CORRUPTED,                        "Persistent memory datastore is corrupt",                              EIO            )\
   DEFINE_VMK_ERR(VMK_PMEM_DATA_CORRUPTED,                   "Persistent memory data block is corrupt",                             EIO            )\
   DEFINE_VMK_ERR(VMK_INVALID_STATE,                         "Invalid state",                                                       EINVAL         )\
   DEFINE_VMK_ERR(VMK_HBR_WIRE_INVALID_DISK_SIZE,            "Disk has invalid size for the operation",                             EINVAL         )\
   DEFINE_VMK_ERR(VMK_NOT_ENOUGH_NUMA_MEMORY,                "Not enough NUMA memory",                                              ENOMEM         )\
   DEFINE_VMK_ERR(VMK_QUOTA_LIMIT_EXCEEDED,                  "Quota Limit exceeded",                                                EDQUOT         )\
/* --- ADD NEW ERROR CODES ABOVE THIS COMMENT. --- */                                                                                               \
   DEFINE_VMK_ERR(VMK_LAST_ERR,                              "Invalid error code",                                                  EINVAL         )\
/* --- VMK_GENERIC_LINUX_ERROR must be last. --- */                                                                                                 \
   DEFINE_VMK_ERR_AT(VMK_GENERIC_LINUX_ERROR,                "Generic service console error", 0x2bad0000,                           EIO            )
/* --- Don't add ERR_AT with negative value. --- */
/** \endcond */

// /*
//  * types
//  */
// /** \cond nodoc */
// #define DEFINE_VMK_ERR(_err, _str, _uerr) /** \brief _str */ _err,
// #define DEFINE_VMK_ERR_AT(_err, _str, _val, _uerr) /** \brief _str */ _err = _val,
// /** \endcond */
// typedef enum {
//    VMK_ERROR_CODES
// } VMK_ReturnStatus;
// /** \cond nodoc */
// #undef DEFINE_VMK_ERR
// #undef DEFINE_VMK_ERR_AT
// /** \endcond */

// clang-format on

typedef struct
{
    VMK_ReturnStatus code;
    const char*      description;
    int              unixerrno;
} VMKErrorCode;

// define how to expand the error above into the structure
#define DEFINE_VMK_ERR_AT(_err, _str, _val, _uerr) {_err, _str, _uerr},
#define DEFINE_VMK_ERR(_err, _str, _uerr)          {_err, _str, _uerr},

VMKErrorCode vmkerrorTable[] = {VMK_ERROR_CODES};

// remove definition to not cause errors with anyone else using the table
#undef DEFINE_VMK_ERR
#undef DEFINE_VMK_ERR_AT

static int comp_vmk_err(const void* a, const void* b)
{
    return ((VMKErrorCode*)a)->code - ((VMKErrorCode*)b)->code;
}

const char* get_VMK_API_Error(VMK_ReturnStatus status)
{
    VMKErrorCode  key = {.code = status};
    VMKErrorCode* err = safe_bsearch(&key, vmkerrorTable, sizeof(vmkerrorTable) / sizeof(vmkerrorTable[0]),
                                     sizeof(VMKErrorCode), comp_vmk_err);
    if (err)
    {
        return err->description;
    }
    else
    {
        return "Unknown VMK Error code";
    }
}
