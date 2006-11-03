/* elf.c - load ELF files */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
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

#include <grub/err.h>
#include <grub/elf.h>
#include <grub/elfload.h>
#include <grub/file.h>
#include <grub/gzio.h>
#include <grub/misc.h>
#include <grub/mm.h>

/* Check if EHDR is a valid ELF header.  */
static grub_err_t
grub_elf_check_header (grub_elf_t elf)
{
  Elf32_Ehdr *e = &elf->ehdr.ehdr32;

  if (e->e_ident[EI_MAG0] != ELFMAG0
      || e->e_ident[EI_MAG1] != ELFMAG1
      || e->e_ident[EI_MAG2] != ELFMAG2
      || e->e_ident[EI_MAG3] != ELFMAG3
      || e->e_ident[EI_VERSION] != EV_CURRENT
      || e->e_version != EV_CURRENT)
    return grub_error (GRUB_ERR_BAD_OS, "invalid arch independent ELF magic");

  return GRUB_ERR_NONE;
}

grub_err_t
grub_elf_close (grub_elf_t elf)
{
  grub_file_t file = elf->file;

  grub_free (elf->phdrs);
  grub_free (elf);

  if (file)
    grub_file_close (file);

  return grub_errno;
}

grub_elf_t
grub_elf_file (grub_file_t file)
{
  grub_elf_t elf;

  elf = grub_malloc (sizeof (*elf));
  if (! elf)
    return 0;

  elf->file = file;
  elf->phdrs = 0;

  if (grub_file_seek (elf->file, 0) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (elf->file, (char *) &elf->ehdr, sizeof (elf->ehdr))
      != sizeof (elf->ehdr))
    {
      grub_error (GRUB_ERR_READ_ERROR, "Cannot read ELF header.");
      goto fail;
    }

  if (grub_elf_check_header (elf))
    goto fail;

  return elf;

fail:
  grub_error_push ();
  grub_elf_close (elf);
  grub_error_pop ();
  return 0;
}

grub_elf_t
grub_elf_open (const char *name)
{
  grub_file_t file;

  file = grub_gzfile_open (name, 1);
  if (! file)
    return 0;

  return grub_elf_file (file);
}


/* 32-bit */

int
grub_elf_is_elf32 (grub_elf_t elf)
{
  return elf->ehdr.ehdr32.e_ident[EI_CLASS] == ELFCLASS32;
}

static int
grub_elf32_load_segment (grub_elf_t elf, Elf32_Phdr *phdr, void *hook)
{
  grub_elf32_load_hook_t load_hook = (grub_elf32_load_hook_t) hook;
  grub_addr_t load_addr;

  if (phdr->p_type != PT_LOAD)
    return 0;

  load_addr = phdr->p_paddr;
  if (load_hook && load_hook (phdr, &load_addr))
    return 1;

  grub_dprintf ("elf", "Loading segment at %llx, size 0x%llx\n",
		(unsigned long long) load_addr,
		(unsigned long long) phdr->p_filesz);

  if (grub_file_seek (elf->file, phdr->p_offset) == (grub_off_t) -1)
    {
      return grub_error (GRUB_ERR_BAD_OS, "Invalid offset in program header");
    }

  if (phdr->p_filesz
      && grub_file_read (elf->file, (void *) load_addr, phdr->p_filesz)
      != (grub_ssize_t) phdr->p_filesz)
    {
      return grub_error (GRUB_ERR_BAD_OS, "Couldn't load segment");
    }

  if (phdr->p_filesz < phdr->p_memsz)
    grub_memset ((void *) (long) (load_addr + phdr->p_filesz),
		 0, phdr->p_memsz - phdr->p_filesz);

  return 0;
}

