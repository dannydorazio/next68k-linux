/* SPDX-License-Identifier: GPL-2.0 */
/* Just enough of <linux/types.h> for the uapi bootinfo headers. */

#ifndef _LOADER_LINUX_TYPES_H
#define _LOADER_LINUX_TYPES_H

#ifndef __ASSEMBLER__
typedef unsigned char		__u8;
typedef unsigned short		__u16;
typedef unsigned int		__u32;
typedef unsigned short		__be16;
typedef unsigned int		__be32;
#endif

#endif
