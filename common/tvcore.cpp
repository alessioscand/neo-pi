/*
 * COMPOSITE on core 2 (see tvcore.h)
 *
 * One ring of lines, written by core 1 (or core 0 without multicore) only at
 * the head and by core 2 only at the tail (the same scheme as the video chip
 * on core 1): release and acquire on the two counters, dsb + sev to wake the
 * other side from wfe.
 */
#include "tvcore.h"
#include "smp.h"
#include <circle/new.h>
#include <circle/util.h>

#define RING	256				// lines: more than a frame

struct TTvLine
{
	u32	 Src[COMP_IN_W];
	unsigned nPhase;
	unsigned nRow;
	boolean	 bRace;
};

static TTvLine s_Ring[RING];
static unsigned s_nHead;			// written by the producer only
static unsigned s_nTail;			// written by core 2 only
static boolean s_bAlive = FALSE;		// core 2 is running the loop
static boolean s_bDead = FALSE;			// core 2 stopped answering
static void *s_pTables = 0;
static TTvLineOut *s_pOut = 0;

// time spent (for LOG.TXT); only one side processes at a time
static u64 s_nFilterTicks, s_nDrawTicks;
static unsigned s_nLines;

static void Process (const TTvLine *e)
{
	static u32 Out[2][COMP_OUT_W];		// [0] core 2, [1] the caller without it
	u32 *pOut = Out[SharedLoad (s_bAlive) && !SharedLoad (s_bDead) ? 0 : 1];
	u64 t0 = Ticks ();
	composite_line (s_pTables, e->Src, e->nPhase, pOut);
	u64 t1 = Ticks ();
	if (s_pOut)
	{
		s_pOut (e->nRow, pOut, e->bRace);
	}
	u64 t2 = Ticks ();
	SharedStore (s_nFilterTicks, s_nFilterTicks + (t1 - t0));
	SharedStore (s_nDrawTicks, s_nDrawTicks + (t2 - t1));
	SharedStore (s_nLines, s_nLines + 1);
}

boolean TvInit (void)
{
	if (s_pTables == 0)
	{
		void *p = new u8[composite_size ()];
		if (p == 0)
		{
			return FALSE;
		}
		composite_init (p);
		SharedRelease (s_pTables, p);
	}
	return TRUE;
}

boolean TvReady (void)
{
	return s_pTables != 0;
}

void TvSetOutput (TTvLineOut *pOut)
{
	TvSync ();
	s_pOut = pOut;
}

void TvWorkerRun (void)
{
	SharedRelease (s_bAlive, (boolean) TRUE);
	SignalEvent ();
	for (;;)
	{
		unsigned t = s_nTail;
		while (SharedAcquire (s_nHead) == t)
		{
			WaitEvent ();
		}
		Process (&s_Ring[t & (RING - 1)]);
		SharedRelease (s_nTail, t + 1);
		SignalEvent ();
	}
}

boolean TvWorkerAlive (void)
{
	return SharedAcquire (s_bAlive) && !SharedLoad (s_bDead);
}

void TvSync (void)
{
	if (!TvWorkerAlive ())
	{
		return;
	}
	if (SharedAcquire (s_nTail) == s_nHead)
	{
		return;
	}
	u64 nLimit = Ticks () + TicksFreq () / 2;
	while (SharedAcquire (s_nTail) != s_nHead)
	{
		if (Ticks () > nLimit)
		{
			// core 2 stopped answering: finish here, without it from now on
			SharedStore (s_bDead, (boolean) TRUE);
			for (unsigned t = SharedLoad (s_nTail); t != s_nHead; t++)
			{
				Process (&s_Ring[t & (RING - 1)]);
			}
			return;
		}
		WaitEvent ();
	}
}

void TvPost (const u32 *pSource, unsigned nPhase, unsigned nRow, boolean bRace)
{
	if (s_pTables == 0)
	{
		return;
	}
	if (!TvWorkerAlive ())
	{
		static TTvLine Line;
		memcpy (Line.Src, pSource, sizeof Line.Src);
		Line.nPhase = nPhase;
		Line.nRow = nRow;
		Line.bRace = bRace;
		Process (&Line);
		return;
	}
	unsigned h = s_nHead;
	if (h - SharedAcquire (s_nTail) >= RING)
	{
		TvSync ();			// full (never in practice)
		TvPost (pSource, nPhase, nRow, bRace);
		return;
	}
	TTvLine *e = &s_Ring[h & (RING - 1)];
	memcpy (e->Src, pSource, sizeof e->Src);
	e->nPhase = nPhase;
	e->nRow = nRow;
	e->bRace = bRace;
	SharedRelease (s_nHead, h + 1);
	SignalEvent ();
}

void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines)
{
	u64 f = SharedLoad (s_nFilterTicks), d = SharedLoad (s_nDrawTicks);
	unsigned n = SharedLoad (s_nLines);
	SharedStore (s_nFilterTicks, (u64) 0);
	SharedStore (s_nDrawTicks, (u64) 0);
	SharedStore (s_nLines, 0u);
	*pFilterUs = (unsigned) (f * 1000000 / TicksFreq ());
	*pDrawUs = (unsigned) (d * 1000000 / TicksFreq ());
	*pLines = n;
}
