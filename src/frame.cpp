/*
 * neo-pi - the emulated frame (see frame.h)
 */
#include "frame.h"
#include "mcline.h"
#include "pacing.h"
#include "racing.h"
#include "smp.h"
#include "tvcore.h"

extern "C" {
#include "geo.h"
#include "geo_lspc.h"
#include "geo_mixer.h"
#include "geo_ymfm.h"
void geo_exec_rephase (unsigned nLine);		// patches/geolith-phase.py
}

u32 g_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES] __attribute__ ((aligned (64)));
int16_t g_AudioBuf[8192] __attribute__ ((aligned (64)));
unsigned g_nAudioFrames;

static CScreen *s_pVideo = 0;
static u32 s_Line[GAME_W * 8], s_Dark[GAME_W * 8];	// core 1's scanline buffers
static u32 s_TvLine[GAME_W * 8];			// core 2's

// read by core 1 for each line
static boolean s_bComposite = FALSE;
static boolean s_bRaced = FALSE;

static unsigned s_nTvBurst = 0;			// colour burst phase of the frame (0-2)
static unsigned s_nCoreFrames = 0, s_nMcFailFrame = 0;

// ---- sound ------------------------------------------------------------------

// resampler=speex: Geolith resamples (Speex, quality 7)
static void AudioCallback (size_t nValues)
{
	g_nAudioFrames = nValues / 2;		// the core counts int16 values (L+R)
}

// Light resampler (default since 0.6.2-rc1): the core hands over the raw
// YM2610 output (about 56.3 kHz) and it is brought to 48 kHz by linear
// interpolation, carrying the position and the last sample across frames.
// Same rate ratio as Geolith's Speex path, a fraction of its cost (Speex at
// quality 7 took about 15% of the frame time on a Pi 3).
#define YM_RATE		56319		// as in geo_mixer.c
#define OUT_RATE	48000

static u64 s_nResPos;			// 32.32 position, 0 = last sample of the previous frame
static int16_t s_ResPrev[2];
static const u64 s_nResStep = ((u64) YM_RATE << 32) / OUT_RATE;

static void AudioCallbackLinear (size_t nValues)
{
	const int16_t *pIn = geo_ymfm_get_buffer ();
	unsigned nIn = (unsigned) (nValues / 2);	// stereo input samples
	unsigned nOut = 0;
	u64 nPos = s_nResPos;
	const unsigned nMaxOut = sizeof g_AudioBuf / sizeof g_AudioBuf[0] / 2;

	// extended input: index 0 = previous frame's last sample, k = pIn[k-1]
	while ((unsigned) (nPos >> 32) < nIn && nOut < nMaxOut)
	{
		unsigned i = (unsigned) (nPos >> 32);
		int f = (int) ((nPos >> 17) & 0x7FFF);		// 15 bits: (diff * f) fits in 32 bits
		int l0, r0;
		if (i == 0)
		{
			l0 = s_ResPrev[0]; r0 = s_ResPrev[1];
		}
		else
		{
			l0 = pIn[2*(i-1)]; r0 = pIn[2*(i-1)+1];
		}
		int l1 = pIn[2*i], r1 = pIn[2*i+1];
		g_AudioBuf[2*nOut]   = (int16_t) (l0 + (((l1 - l0) * f) >> 15));
		g_AudioBuf[2*nOut+1] = (int16_t) (r0 + (((r1 - r0) * f) >> 15));
		nOut++;
		nPos += s_nResStep;
	}
	if (nIn > 0)
	{
		s_ResPrev[0] = pIn[2*(nIn-1)];
		s_ResPrev[1] = pIn[2*(nIn-1)+1];
		nPos -= (u64) nIn << 32;
	}
	s_nResPos = nPos;
	g_nAudioFrames = nOut;
}

void FrameAudioStart (boolean bSpeex)
{
	geo_mixer_set_raw (bSpeex ? 0 : 1);
	geo_mixer_set_callback (bSpeex ? AudioCallback : AudioCallbackLinear);
	s_nResPos = 0;
	s_ResPrev[0] = s_ResPrev[1] = 0;
}

