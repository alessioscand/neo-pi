/*
 * neo-pi - COMPOSITE on core 2 (see tvcore.h)
 *
 * One ring of lines, written by core 1 (or core 0 without multicore) only at
 * the head and by core 2 only at the tail (the same scheme as the video chip
 * on core 1): release and acquire on the two counters, dsb + sev to wake the
 * other side from wfe.
 */
#include "tvcore.h"
#include "fbmap.h"
#include <circle/new.h>
#include <circle/util.h>

#define RING	256				// lines (a frame is 224)

struct TTvLine
{
	u32	 Src[COMP_IN_W];
	unsigned nPhase;
	unsigned nRow;
	boolean	 bRace;
};

static TTvLine s_Ring[RING];
static volatile unsigned s_nHead;		// written by the producer only
static volatile unsigned s_nTail;		// written by core 2 only
static volatile boolean s_bAlive = FALSE;	// core 2 is running the loop
static boolean s_bDead = FALSE;			// core 2 stopped answering
static void *s_pTables = 0;
static TTvLineOut *s_pOut = 0;

static inline u64 Ticks (void)
{
	u64 v;
	__asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r" (v));
	return v;
}

static inline u64 Freq (void)
{
	u64 v;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (v));
	return v;
}

// time spent (for LOG.TXT); only one side processes at a time
static volatile u64 s_nFilterTicks, s_nDrawTicks;
static volatile unsigned s_nLines;

static void Process (const TTvLine *e)
{
	static u32 Out[2][COMP_OUT_W];		// [0] core 2, [1] the caller without it
	u32 *pOut = Out[s_bAlive && !s_bDead ? 0 : 1];
	u64 t0 = Ticks ();
	composite_line (s_pTables, e->Src, e->nPhase, pOut);
	u64 t1 = Ticks ();
	if (s_pOut)
	{
		s_pOut (e->nRow, pOut, e->bRace);
	}
	u64 t2 = Ticks ();
	s_nFilterTicks += t1 - t0;
	s_nDrawTicks += t2 - t1;
	s_nLines++;
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
		__asm__ volatile ("dsb ish" ::: "memory");
		s_pTables = p;
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
	s_bAlive = TRUE;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	for (;;)
	{
		unsigned t = s_nTail;
		while (__atomic_load_n (&s_nHead, __ATOMIC_ACQUIRE) == t)
		{
			__asm__ volatile ("wfe");
		}
		Process (&s_Ring[t & (RING - 1)]);
		__atomic_store_n (&s_nTail, t + 1, __ATOMIC_RELEASE);
		__asm__ volatile ("dsb ish; sev" ::: "memory");
	}
}

boolean TvWorkerAlive (void)
{
	return s_bAlive && !s_bDead;
}

void TvSync (void)
{
	if (!TvWorkerAlive ())
	{
		return;
	}
	if (__atomic_load_n (&s_nTail, __ATOMIC_ACQUIRE) == s_nHead)
	{
		return;
	}
	u64 nLimit = Ticks () + Freq () / 2;
	while (__atomic_load_n (&s_nTail, __ATOMIC_ACQUIRE) != s_nHead)
	{
		if (Ticks () > nLimit)
		{
			// core 2 stopped answering: finish here, without it from now on
			s_bDead = TRUE;
			for (unsigned t = s_nTail; t != s_nHead; t++)
			{
				Process (&s_Ring[t & (RING - 1)]);
			}
			return;
		}
		__asm__ volatile ("wfe");
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
	if (h - __atomic_load_n (&s_nTail, __ATOMIC_ACQUIRE) >= RING)
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
	__atomic_store_n (&s_nHead, h + 1, __ATOMIC_RELEASE);
	__asm__ volatile ("dsb ish; sev" ::: "memory");
}

void TvTakeStats (unsigned *pFilterUs, unsigned *pDrawUs, unsigned *pLines)
{
	u64 f = s_nFilterTicks, d = s_nDrawTicks;
	unsigned n = s_nLines;
	s_nFilterTicks = s_nDrawTicks = 0;
	s_nLines = 0;
	*pFilterUs = (unsigned) (f * 1000000 / Freq ());
	*pDrawUs = (unsigned) (d * 1000000 / Freq ());
	*pLines = n;
}
