
// Compiler implementation of the D programming language
// Copyright (c) 1999-2012 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// http://www.digitalmars.com
// License for redistribution is by either the Artistic License
// in artistic.txt, or the GNU General Public License in gnu.txt.
// See the included readme.txt for details.

/* Implements scanning an object module for names to go in the library table of contents.
 * The object module format is MS-COFF.
 * This format is described in the Microsoft document
 * "Microsoft Portable Executable and Common Object File Format Specification"
 * Revision 8.2 September 21, 2010
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "mscoff.h"

#include "root.h"
#include "mars.h"

#define LOG 0


/*****************************************
 * Reads an object module from base[0..buflen] and passes the names
 * of any exported symbols to (*pAddSymbol)().
 * Input:
 *      pctx            context pointer, pass to *pAddSymbol
 *      pAddSymbol      function to pass the names to
 *      base[0..buflen] contains contents of object module
 *      module_name     name of the object module (used for error messages)
 *      loc             location to use for error printing
 */

void scanMSCoffObjModule(void* pctx, void (*pAddSymbol)(void* pctx, char* name, int pickAny), void *base, size_t buflen, char *module_name, Loc loc)
{
#if LOG
    printf("scanMSCoffObjModule(%s)\n", name);
#endif

#if 0
    unsigned char *buf = (unsigned char *)base;
    int reason = 0;
    uint32_t ncmds;

    struct mach_header *header = (struct mach_header *)buf;
    struct mach_header_64 *header64 = NULL;

    /* First do sanity checks on object file
     */
    if (buflen < sizeof(struct mach_header))
    {
      Lcorrupt:
        error(loc, "MS-Coff object module %s corrupt, %d", module_name, reason);
        return;
    }

    if (header->magic == MH_MAGIC)
    {
        if (header->cputype != CPU_TYPE_I386)
        {
            error(loc, "MS-Coff object module %s has cputype = %d, should be %d",
                    module_name, header->cputype, CPU_TYPE_I386);
            return;
        }
        if (header->filetype != MH_OBJECT)
        {
            error(loc, "MS-Coff object module %s has file type = %d, should be %d",
                    module_name, header->filetype, MH_OBJECT);
            return;
        }
        if (buflen < sizeof(struct mach_header) + header->sizeofcmds)
        {   reason = 2;
            goto Lcorrupt;
        }
        ncmds = header->ncmds;
    }
    else if (header->magic == MH_MAGIC_64)
    {
        header64 = (struct mach_header_64 *)buf;
        if (buflen < sizeof(struct mach_header_64))
            goto Lcorrupt;
        if (header64->cputype != CPU_TYPE_X86_64)
        {
            error(loc, "MS-Coff object module %s has cputype = %d, should be %d",
                    module_name, header64->cputype, CPU_TYPE_X86_64);
            return;
        }
        if (header64->filetype != MH_OBJECT)
        {
            error(loc, "MS-Coff object module %s has file type = %d, should be %d",
                    module_name, header64->filetype, MH_OBJECT);
            return;
        }
        if (buflen < sizeof(struct mach_header_64) + header64->sizeofcmds)
        {   reason = 2;
            goto Lcorrupt;
        }
        ncmds = header64->ncmds;
    }
    else
    {   reason = 1;
        goto Lcorrupt;
    }

    struct segment_command *segment_commands = NULL;
    struct segment_command_64 *segment_commands64 = NULL;
    struct symtab_command *symtab_commands = NULL;
    struct dysymtab_command *dysymtab_commands = NULL;

    // Commands immediately follow mach_header
    char *commands = (char *)buf +
        (header->magic == MH_MAGIC_64
                ? sizeof(struct mach_header_64)
                : sizeof(struct mach_header));
    for (uint32_t i = 0; i < ncmds; i++)
    {   struct load_command *command = (struct load_command *)commands;
        //printf("cmd = 0x%02x, cmdsize = %u\n", command->cmd, command->cmdsize);
        switch (command->cmd)
        {
            case LC_SEGMENT:
                segment_commands = (struct segment_command *)command;
                break;
            case LC_SEGMENT_64:
                segment_commands64 = (struct segment_command_64 *)command;
                break;
            case LC_SYMTAB:
                symtab_commands = (struct symtab_command *)command;
                break;
            case LC_DYSYMTAB:
                dysymtab_commands = (struct dysymtab_command *)command;
                break;
        }
        commands += command->cmdsize;
    }

    if (symtab_commands)
    {
        // Get pointer to string table
        char *strtab = (char *)buf + symtab_commands->stroff;
        if (buflen < symtab_commands->stroff + symtab_commands->strsize)
        {   reason = 3;
            goto Lcorrupt;
        }

        if (header->magic == MH_MAGIC_64)
        {
            // Get pointer to symbol table
            struct nlist_64 *symtab = (struct nlist_64 *)((char *)buf + symtab_commands->symoff);
            if (buflen < symtab_commands->symoff + symtab_commands->nsyms * sizeof(struct nlist_64))
            {   reason = 4;
                goto Lcorrupt;
            }

            // For each symbol
            for (int i = 0; i < symtab_commands->nsyms; i++)
            {   struct nlist_64 *s = symtab + i;
                char *name = strtab + s->n_un.n_strx;

                if (s->n_type & N_STAB)
                    // values in /usr/include/mach-o/stab.h
                    ; //printf(" N_STAB");
                else
                {
                    if (s->n_type & N_PEXT)
                        ;
                    if (s->n_type & N_EXT)
                        ;
                    switch (s->n_type & N_TYPE)
                    {
                        case N_UNDF:
                            break;
                        case N_ABS:
                            break;
                        case N_SECT:
                            if (s->n_type & N_EXT /*&& !(s->n_desc & N_REF_TO_WEAK)*/)
                                (*pAddSymbol)(pctx, name, 1);
                            break;
                        case N_PBUD:
                            break;
                        case N_INDR:
                            break;
                    }
                }
            }
        }
        else
        {
            // Get pointer to symbol table
            struct nlist *symtab = (struct nlist *)((char *)buf + symtab_commands->symoff);
            if (buflen < symtab_commands->symoff + symtab_commands->nsyms * sizeof(struct nlist))
            {   reason = 4;
                goto Lcorrupt;
            }

            // For each symbol
            for (int i = 0; i < symtab_commands->nsyms; i++)
            {   struct nlist *s = symtab + i;
                char *name = strtab + s->n_un.n_strx;

                if (s->n_type & N_STAB)
                    // values in /usr/include/mach-o/stab.h
                    ; //printf(" N_STAB");
                else
                {
                    if (s->n_type & N_PEXT)
                        ;
                    if (s->n_type & N_EXT)
                        ;
                    switch (s->n_type & N_TYPE)
                    {
                        case N_UNDF:
                            break;
                        case N_ABS:
                            break;
                        case N_SECT:
                            if (s->n_type & N_EXT /*&& !(s->n_desc & N_REF_TO_WEAK)*/)
                                (*pAddSymbol)(pctx, name, 1);
                            break;
                        case N_PBUD:
                            break;
                        case N_INDR:
                            break;
                    }
                }
            }
        }
    }
#endif
}

