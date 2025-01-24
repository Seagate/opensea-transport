// SPDX-License-Identifier: MPL-2.0

//! \file platform_helper.h
//! \brief includes the correct OS low-level implementation file
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#if defined(UEFI_C_SOURCE)
#    include "uefi_helper.h"
#elif defined(__linux__) || defined(__DragonFly__)
#    if defined(VMK_CROSS_COMP)
#        include "vm_helper.h"
#    else
#        include "sg_helper.h"
#    endif
#elif defined(__FreeBSD__)
#    include "cam_helper.h"
#elif defined(__NetBSD__)
#    error "Need a NetBSD passthrough helper file"
#elif defined(__OpenBSD__)
#    error "Need a OpenBSD passthrough helper file"
#elif defined(__sun)
#    include "uscsi_helper.h"
#elif defined(_WIN32)
#    include "win_helper.h"
#elif defined(_AIX) // IBM Unix
#    include "aix_helper.h"
#elif defined(__hpux) // HP Unix
#    error "Need a HP UX passthrough helper file"
#elif defined(__APPLE__)
#    include <TargetConditionals.h>
#    if defined(TARGET_OS_MAC)
#        error "Need a Apple passthrough helper file"
#    else
#        error "Need Apple embedded os helper file"
#    endif
#elif defined(__digital__) // tru64 unix
#    error "Need a TRU64 passthrough helper file"
#elif defined(__CYGWIN__) && !defined(_WIN32)
// this is using CYGWIN with POSIX under Windows. This means that the Win API is not available, so attempt to use the sg
// passthrough file
#    if defined(VMK_CROSS_COMP)
#        include "vm_helper.h"
#    else
#        include <sg_helper.h>
#    endif
#else
#    error "Unknown OS. Need to specify helper.h to use\n"
#endif
