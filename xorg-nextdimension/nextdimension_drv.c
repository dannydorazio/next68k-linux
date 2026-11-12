/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * X driver for the NeXTdimension, drawing with the board's i860
 *
 * The board shows 32-bit pixels as 0xRRGGBBXX, an order X's Render
 * extension has no format for, so X doesn't draw in the video memory.  Its
 * framebuffer and its offscreen pixmaps are in the board's DRAM instead, in
 * X's own 0xXXRRGGBB, where the i860 fills and copies them and draws text:
 * Render's glyphs, a color through a mask of 8-bit alpha (EXA).  X's
 * shadow layer collects what changed on the screen, and the i860 copies
 * that to the video memory in the board's order (PRESENT).
 *
 * The kernel's driver (drivers/gpu/drm/tiny/nextdimension.c) starts the
 * i860 with its program (nd-firmware/ndfw.s) and maps the board's DRAM as
 * /dev/fb0's MMIO region.  The program's shared area says which part of the
 * DRAM is X's (ARENA); X and the console take turns at the program's
 * command ring as the VT switches, each starting from HEAD as the other
 * left it.  Without the i860, or with too little DRAM, the framebuffer is
 * in system memory and the CPU copies the changes to the video memory.
 */

#include "xorg-server.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86platformBus.h"
#include "micmap.h"
#include "mipointer.h"
#include "fb.h"
#include "exa.h"
#include "shadow.h"
#include "fbdevhw.h"

#define ND_NAME		"nextdimension"
#define ND_VERSION	1000

#define ND_WIDTH	1120
#define ND_HEIGHT	832
#define ND_VRAM_PITCH	(1152 * 4)
#define ND_PITCH	(ND_WIDTH * 4)		/* X's: a multiple of 16 */

/* the i860's addresses for the board's memory */
#define ND_I860_DRAM	0xF8000000u
#define ND_I860_VRAM	0xFE000000u

/* the program's shared area and ring, in DRAM: as in ndfw.s */
#define ND_FW_READY	0x8000
#define  ND_FW_MAGIC	0x4E44464Cu		/* "NDFL" */
#define ND_FW_HEAD	0x8004
#define ND_FW_TAIL	0x8008
#define ND_FW_ARENA	0x8010
#define ND_FW_ARENA_SIZE 0x8014
#define ND_RING		0x10000
#define ND_RING_WORDS	0x4000

#define ND_OP_WRAP	4
#define ND_OP_FILLP	5
#define ND_OP_COPYP	6
#define ND_OP_PRESENT	7
#define ND_OP_FILL8	8
#define ND_OP_ADD8	9
#define ND_OP_OVERMASK	10

/* how long the i860 may take no command before it's given up on */
#define ND_TIMEOUT_MS	10000

/* more damaged boxes than this are presented as their extents */
#define ND_PRESENT_BOXES 16

typedef struct {
    EntityInfoPtr pEnt;
    Bool want_accel;		/* the Accel option */
    Bool accel;			/* the i860 draws */
    volatile CARD32 *vram;	/* the video memory */
    volatile CARD8 *dram;	/* the board's DRAM */
    CARD8 *fb;			/* X's framebuffer */
    Bool fb_malloced;
    CARD32 fb_i860;		/* the i860's address for it */
    unsigned long fb_size;	/* and the memory after it */
    unsigned int head;		/* where the next command goes */

    /* the operation EXA prepared */
    int op;
    CARD32 dst, dst_pitch, src, src_pitch, color;

    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr CloseScreen;
    ExaDriverPtr exa;
} NDRec, *NDPtr;

#define ND_PTR(p) ((NDPtr)((p)->driverPrivate))

enum { OPTION_FBDEV, OPTION_ACCEL };

static const OptionInfoRec NDOptions[] = {
    { OPTION_FBDEV, "fbdev", OPTV_STRING, {0}, FALSE },
    { OPTION_ACCEL, "Accel", OPTV_BOOLEAN, {0}, FALSE },
    { -1, NULL, OPTV_NONE, {0}, FALSE }
};

