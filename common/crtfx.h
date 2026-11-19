//
// crtfx.h - TEST: CPU ports of libretro CRT shaders (flat, default parameters)
//
// One game line in, M screen rows out. Most shaders also read line L+1 for
// the rows of line L, so line y draws the rows of line y-1 (and its own when
// it is the last one): one game line behind. HYLLIAN, ONETAP, NTSCSIMPLE,
// CRTPI and ZFAST read only lines L and L-1: no lag (CrtFxLag).
//
#ifndef CRTFX_H
#define CRTFX_H

#include <circle/types.h>

#define CRTFX_NONE	 0
#define CRTFX_FAKELOTTES 1	// hunterk, mask from crt-lottes (public domain)
#define CRTFX_BLURPI	 2	// armadillu, MIT (soft preset)
#define CRTFX_EASYMODE	 3	// EasyMode, GPL
#define CRTFX_APERTURE	 4	// EasyMode, GPL
#define CRTFX_HYLLIAN	 5	// Hyllian, MIT (crt-hyllian-fast)
#define CRTFX_ONETAP	 6	// fishku, CC0 (crt-1tap)
#define CRTFX_NTSCSIMPLE 7	// DariusG, GPL (ntsc-simple, composite)
#define CRTFX_CRTPI	 8	// davej, GPL (crt-pi)
#define CRTFX_ZFAST	 9	// Greg Hogan, GPL (zfast_crt_finemask)
#define CRTFX_HYLLORIG	10	// Hyllian, MIT (crt-hyllian-fast, defaults)

// set the shader for scale M (no-op if unchanged); bRLow: red in the low byte
void CrtFxSetup (unsigned nFx, unsigned M, boolean bRLow);
unsigned CrtFxActive (void);
boolean CrtFxLag (void);		// rows one game line behind

// line y of the frame is complete (lines above it too): draw the rows it
// finishes; Row(i) is the framebuffer line i of the picture, at its first pixel
typedef u32 *(*TCrtRowFn) (void *pCtx, unsigned nRow);
void CrtFxLine (const u32 *pFrame, unsigned y, TCrtRowFn pRow, void *pCtx);

// time spent in CrtFxLine since the last call (microseconds), calls
void CrtFxTakeStats (unsigned *pSumUs, unsigned *pMaxUs, unsigned *pCalls);

#endif