static grub_err_t
grub_elf32_load_phdrs (grub_elf_t elf)
{
  grub_ssize_t phdrs_size;

  phdrs_size = elf->ehdr.ehdr32.e_phnum * elf->ehdr.ehdr32.e_phentsize;

  grub_dprintf ("elf", "Loading program headers at 0x%llx, size 0x%x.\n",
		(unsigned long long) elf->ehdr.ehdr32.e_phoff,
		phdrs_size);

  elf->phdrs = grub_malloc (phdrs_size);
  if (! elf->phdrs)
    return grub_errno;

  if ((grub_file_seek (elf->file, elf->ehdr.ehdr32.e_phoff) == (grub_off_t) -1)
      || (grub_file_read (elf->file, elf->phdrs, phdrs_size) != phdrs_size))
    return grub_error (GRUB_ERR_READ_ERROR, "Cannot read program headers");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_elf32_phdr_iterate (grub_elf_t elf,
			 int (*hook) (grub_elf_t, Elf32_Phdr *, void *),
			 void *hook_arg)
{
  Elf32_Phdr *phdrs;
  unsigned int i;

  if (! elf->phdrs)
    if (grub_elf32_load_phdrs (elf))
      return grub_errno;
  phdrs = elf->phdrs;

  for (i = 0; i < elf->ehdr.ehdr32.e_phnum; i++)
    {
      Elf32_Phdr *phdr = phdrs + i;
      grub_dprintf ("elf", "Segment %u: type 0x%x paddr 0x%lx memsz 0x%lx.\n",
		    i, phdr->p_type,
		    (unsigned long) phdr->p_paddr,
		    (unsigned long) phdr->p_memsz);
      if (hook (elf, phdr, hook_arg))
	break;
    }

  return grub_errno;
}

/* Calculate the amount of memory spanned by the segments.  */
grub_size_t
grub_elf32_size (grub_elf_t elf)
{
  Elf32_Addr segments_start = (Elf32_Addr) -1;
  Elf32_Addr segments_end = 0;
  int nr_phdrs = 0;

  /* Run through the program headers to calculate the total memory size we
   * should claim.  */
  auto int calcsize (grub_elf_t _elf, Elf32_Phdr *phdr, void *_arg);
  int calcsize (grub_elf_t __unused _elf, Elf32_Phdr *phdr, void __unused *_arg)
    {
      /* Only consider loadable segments.  */
      if (phdr->p_type != PT_LOAD)
	return 0;
      nr_phdrs++;
      if (phdr->p_paddr < segments_start)
	segments_start = phdr->p_paddr;
      if (phdr->p_paddr + phdr->p_memsz > segments_end)
	segments_end = phdr->p_paddr + phdr->p_memsz;
      return 0;
    }

  grub_elf32_phdr_iterate (elf, calcsize, 0);

  if (nr_phdrs == 0)
    {
      grub_error (GRUB_ERR_BAD_OS, "No program headers present");
      return 0;
    }

  if (segments_end < segments_start)
    {
      /* Very bad addresses.  */
      grub_error (GRUB_ERR_BAD_OS, "Bad program header load addresses");
      return 0;
    }

  return segments_end - segments_start;
}


/* Load every loadable segment into memory specified by `load_hook'.  */
grub_err_t
grub_elf32_load (grub_elf_t elf, grub_elf32_load_hook_t load_hook)
{
  return grub_elf32_phdr_iterate (elf, grub_elf32_load_segment, load_hook);
}



/* 64-bit */

int
grub_elf_is_elf64 (grub_elf_t elf)
{
  return elf->ehdr.ehdr64.e_ident[EI_CLASS] == ELFCLASS64;
}

static int
grub_elf64_load_segment (grub_elf_t elf, Elf64_Phdr *phdr, void *hook)
{
  grub_elf64_load_hook_t load_hook = (grub_elf64_load_hook_t) hook;
  grub_addr_t load_addr;

  if (phdr->p_type != PT_LOAD)
    return 0;

  load_addr = phdr->p_paddr;
  if (load_hook && load_hook (phdr, &load_addr))
    return 1;

  grub_dprintf ("elf", "Loading segment at %llx, size 0x%llx\n",
		(unsigned long long) load_addr,
		(unsigned long long) phdr->p_filesz);

  if (grub_file_seek (elf->file, phdr->p_offset) == (grub_off_t) -1)
    {
      return grub_error (GRUB_ERR_BAD_OS, "Invalid offset in program header");
    }

  if (grub_file_read (elf->file, (void *) load_addr, phdr->p_filesz)
      != (grub_ssize_t) phdr->p_filesz)
    {
      return grub_error (GRUB_ERR_BAD_OS, "Couldn't load segment");
    }

  if (phdr->p_filesz < phdr->p_memsz)
    grub_memset ((void *) (long) (load_addr + phdr->p_filesz),
		 0, phdr->p_memsz - phdr->p_filesz);

  return 0;
}

static grub_err_t
grub_elf64_load_phdrs (grub_elf_t elf)
{
  grub_ssize_t phdrs_size;

  phdrs_size = elf->ehdr.ehdr64.e_phnum * elf->ehdr.ehdr64.e_phentsize;

  grub_dprintf ("elf", "Loading program headers at 0x%llx, size 0x%x.\n",
		(unsigned long long) elf->ehdr.ehdr64.e_phoff,
		phdrs_size);

  elf->phdrs = grub_malloc (phdrs_size);
  if (! elf->phdrs)
    return grub_errno;

  if ((grub_file_seek (elf->file, elf->ehdr.ehdr64.e_phoff) == (grub_off_t) -1)
      || (grub_file_read (elf->file, elf->phdrs, phdrs_size) != phdrs_size))
    return grub_error (GRUB_ERR_READ_ERROR, "Cannot read program headers");

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_elf64_phdr_iterate (grub_elf_t elf,
			 int (*hook) (grub_elf_t, Elf64_Phdr *, void *),
			 void *hook_arg)
{
  Elf64_Phdr *phdrs;
  unsigned int i;

  if (! elf->phdrs)
    if (grub_elf64_load_phdrs (elf))
      return grub_errno;
  phdrs = elf->phdrs;

  for (i = 0; i < elf->ehdr.ehdr64.e_phnum; i++)
    {
      Elf64_Phdr *phdr = phdrs + i;
      grub_dprintf ("elf", "Segment %u: type 0x%x paddr 0x%lx memsz 0x%lx.\n",
		    i, phdr->p_type,
		    (unsigned long) phdr->p_paddr,
		    (unsigned long) phdr->p_memsz);
      if (hook (elf, phdr, hook_arg))
	break;
    }

  return grub_errno;
}

/* Calculate the amount of memory spanned by the segments.  */
grub_size_t
grub_elf64_size (grub_elf_t elf)
{
  Elf64_Addr segments_start = (Elf64_Addr) -1;
  Elf64_Addr segments_end = 0;
  int nr_phdrs = 0;

  /* Run through the program headers to calculate the total memory size we
   * should claim.  */
  auto int calcsize (grub_elf_t _elf, Elf64_Phdr *phdr, void *_arg);
  int calcsize (grub_elf_t __unused _elf, Elf64_Phdr *phdr, void __unused *_arg)
    {
      /* Only consider loadable segments.  */
      if (phdr->p_type != PT_LOAD)
	return 0;
      nr_phdrs++;
      if (phdr->p_paddr < segments_start)
	segments_start = phdr->p_paddr;
      if (phdr->p_paddr + phdr->p_memsz > segments_end)
	segments_end = phdr->p_paddr + phdr->p_memsz;
      return 0;
    }

  grub_elf64_phdr_iterate (elf, calcsize, 0);

  if (nr_phdrs == 0)
    {
      grub_error (GRUB_ERR_BAD_OS, "No program headers present");
      return 0;
    }

  if (segments_end < segments_start)
    {
      /* Very bad addresses.  */
      grub_error (GRUB_ERR_BAD_OS, "Bad program header load addresses");
      return 0;
    }

  return segments_end - segments_start;
}


/* Load every loadable segment into memory specified by `load_hook'.  */
grub_err_t
grub_elf64_load (grub_elf_t elf, grub_elf64_load_hook_t load_hook)
{
  return grub_elf64_phdr_iterate (elf, grub_elf64_load_segment, load_hook);
}

