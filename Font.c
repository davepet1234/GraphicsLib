/*
 * File:    Font.c
 * 
 * Author:  David Petrovic
 * 
 * Description:
 * 
 * https://github.com/tuupola/hagl - d3c172ee40b79080a5e6cd08f69522d4edd2b985
 * https://github.com/tuupola/embedded-fonts
 * 
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h> //###
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include "fonts/font5x7-ISO8859-1.h"
#include "fonts/font5x8-ISO8859-1.h"
#include "fonts/font6x9-ISO8859-1.h"
#include "fonts/font6x10-ISO8859-1.h"
#include "fonts/font6x12-ISO8859-1.h"
#include "fonts/font6x13-ISO8859-1.h"
#include "fonts/font6x13B-ISO8859-1.h"
#include "fonts/font6x13O-ISO8859-1.h"
#include "fonts/font7x13B-ISO8859-1.h"
#include "fonts/font7x13-ISO8859-1.h"
#include "fonts/font7x13O-ISO8859-1.h"
#include "fonts/font7x14B-ISO8859-1.h"
#include "fonts/font7x14-ISO8859-1.h"
#include "fonts/font8x13-ISO8859-1.h"
#include "fonts/font8x13B-ISO8859-1.h"
#include "fonts/font8x13O-ISO8859-1.h"
#include "fonts/font9x15-ISO8859-1.h"
#include "fonts/font9x15B-ISO8859-1.h"
#include "fonts/font9x18-ISO8859-1.h"
#include "fonts/font9x18B-ISO8859-1.h"
#include "fonts/font10x20-ISO8859-1.h"
#include "Font.h"


FONT FontList[] = {
    FONT5x7,
    FONT5x8,
    FONT6x9,
    FONT6x10,
    FONT6x12,
    FONT6x13, FONT6x13B, FONT6x13O,
    FONT7x13B, FONT7x13, FONT7x13O,
    FONT7x14B, FONT7x14,
    FONT8x13, FONT8x13B, FONT8x13O,
    FONT9x15, FONT9x15B,
    FONT9x18, FONT9x18B,
    FONT10x20
};
UINTN FontListSize = sizeof(FontList) / sizeof(FontList[0]);

/*
 * See "Using FONTX font files"
 * http://elm-chan.org/docs/dosv/fontx_e.html
 */
CONST UINT8 *GetCharBitmap (    /* Returns pointer to the font image (NULL:invalid code) */
    FONT font,                  /* Pointer to the FONTX file on the memory */
    UINT16 code                 /* Character code */
)
{
    UINTN nc, bc, sb, eb;
    UINT32 fsz;
    CONST UINT8 *cblk;

    fsz = (font[14] + 7) / 8 * font[15];  /* Get font size */

    if (font[16] == 0) {  /* Single byte code font */
        if (code < 0x100) return &font[17 + code * fsz];
    } else {              /* Double byte code font */
        cblk = &font[18]; nc = 0;  /* Code block table */
        bc = font[17];
        while (bc--) {
            sb = cblk[0] + cblk[1] * 0x100;  /* Get range of the code block */
            eb = cblk[2] + cblk[3] * 0x100;
            if (code >= sb && code <= eb) {  /* Check if in the code block */
                nc += code - sb;             /* Number of codes from top of the block */
//                Print(L"Pos: %d\n", 18 + 4 * font[17] + nc * fsz);
                return &font[18 + 4 * font[17] + nc * fsz];
            }
            nc += eb - sb + 1;     /* Number of codes in the previous blocks */
            cblk += 4;             /* Next code block */
        }
    }

    return 0;   /* Invalid code */
}

VOID PrintFontInfo(FONT font)
{
    CHAR8 FileSig[7] = {0};
    AsciiStrnCpyS(FileSig, 7, (CONST CHAR8 *)&font[0], 6);
    CHAR8 FontName[9] = {0};
    AsciiStrnCpyS(FontName, 9, (CONST CHAR8 *)&font[6], 8);
    UINT8 CodeFlag = font[16];
   
    Print(L"File Sig   : %a\n", FileSig);
    Print(L"Font Name  : %a\n", FontName);
    Print(L"Font Width : %u\n", GetFontWidth(font));
    Print(L"Font Height: %u\n", GetFontHeight(font));
    Print(L"Code Flag  : %u\n", CodeFlag);
}
