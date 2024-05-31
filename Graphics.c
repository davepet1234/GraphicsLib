/*
 * File:    Graphics.c
 * 
 * Author:  David Petrovic
 * 
 * Description:
 * 
*/

#define DEBUG_SUPPORT 0
#define DEVELOPMENT_MODE 0

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h> //###
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include "Graphics.h"

#if EDK2SIM_SUPPORT
#include <Edk2Sim.h>
#else
#define EDK2SIM_GFX_BEGIN
#define EDK2SIM_GFX_END
#endif

#if FONT_SUPPORT
#include "Font.h"
#endif

#if DEBUG_SUPPORT
#include "DebugPrint.h"
#else
#define DbgPrint(...)
#endif

// When defined uses a different function to draw (full) circles that are not clipped
#define CIRCLE_OPTIMISATION 1

#if DEVELOPMENT_MODE
#define DEV_WINDOW_WIDTH 200
TEXT_CONFIG gDebugTxtCfg = {0};
// Usage: DEVPRINT((&gDebugTxtCfg, L"Message\n"));
#define DEVPRINT(x) GPrint x
#else
#define DEVPRINT(x)
#endif

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
STATIC EFI_STATUS put_string(TEXT_CONFIG *TxtCfg, UINT16 *string);
#endif

STATIC BOOLEAN Initialised = FALSE;
#define RENBUF_SIG 0x52425546UL   // "RBUF"

// globals
STATIC UINT32                           gOrigGfxMode = 0;
STATIC UINTN                            gOrigTxtMode = 0;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL     *gGop = NULL;
STATIC UINT32                           gCurrMode = 0;
STATIC RENDER_BUFFER                    gFrameBuffer = {0};
STATIC RENDER_BUFFER                    *gCurrRenBuf = NULL;
STATIC BOOLEAN                          gRenderToScreen = TRUE;
#if FONT_SUPPORT
STATIC TEXT_CONFIG                      gFBTxtCfg = { 0 };
#endif

// macros
#define SWAP(T, x, y) \
    {                 \
        T tmp = x;    \
        x = y;        \
        y = tmp;      \
    }



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

#if DEVELOPMENT_MODE
    SetClipping(0, 0, GetHorRes() - DEV_WINDOW_WIDTH - 1, GetVerRes() - 1);
    CreateTextBox(&gDebugTxtCfg, GetHorRes() - DEV_WINDOW_WIDTH, 0, DEV_WINDOW_WIDTH, GetVerRes(), WHITE, BLUE, FONT7x14);
    ClearTextBox(&gDebugTxtCfg);
#endif

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
    gRenderToScreen = TRUE;
#if FONT_SUPPORT
    // Text Config
    gFBTxtCfg.RenBuf = &gFrameBuffer;
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

EFI_STATUS SetGraphicsMode(UINT32 Mode)
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

EFI_STATUS GetGraphicsMode(UINT32 *Mode)
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
    for (UINT32 i = 0; i < gGop->Mode->MaxMode; ++i) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
        UINTN SizeOfInfo;
        EFI_STATUS Status = gGop->QueryMode(gGop, i, &SizeOfInfo, &Info);
        if (EFI_ERROR(Status)){
            Print(L"ERROR: Failed mode query on %d: 0x%x\n", i, Status);
            return Status;
        }
        BOOLEAN Match = (Info->HorizontalResolution == HorRes && Info->VerticalResolution == VerRes) ? TRUE : FALSE;
        FreePool(Info);
        if (Match) {
            return SetGraphicsMode(i);
        }
    }

    return EFI_UNSUPPORTED;
}

UINT32 NumGraphicsModes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gGop->Mode->MaxMode;
}

EFI_STATUS QueryGraphicsMode(UINT32 Mode, UINT32 *HorRes, UINT32 *VerRes)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    EFI_STATUS Status = gGop->QueryMode(gGop, Mode, &SizeOfInfo, &Info);
    if (EFI_ERROR(Status)){
        return Status;
    }
    if (HorRes) {
        *HorRes = Info->HorizontalResolution;
    }
    if (VerRes) {
        *VerRes = Info->VerticalResolution;
    }
    FreePool(Info);
    return EFI_SUCCESS;
}

INT32 GetFBHorRes(VOID)
{
    if (!Initialised) {
        return 0;
    }
    return gFrameBuffer.HorRes;
}

