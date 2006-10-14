/* lvm.h - LVM support for GRUB utils.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006  Free Software Foundation, Inc.
 *
 *  GRUB is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GRUB_LVM_UTIL_HEADER
#define GRUB_LVM_UTIL_HEADER	1

#ifdef __linux__
int grub_util_lvm_isvolume (char *name);
#endif

#endif /* ! GRUB_RAID_UTIL_HEADER */