/*
 * The command ring
 */

static CARD32
nd_read(NDPtr nd, unsigned int off)
{
    return *(volatile CARD32 *)(nd->dram + off);
}

static void
nd_write(NDPtr nd, unsigned int off, CARD32 val)
{
    *(volatile CARD32 *)(nd->dram + off) = val;
}

/* The i860 stopped taking commands: draw without it from now on. */
static void
nd_i860_dead(ScrnInfoPtr pScrn)
{
    NDPtr nd = ND_PTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
               "the i860 stopped at command %u (head %u); drawing without it\n",
               (unsigned int)nd_read(nd, ND_FW_TAIL), nd->head);
    nd->accel = FALSE;
}

/*
 * Wait for the i860 until done(tail) says so; FALSE if it went
 * ND_TIMEOUT_MS without taking a command.
 */
static Bool
nd_wait(ScrnInfoPtr pScrn, Bool (*done)(NDPtr, unsigned int, unsigned int),
        unsigned int len)
{
    NDPtr nd = ND_PTR(pScrn);
    unsigned int tail, last = ~0u;
    CARD32 since = 0;
    int spins = 0;

    while (nd->accel) {
        tail = nd_read(nd, ND_FW_TAIL);
        if (done(nd, tail, len))
            return TRUE;
        if (tail != last) {
            last = tail;
            since = GetTimeInMillis();
            spins = 0;
        } else if (++spins > 1000) {
            if (GetTimeInMillis() - since > ND_TIMEOUT_MS)
                nd_i860_dead(pScrn);
            else
                usleep(1000);
            spins = 0;
        }
    }
    return FALSE;
}

static Bool
nd_idle(NDPtr nd, unsigned int tail, unsigned int len)
{
    return tail == nd->head;
}

/*
 * Room for len words at head: a command never wraps around the end of the
 * ring and leaves its last word for a WRAP; the ring is never quite full,
 * so that a full one isn't taken for empty.  As in the kernel's driver.
 */
static Bool
nd_room(NDPtr nd, unsigned int tail, unsigned int len)
{
    if (nd->head >= tail) {
        if (nd->head + len < ND_RING_WORDS)
            return TRUE;
        if (len < tail) {
            nd_write(nd, ND_RING + nd->head * 4, ND_OP_WRAP);
            nd->head = 0;
            nd_write(nd, ND_FW_HEAD, 0);
            return TRUE;
        }
        return FALSE;
    }
    return nd->head + len < tail;
}

static void
nd_sync(ScrnInfoPtr pScrn)
{
    nd_wait(pScrn, nd_idle, 0);
}

/* Queue a command of n words; FALSE if the i860 is gone. */
static Bool
nd_command(ScrnInfoPtr pScrn, const CARD32 *words, unsigned int n)
{
    NDPtr nd = ND_PTR(pScrn);
    volatile CARD32 *cmd;
    unsigned int i;

    if (!nd_wait(pScrn, nd_room, n))
        return FALSE;
    cmd = (volatile CARD32 *)(nd->dram + ND_RING + nd->head * 4);
    for (i = 0; i < n; i++)
        cmd[i] = words[i];
    nd->head += n;
    nd_write(nd, ND_FW_HEAD, nd->head);
    return TRUE;
}

/*
 * Showing X's framebuffer
 */

/* The CPU's copy, when there's no i860. */
static void
nd_present_cpu(NDPtr nd, int x, int y, int w, int h)
{
    for (; h > 0; h--, y++) {
        const CARD32 *s = (const CARD32 *)(nd->fb + y * ND_PITCH) + x;
        volatile CARD32 *d = nd->vram + y * (ND_VRAM_PITCH / 4) + x;
        int i;

        for (i = 0; i < w; i++)
            d[i] = s[i] << 8;
    }
}

