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
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include "Graphics.h"
#include "fonts/fonts.h"

#if EDK2SIM_SUPPORT
#include <Edk2Sim.h>
#else
#define EDK2SIM_GFX_BEGIN
#define EDK2SIM_GFX_END
#endif


#if DEBUG_SUPPORT
#include "DebugPrint.h"
#else
#define DbgPrint(...)
#endif

// When defined uses a different function to draw (full) circles that are not clipped
#define CIRCLE_OPTIMISATION 1

#define DEFAULT_FONT        FONT10x20
#define DEFAULT_FG_COLOUR   WHITE
#define DEFAULT_BG_COLOUR   BLACK

// prototypes
STATIC VOID init_globals(VOID);
STATIC VOID init_renbuf(RENDER_BUFFER *RenBuf, INT32 HorRes, INT32 VerRes, INT32 PixPerScnLn, UINT32 *PixelData);
STATIC VOID set_clipping(RENDER_BUFFER *RenBuf, INT32 x0, INT32 y0, INT32 x1, INT32 y1);
STATIC VOID reset_clipping(RENDER_BUFFER *RenBuf);
STATIC BOOLEAN clipped(RENDER_BUFFER *RenBuf);
STATIC VOID draw_line(INT32 x0, INT32 y0, INT32 x1, INT32 y1, UINT32 colour);
STATIC VOID draw_part_circle(INT32 xc, INT32 yc, INT32 r, UINT32 colour);
#if CIRCLE_OPTIMISATION
STATIC BOOLEAN draw_full_circle(INT32 xc, INT32 yc, INT32 r, UINT32 colour);
#endif
#define FONT_WIDTH(FontData) FontData[14]
#define FONT_HEIGHT(FontData) FontData[15]
STATIC CONST UINT8 *get_font_data(FONT Font);
STATIC CONST UINT8 *get_char_bitmap(CONST UINT8 *FontData, UINT16 Code);
STATIC VOID init_text_config(TEXT_CONFIG *TxtCfg, INT32 x, INT32 y, INT32 Width, INT32 Height, UINT32 FgColour, UINT32 BgColour, FONT Font);
STATIC VOID default_text_config(TEXT_CONFIG *TxtCfg, INT32 Width, INT32 Height);
STATIC EFI_STATUS put_string(RENDER_BUFFER *RenBuf, TEXT_CONFIG *TxtCfgOvr, UINT16 *string);

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


// macros
#define SWAP(T, x, y) \
    {                 \
        T tmp = x;    \
        x = y;        \
        y = tmp;      \
    }



EFI_STATUS InitGraphics(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    EFI_STATUS Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void**) &gGop);
    if (EFI_ERROR(Status)){
        DbgPrint(DL_ERROR, "%a(), GOP missing => %a\n", __func__, EFIStatusToStr(Status));
        return Status;
    }
    // remember original graphics mode as takes precidence over that required for text
    gOrigGfxMode = gGop->Mode->Mode;
    gOrigTxtMode = gST->ConOut->Mode->Mode;
    init_globals();
    Initialised = TRUE;

    return Status;
}

STATIC VOID init_globals(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    gCurrMode = gGop->Mode->Mode;
    init_renbuf(&gFrameBuffer, gGop->Mode->Info->HorizontalResolution, gGop->Mode->Info->VerticalResolution, gGop->Mode->Info->PixelsPerScanLine, (UINT32 *)gGop->Mode->FrameBufferBase);
    default_text_config(&gFrameBuffer.TxtCfg, gFrameBuffer.HorRes, gFrameBuffer.VerRes);

    // Default to screen
    gCurrRenBuf = &gFrameBuffer;
    gRenderToScreen = TRUE;
}

EFI_STATUS RestoreConsole(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    // set original graphics mode
    SetGraphicsMode(gOrigGfxMode);
    // set original text mode
    return gST->ConOut->SetMode(gST->ConOut, gOrigTxtMode);
}

EFI_STATUS SetGraphicsMode(UINT32 Mode)
{
    DbgPrint(DL_INFO, "%a(Mode=%u)\n", __func__, Mode);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
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
    DbgPrint(DL_INFO, "%a(Mode=0x%p)\n", __func__, Mode);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
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
    DbgPrint(DL_INFO, "%a(HorRes=%u, VerRes=%u)\n", __func__, HorRes, VerRes);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
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
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return 0;
    }
    return gGop->Mode->MaxMode;
}

