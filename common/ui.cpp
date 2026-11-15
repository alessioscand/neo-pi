/*
 * The menus (see ui.h)
 */
#include "ui.h"
#include <circle/util.h>

#define OX	(GAME_SRC_X + (GAME_W - UI_W) / 2)	// the layout in the buffer
#define OY	(GAME_SRC_Y + (GAME_H - UI_H) / 2)
#define ARGB(c)	(0xFF000000u | (c))
#define BUF_PX	(GAME_SRC_PITCH * GAME_SRC_LINES)

#define HINT_TEXT	0xD2D2D2u		// what a button does
#define FOOTER_DIM	60			// % of nDim: the line under a panel
#define TRADEMARK	0x828796u

// "..." in one character (UI_ELLIPSIS)
static const u8 s_Ellipsis[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x00};

CUI::CUI (void)
:	m_pListFont (g_Theme.pListFont),
	m_bListLower (g_Theme.bListLower),
	m_bGrid (FALSE),
	m_nClipX0 (0),
	m_nClipX1 (UI_W)
{
	memset (m_Buf, 0, sizeof m_Buf);
	memset (m_Snapshot, 0, sizeof m_Snapshot);
}

void CUI::SetListFont (const unsigned char (*pFont)[8], boolean bLower)
{
	m_pListFont = pFont;
	m_bListLower = bLower;
}

void CUI::Pixel (int x, int y, u32 c)
{
	if (x >= m_nClipX0 && x < m_nClipX1 && y >= 0 && y < UI_H)
	{
		m_Buf[(y + OY) * GAME_SRC_PITCH + x + OX] = ARGB (c);
	}
}

void CUI::Fill (int x, int y, int w, int h, u32 c)
{
	for (int j = y; j < y + h; j++)
	{
		for (int i = x; i < x + w; i++)
		{
			Pixel (i, j, c);
		}
	}
}

u32 CUI::Dim (u32 c, unsigned nPercent)
{
	return ((((c >> 16) & 0xFF) * nPercent / 100) << 16)
	     | ((((c >> 8) & 0xFF) * nPercent / 100) << 8)
	     | (((c & 0xFF) * nPercent / 100));
}

// ---- OPTIONS > BEZEL -------------------------------------------------------

static const unsigned s_BezelOrder[4] = {0, BEZEL_INFO, BEZEL_GRID, BEZEL_GRID | BEZEL_INFO};

unsigned BezelNext (unsigned nBezel, int nDir)
{
	unsigned i = 0;
	while (i < 3 && s_BezelOrder[i] != nBezel)
	{
		i++;
	}
	return s_BezelOrder[(i + 4 + (unsigned) nDir) % 4];
}

const char *BezelName (unsigned nBezel, boolean bMenu)
{
	switch (nBezel)
	{
	case BEZEL_INFO:		return bMenu ? "INFO" : "info";
	case BEZEL_GRID:		return bMenu ? "GRID" : "grid";
	case BEZEL_GRID | BEZEL_INFO:	return bMenu ? "GRID+INFO" : "gridinfo";
	default:			return bMenu ? "OFF" : "off";
	}
}

// ---- pieces ----------------------------------------------------------------

// the whole picture: the screen's grid shows through (GRID_HOLE), the same
// as in the border
void CUI::Background (void)
{
	for (int y = 0; y < GAME_H; y++)
	{
		u32 *p = &m_Buf[(y + GAME_SRC_Y) * GAME_SRC_PITCH + GAME_SRC_X];
		for (int x = 0; x < GAME_W; x++)
		{
			p[x] = GRID_HOLE;
		}
	}
	m_bGrid = TRUE;
}

// the paused game (darkened) or, with no game loaded, the background
void CUI::Backdrop (boolean bOverGame)
{
	if (bOverGame)
	{
		memcpy (m_Buf, m_Snapshot, sizeof m_Buf);
		m_bGrid = FALSE;
	}
	else
	{
		Background ();
	}
}

void CUI::SnapshotGame (const u32 *pGameBuf)
{
	for (unsigned i = 0; i < BUF_PX; i++)
	{
		m_Snapshot[i] = ARGB (Dim (pGameBuf[i] & 0xFFFFFF, 30));
	}
}

