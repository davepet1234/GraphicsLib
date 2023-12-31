/*
 * File:    Graphics.c
 * 
 * Author:  David Petrovic
 * 
 * Description:
 * 
*/

#define DEBUG_SUPPORT 0

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h> //###
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include "Graphics.h"

#if FONT_SUPPORT
#include "Font.h"
#endif

#if DEBUG_SUPPORT
#include "DebugPrint.h"
#else
#define DbgPrint(...)
#endif

// When defined uses a different function to drawn (full) circles that are not clipped
#define CIRCLE_OPTIMISATION 1

// When defined uses SetMem32() function to write to FB when possible
#undef EDK2_MEM_FUNC


// prototypes
STATIC VOID init_globals(VOID);
STATIC VOID set_clipping(RENDER_BUFFER *RenBuf, INT32 x0, INT32 y0, INT32 x1, INT32 y1);
STATIC VOID reset_clipping(RENDER_BUFFER *RenBuf);
STATIC BOOLEAN clipped(RENDER_BUFFER *RenBuf);
STATIC VOID draw_line(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour);
STATIC VOID draw_part_circle(INT32 xc, INT32 yc, INT32 r, UINT32 colour);
#if CIRCLE_OPTIMISATION
STATIC BOOLEAN draw_full_circle(INT32 xc, INT32 yc, INT32 r, UINT32 colour);
#endif
#if FONT_SUPPORT
STATIC EFI_STATUS put_string(RENDER_BUFFER *RenBuf, TEXT_CONFIG *TxtCfg, UINT16 *string);
#endif

STATIC BOOLEAN Initialised = FALSE;
#define RENBUF_SIG 0x52425546UL   // "RBUF"

// globals
STATIC UINTN                            gOrigGfxMode = 0;
STATIC UINTN                            gOrigTxtMode = 0;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL     *gGop = NULL;
STATIC UINTN                            gCurrMode = 0;
STATIC RENDER_BUFFER                    gFrameBuffer = {0};
STATIC RENDER_BUFFER                    *gCurrRenBuf = NULL;
#if FONT_SUPPORT
STATIC TEXT_CONFIG                      gFBTxtCfg = { 0 };
#endif

EFI_STATUS InitGraphics(VOID)
{
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void**) &gGop);
    if (EFI_ERROR(Status)){
        DbgPrint(DL_ERROR, "GOP missing!\n");
        goto error_exit;
    }
    // remember original graphics mode as takes precidence over that required for text
    gOrigGfxMode = gGop->Mode->Mode;
    gOrigTxtMode = gST->ConOut->Mode->Mode;
    init_globals();
    Initialised = TRUE;
error_exit:
    return Status;
}

STATIC VOID init_globals(VOID)
{
    gCurrMode = gGop->Mode->Mode;
    gFrameBuffer.Sig = RENBUF_SIG;
    gFrameBuffer.HorRes = gGop->Mode->Info->HorizontalResolution;
    gFrameBuffer.VerRes = gGop->Mode->Info->VerticalResolution;
    gFrameBuffer.PixPerScnLn = gGop->Mode->Info->PixelsPerScanLine;
    gFrameBuffer.PixelData = (UINT32 *)gGop->Mode->FrameBufferBase;
    reset_clipping(&gFrameBuffer);
    gCurrRenBuf = &gFrameBuffer;
#if FONT_SUPPORT
    // Text Config
    gFBTxtCfg.X0 = 0;
    gFBTxtCfg.Y0 = 0;
    gFBTxtCfg.X1 = gFrameBuffer.HorRes - 1;
    gFBTxtCfg.Y1 = gFrameBuffer.VerRes - 1;
    gFBTxtCfg.Font = FONT10x20;
    gFBTxtCfg.CurrX = gFBTxtCfg.X0;
    gFBTxtCfg.CurrY = gFBTxtCfg.Y0;
    gFBTxtCfg.FgColour = WHITE;
    gFBTxtCfg.BgColour = BLACK;
    gFBTxtCfg.BgColourEnabled = TRUE;
    gFBTxtCfg.LineWrapEnabled = TRUE;
    gFBTxtCfg.ScrollEnabled = TRUE;
#endif
}

EFI_STATUS RestoreConsole(VOID)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    // set original graphics mode
    SetGraphicsMode(gOrigGfxMode);
    // set original text mode
    return gST->ConOut->SetMode(gST->ConOut, gOrigTxtMode);
}

EFI_STATUS SetGraphicsMode(UINTN Mode)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    EFI_STATUS Status = gGop->SetMode(gGop, Mode);
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a(): returned %a\n", __func__, EFIStatusToStr(Status));
    }
    init_globals();
    return Status;
}

EFI_STATUS GetGraphicsMode(UINTN *Mode)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    if (!Mode) {
        return EFI_INVALID_PARAMETER;
    }
    *Mode = gCurrMode;
    return EFI_SUCCESS;
}

EFI_STATUS SetDisplayResolution(UINT32 HorRes, UINT32 VerRes)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    for (UINTN i = 0; i < gGop->Mode->MaxMode; ++i) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* ModeInfo = NULL;
        UINTN ModeSize = gGop->Mode->SizeOfInfo;

        EFI_STATUS Status = gGop->QueryMode(gGop, i, &ModeSize, &ModeInfo);
        if (EFI_ERROR(Status)){
            Print(L"ERROR: Failed mode query on %d: 0x%x\n", i, Status);
            return Status;
        }
        if (ModeInfo->HorizontalResolution == HorRes && ModeInfo->VerticalResolution == VerRes) {
            return SetGraphicsMode(i);
        }
	}

    return EFI_UNSUPPORTED;
}