INT32 GetFBVerRes(VOID)
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
        gRenderToScreen = TRUE;
    }
    ZeroMem(RenBuf, sizeof(RENDER_BUFFER));
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
    gRenderToScreen = FALSE;
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
    gRenderToScreen = TRUE;
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
    } else if (RenBuf->ClipX0 >= (INT32)RenBuf->HorRes) {
        RenBuf->ClipX0 = RenBuf->HorRes - 1;
    }
    if (RenBuf->ClipX1 < 0) {
        RenBuf->ClipX1 = 0;
    } else if (RenBuf->ClipX1 >= (INT32)RenBuf->HorRes) {
        RenBuf->ClipX1 = RenBuf->HorRes - 1;
    }
    if (RenBuf->ClipY0 < 0) {
        RenBuf->ClipY0 = 0;
    } else if (RenBuf->ClipY0 >= (INT32)RenBuf->VerRes) {
        RenBuf->ClipY0 = RenBuf->VerRes - 1;
    }
    if (RenBuf->ClipY1 < 0) {
        RenBuf->ClipY1 = 0;
    } else if (RenBuf->ClipY1 >= (INT32)RenBuf->VerRes) {
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
    if (gRenderToScreen) {
        gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, EfiBltVideoFill, 0, 0, 0, 0, gCurrRenBuf->HorRes, gCurrRenBuf->VerRes, 0);
    } else {
        UINT32 *ptr = gCurrRenBuf->PixelData;
        if (gCurrRenBuf->HorRes == gCurrRenBuf->PixPerScnLn) {
            SetMem32(ptr, gCurrRenBuf->PixPerScnLn * gCurrRenBuf->HorRes * sizeof(UINT32), colour);
        } else {
            UINT32 height = gCurrRenBuf->VerRes;
            UINT32 wbytes = gCurrRenBuf->HorRes * sizeof(UINT32);
            while (height--) {
                SetMem32(ptr, wbytes, colour);
                ptr += gCurrRenBuf->PixPerScnLn;
            }
        }
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
    UINT32 height = gCurrRenBuf->ClipY1 - gCurrRenBuf->ClipY0 +1;
    UINT32 wbytes = (gCurrRenBuf->ClipX1 - gCurrRenBuf->ClipX0 + 1) * sizeof(UINT32);
    while (height--) {
        SetMem32(ptr, wbytes, colour);
        ptr += gCurrRenBuf->PixPerScnLn;
    }
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

    EDK2SIM_GFX_BEGIN;
    *ptr = colour;
    EDK2SIM_GFX_END;

}

UINT32 GetPixel(INT32 x, INT32 y)
{
    if (!Initialised) {
        return 0;
    }
    // clip pixel
    if (x < gCurrRenBuf->ClipX0 || x > gCurrRenBuf->ClipX1 || y < gCurrRenBuf->ClipY0 || y > gCurrRenBuf->ClipY1) {
        return 0;
    }
    // draw pixel
    EDK2SIM_GFX_BEGIN;
    UINT32 *ptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
    EDK2SIM_GFX_END;
    return *ptr;
}

STATIC VOID draw_hline(INT32 x, INT32 y, INT32 width, UINT32 colour)
{
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
    UINT32* ptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
    SetMem32(ptr, width * sizeof(UINT32), colour);
}


VOID DrawHLine(INT32 x, INT32 y, INT32 width, UINT32 colour)
{
    if (!Initialised){
        return;
    }
    EDK2SIM_GFX_BEGIN;
    draw_hline(x, y, width, colour);
    EDK2SIM_GFX_END;
}

VOID DrawHLine2(INT32 x0, INT32 x1, INT32 y, UINT32 colour)
{
    if (x1 < x0) {
        SWAP(INT32, x0, x1);
    }
    DrawHLine(x0, y, ABS(x1-x0)+1, colour);
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
    EDK2SIM_GFX_BEGIN;
    while (height--) {
        *ptr = colour;
        ptr += gCurrRenBuf->PixPerScnLn;
    }
    EDK2SIM_GFX_END;
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
    INT32 dy = -ABS(y1 - y0);
    INT32 sy = y0 < y1 ? 1 : -1;
    INT32 error = dx + dy;

    UINT32 *ptr = gCurrRenBuf->PixelData + x0 + (y0 * gCurrRenBuf->PixPerScnLn);

    EDK2SIM_GFX_BEGIN;
    while (TRUE) {

        *ptr = colour;

        if (x0 == x1 && y0 == y1) break;

        INT32 e2 = 2 * error;

        if (e2 >= dy) {
            if (x0 == x1) break;
            error += dy;
            x0 += sx;
            ptr += sx;
        }

        if (e2 <= dx) {
            if (y0 == y1) break;
            error += dx;
            y0 += sy;
            ptr += (sy * (INT32)gCurrRenBuf->PixPerScnLn);
        }
    }
    EDK2SIM_GFX_END;
}