// centred in the band above the game list (y 0-43)
void CUI::Logo (void)
{
	const TUITheme &T = g_Theme;
	const int S = (int) T.nLogoScale;
	const int nW = (int) strlen (T.pLogo[0]) * S, nH = (int) T.nLogoRows * S;
	const int x0 = (UI_W - nW) / 2, y0 = (44 - nH) / 2;
	for (int ly = 0; ly < (int) T.nLogoRows; ly++)
	{
		for (int lx = 0; T.pLogo[ly][lx]; lx++)
		{
			const char *pKey = strchr (T.pLogoKeys, T.pLogo[ly][lx]);
			if (pKey && *pKey)
			{
				Fill (x0 + lx * S, y0 + ly * S, S, S, T.pLogoColors[pKey - T.pLogoKeys]);
			}
		}
	}
}

// a panel: a 2-pixel frame inside an outline
void CUI::Panel (int x0, int y0, int x1, int y1)
{
	Fill (x0, y0, x1 - x0 + 1, y1 - y0 + 1, g_Theme.nFrame2);
	Fill (x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, g_Theme.nFrame1);
	Fill (x0 + 3, y0 + 3, x1 - x0 - 5, y1 - y0 - 5, g_Theme.nFrame2);
	Fill (x0 + 4, y0 + 4, x1 - x0 - 7, y1 - y0 - 7, g_Theme.nPanel);
}

void CUI::Separator (int x0, int x1, int y)
{
	Fill (x0 + 8, y, x1 - x0 - 15, 1, g_Theme.nFrame2);
}

// the menu font (capitals) or the list's; a black shadow under both
void CUI::Text (int x, int y, const char *s, u32 c, unsigned nTall, boolean bList)
{
	for (unsigned i = 0; s[i] != '\0'; i++)
	{
		unsigned ch = (unsigned char) s[i];
		const boolean bEllipsis = ch == (unsigned char) UI_ELLIPSIS[0];
		if (!bEllipsis && (ch < 32 || ch >= 128))
		{
			ch = '?';
		}
		if ((!bList || !m_bListLower) && ch >= 'a' && ch <= 'z')
		{
			ch -= 32;
		}
		const u8 *pGlyph = bEllipsis ? s_Ellipsis
				 : (bList ? m_pListFont[ch - 32] : g_Theme.pMenuFont[ch - 32]);
		for (int pass = 0; pass < 2; pass++)		// shadow, then glyph
		{
			const int d = pass == 0 ? 1 : 0;
			const u32 col = pass == 0 ? 0x000000 : c;
			for (unsigned r = 0; r < 8; r++)
			{
				for (unsigned k = 0; k < 8; k++)
				{
					if (pGlyph[r] & (0x80 >> k))
					{
						for (unsigned t = 0; t < nTall; t++)
						{
							Pixel (x + (int) (i * 8 + k) + d, y + (int) (r * nTall + t) + d, col);
						}
					}
				}
			}
		}
	}
}

void CUI::CText (int y, const char *s, u32 c, unsigned nTall, boolean bList)
{
	Text ((UI_W - (int) strlen (s) * 8) / 2, y, s, c, nTall, bList);
}