UINTN NumGraphicsModes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gGop->Mode->MaxMode;
}

EFI_STATUS QueryGraphicsMode(UINTN Mode, UINT32 *HorRes, UINT32 *VerRes)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* ModeInfo = NULL;
    UINTN ModeSize = gGop->Mode->SizeOfInfo;
    EFI_STATUS Status = gGop->QueryMode(gGop, Mode, &ModeSize, &ModeInfo);
    if (EFI_ERROR(Status)){
        return Status;
    }
    if (HorRes) {
        *HorRes = ModeInfo->HorizontalResolution;
    }
    if (VerRes) {
        *VerRes = ModeInfo->VerticalResolution;
    }
    return EFI_SUCCESS;
}

UINT32 GetFBHorRes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gFrameBuffer.HorRes;
}

UINT32 GetFBVerRes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gFrameBuffer.VerRes;
}

EFI_STATUS CreateRenderBuffer(RENDER_BUFFER *RenBuf, UINT32 Width, UINT32 Height)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (!Initialised) {
        Status = EFI_NOT_READY;
        goto error_exit;
    }
    if (!RenBuf) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    // allocate buffer
    UINTN Memsize = Width*Height*sizeof(UINT32); // or sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) ?????
    UINT32 *Buff = AllocateZeroPool(Memsize);
    if (Buff == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }
    // fill in rest of RenBuf
    RenBuf->Sig = RENBUF_SIG;
    RenBuf->HorRes = Width;
    RenBuf->VerRes = Height;
    RenBuf->PixPerScnLn = Width;
    reset_clipping(RenBuf);
    RenBuf->PixelData = Buff;
error_exit:
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a() returned %a\n", __func__, EFIStatusToStr(Status));
    }
    return Status;
}

EFI_STATUS DestroyRenderBuffer(RENDER_BUFFER *RenBuf)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (!Initialised) {
        Status = EFI_NOT_READY;
        goto error_exit;
    }
    if (!RenBuf) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    if (RenBuf->Sig != RENBUF_SIG) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    if (RenBuf->PixelData) {
        FreePool(RenBuf->PixelData);
        RenBuf->PixelData = NULL;
    }
    if (RenBuf == gCurrRenBuf) {
        // if we are destroying the current render buffer then
        // revert to frame buffer
        gCurrRenBuf = &gFrameBuffer;
    }
error_exit:
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a() returned %a\n", __func__, EFIStatusToStr(Status));
    }
    return Status;
}

EFI_STATUS SetRenderBuffer(RENDER_BUFFER *RenBuf)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (!Initialised) {
        Status = EFI_NOT_READY;
        goto error_exit;
    }
    if (!RenBuf) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    if (RenBuf->Sig != RENBUF_SIG) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    gCurrRenBuf = RenBuf;
error_exit:
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a() returned %a\n", __func__, EFIStatusToStr(Status));
    }
    return Status;
}

EFI_STATUS SetScreenRender(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (!Initialised) {
        Status = EFI_NOT_READY;
        goto error_exit;
    }
    gCurrRenBuf = &gFrameBuffer;
error_exit:
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a() returned %a\n", __func__, EFIStatusToStr(Status));
    }
    return Status;
}

EFI_STATUS DisplayRenderBuffer(RENDER_BUFFER *RenBuf, INT32 x, INT32 y)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (!Initialised) {
        Status = EFI_NOT_READY;
        goto error_exit;
    }
    if (!RenBuf) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    if (RenBuf->Sig != RENBUF_SIG) {
        Status = EFI_INVALID_PARAMETER;
        goto error_exit;
    }
    // (x,y) source in render buffer
    INT32 src_x = 0;
    INT32 src_y = 0;

    // location on screen
    INT32 dst_xl = x;
    INT32 dst_yt = y;
    INT32 dst_xr = x + RenBuf->HorRes - 1;
    INT32 dst_yb = y + RenBuf->VerRes - 1;

    // check if visible
    if (dst_yb < gFrameBuffer.ClipY0 || dst_yt > gFrameBuffer.ClipY1 || dst_xr < gFrameBuffer.ClipX0 || dst_xl > gFrameBuffer.ClipX1) {
        goto error_exit;
    }

    // clip source image
    UINT32 width = dst_xr - dst_xl + 1;
    UINT32 height = dst_yb - dst_yt + 1;
    if (dst_xl < gFrameBuffer.ClipX0) {
        dst_xl = gFrameBuffer.ClipX0;
        width = (dst_xr - gFrameBuffer.ClipX0 + 1);
        src_x = gFrameBuffer.ClipX0 - x;
    }
    if (dst_xr > gFrameBuffer.ClipX1) {
        width -= (dst_xr - gFrameBuffer.ClipX1);
    }
    if (dst_yt < gFrameBuffer.ClipY0) {
        dst_yt = gFrameBuffer.ClipY0;
        height = (dst_yb - gFrameBuffer.ClipY0 + 1);
        src_y = gFrameBuffer.ClipY0 - y;
    }
    if (dst_yb > gFrameBuffer.ClipY1) {
        height -= (dst_yb - gFrameBuffer.ClipY1);
    }

    Status = gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)RenBuf->PixelData, EfiBltBufferToVideo, src_x, src_y, dst_xl, dst_yt, width, height, sizeof(UINT32)*RenBuf->PixPerScnLn);
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
    }
    
error_exit:
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a() returned %a\n", __func__, EFIStatusToStr(Status));
    }
    return Status;
}

UINT32 GetHorRes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gCurrRenBuf->HorRes;
}

UINT32 GetVerRes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gCurrRenBuf->VerRes;
}