// ---- picture ----------------------------------------------------------------

// core 1: a finished line goes straight to the screen (with the scanlines),
// or to core 2 with COMPOSITE
static void LineDone (unsigned nScanline)
{
	if (nScanline < GAME_SRC_Y || nScanline >= GAME_SRC_Y + GAME_H)
	{
		return;
	}
	unsigned nRow = nScanline - GAME_SRC_Y;
	if (SharedLoad (s_bComposite))
	{
		TvPost (g_VideoBuf + nScanline * GAME_SRC_PITCH + GAME_SRC_X,
			(s_nTvBurst + nRow) % COMP_PHASES, nRow, SharedLoad (s_bRaced));
	}
	else
	{
		s_pVideo->DrawGameLine (g_VideoBuf, nRow, s_Line, s_Dark);
		if (SharedLoad (s_bRaced))
		{
			RaceRowDone (nRow);
		}
	}
}

// core 2 (or the caller without it): a filtered line
static void TvLineOut (unsigned nRow, const u32 *pNtsc, boolean bRace)
{
	s_pVideo->DrawNtscLine (nRow, pNtsc, COMP_OUT_W, s_TvLine);
	if (bRace)
	{
		RaceRowDone (nRow);
	}
}

void FrameSetup (CScreen *pVideo)
{
	s_pVideo = pVideo;
	TvSetOutput (TvLineOut);
	geo_lspc_set_buffer (g_VideoBuf);
	geo_mixer_set_buffer (g_AudioBuf);
	geo_mixer_set_callback (AudioCallback);
}

void FrameSync (void)
{
	McSync ();
	if (s_bComposite)
	{
		TvSync ();			// and core 2 its lines
		// the phase steps through all three values: the colour artifacts crawl
		// along the edges even on a still picture (the classic dot crawl)
		s_nTvBurst = (s_nTvBurst + 1) % COMP_PHASES;
	}
	s_nCoreFrames++;
	if (McFailed () && s_nMcFailFrame == 0)
	{
		s_nMcFailFrame = s_nCoreFrames;
	}
}

// the frame starts at PHASE_LINE again; lines drawn meanwhile are not passed on
void FrameRephase (void)
{
	McSync ();
	TMcLineDone *pDone = McLinesOutTo ();
	McLinesOut (0);
	geo_exec_rephase (PHASE_LINE);
	FrameSync ();
	McLinesOut (pDone);
}

void FrameLinesToScreen (boolean bOn)
{
	McLinesOut (bOn ? LineDone : 0);
}

void FrameSetComposite (boolean bOn)
{
	TvSync ();
	SharedStore (s_bComposite, bOn);
	__asm__ volatile ("dsb ish" ::: "memory");
}

boolean FrameIsComposite (void)
{
	return s_bComposite;
}

void FrameSetRaced (boolean bOn)
{
	SharedStore (s_bRaced, bOn);
}

void FrameDraw (void)
{
	if (McEnabled () && McLinesOutTo ())
	{
		return;				// core 1 drew it line by line
	}
	if (s_bComposite)
	{
		// no core 1: the whole frame, filtered on core 2 (or here)
		for (unsigned y = 0; y < GAME_H; y++)
		{
			TvPost (g_VideoBuf + (y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X,
				(s_nTvBurst + y) % COMP_PHASES, y, FALSE);
		}
		TvSync ();
		s_nTvBurst = (s_nTvBurst + 1) % COMP_PHASES;
		return;
	}
	s_pVideo->DrawGame (g_VideoBuf);
}

// the menus and screens go through the same filter as the game, with a fixed
// colour burst phase, so the dots do not crawl on still text
void FrameDrawPicture (u32 *pBuffer)
{
	if (s_bComposite)
	{
		for (unsigned y = 0; y < GAME_H; y++)
		{
			TvPost (pBuffer + (y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X, y % COMP_PHASES, y, FALSE);
		}
		TvSync ();
		return;
	}
	s_pVideo->DrawGame (pBuffer);
}

unsigned FrameMcFailFrame (void)
{
	return s_nMcFailFrame;
}