EFI_STATUS QueryGraphicsMode(UINT32 Mode, UINT32 *HorRes, UINT32 *VerRes)
{
    DbgPrint(DL_INFO, "%a(Mode=%u, HorRes=0x%p, VerRes=0x%p)\n", __func__, Mode, HorRes, VerRes);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
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
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return 0;
    }
    return gFrameBuffer.HorRes;
}

INT32 GetFBVerRes(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return 0;
    }
    return gFrameBuffer.VerRes;
}

STATIC VOID init_renbuf(RENDER_BUFFER *RenBuf, INT32 HorRes, INT32 VerRes, INT32 PixPerScnLn, UINT32 *PixelData)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p, HorRes=%d, VerRes=%d, PixPerScnLn=%d, PixelData=0x%p)\n", __func__, RenBuf, HorRes, VerRes, PixPerScnLn, PixelData);

    RenBuf->Sig = RENBUF_SIG;
    RenBuf->HorRes = HorRes;
    RenBuf->VerRes = VerRes;
    RenBuf->PixPerScnLn = PixPerScnLn;
    reset_clipping(RenBuf);
    RenBuf->PixelData = PixelData;
}

EFI_STATUS CreateRenderBuffer(RENDER_BUFFER *RenBuf, UINT32 Width, UINT32 Height)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p, Width=%u, Height=%d)\n", __func__, RenBuf, Width, Height);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    if (!RenBuf) {
        DbgPrint(DL_ERROR, "%a(), RenBuf=NULL => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    // allocate buffer
    UINTN Memsize = Width*Height*sizeof(UINT32); // or sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) ?????
    UINT32 *Buff = AllocateZeroPool(Memsize);
    if (Buff == NULL) {
        DbgPrint(DL_ERROR, "%a(), memory allocation error => EFI_OUT_OF_RESOURCES\n", __func__);
        return EFI_OUT_OF_RESOURCES;
    }
    init_renbuf(RenBuf, Width, Height, Width, Buff);
    default_text_config(&RenBuf->TxtCfg, Width, Height);

    return EFI_SUCCESS;
}

EFI_STATUS DestroyRenderBuffer(RENDER_BUFFER *RenBuf)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p)\n", __func__, RenBuf);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    if (!RenBuf) {
        DbgPrint(DL_ERROR, "%a(), RenBuf=NULL => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    if (RenBuf->Sig != RENBUF_SIG) {
        DbgPrint(DL_ERROR, "%a(), Invalid Render Buffer => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
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

    return EFI_SUCCESS;
}

EFI_STATUS SetRenderBuffer(RENDER_BUFFER *RenBuf)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p)\n", __func__, RenBuf);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    if (!RenBuf) {
        DbgPrint(DL_ERROR, "%a(), RenBuf=NULL => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    if (RenBuf->Sig != RENBUF_SIG) {
        DbgPrint(DL_ERROR, "%a(), Invalid Render Buffer => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    gCurrRenBuf = RenBuf;
    gRenderToScreen = FALSE;

    return EFI_SUCCESS;
}

EFI_STATUS SetScreenRender(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    gCurrRenBuf = &gFrameBuffer;
    gRenderToScreen = TRUE;

    return EFI_SUCCESS;
}

EFI_STATUS DisplayRenderBuffer(RENDER_BUFFER *RenBuf, INT32 x, INT32 y)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p, x=%d, y=%d)\n", __func__, RenBuf, x, y);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    if (!RenBuf) {
        DbgPrint(DL_ERROR, "%a(), RenBuf=NULL => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    if (RenBuf->Sig != RENBUF_SIG) {
        DbgPrint(DL_ERROR, "%a(), Invalid Render Buffer => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
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
        DbgPrint(DL_WARN, "%a(), not visible => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
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

    EFI_STATUS Status = gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)RenBuf->PixelData, EfiBltBufferToVideo, src_x, src_y, dst_xl, dst_yt, width, height, sizeof(UINT32)*RenBuf->PixPerScnLn);
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_ERROR, "%a(), Blt() => %a\n", __func__, EFIStatusToStr(Status));
    }
    
    return Status;
}

UINT32 GetHorRes(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return 0;
    }
    return gCurrRenBuf->HorRes;
}

UINT32 GetVerRes(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return 0;
    }
    return gCurrRenBuf->VerRes;
}

VOID SetClipping(INT32 x0, INT32 y0, INT32 x1, INT32 y1)
{
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d)\n", __func__, x0, y0, x1, y1);
    set_clipping(gCurrRenBuf, x0, y0, x1, y1);
}