// The pad's buttons: a 13 px disc in the button's colour with its letter, as
// on a Neo Geo pad: A red, B yellow, C green, D blue (E violet, F orange)
static const u32 ButtonColor[6] = {0xDC2828, 0xF0C81E, 0x28B446, 0x325AE6, 0x9646D2, 0xF07828};
static const u32 ButtonInk[6]   = {0xFFFFFF, 0x281E00, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
static const char *const ButtonGlyph[6][7] =
{
	{".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
	{"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."},
	{".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."},
	{"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."},
	{"#####", "#....", "#....", "####.", "#....", "#....", "#####"},
	{"#####", "#....", "#....", "####.", "#....", "#....", "#...."}
};

void CUI::ButtonIcon (int x, int y, char cButton)
{
	if (cButton < 'A' || cButton > 'F')
	{
		return;
	}
	const unsigned b = (unsigned) (cButton - 'A');
	static const char *const Disc[13] =
	{
		"....#####....",
		"..##ooooo##..",
		".#ooooooooo#.",
		".#ooooooooo#.",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		"#ooooooooooo#",
		".#ooooooooo#.",
		".#ooooooooo#.",
		"..##ooooo##..",
		"....#####....",
	};
	for (int j = 0; j < 13; j++)
	{
		for (int i = 0; i < 13; i++)
		{
			if (Disc[j][i] != '.')
			{
				Pixel (x + i, y + j, Disc[j][i] == '#' ? 0x000000 : ButtonColor[b]);
			}
		}
	}
	for (int r = 0; r < 7; r++)
	{
		for (int k = 0; k < 5; k++)
		{
			if (ButtonGlyph[b][r][k] == '#')
			{
				Pixel (x + 4 + k, y + 3 + r, ButtonInk[b]);
			}
		}
	}
}

u32 CUI::ButtonColorOf (char cButton)
{
	return cButton >= 'A' && cButton <= 'F' ? ButtonColor[cButton - 'A'] : UI_WHITE;
}

// the hint bar: each button followed by what it does
void CUI::Buttons (int y, const TButtonHint *pHints, unsigned nHints)
{
	const int nGap = 24;
	int nW = 0;
	for (unsigned i = 0; i < nHints; i++)
	{
		nW += 13 + 4 + (int) strlen (pHints[i].pText) * 8 + (i ? nGap : 0);
	}
	int x = (UI_W - nW) / 2;
	for (unsigned i = 0; i < nHints; i++)
	{
		ButtonIcon (x, y, pHints[i].cButton);
		Text (x + 17, y + 3, pHints[i].pText, HINT_TEXT);
		x += 17 + (int) strlen (pHints[i].pText) * 8 + nGap;
	}
}

// ---- the game list ---------------------------------------------------------

// a name longer than nMax characters: cut after the last whole word that
// fits, then the one-character ellipsis
static void CutAtWord (char *pOut, const char *pName, unsigned nMax)
{
	size_t nLen = strlen (pName);
	if (nLen <= nMax)
	{
		memcpy (pOut, pName, nLen + 1);
		return;
	}
	unsigned nCut = nMax - 1;
	while (nCut > 0 && pName[nCut] != ' ')
	{
		nCut--;
	}
	if (nCut == 0)
	{
		nCut = nMax - 1;		// one long word: cut inside it
	}
	while (nCut > 0 && strchr (" -,:", pName[nCut - 1]))
	{
		nCut--;
	}
	memcpy (pOut, pName, nCut);
	pOut[nCut] = UI_ELLIPSIS[0];
	pOut[nCut + 1] = '\0';
}

// how far a title nExtra pixels too long has scrolled after nTicks frames:
// a pause at the start, forward 1 px every 2 frames, a pause, then back
static int ScrollOffset (int nExtra, unsigned nTicks)
{
	const unsigned nPause = 90;
	const unsigned nRun = (unsigned) nExtra * 2;
	unsigned t = nTicks % (2 * (nPause + nRun));
	if (t < nPause)
	{
		return 0;
	}
	t -= nPause;
	if (t < nRun)
	{
		return (int) (t / 2);
	}
	t -= nRun;
	if (t < nPause)
	{
		return nExtra;
	}
	return nExtra - (int) ((t - nPause) / 2);
}

void CUI::DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel,
		    boolean bOverGame, unsigned nTicks)
{
	const TUITheme &T = g_Theme;
	Backdrop (bOverGame);
	Logo ();
	Panel (12, 44, UI_W - 13, 190);
	Fill (17, 104, UI_W - 34, 23, T.nBand);
	Fill (17, 104, UI_W - 34, 1, T.nFrame2);
	Fill (17, 126, UI_W - 34, 1, T.nFrame2);

	// the selected title, twice as tall (a long one scrolls in the band),
	// 4 above and below it, fading away
	const int nChars = 34, nListW = nChars * 8, nListX = (UI_W - nListW) / 2;
	for (int k = -4; k <= 4; k++)
	{
		const int i = (int) nSel + k;
		if (i < 0 || i >= (int) nCount)
		{
			continue;
		}
		const char *pName = pGames[i].Name;
		if (k == 0)
		{
			const int nExtra = (int) strlen (pName) * 8 - nListW;
			if (nExtra <= 0)
			{
				CText (107, pName, UI_WHITE, 2, TRUE);
			}
			else
			{
				m_nClipX0 = nListX;
				m_nClipX1 = nListX + nListW;
				Text (nListX - ScrollOffset (nExtra, nTicks), 107, pName, UI_WHITE, 2, TRUE);
				m_nClipX0 = 0;
				m_nClipX1 = UI_W;
			}
		}
		else
		{
			char Name[sizeof pGames[0].Name + 1];
			CutAtWord (Name, pName, (unsigned) nChars);
			int nFade = 100 - (k < 0 ? -k : k) * 18;
			CText (115 + k * 11 + (k > 0 ? 8 : -8) - 4, Name, Dim (T.nDim, (unsigned) nFade), 1, TRUE);
		}
	}

	// the bottom row: what the selected game is, and where it is in the list
	Fill (17, 174, UI_W - 34, 1, T.nFrame2);
	char Count[16];
	{
		unsigned a = nSel + 1, b = nCount;
		char *p = Count;
		if (a >= 100) *p++ = (char) ('0' + (a / 100) % 10);
		if (a >= 10) *p++ = (char) ('0' + (a / 10) % 10);
		*p++ = (char) ('0' + a % 10);
		*p++ = '/';
		if (b >= 100) *p++ = (char) ('0' + (b / 100) % 10);
		if (b >= 10) *p++ = (char) ('0' + (b / 10) % 10);
		*p++ = (char) ('0' + b % 10);
		*p = '\0';
	}
	const int nCountW = (int) strlen (Count) * 8;
	Text (UI_W - 18 - nCountW, 178, Count, T.nValue, 1, TRUE);
	if (nCount)
	{
		char Info[sizeof pGames[0].Info];
		strncpy (Info, pGames[nSel].Info, sizeof Info - 1);
		Info[sizeof Info - 1] = '\0';
		const size_t nRoom = (size_t) ((UI_W - 36 - nCountW) / 8) - 1;
		if (strlen (Info) > nRoom) Info[nRoom] = '\0';
		Text (18, 178, Info, T.nValue, 1, TRUE);
	}

	static const TButtonHint Hints[2] = {{'A', "START GAME"}, {'D', "OPTIONS"}};
	Buttons (198, Hints, 2);
}

// ---- panels ----------------------------------------------------------------

void CUI::DrawPanelMenu (boolean bOverGame, const char *pTitle,
			 const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			 const char *pMessage, u32 nMessageColor,
			 const TButtonHint *pHints, unsigned nHints, const char *pFooter,
			 boolean bWide, unsigned nRows, boolean bMoreAbove, boolean bMoreBelow)
{
	const TUITheme &T = g_Theme;
	Backdrop (bOverGame);

	const unsigned nSpace = nRows ? nRows : nItems;	// rows the panel is sized for
	const int nStep = nSpace > 8 ? 12 : 15;		// long lists a bit tighter
	const int nInner = 22 + (int) nSpace * nStep + 22;	// title + rows + message line
	int y0 = (190 - nInner) / 2 + 4;
	if (y0 < 4) y0 = 4;
	const int x0 = bWide ? 12 : 44, x1 = bWide ? UI_W - 13 : UI_W - 45;
	Panel (x0, y0, x1, y0 + nInner + 8);

	// 25 characters fit in the panel: a longer title is cut at a word
	char Title[34];
	CutAtWord (Title, pTitle, 25);
	CText (y0 + 10, Title, T.nTitle);
	Separator (x0, x1, y0 + 22);

	for (unsigned n = 0; n < nItems; n++)
	{
		const int y = y0 + 32 + (int) n * nStep;
		const boolean bSel = n == nSel;
		if (bSel)
		{
			Fill (x0 + 8, y - 3, x1 - x0 - 15, nStep > 12 ? 13 : 12, T.nBand);
			for (int t = 0; t < 4; t++)		// the cursor triangle
			{
				Fill (x0 + 12 + t, y + t, 1, 8 - 2 * t, T.nCursor);
			}
		}
		// on the band everything is white (POWER OFF too: the band may be red)
		u32 nColor = pItems[n].bDisabled ? Dim (T.nDim, 55) : (bSel ? UI_WHITE : T.nDim);
		if (   !pItems[n].bDisabled && !bSel
		    && (strcmp (pItems[n].pLabel, "POWER OFF") == 0 || strcmp (pItems[n].pLabel, "CLEAR MEMORY") == 0))
		{
			nColor = T.nPower;
		}
		Text (x0 + 22, y, pItems[n].pLabel, nColor);
		if (pItems[n].pValue)
		{
			const u32 nValColor = pItems[n].bDisabled ? Dim (T.nDim, 45) : (bSel ? UI_WHITE : Dim (T.nDim, 75));
			Text (x1 - 10 - (int) strlen (pItems[n].pValue) * 8, y, pItems[n].pValue, nValColor);
		}
	}

	if (pMessage && pMessage[0])
	{
		Text (x0 + 22, y0 + 32 + (int) nSpace * nStep + 4, pMessage, nMessageColor);
	}

	// more rows out of view: a small arrow on the frame, in the middle of the
	// top (above) or of the bottom (below)
	for (int a = 0; a < 2; a++)
	{
		if (a == 0 ? !bMoreAbove : !bMoreBelow)
		{
			continue;
		}
		const int cx = UI_W / 2, cy = a == 0 ? y0 : y0 + nInner + 4;
		for (int pass = 0; pass < 2; pass++)		// shadow, then arrow
		{
			for (int r = 0; r < 5; r++)
			{
				const int hw = a == 0 ? r : 4 - r;
				Fill (cx - hw + 1 - pass, cy + r + 1 - pass, 2 * hw + 1, 1, pass == 0 ? 0x000000u : T.nValue);
			}
		}
	}

	if (pFooter && pFooter[0])
	{
		char Footer[38];
		strncpy (Footer, pFooter, sizeof Footer - 1);
		Footer[sizeof Footer - 1] = '\0';
		CText (y0 + nInner + 14, Footer, Dim (T.nDim, FOOTER_DIM));
	}

	Buttons (209, pHints, nHints);
}

void CUI::DrawBox (boolean bOverGame, const char *pLine1, const char *pLine2, const TButtonHint *pHint)
{
	Backdrop (bOverGame);
	Panel (64, 80, UI_W - 65, 150);
	if (pLine2 && pLine2[0])
	{
		if (pLine1) CText (92, pLine1, UI_WHITE);
		CText (110, pLine2, g_Theme.nDim);
		if (pHint) Buttons (126, pHint, 1);
	}
	else
	{
		// one line and the button: centred in the box
		if (pLine1) CText (pHint ? 99 : 111, pLine1, UI_WHITE);
		if (pHint) Buttons (117, pHint, 1);
	}
}

void CUI::DrawBlack (void)
{
	for (unsigned i = 0; i < BUF_PX; i++)
	{
		m_Buf[i] = 0xFF000000u;
	}
	m_bGrid = FALSE;
}

void CUI::DrawLoading (const char *pName, unsigned nPercent)
{
	const TUITheme &T = g_Theme;
	Background ();

	// a long title goes on two lines of up to 26 characters, split at a space
	const unsigned nMax = 26;
	char Line1[34], Line2[34];
	Line2[0] = '\0';
	if (strlen (pName) <= nMax)
	{
		strncpy (Line1, pName, sizeof Line1 - 1);
		Line1[sizeof Line1 - 1] = '\0';
	}
	else
	{
		unsigned nCut = nMax;
		while (nCut > 0 && pName[nCut] != ' ')
		{
			nCut--;
		}
		if (nCut == 0)
		{
			nCut = nMax;			// no space: split inside the word
		}
		memcpy (Line1, pName, nCut);
		Line1[nCut] = '\0';
		const char *pRest = pName + nCut;
		while (*pRest == ' ')
		{
			pRest++;
		}
		strncpy (Line2, pRest, nMax);
		Line2[nMax] = '\0';
	}

	const int dy = Line2[0] ? 6 : 0;		// the box grows by 6 lines up and down
	Panel (32, 84 - dy, UI_W - 33, 150 + dy);
	CText (98 - dy, "LOADING", T.nTitle);
	CText (112 - dy, Line1, UI_WHITE);
	if (Line2[0])
	{
		CText (124 - dy, Line2, UI_WHITE);
	}
	const int nBarW = UI_W - 96, yBar = 130 + dy;
	Fill (48, yBar, nBarW, 8, T.nFrame2);
	Fill (49, yBar + 1, nBarW - 2, 6, T.nPanel);
	Fill (49, yBar + 1, (nBarW - 2) * (int) (nPercent > 100 ? 100 : nPercent) / 100, 6, T.nTitle);
}

void CUI::DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2)
{
	Background ();
	Panel (16, 70, UI_W - 17, 160);

	// the lines centred in the panel: 22 px from the title to the first line
	// below it, 16 px between the other two
	int nH = 8;
	if (pLine1) nH += 22;
	if (pLine2) nH += pLine1 ? 16 : 22;
	int y = 115 - nH / 2;
	CText (y, pTitle, UI_ERR);
	y += 22;
	if (pLine1)
	{
		CText (y, pLine1, UI_WHITE);
		y += 16;
	}
	if (pLine2) CText (y, pLine2, g_Theme.nDim);
}