VOID SetClipping(INT32 x0, INT32 y0, INT32 x1, INT32 y1)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);
    set_clipping(gCurrRenBuf, x0, y0, x1, y1);
}

VOID set_clipping(RENDER_BUFFER *RenBuf, INT32 x0, INT32 y0, INT32 x1, INT32 y1)
{
    // determine top-left bottom-right
    if (x0 < x1) {
        RenBuf->ClipX0 = x0;
        RenBuf->ClipX1 = x1;
    } else {
        RenBuf->ClipX0 = x1;
        RenBuf->ClipX1 = x0;
    }
    if (y0 < y1) {
        RenBuf->ClipY0 = y0;
        RenBuf->ClipY1 = y1;
    } else {
        RenBuf->ClipY0 = y1;
        RenBuf->ClipY1 = y0;
    }
    // clip to screen
    if (RenBuf->ClipX0 < 0) {
        RenBuf->ClipX0 = 0;
    } else if (RenBuf->ClipX0 >= RenBuf->HorRes) {
        RenBuf->ClipX0 = RenBuf->HorRes - 1;
    }
    if (RenBuf->ClipX1 < 0) {
        RenBuf->ClipX1 = 0;
    } else if (RenBuf->ClipX1 >= RenBuf->HorRes) {
        RenBuf->ClipX1 = RenBuf->HorRes - 1;
    }
    if (RenBuf->ClipY0 < 0) {
        RenBuf->ClipY0 = 0;
    } else if (RenBuf->ClipY0 >= RenBuf->VerRes) {
        RenBuf->ClipY0 = RenBuf->VerRes - 1;
    }
    if (RenBuf->ClipY1 < 0) {
        RenBuf->ClipY1 = 0;
    } else if (RenBuf->ClipY1 >= RenBuf->VerRes) {
        RenBuf->ClipY1 = RenBuf->VerRes - 1;
    }
}

VOID ResetClipping(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);
    reset_clipping(gCurrRenBuf);
}

STATIC VOID reset_clipping(RENDER_BUFFER *RenBuf)
{
    RenBuf->ClipX0 = 0;
    RenBuf->ClipY0 = 0;
    RenBuf->ClipX1 = RenBuf->HorRes - 1;
    RenBuf->ClipY1 = RenBuf->VerRes - 1;
}

BOOLEAN Clipped(VOID)
{
    return clipped(gCurrRenBuf);
}

BOOLEAN clipped(RENDER_BUFFER *RenBuf)
{
    if (RenBuf->ClipX0 != 0 || RenBuf->ClipY0 != 0 || RenBuf->ClipX1 != RenBuf->HorRes - 1 || RenBuf->ClipY1 != RenBuf->VerRes - 1) {
        return TRUE;
    }
    return FALSE;
}

VOID ClearScreen(UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    UINT32 *ptr = gCurrRenBuf->PixelData;
    if (gCurrRenBuf->HorRes == gCurrRenBuf->PixPerScnLn) {
#ifdef EDK2_MEM_FUNC
        SetMem32(ptr, gCurrRenBuf->PixPerScnLn * gCurrRenBuf->HorRes * sizeof(UINT32), colour);
#else
    UINT32 count = gCurrRenBuf->PixPerScnLn * gCurrRenBuf->HorRes;
    while (count--) {
        *ptr++ = colour;
    }
#endif
    } else {
#ifdef EDK2_MEM_FUNC
        UINT32 height = gCurrRenBuf->VerRes;
        UINT32 wbytes = gCurrRenBuf->HorRes * sizeof(UINT32);
        while (height--) {
            SetMem32(ptr, wbytes, colour);
            ptr += gCurrRenBuf->PixPerScnLn;
        }
#else
        UINT32 h, w;
        UINT32 offset = gCurrRenBuf->PixPerScnLn - gCurrRenBuf->HorRes;
        for (h=0; h<gCurrRenBuf->VerRes; h++) {
            for (w=0; w<gCurrRenBuf->HorRes; w++) {
                *ptr++ = colour;
            }
            ptr += offset;
        }
#endif        
    }
    // reset text position
    gFBTxtCfg.CurrX = gFBTxtCfg.X0;
    gFBTxtCfg.CurrY = gFBTxtCfg.Y0;
}

VOID ClearClipWindow(UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    UINT32 *ptr = gCurrRenBuf->PixelData + gCurrRenBuf->ClipX0 + (gCurrRenBuf->ClipY0 * gCurrRenBuf->PixPerScnLn);
#ifdef EDK2_MEM_FUNC
    UINT32 height = gCurrRenBuf->ClipY1 - gCurrRenBuf->ClipY0 +1;
    UINT32 wbytes = (gCurrRenBuf->ClipX1 - gCurrRenBuf->ClipX0 + 1) * sizeof(UINT32);
    while (height--) {
        SetMem32(ptr, wbytes, colour);
        ptr += gCurrRenBuf->PixPerScnLn;
    }
#else
    UINT32 height = gCurrRenBuf->ClipY1 - gCurrRenBuf->ClipY0 + 1;
    UINT32 width = gCurrRenBuf->ClipX1 - gCurrRenBuf->ClipX0 + 1;
    UINT32 offset = gCurrRenBuf->PixPerScnLn - width;
    for (UINT32 h=0; h<height; h++) {
        for (UINT32 w=0; w<width; w++) {
            *ptr++ = colour;
        }
        ptr += offset;
    }
#endif
}

VOID PutPixel(INT32 x, INT32 y, UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    // clip pixel
    if (x < gCurrRenBuf->ClipX0 || x > gCurrRenBuf->ClipX1 || y < gCurrRenBuf->ClipY0 || y > gCurrRenBuf->ClipY1) {
        return;
    }
    // draw pixel
    UINT32 *ptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
    *ptr = colour;
}