VOID set_clipping(RENDER_BUFFER *RenBuf, INT32 x0, INT32 y0, INT32 x1, INT32 y1)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p, x0=%d, y0=%d, x1=%d, y1=%d)\n", __func__, x0, y0, x1, y1);

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
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p)\n", __func__, RenBuf);

    RenBuf->ClipX0 = 0;
    RenBuf->ClipY0 = 0;
    RenBuf->ClipX1 = RenBuf->HorRes - 1;
    RenBuf->ClipY1 = RenBuf->VerRes - 1;
}

BOOLEAN Clipped(VOID)
{
    DbgPrint(DL_INFO, "%a()\n", __func__);

    return clipped(gCurrRenBuf);
}

BOOLEAN clipped(RENDER_BUFFER *RenBuf)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p)\n", __func__, RenBuf);

    if (RenBuf->ClipX0 != 0 || RenBuf->ClipY0 != 0 || RenBuf->ClipX1 != RenBuf->HorRes - 1 || RenBuf->ClipY1 != RenBuf->VerRes - 1) {
        return TRUE;
    }
    return FALSE;
}

VOID ClearScreen(UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(colour=0x%08X)\n", __func__, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised", __func__);
        return;
    }
    if (gRenderToScreen) {
        gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, EfiBltVideoFill, 0, 0, 0, 0, gCurrRenBuf->HorRes, gCurrRenBuf->VerRes, 0);
    } else {
        UINT32 *ptr = gCurrRenBuf->PixelData;
        if (gCurrRenBuf->HorRes == gCurrRenBuf->PixPerScnLn) {
            SetMem32(ptr, gCurrRenBuf->PixPerScnLn * gCurrRenBuf->VerRes * sizeof(UINT32), colour);
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
    gCurrRenBuf->TxtCfg.CurrX = gCurrRenBuf->TxtCfg.X0;
    gCurrRenBuf->TxtCfg.CurrY = gCurrRenBuf->TxtCfg.Y0;
}

VOID ClearClipWindow(UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(colour=0x%08X)\n", __func__, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x=%d, y=%d, colour=0x%08X)\n", __func__, x, y, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x=%d, y=%d)\n", __func__, x, y);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x=%d, y=%d, width=%d, colour=0x%08X)\n", __func__, x, y, width, colour);

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
    DbgPrint(DL_INFO, "%a(x=%d, y=%d, width=%d, colour=0x%08X)\n", __func__, x, y, width, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
        return;
    }
    EDK2SIM_GFX_BEGIN;
    draw_hline(x, y, width, colour);
    EDK2SIM_GFX_END;
}

VOID DrawHLine2(INT32 x0, INT32 x1, INT32 y, UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(x0=%d, x0=%d, y=%d, colour=0x%08X)\n", __func__, x0, x1, colour);

    if (x1 < x0) {
        SWAP(INT32, x0, x1);
    }
    DrawHLine(x0, y, ABS(x1-x0)+1, colour);
}

