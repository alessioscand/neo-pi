/*
 * COMPOSITE (OPTIONS > SCANLINES): blargg's snes_ntsc filter on the game's
 * RGB picture (the library works on any 15/16-bit RGB source).
 */
#ifndef COMPOSITE_H
#define COMPOSITE_H

#include <stddef.h>
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_IN_W	PROJECT_WIDTH
#define COMP_OUT_W	(((COMP_IN_W - 1) / 3 + 1) * 7)	/* SNES_NTSC_OUT_WIDTH */
#define COMP_PHASES	3		/* colour burst phases */

/* memory the tables need (about 4 MB); the caller allocates it */
size_t composite_size (void);

/* builds the tables (composite, every field shown: no merging) in pMem;
   takes a while, call it once */
void composite_init (void *pMem);

/* one line: COMP_IN_W pixels 0x00RRGGBB in, COMP_OUT_W pixels 0x00RRGGBB
   out; nPhase = the colour burst phase of this line (0-2) */
void composite_line (const void *pTables, const unsigned *pIn, unsigned nPhase, unsigned *pOut);

#ifdef __cplusplus
}
#endif

#endif