static void
nd_present(ScrnInfoPtr pScrn, int x1, int y1, int x2, int y2)
{
    NDPtr nd = ND_PTR(pScrn);

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > ND_WIDTH)
        x2 = ND_WIDTH;
    if (y2 > ND_HEIGHT)
        y2 = ND_HEIGHT;
    if (x1 >= x2 || y1 >= y2)
        return;

    if (nd->accel) {
        CARD32 cmd[7] = {
            ND_OP_PRESENT,
            nd->fb_i860 + y1 * ND_PITCH + x1 * 4, ND_PITCH,
            ND_I860_VRAM + y1 * ND_VRAM_PITCH + x1 * 4, ND_VRAM_PITCH,
            x2 - x1, y2 - y1
        };

        if (nd_command(pScrn, cmd, 7))
            return;
    }
    nd_present_cpu(nd, x1, y1, x2 - x1, y2 - y1);
}

/* the shadow layer's: show what changed */
static void
nd_shadow_update(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    RegionPtr damage = DamageRegion(pBuf->pDamage);
    int n = RegionNumRects(damage);
    BoxPtr box = RegionRects(damage);

    if (!pScrn->vtSema)
        return;
    if (n > ND_PRESENT_BOXES) {
        box = RegionExtents(damage);
        n = 1;
    }
    for (; n > 0; n--, box++)
        nd_present(pScrn, box->x1, box->y1, box->x2, box->y2);
}

/*
 * EXA: fills and copies by the i860
 */

static CARD32
nd_pixmap_i860(ScrnInfoPtr pScrn, PixmapPtr pPix)
{
    return ND_PTR(pScrn)->fb_i860 + exaGetPixmapOffset(pPix);
}

static Bool
nd_prepare_solid(PixmapPtr pPix, int alu, Pixel planemask, Pixel fg)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);

    if (!nd->accel || !EXA_PM_IS_SOLID(&pPix->drawable, planemask))
        return FALSE;
    switch (pPix->drawable.bitsPerPixel) {
    case 32:
        nd->op = ND_OP_FILLP;
        break;
    case 8:
        nd->op = ND_OP_FILL8;
        break;
    default:
        return FALSE;
    }
    switch (alu) {
    case GXclear:
        fg = 0;
        break;
    case GXset:
        fg = ~0;
        break;
    case GXcopy:
        break;
    default:
        return FALSE;
    }
    nd->dst = nd_pixmap_i860(pScrn, pPix);
    nd->dst_pitch = exaGetPixmapPitch(pPix);
    nd->color = fg;
    return TRUE;
}

static void
nd_solid(PixmapPtr pPix, int x1, int y1, int x2, int y2)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);
    int bytes = nd->op == ND_OP_FILL8 ? 1 : 4;
    CARD32 cmd[6] = {
        nd->op, nd->dst + y1 * nd->dst_pitch + x1 * bytes, nd->dst_pitch,
        x2 - x1, y2 - y1, nd->op == ND_OP_FILL8 ? nd->color & 0xff : nd->color
    };

    if (x1 < x2 && y1 < y2)
        nd_command(pScrn, cmd, 6);
}

static void
nd_done(PixmapPtr pPix)
{
}

static Bool
nd_prepare_copy(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir, int alu,
                Pixel planemask)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);

    if (!nd->accel || alu != GXcopy ||
        pSrc->drawable.bitsPerPixel != 32 ||
        pDst->drawable.bitsPerPixel != 32 ||
        !EXA_PM_IS_SOLID(&pDst->drawable, planemask))
        return FALSE;
    nd->src = nd_pixmap_i860(pScrn, pSrc);
    nd->src_pitch = exaGetPixmapPitch(pSrc);
    nd->dst = nd_pixmap_i860(pScrn, pDst);
    nd->dst_pitch = exaGetPixmapPitch(pDst);
    return TRUE;
}