VOID DrawVLine(INT32 x, INT32 y, INT32 height, UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(x=%d, y=%d, height=%d, colour=0x%08X)\n", __func__, x, y, height, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d, colour=0x%08X)\n", __func__, x0, y0, x1, y1, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d, colour=0x%08X)\n", __func__, x0, y0, x1, y1, colour);

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
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d, x2=%d, y2=%d colour=0x%08X)\n", __func__, x0, y0, x1, y1, x2, y2, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d, x2=%d, y2=%d colour=0x%08X)\n", __func__, x0, y0, x1, y1, x2, y2, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d, colour=0x%08X)\n", __func__, x0, y0, x1, y1, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(x0=%d, y0=%d, x1=%d, y1=%d, colour=0x%08X)\n", __func__, x0, y0, x1, y1, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(xc=%d, yc=%d, r=%d, colour=0x%08X)\n", __func__, xc, yc, r, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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
    DbgPrint(DL_INFO, "%a(xc=%d, yc=%d, r=%d, colour=0x%08X)\n", __func__, xc, yc, r, colour);

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

    DbgPrint(DL_INFO, "%a(xc=%d, yc=%d, r=%d, colour=0x%08X)\n", __func__, xc, yc, r, colour);

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
    DbgPrint(DL_INFO, "%a(xc=%d, yc=%d, r=%d, colour=0x%08X)\n", __func__, xc, yc, r, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
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

STATIC CONST UINT8 *get_font_data(FONT Font)
{
    switch (Font) {
    case FONT5x7:   return font5x7_ISO8859_1;
    case FONT5x8:   return font5x8_ISO8859_1;
    case FONT6x9:   return font6x9_ISO8859_1;
    case FONT6x10:  return font6x10_ISO8859_1;
    case FONT6x12:  return font6x12_ISO8859_1;
    case FONT6x13:  return font6x13_ISO8859_1;
    case FONT6x13B: return font6x13B_ISO8859_1;
    case FONT6x13O: return font6x13O_ISO8859_1;
    case FONT7x13B: return font7x13B_ISO8859_1;
    case FONT7x13:  return font7x13_ISO8859_1;
    case FONT7x13O: return font7x13O_ISO8859_1;
    case FONT7x14B: return font7x14B_ISO8859_1;
    case FONT7x14:  return font7x14_ISO8859_1;
    case FONT8x13:  return font8x13_ISO8859_1;
    case FONT8x13B: return font8x13B_ISO8859_1;
    case FONT8x13O: return font8x13O_ISO8859_1;
    case FONT9x15:  return font9x15_ISO8859_1;
    case FONT9x15B: return font9x15B_ISO8859_1;
    case FONT9x18:  return font9x18_ISO8859_1;
    case FONT9x18B: return font9x18B_ISO8859_1;
    default:
    case FONT10x20: return font10x20_ISO8859_1;
    }
}

/*
 * See "Using FONTX font files"
 * http://elm-chan.org/docs/dosv/fontx_e.html
 */
STATIC CONST UINT8 *get_char_bitmap (  /* Returns pointer to the font image (NULL:invalid code) */
    CONST UINT8 *FontData,              /* Pointer to the FONTX file on the memory */
    UINT16 Code                         /* Character code */
)
{
    UINTN nc, bc, sb, eb;
    UINT32 fsz;
    CONST UINT8 *cblk;

    fsz = (FontData[14] + 7) / 8 * FontData[15];  /* Get font size */

    if (FontData[16] == 0) {  /* Single byte code font */
        if (Code < 0x100) return &FontData[17 + Code * fsz];
    } else {              /* Double byte code font */
        cblk = &FontData[18]; nc = 0;  /* Code block table */
        bc = FontData[17];
        while (bc--) {
            sb = cblk[0] + cblk[1] * 0x100;  /* Get range of the code block */
            eb = cblk[2] + cblk[3] * 0x100;
            if (Code >= sb && Code <= eb) {  /* Check if in the code block */
                nc += Code - sb;             /* Number of codes from top of the block */
//                Print(L"Pos: %d\n", 18 + 4 * FontData[17] + nc * fsz);
                return &FontData[18 + 4 * FontData[17] + nc * fsz];
            }
            nc += eb - sb + 1;     /* Number of codes in the previous blocks */
            cblk += 4;             /* Next code block */
        }
    }

    return 0;   /* Invalid code */
}

VOID PrintFontInfo(CONST UINT8 *FontData)
{
    CHAR8 FileSig[7] = {0};
    AsciiStrnCpyS(FileSig, 7, (CONST CHAR8 *)&FontData[0], 6);
    CHAR8 FontName[9] = {0};
    AsciiStrnCpyS(FontName, 9, (CONST CHAR8 *)&FontData[6], 8);
    UINT8 CodeFlag = FontData[16];
   
    Print(L"File Sig   : %a\n", FileSig);
    Print(L"Font Name  : %a\n", FontName);
    Print(L"Font Width : %u\n", FONT_WIDTH(FontData));
    Print(L"Font Height: %u\n", FONT_HEIGHT(FontData));
    Print(L"Code Flag  : %u\n", CodeFlag);
}

STATIC VOID init_text_config(TEXT_CONFIG *TxtCfg, INT32 x, INT32 y, INT32 Width, INT32 Height, UINT32 FgColour, UINT32 BgColour, FONT Font)
{
    DbgPrint(DL_INFO, "%a(TxtCfg=0x%p, x=%d, y=%d, Width=%d, Height=%d, FgColour=0x%08X, BgColour=0x%08X, Font=%u)\n", __func__, TxtCfg, x, y, Width, Height, FgColour, BgColour, Font);

    TxtCfg->X0 = x;
    TxtCfg->Y0 = y;
    TxtCfg->X1 = x + Width - 1;
    TxtCfg->Y1 = y + Height - 1;
    TxtCfg->Font = Font;
    TxtCfg->FontData = get_font_data(Font);
    TxtCfg->CurrX = TxtCfg->X0;
    TxtCfg->CurrY = TxtCfg->Y0;
    TxtCfg->FgColour = FgColour;
    TxtCfg->BgColour = BgColour;
    TxtCfg->BgColourEnabled = TRUE;
    TxtCfg->LineWrapEnabled = TRUE;
    TxtCfg->ScrollEnabled = TRUE;
}

STATIC VOID default_text_config(TEXT_CONFIG *TxtCfg, INT32 Width, INT32 Height)
{
    DbgPrint(DL_INFO, "%a(TxtCfg=0x%p, Width=%d, Height=%d)\n", __func__, TxtCfg, Width, Height);

    init_text_config(TxtCfg, 0, 0, Width, Height, DEFAULT_FG_COLOUR, DEFAULT_BG_COLOUR, DEFAULT_FONT);
}

#define STRING_SIZE 1024

UINTN EFIAPI GPrint(CHAR16 *sFormat, ...)
{
    DbgPrint(DL_INFO, "%a(sFormat=0x%p ...)\n", __func__, sFormat);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
        return 0;
    }
    if (!sFormat) {
        DbgPrint(DL_WARN, "%a(), sFormat=NULL => 0\n", __func__);
        return 0;
    }
    CHAR16 String[STRING_SIZE];
    VA_LIST vl;
    VA_START(vl, sFormat);
    UINTN Length = UnicodeVSPrint(String, STRING_SIZE, sFormat, vl);
    VA_END(vl);
    EFI_STATUS Status = put_string(gCurrRenBuf, NULL, String);
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_INFO, "%a(), put_string() => %a => 0\n", __func__, EFIStatusToStr(Status));
        return 0;
    }

    return Length;
}