// ---- controls set-up -------------------------------------------------------

void CUI::DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
		       const char *const *pValues, const char *pFooter)
{
	const TUITheme &T = g_Theme;
	Backdrop (bOverGame);

	const int x0 = 44, x1 = UI_W - 45;
	Panel (x0, 12, x1, 196);
	CText (22, pTitle, T.nTitle);
	Separator (x0, x1, 34);

	// nStep is the slot being asked for, in the order of the logical bits
	// (A B C D START SELECT, then E F with 6 buttons), all of them = done;
	// the rows: A B C D (E F) SELECT START, the order they are asked in
	static const char *const Names[8] = {"A", "B", "C", "D", "START", "SELECT", "E", "F"};
	static const char cButton[8] = {'A', 'B', 'C', 'D', 0, 0, 'E', 'F'};
#if PROJECT_BUTTONS == 6
	static const int SlotOfRow[] = {0, 1, 2, 3, 6, 7, 5, 4};
	const int nRowY = 66, nPitch = 14, nPressY = 42;
#else
	static const int SlotOfRow[] = {0, 1, 2, 3, 5, 4};
	const int nRowY = 76, nPitch = 16, nPressY = 46;
#endif
	const int nSlots = (int) (sizeof SlotOfRow / sizeof SlotOfRow[0]);

	// the request, big: PRESS A, PRESS START ...
	if (nStep >= 0 && nStep < nSlots)
	{
		const char *pName = Names[nStep];
		const int x = (UI_W - (6 + (int) strlen (pName)) * 8) / 2;
		Text (x, nPressY, "PRESS ", UI_WHITE, 2);
		Text (x + 48, nPressY, pName, cButton[nStep] ? ButtonColorOf (cButton[nStep]) : T.nValue, 2);
	}
	else
	{
		CText (nPressY, "SAVED", UI_OK, 2);
	}

	for (int r = 0; r < nSlots; r++)
	{
		const int y = nRowY + r * nPitch;
		const int i = SlotOfRow[r];
		const boolean bSel = i == nStep;
		if (bSel)
		{
			Fill (x0 + 8, y - 1, x1 - x0 - 15, nPitch - 1, T.nBand);
		}
		if (cButton[i])
		{
			ButtonIcon (x0 + 20, y, cButton[i]);
		}
		else
		{
			Text (x0 + 20, y + 3, i == 4 ? "START" : "SELECT / COIN", bSel ? UI_WHITE : T.nDim);
		}
		const char *pValue = bSel ? "?" : pValues[i];
		if (pValue && pValue[0])
		{
			Text (x1 - 10 - (int) strlen (pValue) * 8, y + 3, pValue, bSel ? T.nValue : Dim (T.nDim, 85));
		}
	}

	if (pFooter && pFooter[0])
	{
		CText (182, pFooter, Dim (T.nDim, FOOTER_DIM));
	}
}

