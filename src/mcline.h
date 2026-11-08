/*
 * neo-pi - the video chip's lines drawn on core 1 (patches/geolith-mc.py)
 *
 * Core 0 (68000, Z80, sound) queues each line; core 1 draws the queued lines in
 * order and passes each one on (to the screen with the scanlines, or to core 2
 * for COMPOSITE). Before anything the drawing reads changes, core 0 waits until
 * core 1 has drawn every queued line, so the picture is identical to the
 * single core one. A stuck core 1 is detected after 0.5 s: the game goes on
 * single core.
 */
#ifndef MCLINE_H
#define MCLINE_H

#include <circle/types.h>

// called on core 1 after each line it has drawn (0..263, the core's numbering)
typedef void TMcLineDone (unsigned nScanline);

void McLinesOut (TMcLineDone *pDone);	// 0: the lines are not passed on
TMcLineDone *McLinesOutTo (void);
void McEnable (boolean bOn);		// on: the core's lines go through core 1
boolean McEnabled (void);
boolean McFailed (void);
boolean McAlive (void);
void McSync (void);			// core 0: wait for every queued line
void McWorker (void);			// core 1's loop, never returns

// diagnostics when core 1 was found stuck
void McFailText (char *pOut, size_t nSize, unsigned nFrame);

#endif