UINTN EFIAPI GPrintTextBox(TEXT_BOX *TxtBox, CHAR16 *sFormat, ...)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p, sFormat=0x%p ...)\n", __func__, TxtBox, sFormat);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => 0\n", __func__);
        return 0;
    }
    if (!TxtBox) {
        DbgPrint(DL_ERROR, "%a(), TxtBox=NULL => 0\n", __func__);
        return 0;
    }
    if (!sFormat) {
        DbgPrint(DL_WARN, "%a(), sFormat=NULL => 0\n", __func__);
        return 0;
    }
    CHAR16 String[STRING_SIZE];
    VA_LIST vl;
    VA_START(vl, sFormat);
    UINTN Length = UnicodeVSPrint(String, STRING_SIZE, sFormat, vl);
    VA_END(vl);
    EFI_STATUS Status = put_string(TxtBox->RenBuf, &TxtBox->TxtCfg, String);
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_INFO, "%a(), put_string() => %a => 0\n", __func__, EFIStatusToStr(Status));
        return 0;
    }

    return Length;
}

EFI_STATUS EFIAPI GPutString(INT32 x, INT32 y, UINT32 FgColour, UINT32 BgColour, BOOLEAN BgColourEnabled, FONT Font, CHAR16 *sFormat, ...)
{
    DbgPrint(DL_INFO, "%a(x=%d, y=%d, FgColour=0x%08X, BgColour=0x%08X, BgColourEnabled=%u, Font=%u, sFormat=0x%p)\n", __func__, x, y, FgColour, BgColour, BgColourEnabled, Font, sFormat);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    TEXT_CONFIG TxtCfg = {
        .X0 = gCurrRenBuf->ClipX0,
        .Y0 = gCurrRenBuf->ClipY0,
        .X1 = gCurrRenBuf->ClipX1,
        .Y1 = gCurrRenBuf->ClipY1,
        .Font = Font,
        .FontData = get_font_data(Font),
        .CurrX = x,
        .CurrY = y,  
        .FgColour = FgColour,
        .BgColour = BgColour,
        .BgColourEnabled = BgColourEnabled,
        .LineWrapEnabled = FALSE,
        .ScrollEnabled = FALSE
    };
    CHAR16 String[STRING_SIZE];
    VA_LIST vl;
    VA_START(vl, sFormat);
    UnicodeVSPrint(String, STRING_SIZE, sFormat, vl);
    VA_END(vl);
    EFI_STATUS Status = put_string(gCurrRenBuf, &TxtCfg, String);
    if (EFI_ERROR(Status)) {
        DbgPrint(DL_WARN, "%a(), put_string() => %a\n", __func__, EFIStatusToStr(Status));
        return Status;
    }

    return EFI_SUCCESS;
}

