/*
 * neo-pi - its look: colours, fonts, logo, credits and the grid (theme.h)
 */
#include "theme.h"
#include "fonts.h"

// the logo, 30x15 drawn twice as big: N "NEO", P "PI", W border, . plate
static const char *const s_Logo[] =
{
	" WWWWWWWWWWWWWWWWWWWWWWWWWWWW ",
	"W............................W",
	"W.NNNNN.NN.NNNNNNNN.NNNNNNNN.W",
	"W.NNNNN.NN.NNNN.....NNNNNNNN.W",
	"W.NNNNNNNN.NNNNNNNN.NNN..NNN.W",
	"W.NN.NNNNN.NNNN.....NNNNNNNN.W",
	"W.NN.NNNNN.NNNNNNNN.NNNNNNNN.W",
	"W............................W",
	"W.PPPPPPPP.PPPP..............W",
	"W.PPPP..PP.PPPP..............W",
	"W.PPPPPPPP.PPPP..............W",
	"W.PPPP.....PPPP..............W",
	"W.PPPP.....PPPP.....WWW..WWW.W",
	"W...................W W..W W.W",
	" WWWWWWWWWWWWWWWWWWWW WWWW WW ",
};

static const char s_LogoKeys[] = "NPW.";
static const u32 s_LogoColors[] = {0xF0C81E, 0x4678FF, 0xC8D2E1, 0x000000};	// yellow = button B, blue a little brighter than D, grey = the panels' frame

static const char *const s_Credits[][2] =
{
	{"CORE",   "GEOLITH - R.CARMICHAEL"},
	{"68000",  "MUSASHI - K.STENERUD"},
	{"Z80",    "N.ALLEMAND, R.CARMICHAEL"},
	{"SOUND",  "YMFM - A.GILES"},
	{"AUDIO",  "SPEEX - J.M.VALIN"},
	{"ZIP",    "MINIZ - R.GELDREICH"},
	{"SYSTEM", "CIRCLE - R.STANGE"},
	{"NTSC",   "BLARGG - S.GREEN"},
	{"FONT",   "ZX ORIGINS - D.GUARD"}
};

// dark blue panels, a blue band
const TUITheme g_Theme =
{
	"NEO-PI",
	0xC8D2E1, 0x5A6478, 0x060A14,		// frame, outline, panel
	0x143264, 0xFF5050,			// band, cursor
	0x78C8FF, 0x78C8FF, 0x96A0AF,		// title, value, dim
	0xFF5050,				// POWER OFF
	g_FontMatinee, g_FontMatinee, TRUE,	// the list: the BIOS's font when it is loaded
	g_FontYouSquared,			// around the picture
	0x5A6478, FALSE,			// the list's bottom row: outline, menu font
	s_Logo, sizeof s_Logo / sizeof s_Logo[0], 2, s_LogoKeys, s_LogoColors,
	s_Credits, sizeof s_Credits / sizeof s_Credits[0],
	{8, 3}, 0x1E386A, 24,			// a frame, the grid lit near it
	{0, 0},
	10					// a fabric of diamonds, through the frame's corners
};

// the grid: a fabric of diamonds, a line every nFabric game pixels along both
// diagonals through the frame's corners, one framebuffer pixel wide (two at
// 720p)
u32 ThemeGrid (int x, int y, int M, int nHalfW, int nHalfH)
{
	(void) nHalfW;				// no dots: nothing to leave out at the edges
	(void) nHalfH;
	const int P = g_Theme.nFabric * M, t = (M + 1) / 2;
	x += P * 64;
	y += P * 64;
	return (x + y) % P < t || (x - y + P * 128) % P < t ? 0x122344 : 0x060E1E;
}

u32 ThemeGround (void)
{
	return 0x060E1E;
}