VOID DrawTriangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, INT32 x2, INT32 y2, UINT32 colour)
{
    if (!Initialised) {
        return;    
    }
    // sort the vertices, y0 < y1 < y2
    // same order as for a filled triangle
    if (y0 > y1) {
        SWAP(INT32, x0, x1);
        SWAP(INT32, y0, y1);
    }
    if (y0 > y2) {
        SWAP(INT32, x0, x2);
        SWAP(INT32, y0, y2);
    }
    if (y1 > y2) {
        SWAP(INT32, x1, x2);
        SWAP(INT32, y1, y2);
    }
    DrawLine(x0, y0, x1, y1, colour);
    DrawLine(x1, y1, x2, y2, colour);
    DrawLine(x2, y2, x0, y0, colour);
}

// Fill a triangle - Bresenham method
// TODO: full clipping before rendering
VOID DrawFillTriangle(INT32 x0, INT32 y0, INT32 x1, INT32 y1, INT32 x2, INT32 y2, UINT32 colour)
{
    if (!Initialised) {
        return;    
    }
    // sort the vertices, y0 < y1 < y2
    if (y0 > y1) {
        SWAP(INT32, x0, x1);
        SWAP(INT32, y0, y1);
    }
    if (y0 > y2) {
        SWAP(INT32, x0, x2);
        SWAP(INT32, y0, y2);
    }
    if (y1 > y2) {
        SWAP(INT32, x1, x2);
        SWAP(INT32, y1, y2);
    }
    // clip y
    if (y2 < gCurrRenBuf->ClipY0 || y0 > gCurrRenBuf->ClipY1) {
        return;
    }

    // A [x0,y0] -> [x2,y2]
    INT32 Ax = x0;
    INT32 Ay = y0;
    INT32 Adx = ABS(x2 - x0);
    INT32 Asx = x0 < x2 ? 1 : -1;
    INT32 Ady = -ABS(y2 - y0);
    INT32 Asy = y0 < y2 ? 1 : -1;
    INT32 Aerror = Adx + Ady;

    INT32 Bx, By, Btargetx, Btargety, Bdx, Bsx, Bdy, Bsy, Berror;
    if (y0 != y1) {
        // B [x0,y0] -> [x1,y1]
        Bx = x0;
        By = y0;
        Btargetx = x1;
        Btargety = y1;
        Bdx = ABS(Btargetx - Bx);
        Bsx = Bx < Btargetx ? 1 : -1;
        Bdy = -ABS(Btargety - By);
        Bsy = By < Btargety ? 1 : -1;
        Berror = Bdx + Bdy;
    } else {
        // flat top
        // B [x1,y1] -> [x2,y2]
        Bx = x1;
        By = y1;
        Btargetx = x2;
        Btargety = y2;
        Bdx = ABS(Btargetx - Bx);
        Bsx = Bx < Btargetx ? 1 : -1;
        Bdy = -ABS(Btargety - By);
        Bsy = By < Btargety ? 1 : -1;
        Berror = Bdx + Bdy;
    }

    BOOLEAN Acomplete = FALSE;
    BOOLEAN Bcomplete = FALSE;

    EDK2SIM_GFX_BEGIN;
    while (!Acomplete && !Bcomplete) {

        INT32 currentY = Ay; // current horizontial line y-ord

        if (currentY > gCurrRenBuf->ClipY1) break; // clipped

        // Segment A
        INT32 Aminx = Ax;
        INT32 Amaxx = Ax;
        BOOLEAN Aychange = FALSE;
        BOOLEAN Axchange = FALSE;
        while (!Aychange) {			
            if (Axchange) {	// if x-ord change then update x-ord min/max
                if (Ax > Amaxx) {
                    Amaxx = Ax;
                } else {
                    Aminx = Ax;
                }
            }
            // [x,y] point on A line
            if (Ax == x2 && Ay == y2) {	// check if reached end
                Acomplete = TRUE;
                break;
            }
            INT32 Ae2 = 2 * Aerror;
            if (Ae2 >= Ady) {
                // update x-ord				
                if (Ax == x2) {	// check if reached end
                    Acomplete = TRUE;
                    break;
                }
                Aerror += Ady;
                Ax += Asx;
                Axchange = TRUE;
            }
            if (Ae2 <= Adx) {
                // update y-ord
                if (Ay == y2) {  // check if reached end
                    Acomplete = TRUE;
                    break;
                }
                Aerror += Adx;
                Ay += Asy;
                Aychange = TRUE;
            }
        }

        // Segment B (2 parts)
        INT32 Bminx = Bx;
        INT32 Bmaxx = Bx;
        BOOLEAN Bychange = FALSE;
        BOOLEAN Bxchange = FALSE;
        while (!Bychange) {
            if (Bxchange) {		// if x-ord change then update x-ord min/max
                if (Bx > Bmaxx) {
                    Bmaxx = Bx;
                }
                else {
                    Bminx = Bx;
                }
            }
            // [x,y] point on B line
            if (Bx == Btargetx && By == Btargety) {	// check if reached end
                Bcomplete = TRUE;
                break;
            }
            INT32 Be2 = 2 * Berror;
            if (Be2 >= Bdy) {
                // update x-ord
                if (Bx == Btargetx) {	// check if reached end
                    Bcomplete = TRUE;
                    break;
                }
                Berror += Bdy;
                Bx += Bsx;
                Bxchange = TRUE;
            }
            if (Be2 <= Bdx) {
                // update y-ord				
                if (By == Btargety) {	// check if reached end
                    Bcomplete = TRUE;
                    break;
                }
                Berror += Bdx;
                By += Bsy;
                if (By == Ay) {			// matched y-ord on A line
                    Bychange = TRUE;
                } else {
                    // B y-ord changed but not matching A y-ord so reset x-ord min/mix and go round again
                    Bminx = Bx;
                    Bmaxx = Bx;
                }
            }
        }
        // draw horizontial line A to B
        {
            INT32 xl = (Aminx < Bminx) ? Aminx : Bminx;
            INT32 xr = (Amaxx > Bmaxx) ? Amaxx : Bmaxx;
            INT32 y = currentY;
            DEVPRINT((&gDebugTxtCfg, L"[%d,%d],%d\n", xl, xr, y));
            if (y < gCurrRenBuf->ClipY0 || y > gCurrRenBuf->ClipY1 || xr < gCurrRenBuf->ClipX0 || xl > gCurrRenBuf->ClipX1) {
                goto skip;  // off screen
            }
            if (xl < gCurrRenBuf->ClipX0) {
                xl = gCurrRenBuf->ClipX0;
            }
            if (xr > gCurrRenBuf->ClipX1) {
                xr = gCurrRenBuf->ClipX1;
            }
            INT32 width = xr - xl + 1;
            // draw line
            UINT32 *ptr = gCurrRenBuf->PixelData + xl + (y * gCurrRenBuf->PixPerScnLn);
            SetMem32(ptr, width * sizeof(UINT32), colour);
            skip:;  // ";" as complier requires a statement after label!
        }
        if (!Acomplete && Bcomplete && Bx == x1 && By == y1) {	// switch to next B segment
            // B [x1,y1] -> [x2,y2]
            Bx = x1;
            By = y1;
            Btargetx = x2;
            Btargety = y2;
            Bdx = ABS(Btargetx - Bx);
            Bsx = Bx < Btargetx ? 1 : -1;
            Bdy = -ABS(Btargety - By);
            Bsy = By < Btargety ? 1 : -1;
            Berror = Bdx + Bdy;
            Bcomplete = FALSE;
        }
    }
    EDK2SIM_GFX_END;
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
    EDK2SIM_GFX_BEGIN;
    UINT32 *ptr = gCurrRenBuf->PixelData + xl + (yt * gCurrRenBuf->PixPerScnLn);
    // top line
    if (top) {
        SetMem32(ptr, width * sizeof(UINT32), colour);
        ptr += (width - 1);
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
        ptr -= (width - 1);
        SetMem32(ptr, width * sizeof(UINT32), colour);
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
    EDK2SIM_GFX_END;
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
    if (gRenderToScreen) {
        gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, EfiBltVideoFill, 0, 0, xl, yt, width, height, 0);
    } else {        
        UINT32 *ptr = gCurrRenBuf->PixelData + xl + (yt * gCurrRenBuf->PixPerScnLn);
        while (height--) {
            SetMem32(ptr, width * sizeof(UINT32), colour);
            ptr += gCurrRenBuf->PixPerScnLn;
        }
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

    EDK2SIM_GFX_BEGIN;
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
    EDK2SIM_GFX_END;

    return TRUE;
}
#endif

VOID DrawFillCircle(INT32 xc, INT32 yc, INT32 r, UINT32 colour)
{
    if (!Initialised) {
        return;
    }
    INT32 x = 0;
    INT32 y = r;
    INT32 d = 3 - 2 * r;

    EDK2SIM_GFX_BEGIN;
    while (y >= x) {
        draw_hline(xc - x, yc + y, x * 2 + 1, colour); // bottom
        draw_hline(xc - x, yc - y, x * 2 + 1, colour); // top
        draw_hline(xc - y, yc + x, y * 2 + 1, colour); // mid-bottom
        draw_hline(xc - y, yc - x, y * 2 + 1, colour); // mid-top
        x++;

        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
    EDK2SIM_GFX_END;
}

#if FONT_SUPPORT

#define STRING_SIZE 256

UINTN EFIAPI GPrint(TEXT_CONFIG *TxtCfg, CHAR16 *sFormat, ...)
{
    if (!Initialised) {
        return 0;
    }
    CHAR16 String[STRING_SIZE];
    VA_LIST vl;
    VA_START(vl, sFormat);
    UINTN Length = UnicodeVSPrint(String, STRING_SIZE, sFormat, vl);
    VA_END(vl);
    put_string(TxtCfg ? TxtCfg : &gFBTxtCfg, String);
    return Length;
}

EFI_STATUS GPutString(INT32 x, INT32 y, UINT16 *string, UINT32 FgColour, UINT32 BgColour, BOOLEAN BgColourEnabled, FONT Font)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    TEXT_CONFIG TxtCfg = {
        .RenBuf = gCurrRenBuf,
        .X0 = gCurrRenBuf->ClipX0,
        .Y0 = gCurrRenBuf->ClipY0,
        .X1 = gCurrRenBuf->ClipX1,
        .Y1 = gCurrRenBuf->ClipY1,
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
    return put_string(&TxtCfg, string);
}

STATIC EFI_STATUS put_string(TEXT_CONFIG *TxtCfg, UINT16 *string)
{
    if (!TxtCfg || !string || string[0] == L'\0') {
        return 0;
    }
    if (!TxtCfg->RenBuf) {
        return 0; // no associated render buffer
    }
    if (TxtCfg->RenBuf != gCurrRenBuf) {
        return 0; // current render buffer is not associated with this text box
    }
    INT32 x = TxtCfg->CurrX;
    INT32 y = TxtCfg->CurrY;

    INT32 HorRes = TxtCfg->X1 - TxtCfg->X0 + 1;
    INT32 VerRes = TxtCfg->Y1 - TxtCfg->Y0 + 1;

    INT32 FontWidth = GetFontWidth(TxtCfg->Font);
    INT32 FontHeight = GetFontHeight(TxtCfg->Font);

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
    INT32 i = 0;
    if (x < TxtCfg->X0) {
        // determine first visible character and position
        i = (TxtCfg->X0 - x) / FontWidth;
        x += (i * FontWidth);
    }

    UINT32 *char_rbptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);

    EDK2SIM_GFX_BEGIN;
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
            char_rbptr = gCurrRenBuf->PixelData + x + (y * gCurrRenBuf->PixPerScnLn);
        }

        // line feed
        if (code == L'\n' || DoLineWrap) {
            // move down to the next line without returning to the beginning of the line
            if ( (y + 2*FontHeight-1 > TxtCfg->Y1) && TxtCfg->ScrollEnabled) {
                // scroll screen if line is below bottom
                INT32 diff = y + 2*FontHeight-1 - TxtCfg->Y1;
                y = TxtCfg->Y0 + VerRes - FontHeight;
                // scroll
                if (gRenderToScreen) {
                    EFI_STATUS Status = gGop->Blt(gGop,
                                                    (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)NULL, 
                                                    EfiBltVideoToVideo, 
                                                    TxtCfg->X0, TxtCfg->Y0 + diff, 
                                                    TxtCfg->X0, TxtCfg->Y0, 
                                                    HorRes, VerRes - diff, 0);                
                    if (EFI_ERROR(Status)) {
                        DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
                    }
                } else {
                    UINT32 *dstptr = gCurrRenBuf->PixelData + TxtCfg->X0 + (TxtCfg->Y0 * gCurrRenBuf->PixPerScnLn);
                    UINT32 *srcptr = dstptr + (diff * gCurrRenBuf->PixPerScnLn);
                    UINTN height = VerRes - diff;
                    while (height--) {
                        CopyMem(dstptr, srcptr, HorRes * sizeof(UINT32));
                        dstptr += gCurrRenBuf->PixPerScnLn;
                        srcptr += gCurrRenBuf->PixPerScnLn;
                    }
                }
                // blank scrolled area
                UINT32 colour = TxtCfg->BgColour;
                if (gRenderToScreen) {
                    EFI_STATUS Status = gGop->Blt(gGop, 
                                            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, 
                                            EfiBltVideoFill, 
                                            0, 0, 
                                            TxtCfg->X0, TxtCfg->Y0 + VerRes - FontHeight, 
                                            HorRes, FontHeight, 0);
                    if (EFI_ERROR(Status)) {
                        DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
                    }
                } else {
                    UINT32 *ptr = gCurrRenBuf->PixelData + TxtCfg->X0 + ((TxtCfg->Y0 + VerRes - FontHeight) * gCurrRenBuf->PixPerScnLn);
                    UINTN height = FontHeight;
                    while (height--) {
                        SetMem32(ptr, HorRes * sizeof(UINT32), colour);
                        ptr += gCurrRenBuf->PixPerScnLn;
                    }
                }
                char_rbptr += ((FontHeight - diff) * gCurrRenBuf->PixPerScnLn);
            } else {
                y += FontHeight;
                char_rbptr += (FontHeight * gCurrRenBuf->PixPerScnLn);
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
                    lh_rbptr += gCurrRenBuf->PixPerScnLn;
                }
                char_rbptr += FontWidth;
                x += FontWidth;
            }
        }            

        // next character
        i++;
    }
    EDK2SIM_GFX_END;

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