/* the program copies backwards when the destination is above the source */
static void
nd_copy(PixmapPtr pDst, int sx, int sy, int dx, int dy, int w, int h)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);
    CARD32 cmd[7] = {
        ND_OP_COPYP,
        nd->src + sy * nd->src_pitch + sx * 4, nd->src_pitch,
        nd->dst + dy * nd->dst_pitch + dx * 4, nd->dst_pitch,
        w, h
    };

    if (w > 0 && h > 0)
        nd_command(pScrn, cmd, 7);
}

/*
 * Render, for text: glyphs added up in a mask of 8-bit alpha, and a color
 * put over the destination through it
 */

/* the solid color of a picture, premultiplied 0xAARRGGBB, if it has one */
static Bool
nd_picture_color(ScrnInfoPtr pScrn, PicturePtr pPict, PixmapPtr pPix,
                 CARD32 *color)
{
    if (pPict->transform || pPict->alphaMap)
        return FALSE;
    if (!pPict->pDrawable) {
        if (pPict->pSourcePict->type != SourcePictTypeSolidFill)
            return FALSE;
        *color = pPict->pSourcePict->solidFill.color;
        return TRUE;
    }
    if (pPict->pDrawable->width != 1 || pPict->pDrawable->height != 1 ||
        !pPict->repeat ||
        (pPict->format != PICT_a8r8g8b8 && pPict->format != PICT_x8r8g8b8))
        return FALSE;
    if (!pPix)		/* checking: it will be */
        return TRUE;
    nd_sync(pScrn);
    *color = *(CARD32 *)(ND_PTR(pScrn)->fb + exaGetPixmapOffset(pPix));
    if (pPict->format == PICT_x8r8g8b8)
        *color |= 0xff000000;
    return TRUE;
}

static Bool
nd_plain(PicturePtr pPict, CARD32 format)
{
    return pPict && pPict->pDrawable && pPict->format == format &&
           !pPict->transform && !pPict->alphaMap && !pPict->repeat &&
           !pPict->componentAlpha;
}

static Bool
nd_is_32bit(PicturePtr pPict)
{
    return pPict->pDrawable && !pPict->alphaMap &&
           (pPict->format == PICT_x8r8g8b8 || pPict->format == PICT_a8r8g8b8);
}

/*
 * Glyphs into a mask: Add from 8-bit alpha (EXA asks first with the text's
 * own source, so any source passes here); a color through such a mask,
 * Over 32-bit pixels.
 */
static Bool
nd_check_composite(int op, PicturePtr pSrc, PicturePtr pMask, PicturePtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->pDrawable->pScreen);
    CARD32 color;

    if (op == PictOpAdd && !pMask && pDst->format == PICT_a8 &&
        !pDst->alphaMap)
        return TRUE;
    return op == PictOpOver && nd_plain(pMask, PICT_a8) && nd_is_32bit(pDst) &&
           nd_picture_color(pScrn, pSrc, NULL, &color);
}

static Bool
nd_prepare_composite(int op, PicturePtr pSrcPict, PicturePtr pMaskPict,
                     PicturePtr pDstPict, PixmapPtr pSrc, PixmapPtr pMask,
                     PixmapPtr pDst)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);

    if (!nd->accel)
        return FALSE;
    nd->dst = nd_pixmap_i860(pScrn, pDst);
    nd->dst_pitch = exaGetPixmapPitch(pDst);
    if (op == PictOpAdd) {
        if (pMaskPict || !pSrc || !nd_plain(pSrcPict, PICT_a8))
            return FALSE;
        nd->op = ND_OP_ADD8;
        nd->src = nd_pixmap_i860(pScrn, pSrc);
        nd->src_pitch = exaGetPixmapPitch(pSrc);
        return TRUE;
    }
    if (!pMask || !nd_picture_color(pScrn, pSrcPict, pSrc, &nd->color))
        return FALSE;
    nd->op = ND_OP_OVERMASK;
    nd->src = nd_pixmap_i860(pScrn, pMask);
    nd->src_pitch = exaGetPixmapPitch(pMask);
    return TRUE;
}