STATIC EFI_STATUS put_string(RENDER_BUFFER *RenBuf, TEXT_CONFIG *TxtCfgOvr, UINT16 *string)
{
    DbgPrint(DL_INFO, "%a(RenBuf=0x%p, TxtCfgOvr=0x%p, string=0x%p ...)\n", __func__, RenBuf, TxtCfgOvr, string);

    if (!string) {
        DbgPrint(DL_ERROR, "%a(), string=NULL => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    if (string[0] == L'\0') {
        DbgPrint(DL_INFO, "%a(), empty string => EFI_SUCCESS\n", __func__);
        return EFI_SUCCESS;
    }
    TEXT_CONFIG *TxtCfg = TxtCfgOvr ? TxtCfgOvr : &RenBuf->TxtCfg;

    INT32 x = TxtCfg->CurrX;
    INT32 y = TxtCfg->CurrY;

    INT32 HorRes = TxtCfg->X1 - TxtCfg->X0 + 1;
    INT32 VerRes = TxtCfg->Y1 - TxtCfg->Y0 + 1;

    INT32 FontWidth = FONT_WIDTH(TxtCfg->FontData);
    INT32 FontHeight = FONT_HEIGHT(TxtCfg->FontData);

    UINTN numChars = StrLen(string);
    if (y < TxtCfg->Y0) {
        DbgPrint(DL_WARN, "%a(), off top => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY; // off top 
    }
    if (y + FontHeight - 1 > TxtCfg->Y1) {
        DbgPrint(DL_WARN, "%a(). off bottom => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY; // off bottom 
    }
    if (x+(FontWidth * numChars) - 1 < TxtCfg->X0) {
        DbgPrint(DL_WARN, "%a(), off left => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY; // off left
    }
    if ( (x > TxtCfg->X1) && !TxtCfg->LineWrapEnabled) {
        DbgPrint(DL_WARN, "%a(), off right and line wrap not enabled => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY; // off right and line wrap not enabled
    }
    INT32 i = 0;
    if (x < TxtCfg->X0) {
        // determine first visible character and position
        i = (TxtCfg->X0 - x) / FontWidth;
        x += (i * FontWidth);
    }

    UINT32 *char_rbptr = RenBuf->PixelData + x + (y * RenBuf->PixPerScnLn);

    EDK2SIM_GFX_BEGIN;
    while (TRUE) {
        // get character to display
        UINT16 code = string[i];

        // end of string
        if (code == L'\0') {
            break;
        }

        BOOLEAN DoLineWrap = FALSE;

        // do carriage return check before checking if char off right of screen
        if (code == L'\r' || DoLineWrap) {
            // carriage return
            // move to the beginning of the line without advancing to the next line
            x = TxtCfg->X0;
            char_rbptr = RenBuf->PixelData + x + (y * RenBuf->PixPerScnLn);
        } else if ( (x+FontWidth-1 > TxtCfg->X1) && TxtCfg->LineWrapEnabled) {
            // char off right of screen
            DoLineWrap = TRUE;
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
                if (RenBuf == &gFrameBuffer) {
                    EFI_STATUS Status = gGop->Blt(gGop,
                                                    (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)NULL, 
                                                    EfiBltVideoToVideo, 
                                                    TxtCfg->X0, TxtCfg->Y0 + diff, 
                                                    TxtCfg->X0, TxtCfg->Y0, 
                                                    HorRes, VerRes - diff, 0);                
                    if (EFI_ERROR(Status)) {
                        DbgPrint(DL_ERROR, "%a(), Blt() => %a\n", __func__, EFIStatusToStr(Status));
                    }
                } else {
                    UINT32 *dstptr = RenBuf->PixelData + TxtCfg->X0 + (TxtCfg->Y0 * RenBuf->PixPerScnLn);
                    UINT32 *srcptr = dstptr + (diff * RenBuf->PixPerScnLn);
                    UINTN height = VerRes - diff;
                    while (height--) {
                        CopyMem(dstptr, srcptr, HorRes * sizeof(UINT32));
                        dstptr += RenBuf->PixPerScnLn;
                        srcptr += RenBuf->PixPerScnLn;
                    }
                }
                // blank scrolled area
                UINT32 colour = TxtCfg->BgColour;
                if (RenBuf == &gFrameBuffer) {
                    EFI_STATUS Status = gGop->Blt(gGop, 
                                            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, 
                                            EfiBltVideoFill, 
                                            0, 0, 
                                            TxtCfg->X0, TxtCfg->Y0 + VerRes - FontHeight, 
                                            HorRes, FontHeight, 0);
                    if (EFI_ERROR(Status)) {
                        DbgPrint(DL_ERROR, "%a(), Blt() => %a\n", __func__, EFIStatusToStr(Status));
                    }
                } else {
                    UINT32 *ptr = RenBuf->PixelData + TxtCfg->X0 + ((TxtCfg->Y0 + VerRes - FontHeight) * RenBuf->PixPerScnLn);
                    UINTN height = FontHeight;
                    while (height--) {
                        SetMem32(ptr, HorRes * sizeof(UINT32), colour);
                        ptr += RenBuf->PixPerScnLn;
                    }
                }
                char_rbptr += ((FontHeight - diff) * RenBuf->PixPerScnLn);
            } else {
                y += FontHeight;
                char_rbptr += (FontHeight * RenBuf->PixPerScnLn);
            }
        }

        // printable character
        if (code != L'\r' && code != L'\n') {
            CONST UINT8 *CharData = get_char_bitmap(TxtCfg->FontData, code);
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
    EDK2SIM_GFX_END;

    TxtCfg->CurrX = x;
    TxtCfg->CurrY = y;

    return EFI_SUCCESS;
}

VOID EnableTextBackground(BOOLEAN State)
{
    DbgPrint(DL_INFO, "%a(State=%u)\n", __func__, State);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    gCurrRenBuf->TxtCfg.BgColourEnabled = State;
}

VOID EnableTextBoxBackground(TEXT_BOX *TxtBox, BOOLEAN State)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p, State=%u)\n", __func__, TxtBox, State);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    if (!TxtBox) {
        DbgPrint(DL_ERROR, "%a(), TxtBox=NULL\n", __func__);
        return;
    }
    TxtBox->TxtCfg.BgColourEnabled = State;
}

VOID SetTextForeground(UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(colour=0x%08X)\n", __func__, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    gCurrRenBuf->TxtCfg.FgColour = colour;
}

VOID SetTextBoxForeground(TEXT_BOX *TxtBox, UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p, colour=0x%08X)\n", __func__, TxtBox, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    if (!TxtBox) {
        DbgPrint(DL_ERROR, "%a(), TxtBox=NULL\n", __func__);
        return;
    }
    TxtBox->TxtCfg.FgColour = colour;
}

VOID SetTextBackground(UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(colour=0x%08X)\n", __func__, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    gCurrRenBuf->TxtCfg.BgColour = colour;
}

VOID SetTextBoxBackground(TEXT_BOX *TxtBox, UINT32 colour)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p, colour=0x%08X)\n", __func__, TxtBox, colour);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    if (!TxtBox) {
        DbgPrint(DL_ERROR, "%a(), TxtBox=NULL\n", __func__);
        return;
    }
    TxtBox->TxtCfg.BgColour = colour;
}

