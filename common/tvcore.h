/*
 * COMPOSITE on core 2.
 *
 * Core 1 draws the video chip's lines; with COMPOSITE it queues each finished
 * line for core 2 and goes on. Core 2 runs blargg's filter on it (COMP_IN_W
 * -> COMP_OUT_W pixels) and hands it to the output callback, which scales it
 * onto the screen and tells the beam racer. Nothing waits for a whole frame:
 * no added lag, beam racing still works. Without core 2 (multicore=off, or it
 * stops answering) the lines are filtered by the caller instead.
 */
#ifndef TVCORE_H
#define TVCORE_H

#include <circle/types.h>
#include "composite.h"

// called for each filtered line (on core 2, or on the caller without it)
typedef void TTvLineOut (unsigned nRow, const u32 *pNtsc, boolean bRace);

// the filter's tables (about 4 MB, built once; takes a moment)
boolean TvInit (void);
boolean TvReady (void);

void TvSetOutput (TTvLineOut *pOut);

// core 2's loop (from CMultiCoreSupport::Run); never returns
void TvWorkerRun (void);
boolean TvWorkerAlive (void);

// queues one line: COMP_IN_W pixels 0x00RRGGBB (copied), its burst phase,
// its row in the picture, whether the frame is raced
void TvPost (const u32 *pSource, unsigned nPhase, unsigned nRow, boolean bRace);

// waits until every queued line has been filtered and drawn
void TvSync (void);

// time spent since the last call: filtering, drawing (microseconds), lines
void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines);

#endif
