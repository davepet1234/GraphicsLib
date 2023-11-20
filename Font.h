/*
 * File:    Font.h
 * 
 * Author:  David Petrovic
 *
 * Description:
 * 
 * 
 */

#ifndef FONT_H
#define FONT_H

typedef CONST UINT8* FONT;

extern CONST UINT8 font5x7_ISO8859_1[];
extern CONST UINT8 font5x8_ISO8859_1[];
extern CONST UINT8 font6x9_ISO8859_1[];
extern CONST UINT8 font6x10_ISO8859_1[];
extern CONST UINT8 font6x12_ISO8859_1[];
extern CONST UINT8 font6x13_ISO8859_1[];
extern CONST UINT8 font6x13B_ISO8859_1[];
extern CONST UINT8 font6x13O_ISO8859_1[];
extern CONST UINT8 font7x13B_ISO8859_1[];
extern CONST UINT8 font7x13_ISO8859_1[];
extern CONST UINT8 font7x13O_ISO8859_1[];
extern CONST UINT8 font7x14B_ISO8859_1[];
extern CONST UINT8 font7x14_ISO8859_1[];
extern CONST UINT8 font8x13_ISO8859_1[];
extern CONST UINT8 font8x13B_ISO8859_1[];
extern CONST UINT8 font8x13O_ISO8859_1[];
extern CONST UINT8 font9x15_ISO8859_1[];
extern CONST UINT8 font9x15B_ISO8859_1[];
extern CONST UINT8 font9x18_ISO8859_1[];
extern CONST UINT8 font9x18B_ISO8859_1[];
extern CONST UINT8 font10x20_ISO8859_1[];

#define FONT5x7     font5x7_ISO8859_1
#define FONT5x8     font5x8_ISO8859_1
#define FONT6x9     font6x9_ISO8859_1
#define FONT6x10    font6x10_ISO8859_1
#define FONT6x12    font6x12_ISO8859_1
#define FONT6x13    font6x13_ISO8859_1
#define FONT6x13B   font6x13B_ISO8859_1
#define FONT6x13O   font6x13O_ISO8859_1
#define FONT7x13B   font7x13B_ISO8859_1
#define FONT7x13    font7x13_ISO8859_1
#define FONT7x13O   font7x13O_ISO8859_1
#define FONT7x14B   font7x14B_ISO8859_1
#define FONT7x14    font7x14_ISO8859_1
#define FONT8x13    font8x13_ISO8859_1
#define FONT8x13B   font8x13B_ISO8859_1
#define FONT8x13O   font8x13O_ISO8859_1
#define FONT9x15    font9x15_ISO8859_1
#define FONT9x15B   font9x15B_ISO8859_1
#define FONT9x18    font9x18_ISO8859_1
#define FONT9x18B   font9x18B_ISO8859_1
#define FONT10x20   font10x20_ISO8859_1

extern FONT FontList[];
extern UINTN FontListSize;

CONST UINT8 *GetCharBitmap(FONT font, UINT16 code);
inline CONST UINT8 GetFontWidth(FONT font) { return font[14]; }
inline CONST UINT8 GetFontHeight(FONT font) { return font[15]; }
VOID PrintFontInfo(FONT font);


#endif // FONT_H