static void
nd_composite(PixmapPtr pDst, int srcX, int srcY, int maskX, int maskY,
             int dstX, int dstY, int w, int h)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);

    if (w <= 0 || h <= 0)
        return;
    if (nd->op == ND_OP_ADD8) {
        CARD32 cmd[7] = {
            ND_OP_ADD8,
            nd->src + srcY * nd->src_pitch + srcX, nd->src_pitch,
            nd->dst + dstY * nd->dst_pitch + dstX, nd->dst_pitch,
            w, h
        };

        nd_command(pScrn, cmd, 7);
    } else {
        CARD32 cmd[8] = {
            ND_OP_OVERMASK, nd->color,
            nd->src + maskY * nd->src_pitch + maskX, nd->src_pitch,
            nd->dst + dstY * nd->dst_pitch + dstX * 4, nd->dst_pitch,
            w, h
        };

        nd_command(pScrn, cmd, 8);
    }
}

/* new glyphs into EXA's cache: the CPU writes the board's memory */
static Bool
nd_upload_to_screen(PixmapPtr pDst, int x, int y, int w, int h, char *src,
                    int src_pitch)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
    NDPtr nd = ND_PTR(pScrn);
    int bytes = pDst->drawable.bitsPerPixel / 8;
    int pitch = exaGetPixmapPitch(pDst);
    CARD8 *dst = nd->fb + exaGetPixmapOffset(pDst) + y * pitch + x * bytes;

    if (!bytes)
        return FALSE;
    nd_sync(pScrn);
    for (; h > 0; h--, dst += pitch, src += src_pitch)
        memcpy(dst, src, w * bytes);
    return TRUE;
}

static int
nd_mark_sync(ScreenPtr pScreen)
{
    return 0;
}

static void
nd_wait_marker(ScreenPtr pScreen, int marker)
{
    nd_sync(xf86ScreenToScrn(pScreen));
}

static Bool
nd_exa_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    NDPtr nd = ND_PTR(pScrn);
    ExaDriverPtr exa = exaDriverAlloc();

    if (!exa)
        return FALSE;
    exa->exa_major = EXA_VERSION_MAJOR;
    exa->exa_minor = EXA_VERSION_MINOR;
    exa->memoryBase = nd->fb;
    exa->memorySize = nd->fb_size;
    exa->offScreenBase = ND_PITCH * ND_HEIGHT;
    exa->pixmapOffsetAlign = 16;	/* for the i860's quad loads */
    exa->pixmapPitchAlign = 16;
    exa->flags = EXA_OFFSCREEN_PIXMAPS;
    exa->maxX = 4096;
    exa->maxY = 4096;

    exa->PrepareSolid = nd_prepare_solid;
    exa->Solid = nd_solid;
    exa->DoneSolid = nd_done;
    exa->PrepareCopy = nd_prepare_copy;
    exa->Copy = nd_copy;
    exa->DoneCopy = nd_done;
    exa->CheckComposite = nd_check_composite;
    exa->PrepareComposite = nd_prepare_composite;
    exa->Composite = nd_composite;
    exa->DoneComposite = nd_done;
    exa->UploadToScreen = nd_upload_to_screen;
    exa->MarkSync = nd_mark_sync;
    exa->WaitMarker = nd_wait_marker;

    if (!exaDriverInit(pScreen, exa)) {
        free(exa);
        return FALSE;
    }
    nd->exa = exa;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "the i860 fills, copies and draws text, with %lu KB of the board's memory for pixmaps\n",
               (nd->fb_size - exa->offScreenBase) >> 10);
    return TRUE;
}

/*
 * Screen
 */