VOID DrawHLine(INT32 x, INT32 y, INT32 width, UINT32 colour)
{
    if (!Initialised){
        return;
    }
    // clip line
    INT32 x1 = x + width - 1;
    if (y < gCurrRenBuf->ClipY0 || y > gCurrRenBuf->ClipY1 || x1 < gCurrRenBuf->ClipX0 || x > gCurrRenBuf->ClipX1) {
        return;
    }
    if (x < gCurrRenBuf->ClipX0) {
        x = gCurrRenBuf->ClipX0;
        width = (x1 - gCurrRenBuf->ClipX0 + 1);
    }
    if (x1 > gCurrRenBuf->ClipX1) {
        width -= (x1 - gCurrRenBuf->ClipX1);
    }
    // draw line
    UINT32 *ptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
#ifdef EDK2_MEM_FUNC
    SetMem32(ptr, width * sizeof(UINT32), colour);
#else
    while (width--) {
        *ptr++ = colour;
    }
#endif    
}

VOID DrawVLine(INT32 x, INT32 y, INT32 height, UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    // clip line
    INT32 y1 = y + height - 1;
    if (x < gCurrRenBuf->ClipX0 || x > gCurrRenBuf->ClipX1 || y1 < gCurrRenBuf->ClipY0 || y > gCurrRenBuf->ClipY1) {
        return;
    }
    if (y < gCurrRenBuf->ClipY0) {
        y = gCurrRenBuf->ClipY0;
        height = (y1 - gCurrRenBuf->ClipY0 + 1);
    }
    if (y1 > gCurrRenBuf->ClipY1) {
        height -= (y1 - gCurrRenBuf->ClipY1);
    }
    // draw line
    UINT32 *ptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
    while (height--) {
        *ptr = colour;
        ptr += gCurrRenBuf->PixPerScnLn;
    }
}

VOID DrawLine(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    if ( !(x0 < gCurrRenBuf->ClipX0 && x1 < gCurrRenBuf->ClipX0) && !(x0 > gCurrRenBuf->ClipX1 && x1 > gCurrRenBuf->ClipX1) ) {
        if ( !(y0 < gCurrRenBuf->ClipY0 && y1 < gCurrRenBuf->ClipY0) && !(y0 > gCurrRenBuf->ClipY1 && y1 > gCurrRenBuf->ClipY1) ) {
            INT32 x[2], y[2];
            x[0] = x0;
            y[0] = y0;
            x[1] = x1;
            y[1] = y1;    
            
            BOOLEAN visible = TRUE;
            UINT32 i = 0;
            while (i<2 && visible) {
                if (y[i] > gCurrRenBuf->ClipY1) { // bottom
                    y[i] = gCurrRenBuf->ClipY1;
                    x[i] = x0 + (x1 - x0) * (gCurrRenBuf->ClipY1 - y0) / (y1 - y0);
                    if (x[i] < gCurrRenBuf->ClipX0 || x[i] > gCurrRenBuf->ClipX1) {
                        visible = FALSE;
                    }
                } else if (y[i] < gCurrRenBuf->ClipY0) { // top
                    y[i] = gCurrRenBuf->ClipY0;
                    x[i] = x0 + (x1 - x0) * (gCurrRenBuf->ClipY0 - y0) / (y1 - y0);
                    if (x[i] < gCurrRenBuf->ClipX0 || x[i] > gCurrRenBuf->ClipX1) {
                        visible = FALSE;
                    }
                } else if (x[i] > gCurrRenBuf->ClipX1) { // right
                    x[i] = gCurrRenBuf->ClipX1;
                    y[i] = y0 + (y1 - y0) * (gCurrRenBuf->ClipX1 - x0) / (x1 - x0);
                    if (y[i] < gCurrRenBuf->ClipY0 || y[i] > gCurrRenBuf->ClipY1) {
                        visible = FALSE;
                    }
                }  else if (x[i] < gCurrRenBuf->ClipX0) { // left
                    x[i] = gCurrRenBuf->ClipX0;
                    y[i] = y0 + (y1 - y0) * (gCurrRenBuf->ClipX0 - x0) / (x1 - x0);
                    if (y[i] < gCurrRenBuf->ClipY0 || y[i] > gCurrRenBuf->ClipY1) {
                        visible = FALSE;
                    }
                }
                i++;
            }
            if (visible) {
                draw_line(x[0], y[0], x[1], y[1], colour);
            }
        }
    }
}

STATIC VOID draw_line(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour)
{
    INT32 dx = ABS(x1 - x0);
    INT32 sx = x0 < x1 ? 1 : -1;
    INT32 dy = ABS(y1 - y0);
    INT32 sy = y0 < y1 ? 1 : -1;
    INT32 err = (dx > dy ? dx : -dy) / 2;
       
    UINT32 *ptr = gCurrRenBuf->PixelData + x0 + (y0 * gCurrRenBuf->PixPerScnLn);

    while (1) {
        *ptr = colour;

        if (x0 == x1 && y0 == y1) {
            break;
        };

        INT32 e2 = err + err;

        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
            ptr += sx;
        }

        if (e2 < dy) {
            err += dx;
            y0 += sy;
            ptr += (sy * (INT32)gCurrRenBuf->PixPerScnLn);
        }
    }
}