VOID SetFont(FONT font)
{
    DbgPrint(DL_INFO, "%a(Font=%u)\n", __func__, font);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    gCurrRenBuf->TxtCfg.Font = font;
    gCurrRenBuf->TxtCfg.FontData = get_font_data(font);
}

CONST CHAR8 *GetFontName(FONT Font)
{
    DbgPrint(DL_INFO, "%a(Font=%u)\n", __func__, Font);

#define NAME(FONTID) case FONTID: return #FONTID;
    switch (Font) {
        NAME(FONT5x7)
        NAME(FONT5x8)
        NAME(FONT6x9)
        NAME(FONT6x10)
        NAME(FONT6x12)
        NAME(FONT6x13)
        NAME(FONT6x13B)
        NAME(FONT6x13O)
        NAME(FONT7x13)
        NAME(FONT7x13B)
        NAME(FONT7x13O)
        NAME(FONT7x14)
        NAME(FONT7x14B)
        NAME(FONT8x13)
        NAME(FONT8x13B)
        NAME(FONT8x13O)
        NAME(FONT9x15)
        NAME(FONT9x15B)
        NAME(FONT9x18)
        NAME(FONT9x18B)
        NAME(FONT10x20)
        default:
            break;
    }
    return "NOFONT";
#undef NAME
}


UINT8 GetFontWidth(FONT Font)
{
    DbgPrint(DL_INFO, "%a(Font=%u)\n", __func__, Font);

    return FONT_WIDTH(get_font_data(Font));
}