static Bool
nd_create_screen_resources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    NDPtr nd = ND_PTR(pScrn);
    Bool ret;

    pScreen->CreateScreenResources = nd->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = nd_create_screen_resources;
    if (!ret)
        return FALSE;

    return shadowAdd(pScreen, pScreen->GetScreenPixmap(pScreen),
                     nd_shadow_update, NULL, 0, NULL);
}

static Bool
nd_close_screen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    NDPtr nd = ND_PTR(pScrn);

    nd_sync(pScrn);
    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));
    if (nd->exa) {
        exaDriverFini(pScreen);
        free(nd->exa);
        nd->exa = NULL;
    }
    if (nd->fb_malloced)
        free(nd->fb);
    nd->fb = NULL;
    fbdevHWUnmapMMIO(pScrn);
    fbdevHWUnmapVidmem(pScrn);
    pScrn->vtSema = FALSE;

    pScreen->CloseScreen = nd->CloseScreen;
    return pScreen->CloseScreen(pScreen);
}

/*
 * Where X's framebuffer goes: the board's DRAM if the i860 runs the program
 * and the kernel left X enough of it, else system memory.
 */
static void
nd_setup_memory(ScrnInfoPtr pScrn, Bool want_accel)
{
    NDPtr nd = ND_PTR(pScrn);
    CARD32 arena = 0, size = 0;

    nd->accel = FALSE;
    nd->dram = want_accel ? fbdevHWMapMMIO(pScrn) : NULL;
    if (nd->dram && nd_read(nd, ND_FW_READY) == ND_FW_MAGIC) {
        arena = nd_read(nd, ND_FW_ARENA);
        size = nd_read(nd, ND_FW_ARENA_SIZE);
    }
    if (size >= ND_PITCH * ND_HEIGHT) {
        nd->fb = (CARD8 *)nd->dram + arena;
        nd->fb_i860 = ND_I860_DRAM + arena;
        nd->fb_size = size;
        nd->head = nd_read(nd, ND_FW_HEAD);
        nd->accel = TRUE;
        return;
    }
    if (want_accel)
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "the i860 isn't running, or there's too little of its memory: drawing without it\n");
    nd->fb_size = ND_PITCH * ND_HEIGHT;
    nd->fb = calloc(1, nd->fb_size);
    nd->fb_malloced = TRUE;
}

static Bool
nd_screen_init(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    NDPtr nd = ND_PTR(pScrn);
    VisualPtr visual;

    nd->vram = fbdevHWMapVidmem(pScrn);
    if (!nd->vram)
        return FALSE;
    nd_setup_memory(pScrn, nd->want_accel);
    if (!nd->fb)
        return FALSE;

    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits,
                          TrueColor) ||
        !miSetPixmapDepths())
        return FALSE;

    if (!fbScreenInit(pScreen, nd->fb, pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
                      pScrn->bitsPerPixel))
        return FALSE;
    for (visual = pScreen->visuals + pScreen->numVisuals;
         --visual >= pScreen->visuals; ) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed = pScrn->offset.red;
            visual->offsetGreen = pScrn->offset.green;
            visual->offsetBlue = pScrn->offset.blue;
            visual->redMask = pScrn->mask.red;
            visual->greenMask = pScrn->mask.green;
            visual->blueMask = pScrn->mask.blue;
        }
    }
    if (!fbPictureInit(pScreen, NULL, 0))
        return FALSE;
    xf86SetBlackWhitePixels(pScreen);

    if (nd->accel && !nd_exa_init(pScreen)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EXA didn't start\n");
        return FALSE;
    }

    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());
    if (!miCreateDefColormap(pScreen))
        return FALSE;
    if (!shadowSetup(pScreen))
        return FALSE;

    nd->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = nd_create_screen_resources;
    nd->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = nd_close_screen;
    pScreen->SaveScreen = fbdevHWSaveScreenWeak();

    pScrn->vtSema = TRUE;
    return TRUE;
}

