/*****************************************************************************/
/*                                                                           */
/*                                   bin.c                                   */
/*                                                                           */
/*                  Module to handle the raw binary format                   */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 1999-2012, Ullrich von Bassewitz                                      */
/*                Roemerstrasse 52                                           */
/*                D-70794 Filderstadt                                        */
/* EMail:         uz@cc65.org                                                */
/*                                                                           */
/*                                                                           */
/* This software is provided 'as-is', without any expressed or implied       */
/* warranty.  In no event will the authors be held liable for any damages    */
/* arising from the use of this software.                                    */
/*                                                                           */
/* Permission is granted to anyone to use this software for any purpose,     */
/* including commercial applications, and to alter it and redistribute it    */
/* freely, subject to the following restrictions:                            */
/*                                                                           */
/* 1. The origin of this software must not be misrepresented; you must not   */
/*    claim that you wrote the original software. If you use this software   */
/*    in a product, an acknowledgment in the product documentation would be  */
/*    appreciated but is not required.                                       */
/* 2. Altered source versions must be plainly marked as such, and must not   */
/*    be misrepresented as being the original software.                      */
/* 3. This notice may not be removed or altered from any source              */
/*    distribution.                                                          */
/*                                                                           */
/*****************************************************************************/



#include <stdio.h>
#include <string.h>
#include <errno.h>

/* common */
#include "alignment.h"
#include "print.h"
#include "xmalloc.h"

/* ld65 */
#include "img.h"
#include "config.h"
#include "exports.h"
#include "expr.h"
#include "error.h"
#include "global.h"
#include "fileio.h"
#include "lineinfo.h"
#include "memarea.h"
#include "segments.h"
#include "spool.h"



/*****************************************************************************/
/*                                   Data                                    */
/*****************************************************************************/



struct ImgDesc {
    unsigned    Undef;          /* Count of undefined externals */
    FILE*       F;              /* Output file */
    const char* Filename;       /* Name of output file */
};



/*****************************************************************************/
/*                                   Code                                    */
/*****************************************************************************/



ImgDesc* NewImgDesc (void)
/* Create a new binary format descriptor */
{
    /* Allocate memory for a new ImgDesc struct */
    ImgDesc* D = xmalloc (sizeof (ImgDesc));

    /* Initialize the fields */
    D->Undef    = 0;
    D->F        = 0;
    D->Filename = 0;

    /* Return the created struct */
    return D;
}



void FreeImgDesc (ImgDesc* D)
/* Free a binary format descriptor */
{
    xfree (D);
}



static unsigned ImgWriteExpr (ExprNode* E, int Signed, unsigned Size,
                              unsigned long Offs attribute ((unused)),
                              void* Data)
/* Called from SegWrite for an expression. Evaluate the expression, check the
** range and write the expression value to the file.
*/
{
    /* There's a predefined function to handle constant expressions */
    return SegWriteConstExpr (((ImgDesc*)Data)->F, E, Signed, Size);
}



static void PrintBoolVal (const char* Name, int B)
/* Print a boolean value for debugging */
{
    Print (stdout, 2, "      %s = %s\n", Name, B? "true" : "false");
}



static void PrintNumVal (const char* Name, unsigned long V)
/* Print a numerical value for debugging */
{
    Print (stdout, 2, "      %s = 0x%lx\n", Name, V);
}



static void ImgWriteMem (ImgDesc* D, MemoryArea* M)
/* Write the segments of one memory area to a file */
{
    unsigned I;

    /* Walk over all segments in this memory area */
    for (I = 0; I < CollCount (&M->SegList); ++I) {

        int DoWrite;

        /* Get the segment */
        SegDesc* S = CollAtUnchecked (&M->SegList, I);

        /* Keep the user happy */
        Print (stdout, 1, "    Writing `%s'\n", GetString (S->Name));

        /* Writes do only occur in the load area and not for BSS segments */
        DoWrite = (S->Flags & SF_BSS) == 0      &&      /* No BSS segment */
                   S->Load == M                 &&      /* LOAD segment */
                   S->Seg->Dumped == 0;                 /* Not already written */

        /* Output debugging stuff */
        PrintBoolVal ("bss", S->Flags & SF_BSS);
        PrintBoolVal ("LoadArea", S->Load == M);
        PrintBoolVal ("Dumped", S->Seg->Dumped);
        PrintBoolVal ("DoWrite", DoWrite);
        PrintNumVal  ("Address", S->Seg->PC);
        PrintNumVal  ("Size", S->Seg->Size);
        PrintNumVal  ("FileOffs", (unsigned long) ftell (D->F));

        /* Now write the segment to disk if it is not a BSS type segment and
        ** if the memory area is the load area.
        */
        if (DoWrite) {
			// Check that the segment's start address is properly aligned.
			if (S->Addr != AlignAddr (S->Addr, S->LoadAlignment)) {
				Internal ("Invalid alignment for segment %s: %ld/%lu",
                  GetString (S->Name), S->Addr, S->LoadAlignment);
			}

            unsigned long P = ftell (D->F);
			Write16 (D->F, S->Seg->PC);
			Write16 (D->F, S->Seg->Size);
            SegWrite (D->Filename, D->F, S->Seg, ImgWriteExpr, D);
            PrintNumVal ("Wrote", (unsigned long) (ftell (D->F) - P));
        }

        /* If this was the load memory area, mark the segment as dumped */
        if (S->Load == M) {
            S->Seg->Dumped = 1;
        }
    }
}



static int ImgUnresolved (unsigned Name attribute ((unused)), void* D)
/* Called if an unresolved symbol is encountered */
{
    /* Unresolved symbols are an error in binary format. Bump the counter
    ** and return zero telling the caller that the symbol is indeed
    ** unresolved.
    */
    ((ImgDesc*) D)->Undef++;
    return 0;
}



void ImgWriteTarget (ImgDesc* D, struct File* F)
/* Write a binary output file */
{
    unsigned I;

    /* Place the filename in the control structure */
    D->Filename = GetString (F->Name);

    /* Check for unresolved symbols. The function ImgUnresolved is called
    ** if we get an unresolved symbol.
    */
    D->Undef = 0;               /* Reset the counter */
    CheckUnresolvedImports (ImgUnresolved, D);
    if (D->Undef > 0) {
        /* We had unresolved symbols, cannot create output file */
        Error ("%u unresolved external(s) found - cannot create output file", D->Undef);
    }

    /* Open the file */
    D->F = fopen (D->Filename, "wb");
    if (D->F == 0) {
        Error ("Cannot open `%s': %s", D->Filename, strerror (errno));
    }

    /* Keep the user happy */
    Print (stdout, 1, "Opened `%s'...\n", D->Filename);

    /* Dump all memory areas */
    for (I = 0; I < CollCount (&F->MemoryAreas); ++I) {
        /* Get this entry */
        MemoryArea* M = CollAtUnchecked (&F->MemoryAreas, I);
        Print (stdout, 1, "  Dumping `%s'\n", GetString (M->Name));
        ImgWriteMem (D, M);
    }

    /* Close the file */
    if (fclose (D->F) != 0) {
        Error ("Cannot write to `%s': %s", D->Filename, strerror (errno));
    }

    /* Reset the file and filename */
    D->F        = 0;
    D->Filename = 0;
}