VOID DrawRectangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour)
{
    if (!Initialised) {
        return;    
    }
    // determine top-left and bottom-right
    INT32 xl, xr, yt, yb;
    if (x0 < x1) {
        xl = x0;
        xr = x1;
    } else {
        xl = x1;
        xr = x0;
    }
    if (y0 < y1) {
        yt = y0;
        yb = y1;
    } else {
        yt = y1;
        yb = y0;
    }
    // clip rectamgle
    if (yb < gCurrRenBuf->ClipY0 || yt > gCurrRenBuf->ClipY1 || xr < gCurrRenBuf->ClipX0 || xl > gCurrRenBuf->ClipX1) {
        return;
    }
    INT32 width = xr - xl + 1;
    INT32 height = yb - yt + 1;
    BOOLEAN left=TRUE, right=TRUE, top=TRUE, bottom=TRUE;
    if (xl < gCurrRenBuf->ClipX0) {
        left = FALSE;
        xl = gCurrRenBuf->ClipX0;
        width = (xr - gCurrRenBuf->ClipX0 + 1);
    }
    if (xr > gCurrRenBuf->ClipX1) {
        right = FALSE;
        width -= (xr - gCurrRenBuf->ClipX1);
    }
    if (yt < gCurrRenBuf->ClipY0) {
        top = FALSE;
        yt = gCurrRenBuf->ClipY0;
        height = (yb - gCurrRenBuf->ClipY0 + 1);
    }
    if (yb > gCurrRenBuf->ClipY1) {
        bottom = FALSE;
        height -= (yb - gCurrRenBuf->ClipY1);
    }
    // draw rectangle
    UINT32 *ptr = gCurrRenBuf->PixelData + xl + (yt * gCurrRenBuf->PixPerScnLn);
    // top line
    if (top) {
#ifdef EDK2_MEM_FUNC
        SetMem32(ptr, width * sizeof(UINT32), colour);
        ptr += (width - 1);
#else
        INT32 w = width;
        while (TRUE) {
            *ptr = colour;
            w--;
            if (!w) break;
            ptr++;
        }
#endif        
    } else {
        ptr += (width - 1);
    }
    // right line
    if (right) {
        INT32 h = height;
        while (TRUE) {
            *ptr = colour;
            h--;
            if (!h) break;
            ptr += gCurrRenBuf->PixPerScnLn;
        }
    } else {
        ptr += (gCurrRenBuf->PixPerScnLn * (height - 1));
    }
    // bottom line 
    if (bottom) {
#ifdef EDK2_MEM_FUNC
        ptr -= (width - 1);
        SetMem32(ptr, width * sizeof(UINT32), colour);
#else
        INT32 w = width;
        while (TRUE) {
            *ptr = colour;
            w--;
            if (!w) break;
            ptr--;
        }
#endif
    } else {
        ptr -= (width - 1);
    }
    // left line
    if (left) {
        INT32 h = height;
        while (TRUE) {
            *ptr = colour;
            h--;
            if (!h) break;
            ptr -= gCurrRenBuf->PixPerScnLn;
        }
    }
}

VOID DrawFillRectangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour)
{
    if (!Initialised) {
        return;    
    }
    // determine top-left and bottom-right
    INT32 xl, xr, yt, yb;
    if (x0 < x1) {
        xl = x0;
        xr = x1;
    } else {
        xl = x1;
        xr = x0;
    }
    if (y0 < y1) {
        yt = y0;
        yb = y1;
    } else {
        yt = y1;
        yb = y0;
    }
    // clip rectamgle
    if (yb < gCurrRenBuf->ClipY0 || yt > gCurrRenBuf->ClipY1 || xr < gCurrRenBuf->ClipX0 || xl > gCurrRenBuf->ClipX1) {
        return;
    }
    UINT32 width = xr - xl + 1;
    UINT32 height = yb - yt + 1;
    if (xl < gCurrRenBuf->ClipX0) {
        xl = gCurrRenBuf->ClipX0;
        width = (xr - gCurrRenBuf->ClipX0 + 1);
    }
    if (xr > gCurrRenBuf->ClipX1) {
        width -= (xr - gCurrRenBuf->ClipX1);
    }
    if (yt < gCurrRenBuf->ClipY0) {
        yt = gCurrRenBuf->ClipY0;
        height = (yb - gCurrRenBuf->ClipY0 + 1);
    }
    if (yb > gCurrRenBuf->ClipY1) {
        height -= (yb - gCurrRenBuf->ClipY1);
    }
    // draw rectangle    
    UINT32 *ptr = gCurrRenBuf->PixelData + xl + (yt * gCurrRenBuf->PixPerScnLn);
    while (height--) {
#ifdef EDK2_MEM_FUNC
    SetMem32(ptr, width * sizeof(UINT32), colour);
    ptr += gCurrRenBuf->PixPerScnLn;
#else
        UINT32 w = width;
        while (w--) {
            *ptr++ = colour;
        }
        ptr += (gCurrRenBuf->PixPerScnLn - width);
#endif    
    }
}

/*
 * DrawCircle()
 */

VOID DrawCircle(INT32 xc, INT32 yc, INT32 r, UINT32 colour)
{
    if (!Initialised) {
        return;    
    }
#if CIRCLE_OPTIMISATION
    if (draw_full_circle(xc, yc, r, colour)) {
        // full circle was drawn
        return;
    }
#endif
    // draw_full_circle() returned FALSE!
    draw_part_circle(xc, yc, r, colour);
}

