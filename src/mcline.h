/*
 * neo-pi - the video lines drawn on core 1 (patches/geolith-mc.py): core 0
 * queues them, and waits for them before changing what they read (the same
 * picture as single core); core 1 stuck 0.5 s: single core.
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
