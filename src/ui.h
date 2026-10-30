/*
 * neo-pi - menus drawn at Neo Geo resolution (304x224) in the same buffer
 * format as the core (320x264, XRGB8888 with R high), so they go through the
 * same scaling and filters as the game.
 */
#ifndef NEOPI_UI_H
#define NEOPI_UI_H

#include <circle/types.h>
#include "video.h"

#define UI_W	GAME_W		// 304
#define UI_H	GAME_H		// 224

// Palette "silver / blue" (0xRRGGBB)
#define UI_BG1		0x060E1Eu
#define UI_BG2		0x0A1830u
#define UI_FRAME1	0xC8D2E1u
#define UI_FRAME2	0x5A6478u
#define UI_PANEL	0x060A14u
#define UI_BAND		0x143264u
#define UI_TITLE	0x78C8FFu
#define UI_VALUE	0x78C8FFu
#define UI_DIM		0x96A0AFu
#define UI_WHITE	0xF8F8F8u
#define UI_CURSOR	0xFF5050u
#define UI_OK		0x6EBE6Eu
#define UI_ERR		0xFF6E6Eu
#define UI_WARN		0xF0C828u
#define UI_GREY		0xA0A0AAu

struct TMenuItem
{
	const char	*pLabel;
	const char	*pValue;	// 0 = none
	boolean		 bDisabled;
};

struct TButtonHint
{
	char		 cButton;	// 'A' 'B' 'C' 'D'
	const char	*pText;
};

struct TGameEntry
{
	char	Name[34];	// from the .neo header
	char	File[64];	// file name in the neopi folder
};

class CUI
{
public:
	CUI (void);

	// sfix.sfix from the user's BIOS: the console's own 8x8 font
	void SetFont (const u8 *pSFix, size_t nSize);

	u32 *GetBuffer (void)	{ return m_Buf; }

	// bOverGame: a game is loaded, show it paused behind the list (SnapshotGame)
	void DrawList (const TGameEntry *pGames, unsigned nCount, unsigned nSel, const char *pMessage,
		       boolean bOverGame);
	// Menu over a darkened copy of the game frame (call SnapshotGame when it opens)
	void SnapshotGame (const u32 *pGameBuf);
	// Generic panel menu: bOverGame = draw over the snapshot, else over the pattern
	void DrawPanelMenu (boolean bOverGame, const char *pTitle,
			    const TMenuItem *pItems, unsigned nItems, unsigned nSel,
			    const char *pMessage, u32 nMessageColor,
			    const TButtonHint *pHints, unsigned nHints, const char *pFooter);
	// Small box drawn over the current buffer contents
	void DrawBox (const char *pLine1, const char *pLine2, const TButtonHint *pHint);
	// Controls set-up: nStep 0-5 = A B C D START SELECT being asked, 6 = saved
	void DrawMapping (boolean bOverGame, const char *pTitle, int nStep,
			  const char *const *pValues, const char *pFooter);
	void DrawLoading (const char *pName, unsigned nPercent);
	// Credits over the darkened game (call SnapshotGame first)
	void DrawCredits (const char *pVersion);
	void DrawMessage (const char *pTitle, const char *pLine1, const char *pLine2);
	void DrawBlack (void);

	static u32 Dim (u32 c, unsigned nPercent);

private:
	void Pixel (int x, int y, u32 c);
	void Fill (int x, int y, int w, int h, u32 c);
	void Background (void);
	void Backdrop (boolean bOverGame);
	void Logo (void);
	void Panel (int x0, int y0, int x1, int y1);
	void Text (int x, int y, const char *s, u32 c, unsigned nTall = 1, boolean bListFont = FALSE);
	void CText (int y, const char *s, u32 c, unsigned nTall = 1, boolean bListFont = FALSE);
	void Buttons (int y, const TButtonHint *pHints, unsigned nHints);
	void ButtonIcon (int x, int y, char cButton);
	static u32 ButtonColorOf (char cButton);

private:
	u32	m_Buf[GAME_SRC_PITCH * GAME_SRC_LINES] __attribute__ ((aligned (64)));
	u32	m_Snapshot[UI_W * UI_H];
	u8	m_Font[128][8];		// list font (sfix.sfix), 1 bit per pixel, bit 7 = left
	boolean	m_bFont;
};

#endif