UINT8 GetFontHeight(FONT Font)
{
    DbgPrint(DL_INFO, "%a(Font=%u)\n", __func__, Font);

    return FONT_HEIGHT(get_font_data(Font));
}

VOID SetTextBoxFont(TEXT_BOX *TxtBox, FONT Font)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p, Font=%u)\n", __func__, TxtBox, Font);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised\n", __func__);
        return;
    }
    if (!TxtBox) {
        DbgPrint(DL_ERROR, "%a(), TxtBox=NULL\n", __func__);
        return;
    }
    TxtBox->TxtCfg.Font = Font;
    TxtBox->TxtCfg.FontData = get_font_data(Font);
}

EFI_STATUS CreateTextBox(TEXT_BOX *TxtBox, RENDER_BUFFER *RenBuf, INT32 x, INT32 y, INT32 Width, INT32 Height, UINT32 FgColour, UINT32 BgColour, FONT Font)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p, RenBuf=0x%p, x=%d, y=%d, Width=%d, Height=%d, FgColour=0x%08X, BgColour=0x%08X, Font=%u)\n", __func__, TxtBox, RenBuf, x, y, Width, Height, FgColour, BgColour, Font);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    if (!TxtBox) {
        DbgPrint(DL_ERROR, "%a(), TxtBox=NULL => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }

    // determine render buffer targeted (NULL == Screen)
    TxtBox->RenBuf = RenBuf ? RenBuf : &gFrameBuffer;

    if (TxtBox->RenBuf->Sig != RENBUF_SIG) {
        DbgPrint(DL_ERROR, "%a(), Invalid Render Buffer => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }

    if (y + Height - 1 < 0 || y > TxtBox->RenBuf->VerRes - 1 || x + Width - 1 < 0 || x > TxtBox->RenBuf->HorRes - 1) {
        ZeroMem(TxtBox, sizeof(TEXT_BOX));
        DbgPrint(DL_ERROR, "%a(): Invalid size\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    if (x < 0) x = 0;
    if (x + Width > TxtBox->RenBuf->HorRes) Width = TxtBox->RenBuf->HorRes - x;
    if (y < 0) y = 0;
    if (y + Height > TxtBox->RenBuf->VerRes) Height = TxtBox->RenBuf->VerRes - y;

    init_text_config(&TxtBox->TxtCfg, x, y, Width, Height, FgColour, BgColour, Font);

    ClearTextBox(TxtBox);

    return EFI_SUCCESS;
}

EFI_STATUS ClearTextBox(TEXT_BOX *TxtBox)
{
    DbgPrint(DL_INFO, "%a(TxtBox=0x%p)\n", __func__, TxtBox);

    if (!Initialised) {
        DbgPrint(DL_ERROR, "%a(), GraphicsLib not initialised => EFI_NOT_READY\n", __func__);
        return EFI_NOT_READY;
    }
    if (TxtBox->RenBuf->Sig != RENBUF_SIG) {
        DbgPrint(DL_ERROR, "%a(), Invalid Render Buffer => EFI_INVALID_PARAMETER\n", __func__);
        return EFI_INVALID_PARAMETER;
    }
    UINT32 HorRes = TxtBox->TxtCfg.X1 - TxtBox->TxtCfg.X0 + 1;
    UINT32 VerRes = TxtBox->TxtCfg.Y1 - TxtBox->TxtCfg.Y0 + 1;
    UINT32 colour = TxtBox->TxtCfg.BgColour;
    if (TxtBox->RenBuf == &gFrameBuffer) {
        EFI_STATUS Status = gGop->Blt(gGop, 
                                (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&colour, 
                                EfiBltVideoFill, 
                                0, 0, 
                                TxtBox->TxtCfg.X0, TxtBox->TxtCfg.Y0, 
                                HorRes, VerRes, 0);                
        if (EFI_ERROR(Status)) {
            DbgPrint(DL_ERROR, "%a(), Blt() => %a\n", __func__, EFIStatusToStr(Status));
        }
    } else {
        UINT32 *ptr = TxtBox->RenBuf->PixelData + TxtBox->TxtCfg.X0 + (TxtBox->TxtCfg.Y0 * TxtBox->RenBuf->PixPerScnLn);
        UINTN height = VerRes;
        while (height--) {
            SetMem32(ptr, HorRes * sizeof(UINT32), colour);
            ptr += TxtBox->RenBuf->PixPerScnLn;
        }
    }

    return EFI_SUCCESS;
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
