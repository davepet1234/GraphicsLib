/*
 * File:    Graphics.h
 * 
 * Author:  David Petrovic
 *
 * Description:
 * 
 * 
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#define FONT_SUPPORT  1

#include <Uefi.h>
#include <Library/UefiLib.h>
#if FONT_SUPPORT
#include "Font.h"
#endif

typedef struct {
    UINT32  Sig;
    INT32   HorRes;
    INT32   VerRes;
    INT32   PixPerScnLn;
    INT32   ClipX0;
    INT32   ClipY0;
    INT32   ClipX1;
    INT32   ClipY1;
    UINT32  *PixelData;
} RENDER_BUFFER;

#if FONT_SUPPORT
typedef struct {
    RENDER_BUFFER   *RenBuf;
    INT32           X0;
    INT32           Y0;
    INT32           X1;
    INT32           Y1;
    FONT            Font;
    INT32           CurrX;
    INT32           CurrY;  
    UINT32          FgColour;
    UINT32          BgColour;
    BOOLEAN         BgColourEnabled;  // determines if text background is written
    BOOLEAN         LineWrapEnabled;
    BOOLEAN         ScrollEnabled;    
} TEXT_CONFIG;
#endif

// Functions to initialise frame buffer
EFI_STATUS InitGraphics(VOID);
EFI_STATUS RestoreConsole(VOID);
EFI_STATUS SetGraphicsMode(UINT32 Mode);
EFI_STATUS GetGraphicsMode(UINT32 *Mode);
EFI_STATUS SetDisplayResolution(UINT32 Width, UINT32 Height);
UINT32 NumGraphicsModes(VOID);
EFI_STATUS QueryGraphicsMode(UINT32 Mode, UINT32 *HorRes, UINT32 *VerRes);
INT32 GetFBHorRes(VOID);
INT32 GetFBVerRes(VOID);

// Functions that control render target
EFI_STATUS CreateRenderBuffer(RENDER_BUFFER *RenBuf, UINT32 Width, UINT32 Height);
EFI_STATUS DestroyRenderBuffer(RENDER_BUFFER *RenBuf);
EFI_STATUS SetRenderBuffer(RENDER_BUFFER *RenBuf);
EFI_STATUS SetScreenRender(VOID);
EFI_STATUS DisplayRenderBuffer(RENDER_BUFFER *RenBuf, INT32 x, INT32 y);

// Functions that operate on current render target
UINT32 GetHorRes(VOID);
UINT32 GetVerRes(VOID);

VOID ResetClipping(VOID);
VOID SetClipping(INT32 x0, INT32 y0, INT32 x1, INT32 y1);
BOOLEAN Clipped(VOID);

VOID ClearScreen(UINT32 colour);
VOID ClearClipWindow(UINT32 colour);

VOID PutPixel(INT32 x, INT32 y, UINT32 colour);
UINT32 GetPixel(INT32 x, INT32 y);
VOID DrawHLine(INT32 x, INT32 y, INT32 width, UINT32 colour);
VOID DrawHLine2(INT32 x0, INT32 x1, INT32 y, UINT32 colour);
VOID DrawVLine(INT32 x, INT32 y, INT32 height, UINT32 colour);
VOID DrawLine(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour);
VOID DrawTriangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, INT32 x2, INT32 y2, UINT32 colour);
VOID DrawRectangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour);
VOID DrawFillTriangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, INT32 x2, INT32 y2, UINT32 colour);
VOID DrawFillRectangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour);
VOID DrawCircle(INT32 xc, INT32 yc, INT32 r, UINT32 colour);
VOID DrawFillCircle(INT32 xc, INT32 yc, INT32 r, UINT32 colour);

#if FONT_SUPPORT

UINTN EFIAPI GPrint(TEXT_CONFIG *TxtCfg, CHAR16 *sFormat, ...);
EFI_STATUS GPutString(INT32 x, INT32 y, UINT16 *string, UINT32 FgColour, UINT32 BgColour, BOOLEAN BgColourEnabled, FONT Font);

VOID EnableTextBackground(BOOLEAN State);
VOID SetTextForeground(UINT32 colour);
VOID SetTextBackground(UINT32 colour);
VOID SetFont(FONT font);

VOID EnableTextBoxBackground(TEXT_CONFIG *TxtCfg, BOOLEAN State);
VOID SetTextBoxForeground(TEXT_CONFIG *TxtCfg, UINT32 colour);
VOID SetTextBoxBackground(TEXT_CONFIG *TxtCfg, UINT32 colour);
VOID SetTextBoxFont(TEXT_CONFIG *TxtCfg, FONT font);

EFI_STATUS CreateTextBox(TEXT_CONFIG *TxtCfg, INT32 x, INT32 y, INT32 Width, INT32 Height, UINT32 FgColour, UINT32 BgColour, FONT Font);
VOID ClearTextBox(TEXT_CONFIG *TxtCfg);

#endif // FONT_SUPPORT


#define RGB_COLOUR(r, g, b) (((r) << 16) | ((g) << 8) | (b))

#define RED         RGB_COLOUR(255, 0, 0)
#define DARK_RED    RGB_COLOUR(139, 0, 0)
#define PINK        RGB_COLOUR(255, 192, 203)
#define DEEP_PINK   RGB_COLOUR(255, 20, 147)
#define ORANGE      RGB_COLOUR(255, 165, 0)
#define DARK_ORANGE RGB_COLOUR(255, 140, 0)
#define GOLD        RGB_COLOUR(255, 215, 0)
#define YELLOW      RGB_COLOUR(255, 255, 0)
#define VIOLET      RGB_COLOUR(238, 130, 238)
#define MAGENTA     RGB_COLOUR(255, 0, 255)
#define DARK_VIOLET RGB_COLOUR(148, 0, 211)
#define INDIGO      RGB_COLOUR(75, 0, 130)
#define LIGHT_GREEN RGB_COLOUR(144, 238, 144)
#define GREEN       RGB_COLOUR(0, 255, 0)
#define DARK_GREEN  RGB_COLOUR(0, 100, 0)
#define OLIVE       RGB_COLOUR(128, 128, 0)
#define CYAN        RGB_COLOUR(0, 255, 255)
#define LIGHT_BLUE  RGB_COLOUR(173, 216, 230)
#define BLUE        RGB_COLOUR(0, 0, 255)
#define DARK_BLUE   RGB_COLOUR(0, 0, 139)
#define BROWN       RGB_COLOUR(165, 42, 42)
#define MAROON      RGB_COLOUR(128, 0, 0)
#define WHITE       RGB_COLOUR(255, 255, 255)
#define LIGHT_GRAY  RGB_COLOUR(211, 211, 211)
#define GRAY        RGB_COLOUR(128, 128, 128)
#define DARK_GRAY   RGB_COLOUR(169, 169, 169)
#define SILVER      RGB_COLOUR(192, 192, 192)
#define BLACK       RGB_COLOUR(0, 0, 0)
#define PURPLE      RGB_COLOUR(128, 0, 128)

//-------------------------------------------------------------------------
// WIP
//-------------------------------------------------------------------------


#endif // GRAPHICS_H
