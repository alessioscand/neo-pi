/*
 * neo-pi - the emulated frame: its picture and sound, and where its lines go
 *
 * With multicore each line the video chip finishes goes straight to the screen
 * (with the scanlines) from core 1; with COMPOSITE it goes to core 2, which
 * filters it and puts it on screen. Single core, the whole frame is drawn after
 * geo_exec().
 */
#ifndef FRAME_H
#define FRAME_H

#include "screen.h"
#include <stdint.h>

extern u32 g_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES];

// the frame's sound, 48 kHz stereo
extern int16_t g_AudioBuf[8192];
extern unsigned g_nAudioFrames;		// stereo frames

void FrameSetup (CScreen *pVideo);	// once, before the cores start
void FrameAudioStart (boolean bSpeex);	// at each game load: the resampler to use
void FrameSync (void);			// wait until cores 1 and 2 have finished the frame
void FrameRephase (void);		// after a reset or a loaded state (see pacing.h)

void FrameLinesToScreen (boolean bOn);	// core 1 passes each finished line on
boolean FrameTvInit (void);		// COMPOSITE's tables (about 4 MB, built once)
void FrameSetComposite (boolean bOn);
boolean FrameIsComposite (void);
void FrameSetRaced (boolean bOn);	// this frame is beam raced
void FrameDraw (void);			// g_VideoBuf to the screen, if not there yet
void FrameDrawPicture (u32 *pBuffer);	// a UI picture, through the same filter

unsigned FrameMcFailFrame (void);	// frame at which core 1 stopped answering

#endif