/* the console has drawn over the screen: show X's again */
static Bool
nd_enter_vt(ScrnInfoPtr pScrn)
{
    NDPtr nd = ND_PTR(pScrn);

    if (nd->accel)
        nd->head = nd_read(nd, ND_FW_HEAD);	/* the console's */
    pScrn->vtSema = TRUE;
    nd_present(pScrn, 0, 0, ND_WIDTH, ND_HEIGHT);
    return TRUE;
}

/* leave the console an idle ring */
static void
nd_leave_vt(ScrnInfoPtr pScrn)
{
    nd_sync(pScrn);
    pScrn->vtSema = FALSE;
}

static Bool
nd_switch_mode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    return TRUE;
}

static void
nd_adjust_frame(ScrnInfoPtr pScrn, int x, int y)
{
}

static void
nd_free_screen(ScrnInfoPtr pScrn)
{
    fbdevHWFreeRec(pScrn);
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static Bool
nd_pre_init(ScrnInfoPtr pScrn, int flags)
{
    rgb zeros = { 0, 0, 0 };
    Gamma gzeros = { 0.0, 0.0, 0.0 };
    NDPtr nd;
    OptionInfoRec opts[sizeof(NDOptions) / sizeof(NDOptions[0])];
    const char *dev;

    if (flags & PROBE_DETECT)
        return FALSE;
    if (pScrn->numEntities != 1)
        return FALSE;

    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->driverPrivate = XNFcallocarray(1, sizeof(NDRec));
    nd = ND_PTR(pScrn);
    nd->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

    dev = xf86FindOptionValue(nd->pEnt->device->options, "fbdev");
    if (!fbdevHWInit(pScrn, NULL, (char *)dev))
        return FALSE;
    if (strncmp(fbdevHWGetName(pScrn), "nextdimension", 13)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s isn't a NeXTdimension\n", fbdevHWGetName(pScrn));
        return FALSE;
    }

    if (!xf86SetDepthBpp(pScrn, 24, 0, 32, Support32bppFb))
        return FALSE;
    xf86PrintDepthBpp(pScrn);
    if (pScrn->depth != 24 || pScrn->bitsPerPixel != 32) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "only depth 24 in 32 bits per pixel\n");
        return FALSE;
    }
    if (!xf86SetWeight(pScrn, zeros, zeros) ||
        !xf86SetDefaultVisual(pScrn, -1) ||
        !xf86SetGamma(pScrn, gzeros))
        return FALSE;
    pScrn->progClock = TRUE;
    pScrn->rgbBits = 8;
    pScrn->chipset = (char *)ND_NAME;
    pScrn->videoRam = fbdevHWGetVidmem(pScrn);

    xf86CollectOptions(pScrn, NULL);
    memcpy(opts, NDOptions, sizeof(NDOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, opts);
    nd->want_accel = xf86ReturnOptValBool(opts, OPTION_ACCEL, TRUE);

    /* the board's one mode */
    fbdevHWUseBuildinMode(pScrn);
    pScrn->currentMode = pScrn->modes;
    pScrn->virtualX = ND_WIDTH;
    pScrn->virtualY = ND_HEIGHT;
    pScrn->displayWidth = ND_WIDTH;
    xf86PrintModes(pScrn);
    xf86SetDpi(pScrn, 0, 0);

    if (!xf86LoadSubModule(pScrn, "fb") ||
        !xf86LoadSubModule(pScrn, "shadow") ||
        !xf86LoadSubModule(pScrn, "exa"))
        return FALSE;
    return TRUE;
}

static void
nd_setup_scrn(ScrnInfoPtr pScrn)
{
    pScrn->driverVersion = ND_VERSION;
    pScrn->driverName = (char *)ND_NAME;
    pScrn->name = (char *)ND_NAME;
    pScrn->PreInit = nd_pre_init;
    pScrn->ScreenInit = nd_screen_init;
    pScrn->SwitchMode = nd_switch_mode;
    pScrn->AdjustFrame = nd_adjust_frame;
    pScrn->EnterVT = nd_enter_vt;
    pScrn->LeaveVT = nd_leave_vt;
    pScrn->FreeScreen = nd_free_screen;
}