EFI_STATUS CreateTextBox(TEXT_CONFIG *TxtCfg, INT32 x, INT32 y, INT32 Width, INT32 Height, UINT32 FgColour, UINT32 BgColour, FONT Font)
{
    if (!Initialised) {
        return EFI_NOT_READY;
    }
    if (y + Height - 1 < 0 || y > gCurrRenBuf->VerRes - 1 || x + Width - 1 < 0 || x > gCurrRenBuf->HorRes - 1) {
        ZeroMem(TxtCfg, sizeof(TEXT_CONFIG));
        return EFI_INVALID_PARAMETER;
    }
    TxtCfg->RenBuf = gCurrRenBuf;   
    if (x < 0) x = 0;
    if (x + Width > gCurrRenBuf->HorRes) Width = gCurrRenBuf->HorRes - x;
    if (y < 0) y = 0;
    if (y + Height > gCurrRenBuf->VerRes) Height = gCurrRenBuf->VerRes - y;

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

    return EFI_SUCCESS;
}

VOID ClearTextBox(TEXT_CONFIG *TxtCfg)
{
    if (!Initialised) {
        return;
    }
    if (!TxtCfg->RenBuf) {
        return; // no associated render buffer
    }
    if (TxtCfg->RenBuf != gCurrRenBuf) {
        return; // current render buffer is not associated with this text box
    }
    UINT32 HorRes = TxtCfg->X1 - TxtCfg->X0 + 1;
    UINT32 VerRes = TxtCfg->Y1 - TxtCfg->Y0 + 1;
    UINT32 colour = TxtCfg->BgColour;
    if (gRenderToScreen) {
        EFI_STATUS Status = gGop->Blt(gGop, 
                                (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, 
                                EfiBltVideoFill, 
                                0, 0, 
                                TxtCfg->X0, TxtCfg->Y0, 
                                HorRes, VerRes, 0);                
        if (EFI_ERROR(Status)) {
            DbgPrint(DL_ERROR, "%a(): Blt() returned %a\n", __func__, EFIStatusToStr(Status));
        }
    } else {
        UINT32 *ptr = gCurrRenBuf->PixelData + TxtCfg->X0 + (TxtCfg->Y0 * gCurrRenBuf->PixPerScnLn);
        UINTN height = VerRes;
        while (height--) {
            SetMem32(ptr, HorRes * sizeof(UINT32), colour);
            ptr += gCurrRenBuf->PixPerScnLn;
        }
    }
}

#endif // FONT_SUPPORT

//-------------------------------------------------------------------------
// WIP
//-------------------------------------------------------------------------


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