STATIC VOID draw_part_circle(INT32 xc, INT32 yc, INT32 r, UINT32 colour)
{
    INT32 x = 0;
    INT32 y = r;
    INT32 d = 3 - 2 * r;

    PutPixel(xc + x, yc + y, colour);
    PutPixel(xc - x, yc + y, colour);
    PutPixel(xc + x, yc - y, colour);
    PutPixel(xc - x, yc - y, colour);
    PutPixel(xc + y, yc + x, colour);
    PutPixel(xc - y, yc + x, colour);
    PutPixel(xc + y, yc - x, colour);
    PutPixel(xc - y, yc - x, colour);

    while (y >= x) {
        x++;

        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }

        PutPixel(xc + x, yc + y, colour);
        PutPixel(xc - x, yc + y, colour);
        PutPixel(xc + x, yc - y, colour);
        PutPixel(xc - x, yc - y, colour);
        PutPixel(xc + y, yc + x, colour);
        PutPixel(xc - y, yc + x, colour);
        PutPixel(xc + y, yc - x, colour);
        PutPixel(xc - y, yc - x, colour);
    }
}

#if CIRCLE_OPTIMISATION
/*
 * draw_full_circle() - return TRUE if circle drawn
 */
STATIC BOOLEAN draw_full_circle(INT32 xc, INT32 yc, INT32 r, UINT32 colour)
{
    INT32 x = 0;
    INT32 y = r;
    INT32 d = 3 - 2 * r;

    if (gCurrRenBuf->ClipX0 + r > xc || gCurrRenBuf->ClipX1 < xc + r || gCurrRenBuf->ClipY0 + r > yc || gCurrRenBuf->ClipY1 < yc + r) {
        // circle is clipped so exit
        return FALSE;
    }

    // (xc + x, yc + y)
    UINT32 *ptr_br = gCurrRenBuf->PixelData + xc + x + ((yc + y) * gCurrRenBuf->PixPerScnLn);
    *ptr_br = colour;
    // (xc - x, yc + y)
    UINT32 *ptr_bl = ptr_br;
    *ptr_br = colour;
    // (xc + x, yc - y)
    UINT32 *ptr_tr = gCurrRenBuf->PixelData + xc + x + ((yc - y) * gCurrRenBuf->PixPerScnLn);
    *ptr_tr = colour;
    // (xc - x, yc - y)
    UINT32 *ptr_tl = ptr_tr;
    *ptr_tl = colour;
    // (xc + y, yc + x)
    UINT32 *ptr_rd = gCurrRenBuf->PixelData + xc + y + ((yc + x) * gCurrRenBuf->PixPerScnLn);
    *ptr_rd = colour;
    // (xc - y, yc + x)
    UINT32 *ptr_ld = gCurrRenBuf->PixelData + xc - y + ((yc + x) * gCurrRenBuf->PixPerScnLn);
    *ptr_ld = colour;
    // (xc + y, yc - x)
    UINT32 *ptr_ru = ptr_rd;
    *ptr_ru = colour;
    // (xc - y, yc - x)
    UINT32 *ptr_lu = ptr_ld;
    *ptr_ru = colour;
    
    while (y >= x) {
        x++;
        ptr_br++;
        ptr_bl--;
        ptr_tr++;
        ptr_tl--;
        ptr_rd += gCurrRenBuf->PixPerScnLn;
        ptr_ld += gCurrRenBuf->PixPerScnLn;
        ptr_ru -= gCurrRenBuf->PixPerScnLn;
        ptr_lu -= gCurrRenBuf->PixPerScnLn;

        if (d > 0) {
            y--;
            ptr_br -= gCurrRenBuf->PixPerScnLn;
            ptr_bl -= gCurrRenBuf->PixPerScnLn;
            ptr_tr += gCurrRenBuf->PixPerScnLn;
            ptr_tl += gCurrRenBuf->PixPerScnLn;
            ptr_rd--;
            ptr_ld++;
            ptr_ru--;
            ptr_lu++;

            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
        
        *ptr_br = colour;   // (xc + x, yc + y)
        *ptr_bl = colour;   // (xc - x, yc + y)
        *ptr_tr = colour;   // (xc + x, yc - y)
        *ptr_tl = colour;   // (xc - x, yc - y)
        *ptr_rd = colour;   // (xc + y, yc + x)
        *ptr_ld = colour;   // (xc - y, yc + x)
        *ptr_ru = colour;   // (xc + y, yc - x)
        *ptr_lu = colour;   // (xc - y, yc - x)
    }
    return TRUE;
}
#endif

VOID DrawFillCircle(INT32 xc, INT32 yc, INT32 r, UINT32 colour)
{
    INT32 x = 0;
    INT32 y = r;
    INT32 d = 3 - 2 * r;

    while (y >= x) {
        DrawHLine(xc - x, yc + y, x * 2 + 1, colour); // bottom
        DrawHLine(xc - x, yc - y, x * 2 + 1, colour); // top
        DrawHLine(xc - y, yc + x, y * 2 + 1, colour); // mid-bottom
        DrawHLine(xc - y, yc - x, y * 2 + 1, colour); // mid-top
        x++;

        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

#if FONT_SUPPORT

#define STRING_SIZE 256

UINTN EFIAPI GPrint(TEXT_CONFIG *TxtCfg, CHAR16 *sFormat, ...)
{
    CHAR16 String[STRING_SIZE];
    VA_LIST vl;
    VA_START(vl, sFormat);
    UINTN Length = UnicodeVSPrint(String, STRING_SIZE, sFormat, vl);
    VA_END(vl);
    put_string(&gFrameBuffer, TxtCfg ? TxtCfg : &gFBTxtCfg, String);
    return Length;
}

EFI_STATUS GPutString(INT32 x, INT32 y, UINT16 *string, UINT32 FgColour, UINT32 BgColour, BOOLEAN BgColourEnabled, FONT Font)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    TEXT_CONFIG TxtCfg = {
        .X0 = gFrameBuffer.ClipX0,
        .Y0 = gFrameBuffer.ClipY0,
        .X1 = gFrameBuffer.ClipX1,
        .Y1 = gFrameBuffer.ClipY1,
        .Font = Font,
        .CurrX = x,
        .CurrY = y,  
        .FgColour = FgColour,
        .BgColour = BgColour,
        .BgColourEnabled = BgColourEnabled,
        .LineWrapEnabled = FALSE,
        .ScrollEnabled = FALSE
    };
    DbgPrint(DL_INFO, "x=%d, y=%d, X0=%d, Y0=%d, X1=%d, Y1=%d\n", x, y, TxtCfg.X0, TxtCfg.Y0, TxtCfg.X1, TxtCfg.Y1);
    // Currently output to frame buffer so we can use blt to scroll 
    return put_string(&gFrameBuffer, &TxtCfg, string);
}

STATIC EFI_STATUS put_string(RENDER_BUFFER *RenBuf, TEXT_CONFIG *TxtCfg, UINT16 *string)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    INT32 x = TxtCfg->CurrX;
    INT32 y = TxtCfg->CurrY;

    UINT32 HorRes = TxtCfg->X1 - TxtCfg->X0 + 1;
    UINT32 VerRes = TxtCfg->Y1 - TxtCfg->Y0 + 1;

    UINTN FontWidth = GetFontWidth(TxtCfg->Font);
    UINTN FontHeight = GetFontHeight(TxtCfg->Font);

    UINTN numChars = StrLen(string);
    if (y < TxtCfg->Y0) {
        return EFI_NOT_READY; // off top 
    }
    if (y + FontHeight - 1 > TxtCfg->Y1) {
        return EFI_NOT_READY; // off bottom 
    }
    if (x+(FontWidth * numChars) - 1 < TxtCfg->X0) {
        return EFI_NOT_READY; // off left
    }
    if ( (x > TxtCfg->X1) && !TxtCfg->LineWrapEnabled) {
        return EFI_NOT_READY; // off right and line wrap not enabled
    }
    UINTN i = 0;
    if (x < TxtCfg->X0) {
        // determine first visible character and position
        i = (TxtCfg->X0 - x) / FontWidth;
        x += (i * FontWidth);
    }

    UINT32 *char_rbptr = RenBuf->PixelData + x + (y * RenBuf->PixPerScnLn);

    while (TRUE) {
        // get character to display
        UINT16 code = string[i];

        // end of string
        if (code == L'\0') {
            break;
        }

        BOOLEAN DoLineWrap = FALSE;

        // char off right of screen
        if ( (x + FontWidth > TxtCfg->X1) && TxtCfg->LineWrapEnabled) {
            DoLineWrap = TRUE;
        }

        // carriage return
        if (code == L'\r' || DoLineWrap) {
            // move to the beginning of the line without advancing to the next line
            x = TxtCfg->X0;
            char_rbptr = RenBuf->PixelData + x + (y * RenBuf->PixPerScnLn);
        }

        // line feed
        if (code == L'\n' || DoLineWrap) {
            // move down to the next line without returning to the beginning of the line
            if ( (y + 2*FontHeight-1 > TxtCfg->Y1) && TxtCfg->ScrollEnabled) {
                // scroll screen if line is below bottom
                INT32 diff = y + 2*FontHeight-1 - TxtCfg->Y1;
                y = TxtCfg->Y0 + VerRes - FontHeight;
                // scroll
                EFI_STATUS Status = gGop->Blt(gGop,
                                                (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)NULL, 
                                                EfiBltVideoToVideo, 
                                                TxtCfg->X0, TxtCfg->Y0 + diff, 
                                                TxtCfg->X0, TxtCfg->Y0, 
                                                HorRes, VerRes - diff, 0);                
                if (EFI_ERROR(Status)) {
                    DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
                }
                // blank scrolled area
                UINT32 colour = TxtCfg->BgColour;
                Status = gGop->Blt(gGop, 
                                        (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, 
                                        EfiBltVideoFill, 
                                        0, 0, 
                                        TxtCfg->X0, TxtCfg->Y0 + VerRes - FontHeight, 
                                        HorRes, FontHeight, 0);                
                if (EFI_ERROR(Status)) {
                    DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
                }
                char_rbptr += ((FontHeight - diff) * RenBuf->PixPerScnLn);
            } else {
                y += FontHeight;
                char_rbptr += (FontHeight * RenBuf->PixPerScnLn);
            }
        }

        // printable character
        if (code != L'\r' && code != L'\n') {
            CONST UINT8 *CharData = GetCharBitmap(TxtCfg->Font, code);
            if ( CharData && (x >= TxtCfg->X0) && (x + FontWidth - 1 <= TxtCfg->X1) ){
                // char on screen
                UINT32 *lh_rbptr = char_rbptr;
                for (UINTN h = 0; h < FontHeight; h++) {
                    UINT32 *rbptr = lh_rbptr;
                    UINT8 Data = 0;
                    for (UINTN w = 0; w < FontWidth; w++) {
                        if (w % 8 == 0) {
                            Data = *(CharData++);
                        }
                        if (Data & 0x80) {
                            *(rbptr++) = TxtCfg->FgColour;
                        } else if (TxtCfg->BgColourEnabled) {
                            *(rbptr++) = TxtCfg->BgColour;
                        } else {
                            rbptr++;
                        }  
                        Data <<= 1;
                    }
                    lh_rbptr += RenBuf->PixPerScnLn;
                }
                char_rbptr += FontWidth;
                x += FontWidth;
            }
        }            

        // next character
        i++;
    }
    TxtCfg->CurrX = x;
    TxtCfg->CurrY = y;
    return EFI_SUCCESS;
}

VOID EnableTextBackground(BOOLEAN State)
{
    EnableTextBoxBackground(&gFBTxtCfg, State);
}

VOID EnableTextBoxBackground(TEXT_CONFIG *TxtCfg, BOOLEAN State)
{
    TxtCfg->BgColourEnabled = State;
}

VOID SetTextForeground(UINT32 colour)
{
    SetTextBoxForeground(&gFBTxtCfg, colour);
}

VOID SetTextBoxForeground(TEXT_CONFIG *TxtCfg, UINT32 colour)
{
    TxtCfg->FgColour = colour;
}

VOID SetTextBackground(UINT32 colour)
{
    SetTextBoxBackground(&gFBTxtCfg, colour);
}

VOID SetTextBoxBackground(TEXT_CONFIG *TxtCfg, UINT32 colour)
{
    TxtCfg->BgColour = colour;
}

VOID SetFont(FONT font)
{
    SetTextBoxFont(&gFBTxtCfg, font);
}

VOID SetTextBoxFont(TEXT_CONFIG *TxtCfg, FONT font)
{
    TxtCfg->Font = font;
}

VOID CreateTextBox(TEXT_CONFIG *TxtCfg, INT32 x, INT32 y, INT32 Width, INT32 Height, UINT32 FgColour, UINT32 BgColour, FONT Font)
{
    RENDER_BUFFER *RenBuf = &gFrameBuffer;

    if (x < 0) x = 0;
    if (x > RenBuf->HorRes - 1) x = RenBuf->HorRes - 1;
    if (x + Width > RenBuf->HorRes) Width = RenBuf->HorRes - x;
    if (y < 0) y = 0;
    if (y > RenBuf->VerRes - 1) y = RenBuf->VerRes - 1;
    if (y + Height > RenBuf->VerRes) Height = RenBuf->VerRes - y;

    TxtCfg->X0 = x;
    TxtCfg->Y0 = y;
    TxtCfg->X1 = x + Width - 1;
    TxtCfg->Y1 = y + Height - 1;;

    TxtCfg->Font = Font;;
    TxtCfg->CurrX = TxtCfg->X0;
    TxtCfg->CurrY = TxtCfg->Y0;
    TxtCfg->FgColour = FgColour;
    TxtCfg->BgColour = BgColour;
    TxtCfg->BgColourEnabled = TRUE;
    TxtCfg->LineWrapEnabled = TRUE;
    TxtCfg->ScrollEnabled = TRUE;
}

VOID ClearTextBox(TEXT_CONFIG *TxtCfg)
{
    UINT32 HorRes = TxtCfg->X1 - TxtCfg->X0 + 1;
    UINT32 VerRes = TxtCfg->Y1 - TxtCfg->Y0 + 1;
    UINT32 colour = TxtCfg->BgColour;
    EFI_STATUS Status = gGop->Blt(gGop, 
                            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, 
                            EfiBltVideoFill, 
                            0, 0, 
                            TxtCfg->X0, TxtCfg->Y0, 
                            HorRes, VerRes, 0);                
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
    }
}

#endif // FONT_SUPPORT

//-------------------------------------------------------------------------
// WIP
//-------------------------------------------------------------------------

VOID WipBltClearScreen(UINT32 colour)
{
    if (!Initialised) {
        return;
    }
#if 0    
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Background = {
        .Blue = colour & 0xFF,
        .Green = (colour & 0xFF00) >> 8,
        .Red = (colour & 0xFF0000) >> 16
    };
    Print(L"R=%u, G=%u, B=%u\n", Background.Red, Background.Green, Background.Blue);
#endif    
    gGop->Blt (gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, EfiBltVideoFill, 0, 0, 0, 0, gCurrRenBuf->HorRes, gCurrRenBuf->VerRes, 0);
}

VOID WipSetMem64ClearScreen(UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    UINT32 *ptr = gCurrRenBuf->PixelData;
    UINT64 colour64 = ((UINT64)colour << 32) | colour;
    if (gCurrRenBuf->HorRes == gCurrRenBuf->PixPerScnLn) {
        SetMem64(ptr, gCurrRenBuf->PixPerScnLn * gCurrRenBuf->HorRes * sizeof(UINT32), colour64);
    } else {
        UINT32 height = gCurrRenBuf->VerRes;
        UINT32 wbytes = gCurrRenBuf->HorRes * sizeof(UINT32);
        while (height--) {
            SetMem64(ptr, wbytes, colour64);
            ptr += gCurrRenBuf->PixPerScnLn;
        }
    }
}

VOID WipClearScreenBlack(VOID)
{
    if (!Initialised) {
        return;
    }
    ZeroMem(gCurrRenBuf->PixelData, gCurrRenBuf->PixPerScnLn * gCurrRenBuf->HorRes * sizeof(UINT32));
}



//-------------------------------------------------------------------------
// DEBUG
//-------------------------------------------------------------------------

#if 0
// check to see if ptr does actual map to supplied co-ordinates!
STATIC VOID CheckPtr(UINT32 *ptr, INT32 x, INT32 y)
{
    UINT32 *ptr2 = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
    if (ptr != ptr2) {
        INT32 y2 = (ptr - gCurrRenBuf->PixelData) / gCurrRenBuf->PixPerScnLn;
        INT32 x2 = (ptr - gCurrRenBuf->PixelData) % gCurrRenBuf->PixPerScnLn;
        Print(L"Mismatch: Passed(%d, %d) Actual(%d, %d)\n", x, y, x2, y2);
    }
}
#endif
