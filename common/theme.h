/*
 * What makes the menus and the border one project's own: colours, fonts,
 * logo, credits and the background ("grid"). Each project defines them in
 * its src/theme.cpp; common/ui.cpp and common/video.cpp use them.
 */
#ifndef COMMON_THEME_H
#define COMMON_THEME_H

#include <circle/types.h>

struct TUITheme
{
	const char	*pName;			// "S16-PI": the credits' title

	// colours, 0xRRGGBB
	u32		 nFrame1;		// the panels' frame
	u32		 nFrame2;		// its outline, the separators
	u32		 nPanel;		// inside a panel
	u32		 nBand;			// the selected line
	u32		 nCursor;		// the triangle on it
	u32		 nTitle;		// panel titles, LOADING, the bar
	u32		 nValue;		// values, the scroll arrows, the author
	u32		 nDim;			// labels, names not selected
	u32		 nPower;		// POWER OFF, CLEAR MEMORY

	// fonts: 96 characters from 0x20, 8x8 (fonts.h)
	const unsigned char (*pMenuFont)[8];
	const unsigned char (*pListFont)[8];	// the game list's titles
	boolean		 bListLower;		// it has lower case (else capitals)
	const unsigned char (*pInfoFont)[8];	// around the picture: title,
						// buttons, status line, messages

	// the game list's bottom row (set and region or maker, the counter)
	u32		 nListInfo;		// its colour
	boolean		 bListInfoInListFont;	// in pListFont (else pMenuFont)

	// the logo above the game list: one string per row, one character per
	// pixel; pLogoKeys[i] is drawn in pLogoColors[i], anything else is clear
	const char *const *pLogo;
	unsigned	 nLogoRows;
	unsigned	 nLogoScale;		// 1 or 2
	const char	*pLogoKeys;
	const u32	*pLogoColors;

	// credits: up to 10 rows "WHAT", "WHO" (the trademarks: README, LICENSES)
	const char *const (*pCredits)[2];
	unsigned	 nCredits;

	// around a game, optional: [0] with the bands (1080p), [1] without (720p)
	// - nFrame: a frame line nFrame game pixels from the picture, holding the
	//   title and the buttons (0: none, the grid's dither as usual); the grid
	//   then whole, its lines lit near the frame: nFrameColour on it, back to
	//   their own colour nFrameGlow game pixels away
	// - nSideShift: the side columns' text this many game pixels further
	//   from the picture (or from the frame)
	int		 nFrame[2];
	u32		 nFrameColour;
	int		 nFrameGlow;
	int		 nSideShift[2];
	// the grid as a fabric (0: counted from the screen's centre): its step in
	// game pixels, its lines through the frame's corners (ThemeGrid gets x, y
	// from the bottom right one); the frame then a pixel wider at the left and
	// the top if needed, so that all four corners are on the lattice
	int		 nFabric;
	// the background in game pixels (FALSE: thin lines at the framebuffer's
	// pixels): its lines a game pixel wide, its steps by whole game pixels
	boolean		 bPixel;
};

extern const TUITheme g_Theme;

// The background (the menus' and the border's "grid"), 0xRRGGBB, at a
// point x, y counted from the screen's centre (signed), with M points to a
// game pixel (with bPixel the points are game pixels, M 1): thin lines, a
// point or two wide, spaced in game pixels; nHalfW, nHalfH: half the screen, to leave out
// what the screen's edge would cut; with nFabric, x and y from the frame's
// bottom right corner instead; ThemeGround: its empty colour
u32 ThemeGrid (int x, int y, int M, int nHalfW, int nHalfH);
u32 ThemeGround (void);

#endif