/*
 * Probing: from a Device section that names this driver, or for the
 * kernel's DRM device, through an OutputClass that does
 */

static Bool
nd_probe(DriverPtr drv, int flags)
{
    GDevPtr *devSections;
    int i, n;
    Bool found = FALSE;

    if (flags & PROBE_DETECT)
        return FALSE;
    n = xf86MatchDevice(ND_NAME, &devSections);
    if (n <= 0)
        return FALSE;
    if (!xf86LoadDrvSubModule(drv, "fbdevhw"))
        return FALSE;

    for (i = 0; i < n; i++) {
        const char *dev = xf86FindOptionValue(devSections[i]->options,
                                              "fbdev");
        ScrnInfoPtr pScrn;
        int entity;

        if (!fbdevHWProbe(NULL, (char *)dev, NULL))
            continue;
        entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
        pScrn = xf86ConfigFbEntity(NULL, 0, entity, NULL, NULL, NULL, NULL);
        if (pScrn) {
            nd_setup_scrn(pScrn);
            found = TRUE;
        }
    }
    free(devSections);
    return found;
}

static Bool
nd_platform_probe(DriverPtr drv, int entity, int flags,
                  struct xf86_platform_device *dev, intptr_t match_data)
{
    ScrnInfoPtr pScrn;

    if (flags & PLATFORM_PROBE_GPU_SCREEN)
        return FALSE;
    if (!xf86LoadDrvSubModule(drv, "fbdevhw") ||
        !fbdevHWProbe(NULL, NULL, NULL))
        return FALSE;
    pScrn = xf86AllocateScreen(drv, 0);
    if (!pScrn)
        return FALSE;
    xf86AddEntityToScreen(pScrn, entity);
    nd_setup_scrn(pScrn);
    return TRUE;
}

static void
nd_identify(int flags)
{
    xf86Msg(X_INFO, "%s: driver for the NeXTdimension\n", ND_NAME);
}

static const OptionInfoRec *
nd_available_options(int chipid, int busid)
{
    return NDOptions;
}

static Bool
nd_driver_func(ScrnInfoPtr pScrn, xorgDriverFuncOp op, void *ptr)
{
    switch (op) {
    case GET_REQUIRED_HW_INTERFACES:
        *(xorgHWFlags *)ptr = 0;
        return TRUE;
    case SUPPORTS_SERVER_FDS:
        return TRUE;
    default:
        return FALSE;
    }
}

_X_EXPORT DriverRec NEXTDIMENSION = {
    .driverVersion = ND_VERSION,
    .driverName = (char *)ND_NAME,
    .Identify = nd_identify,
    .Probe = nd_probe,
    .AvailableOptions = nd_available_options,
    .driverFunc = nd_driver_func,
    .platformProbe = nd_platform_probe,
};

static void *
nd_setup(void *module, void *opts, int *errmaj, int *errmin)
{
    static Bool done;

    if (done) {
        if (errmaj)
            *errmaj = LDR_ONCEONLY;
        return NULL;
    }
    done = TRUE;
    xf86AddDriver(&NEXTDIMENSION, module, HaveDriverFuncs);
    return (void *)1;
}

static XF86ModuleVersionInfo nd_version = {
    ND_NAME, "next68k-linux", MODINFOSTRING1, MODINFOSTRING2,
    XORG_VERSION_CURRENT, 1, 0, 0,
    ABI_CLASS_VIDEODRV, ABI_VIDEODRV_VERSION, MOD_CLASS_VIDEODRV,
    { 0, 0, 0, 0 }
};

_X_EXPORT XF86ModuleData nextdimensionModuleData = {
    &nd_version, nd_setup, NULL
};
