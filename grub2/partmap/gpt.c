/* gpt.c - Read GUID Partition Tables (GPT).  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2005,2006  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/partition.h>
#include <grub/dl.h>
#include <grub/pc_partition.h>

struct grub_gpt_header
{
  grub_uint8_t magic[8];
  grub_uint32_t version;
  grub_uint32_t headersize;
  grub_uint32_t crc32;
  grub_uint32_t unused1;
  grub_uint64_t primary;
  grub_uint64_t backup;
  grub_uint64_t start;
  grub_uint64_t end;
  grub_uint8_t guid[16];
  grub_uint64_t partitions;
  grub_uint32_t maxpart;
  grub_uint32_t partentry_size;
  grub_uint32_t partentry_crc32;
} __attribute__ ((packed));

struct grub_gpt_partentry
{
  grub_uint8_t type[16];
  grub_uint8_t guid[16];
  grub_uint64_t start;
  grub_uint64_t end;
  grub_uint8_t attrib;
  char name[72];
} __attribute__ ((packed));

static grub_uint8_t grub_gpt_magic[8] =
  {
    45, 46, 49, 20, 50, 41, 52, 54
  };

static grub_uint8_t grub_gpt_partition_type_empty[16] = { 0 };

static struct grub_partition_map grub_gpt_partition_map;

#ifndef GRUB_UTIL
static grub_dl_t my_mod;
#endif



static grub_err_t
gpt_partition_map_iterate (grub_disk_t disk,
			   int (*hook) (grub_disk_t disk,
					const grub_partition_t partition))
{
  struct grub_partition part;
  struct grub_gpt_header gpt;
  struct grub_gpt_partentry entry;
  struct grub_disk raw;
  struct grub_pc_partition_mbr mbr;
  grub_uint64_t entries;
  int partno = 1;
  unsigned int i;
  int last_offset = 0;

  /* Enforce raw disk access.  */
  raw = *disk;
  raw.partition = 0;

  /* Read the protective MBR.  */
  if (grub_disk_read (&raw, 0, 0, sizeof (mbr), (char *) &mbr))
    return grub_errno;

  /* Check if it is valid.  */
  if (mbr.signature != grub_cpu_to_le16 (GRUB_PC_PARTITION_SIGNATURE))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "no signature");

  /* Make sure the MBR is a protective MBR and not a normal MBR.  */
  if (mbr.entries[0].type != GRUB_PC_PARTITION_TYPE_GPT_DISK)
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "no GPT partition map found");

  /* Read the GPT header.  */
  if (grub_disk_read (&raw, 1, 0, sizeof (gpt), (char *) &gpt))
    return grub_errno;

  if (! grub_memcmp (gpt.magic, grub_gpt_magic, sizeof (grub_gpt_magic)))
    return grub_error (GRUB_ERR_BAD_PART_TABLE, "no valid GPT header");

  grub_dprintf ("gpt", "Read a valid GPT header\n");

  entries = grub_le_to_cpu64 (gpt.partitions);
  for (i = 0; i < grub_le_to_cpu32 (gpt.maxpart); i++)
    {
      if (grub_disk_read (&raw, entries, last_offset,
			  sizeof (entry), (char *) &entry))
	return grub_errno;

      if (grub_memcmp (grub_gpt_partition_type_empty, entry.type,
		       sizeof (grub_gpt_partition_type_empty)))
	{
	  /* Calculate the first block and the size of the partition.  */
	  part.start = grub_le_to_cpu64 (entry.start);
	  part.len = (grub_le_to_cpu64 (entry.end)
		      - grub_le_to_cpu64 (entry.start) + 1);
	  part.offset = entries;
	  part.index = partno;
	  part.partmap = &grub_gpt_partition_map;

	  grub_dprintf ("gpt", "GPT entry %d: start=%lld, length=%lld\n",
			partno, part.start, part.len);

	  if (hook (disk, &part))
	    return grub_errno;
	}

      partno++;
      last_offset += grub_le_to_cpu32 (gpt.partentry_size);
      if (last_offset == GRUB_DISK_SECTOR_SIZE)
	{
	  last_offset = 0;
	  entries++;
	}
    }

  return 0;
}


static grub_partition_t
gpt_partition_map_probe (grub_disk_t disk, const char *str)
{
  grub_partition_t p = 0;
  int partnum = 0;
  char *s = (char *) str;

  auto int find_func (grub_disk_t d, const grub_partition_t partition);
    
  int find_func (grub_disk_t d __attribute__ ((unused)),
		 const grub_partition_t partition)
      {
      if (partnum == partition->index)
	{
	  p = (grub_partition_t) grub_malloc (sizeof (*p));
	  if (! p)
	    return 1;
	  
	  grub_memcpy (p, partition, sizeof (*p));
	  return 1;
	}
      
      return 0;
    }
  
  /* Get the partition number.  */
  partnum = grub_strtoul (s, 0, 10);
  if (grub_errno)
    {
      grub_error (GRUB_ERR_BAD_FILENAME, "invalid partition");
      return 0;
    }

  if (gpt_partition_map_iterate (disk, find_func))
    goto fail;

  return p;

 fail:
  grub_free (p);
  return 0;
}


static char *
gpt_partition_map_get_name (const grub_partition_t p)
{
  char *name;

  name = grub_malloc (13);
  if (! name)
    return 0;

  grub_sprintf (name, "%d", p->index);
  return name;
}


/* Partition map type.  */
static struct grub_partition_map grub_gpt_partition_map =
  {
    .name = "gpt_partition_map",
    .iterate = gpt_partition_map_iterate,
    .probe = gpt_partition_map_probe,
    .get_name = gpt_partition_map_get_name
  };

GRUB_MOD_INIT(gpt_partition_map)
{
  grub_partition_map_register (&grub_gpt_partition_map);
#ifndef GRUB_UTIL
  my_mod = mod;
#endif
}

GRUB_MOD_FINI(gpt_partition_map)
{
  grub_partition_map_unregister (&grub_gpt_partition_map);
}
