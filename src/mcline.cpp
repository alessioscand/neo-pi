/*
 * neo-pi - the video chip's lines drawn on core 1 (see mcline.h)
 */
#include "mcline.h"

extern "C" {
int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);

// added to the core by patches/geolith-mc.py
extern unsigned geo_lspc_mc_enabled;
extern volatile unsigned geo_lspc_mc_alive;
extern unsigned geo_lspc_mc_failed;
extern unsigned geo_lspc_mc_fail_head, geo_lspc_mc_fail_tail, geo_lspc_mc_fail_stage;
extern unsigned geo_lspc_mc_fail_line, geo_lspc_mc_fail_lines, geo_lspc_mc_fail_beatmoved;
extern unsigned long long geo_lspc_mc_freq;
extern void (*geo_lspc_mc_linecb) (unsigned nScanline);
void geo_lspc_mc_worker (volatile unsigned *pStop);
void geo_lspc_mc_sync (void);
}

void McLinesOut (TMcLineDone *pDone)
{
	geo_lspc_mc_linecb = pDone;
	__asm__ volatile ("dsb ish" ::: "memory");
}

TMcLineDone *McLinesOutTo (void)
{
	return geo_lspc_mc_linecb;
}

void McEnable (boolean bOn)
{
	geo_lspc_mc_enabled = bOn ? 1 : 0;
}

boolean McEnabled (void)
{
	return geo_lspc_mc_enabled != 0;
}

boolean McFailed (void)
{
	return geo_lspc_mc_failed != 0;
}

boolean McAlive (void)
{
	return geo_lspc_mc_alive != 0;
}

void McSync (void)
{
	geo_lspc_mc_sync ();
}

void McWorker (void)
{
	static volatile unsigned nStop = 0;		// never set: core 1 runs until power off
	geo_lspc_mc_worker (&nStop);
}

void McFailText (char *pOut, size_t nSize, unsigned nFrame)
{
	snprintf (pOut, nSize, "  core 1 stuck at frame %u: queue %u/%u, %s line %u, "
		  "%u lines, beat %s, %llu Hz",
		  nFrame, geo_lspc_mc_fail_tail, geo_lspc_mc_fail_head,
		  geo_lspc_mc_fail_stage ? "drawing" : "waiting after", geo_lspc_mc_fail_line,
		  geo_lspc_mc_fail_lines, geo_lspc_mc_fail_beatmoved ? "moving" : "stopped",
		  geo_lspc_mc_freq);
}