// ---- credits ---------------------------------------------------------------

void CUI::DrawCredits (const char *pVersion)
{
	const TUITheme &T = g_Theme;
	memcpy (m_Buf, m_Snapshot, sizeof m_Buf);

	const int x0 = 18, x1 = UI_W - 19, y0 = 6;
	Panel (x0, y0, x1, y0 + 187);
	CText (y0 + 10, "CREDITS", T.nTitle);
	Separator (x0, x1, y0 + 22);

	char Title[32];
	strncpy (Title, T.pName, sizeof Title - 1);
	Title[sizeof Title - 1] = '\0';
	strncat (Title, " ", sizeof Title - strlen (Title) - 1);
	strncat (Title, pVersion, sizeof Title - strlen (Title) - 1);
	CText (y0 + 28, Title, UI_WHITE, 2, TRUE);
	CText (y0 + 48, "BY A.SCANDEREBECH", T.nValue);
	Separator (x0, x1, y0 + 62);

	for (unsigned i = 0; i < T.nCredits && i < 10; i++)
	{
		const int y = y0 + 67 + (int) i * 9;
		Text (x0 + 12, y, T.pCredits[i][0], Dim (T.nDim, 80));
		Text (x0 + 12 + 7 * 8, y, T.pCredits[i][1], 0xEBEBEB);
	}
	Separator (x0, x1, y0 + 157);
	CText (y0 + 162, T.pTrademark1, TRADEMARK);
	CText (y0 + 173, T.pTrademark2, TRADEMARK);

	static const TButtonHint Close = {'B', "CLOSE"};
	Buttons (201, &Close, 1);
}
