/*
 * HDMI output (see video.h)
 */
#include "video.h"
#include "fonts.h"
#include "theme.h"
#include "fbmap.h"
#include <circle/bcmpropertytags.h>
#include <circle/font.h>
#include <circle/util.h>

#define PROPTAG_RELEASE_BUFFER	0x00048001
#define PROPTAG_GET_PIXEL_ORDER	0x00040006	// 0 = BGR, 1 = RGB

#define MAX_LINE	(GAME_W * 8)		// longest scaled line we support

// the border: the theme's grid (ThemeGrid, thin lines at the framebuffer's
// pixels), the same as behind the menus.
// Around a game it gives way to the background colour near the picture:
// nothing within GRID_CLEAR game pixels, then a retro dither (4x4 Bayer)
// up to GRID_FADE, where the grid is whole. With the theme's frame: nothing
// inside it, the grid whole outside, its lines lit near the frame
#define GRID_CLEAR	6
#define GRID_FADE	32
static const u8 s_Bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// the text: the theme's pInfoFont at 2 screen pixels per font pixel, made
// proportional once (each glyph moved to the left edge, the next one a
// pixel after it)
#define TEXT_H		7			// font pixels: capitals and digits
#define TEXT_PITCH	12			// between lines
#define INFO_WHITE	0xECECECu
#define INFO_GREY	0x808A8Fu
#define INFO_NEAR	6			// title and buttons: from the picture
#define SIDE_LINES	8
#define SIDE_CHARS	48
#define BLOCK_H		(SIDE_LINES * TEXT_PITCH * 2 + 4)	// a column's text, 2x

static unsigned char s_InfoFont[96][8];		// pInfoFont, at the left edge
static unsigned char s_InfoWidth[96];		// where the next glyph starts

static void MakeInfoFont (void)
{
	for (unsigned c = 0; c < 96; c++)
	{
		const unsigned char *pGlyph = g_Theme.pInfoFont[c];
		unsigned char nAll = 0;			// every column the glyph uses
		for (unsigned r = 0; r < 8; r++)
		{
			nAll |= pGlyph[r];
		}
		if (nAll == 0)				// the space
		{
			memset (s_InfoFont[c], 0, 8);
			s_InfoWidth[c] = 3;
			continue;
		}
		unsigned nLeft = 0, nRight = 7;
		while (!(nAll & (0x80 >> nLeft))) nLeft++;
		while (!(nAll & (0x80 >> nRight))) nRight--;
		for (unsigned r = 0; r < 8; r++)
		{
			s_InfoFont[c][r] = (unsigned char) (pGlyph[r] << nLeft);
		}
		s_InfoWidth[c] = (unsigned char) (nRight - nLeft + 2);
	}
}

CVideoOut::CVideoOut (void)
:	m_pFrameBuffer (0),
	m_nDispW (0),
	m_nDispH (0),
	m_nWidth (0),
	m_nHeight (0),
	m_nPitchPx (0),
	m_pBase (0),
	m_nBack (1),
	m_bGameMode (FALSE),
	m_bRacing (FALSE),
	m_nTextScale (1),
	m_nScale (1),
	m_nK (1),
	m_nM (1),
	m_nFilter (FILTER_NONE),
	m_nGameX (0),
	m_nGameY (0),
	m_nU (1),
	m_bSide (FALSE),
	m_bBands (FALSE),
	m_nGap (0),
	m_nSideGap (0),
	m_nFrame (0),
	m_nColTop (0),
	m_nColBot (0),
	m_nBezel (0),
	m_nBezelId (0),
	m_bBezelDrawn (FALSE),
	m_pBlock (0),
	m_pGridPic (0),
	m_pLine (0),
	m_pDark (0),

	m_nPixelOrder (1),
	m_nWCPages (0),
	m_bRLow (TRUE)
{
	memset (&m_Info, 0, sizeof m_Info);
	memset (m_Text, 0, sizeof m_Text);
	memset (m_nTextSum, 0, sizeof m_nTextSum);
	MakeInfoFont ();
}

CVideoOut::~CVideoOut (void)
{
	delete m_pFrameBuffer;
	delete [] m_pLine;
	delete [] m_pDark;
	delete [] m_pBlock;
	delete [] m_pGridPic;
}

boolean CVideoOut::Allocate (unsigned nWidth, unsigned nHeight)
{
	if (m_pFrameBuffer != 0)
	{
		CBcmPropertyTags Tags;
		TPropertyTagSimple Release;
		Tags.GetTag (PROPTAG_RELEASE_BUFFER, &Release, sizeof Release);	// may be unsupported
		delete m_pFrameBuffer;
	}

	m_pFrameBuffer = new CBcmFrameBuffer (nWidth, nHeight, 32, nWidth, nHeight * 2, 0, TRUE);
	if (   m_pFrameBuffer == 0
	    || !m_pFrameBuffer->Initialize ())
	{
		return FALSE;
	}

	m_nWidth   = m_pFrameBuffer->GetWidth ();
	m_nHeight  = m_pFrameBuffer->GetHeight ();
	m_nPitchPx = m_pFrameBuffer->GetPitch () / 4;
	m_pBase    = (u32 *) (uintptr) m_pFrameBuffer->GetBuffer ();
	if (   m_pBase == 0
	    || m_nWidth != nWidth || m_nHeight != nHeight
	    || m_pFrameBuffer->GetVirtHeight () < 2 * m_nHeight)
	{
		return FALSE;
	}

	// Byte order of the pixels: QEMU reports RGB (1, red in the low byte),
	// the Pi 4 firmware (framebuffer_swap=1 by default) reports BGR (0, red
	// in the high byte, the same layout as the game's buffer)
	CBcmPropertyTags Tags;
	TPropertyTagSimple Order;
	if (Tags.GetTag (PROPTAG_GET_PIXEL_ORDER, &Order, sizeof Order))
	{
		m_nPixelOrder = Order.nValue;
	}
	m_bRLow = m_nPixelOrder != 0;

	// write-combining: stores merged into bursts instead of one bus write each
	m_nWCPages = FbWriteCombine ((uintptr) m_pBase, (size_t) m_nPitchPx * 4 * m_pFrameBuffer->GetVirtHeight ());

	Clear (0, RGB (0, 0, 0));
	Clear (1, RGB (0, 0, 0));
	ShowBuffer (0);
	return TRUE;
}

boolean CVideoOut::Initialize (void)
{
	// current HDMI mode (chosen by the firmware from config.txt / EDID)
	CBcmPropertyTags Tags;
	TPropertyTagDisplayDimensions Dim;
	if (   Tags.GetTag (PROPTAG_GET_DISPLAY_DIMENSIONS, &Dim, sizeof Dim)
	    && Dim.nWidth >= 640 && Dim.nWidth <= 4096
	    && Dim.nHeight >= 480 && Dim.nHeight <= 2160)
	{
		m_nDispW = Dim.nWidth;
		m_nDispH = Dim.nHeight;
	}
	else
	{
		m_nDispW = 640;
		m_nDispH = 480;
	}

	m_pLine = new u32[MAX_LINE];
	m_pDark = new u32[MAX_LINE];
	m_pBlock = new u32[m_nDispW * BLOCK_H];
	if (m_pLine == 0 || m_pDark == 0 || m_pBlock == 0)
	{
		return FALSE;
	}

	return SetConsoleMode ();
}

boolean CVideoOut::SetConsoleMode (void)
{
	m_bGameMode = FALSE;
	m_bSide = FALSE;
	m_bBezelDrawn = FALSE;
	memset (m_Text, 0, sizeof m_Text);
	memset (m_nTextSum, 0, sizeof m_nTextSum);
	m_nTextScale = m_nDispH >= 900 ? 2 : 1;
	return Allocate (m_nDispW, m_nDispH);
}

boolean CVideoOut::SetGameMode (unsigned nFilter, boolean bAllowHVS)
{
	m_nFilter = nFilter < FILTER_COUNT ? nFilter : FILTER_NONE;


	// total integer scale S
	unsigned sx = m_nDispW / GAME_W, sy = m_nDispH / GAME_H;
	m_nScale = sx < sy ? sx : sy;
	if (m_nScale < 1) m_nScale = 1;
	if (m_nScale > 8) m_nScale = 8;

	// HVS factor k: largest divisor of S that divides the display exactly;
	// with a filter the CPU must draw at least 2x (m >= 2); at most 2, so
	// that the text beside the picture can have 2 screen pixels per font pixel
	m_nK = 1;
	if (bAllowHVS)
	{
		for (unsigned k = m_nScale < 2 ? m_nScale : 2; k >= 2; k--)
		{
			if (   m_nScale % k == 0
			    && m_nDispW % k == 0 && m_nDispH % k == 0
			    && (m_nFilter == FILTER_NONE || m_nScale / k >= 2))
			{
				m_nK = k;
				break;
			}
		}
	}
	m_nM = m_nScale / m_nK;

	unsigned nFbW = m_nDispW / m_nK, nFbH = m_nDispH / m_nK;
	if (!Allocate (nFbW, nFbH))
	{
		// fall back to CPU scaling at the full resolution
		m_nK = 1;
		m_nM = m_nScale;
		if (!Allocate (m_nDispW, m_nDispH))
		{
			return FALSE;
		}
	}

	unsigned nGameW = GAME_W * m_nM, nGameH = GAME_H * m_nM;
	m_nGameX = (m_nWidth - nGameW) / 2;
	m_nGameY = (m_nHeight - nGameH) / 2;

	// text beside the picture: the theme's pInfoFont, 2 screen pixels per font pixel
	m_nU = m_nK >= 2 ? 1 : 2;
	const unsigned U = m_nU, nTextH = TEXT_H * U;
	// the title and the buttons above and below the picture: only in bands
	// at least 3 lines of text tall (1080p, not 720p); the text in the columns
	// then keeps from the picture the distance the title has from it, else
	// 24 screen pixels
	m_bBands = m_nGameY * m_nK >= 3 * TEXT_H * 2;
	m_nGap = m_bBands ? (m_nGameY - nTextH) / 2 : (24 + m_nK - 1) / m_nK;
	// the theme's frame, if any (at most 2 game pixels short of the screen's
	// top and bottom): the columns' text outside it; without one, the text
	// nSideShift game pixels further out
	const unsigned nSize = m_bBands ? 0 : 1;
	m_nFrame = g_Theme.nFrame[nSize];
	const int nRoom = (int) (m_nGameY / m_nM) - 2;
	if (m_nFrame > nRoom) m_nFrame = nRoom > 0 ? nRoom : 0;
	m_nSideGap = m_nFrame > 0 ? (unsigned) (m_nFrame + 1) * m_nM + m_nGap / 2
				  : m_nGap + (unsigned) g_Theme.nSideShift[nSize] * m_nM;

	// the frame's thin lines (t framebuffer pixels in the middle of their game
	// pixel), from the screen's centre; with a fabric the grid counts from the
	// bottom right corner, and the left and top lines go as far out as needed
	// (a pixel or two) to be on the lattice too
	{
		const int M = (int) m_nM, t = (M + 1) / 2;
		m_nFrameR = GAME_W / 2 * M - 1 + m_nFrame * M + (M - t) / 2;
		m_nFrameB = GAME_H / 2 * M - 1 + m_nFrame * M + (M - t) / 2;
		m_nFrameL = m_nFrameR + 1;		// the mirror image
		m_nFrameT = m_nFrameB + 1;
		m_nGridX0 = m_nGridY0 = 0;
		if (m_nFrame > 0 && g_Theme.nFabric > 0)
		{
			const int P = g_Theme.nFabric * M;
			while ((m_nFrameL + m_nFrameR) % P != 0) m_nFrameL++;
			while ((m_nFrameT + m_nFrameB) % P != 0) m_nFrameT++;
			m_nGridX0 = m_nFrameR;
			m_nGridY0 = m_nFrameB;
		}
	}
	m_bSide = m_nGameX >= m_nSideGap + m_nGap + 40 * U;
	if (!m_bSide)
	{
		m_bBands = FALSE;	// the bands hold the status line and the messages
	}
	m_nColTop = m_nGameY > m_nGap ? m_nGameY : m_nGap;
	m_nColBot = m_nGameY + nGameH;
	if (m_nColBot > m_nHeight - m_nGap) m_nColBot = m_nHeight - m_nGap;

	// the grid where the picture goes, for the menus with no game
	delete [] m_pGridPic;
	m_pGridPic = new u32[nGameW * nGameH];
	if (m_pGridPic == 0)
	{
		return FALSE;
	}
	for (unsigned y = 0; y < nGameH; y++)
	{
		for (unsigned x = 0; x < nGameW; x++)
		{
			m_pGridPic[y * nGameW + x] = GridPixel (m_nGameX + x, m_nGameY + y);
		}
	}

	// the border: drawn again by the next SetBezel
	m_bBezelDrawn = FALSE;			// Allocate cleared both buffers
	memset (m_Text, 0, sizeof m_Text);
	memset (m_nTextSum, 0, sizeof m_nTextSum);

	m_bGameMode = TRUE;
	return TRUE;
}

unsigned CVideoOut::GetColumns (void) const
{
	return m_nWidth / (m_Font.GetCharWidth () * m_nTextScale);
}

unsigned CVideoOut::GetRows (void) const
{
	return m_nHeight / (m_Font.GetCharHeight () * m_nTextScale);
}

void CVideoOut::Clear (unsigned nBuffer, u32 nColor)
{
	nColor = Native (nColor);
	for (unsigned y = 0; y < m_nHeight; y++)
	{
		u32 *p = Row (nBuffer, y);
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = nColor;
		}
	}
}

void CVideoOut::ClearRows (unsigned nBuffer, unsigned nFirstRow, unsigned nRows, u32 nColor)
{
	nColor = Native (nColor);
	unsigned nCharH = m_Font.GetCharHeight () * m_nTextScale;
	unsigned y0 = nFirstRow * nCharH;
	unsigned y1 = (nFirstRow + nRows) * nCharH;
	if (y1 > m_nHeight)
	{
		y1 = m_nHeight;
	}
	for (unsigned y = y0; y < y1; y++)
	{
		u32 *p = Row (nBuffer, y);
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			p[x] = nColor;
		}
	}
}

void CVideoOut::DrawChar (unsigned nBuffer, unsigned nX, unsigned nY, char ch, u32 nFG, u32 nBG,
			  unsigned nTextScale, const CCharGenerator &Font)
{
	nFG = Native (nFG);
	nBG = Native (nBG);
	unsigned nCharW = Font.GetCharWidth ();
	unsigned nCharH = Font.GetCharHeight ();
	if (   nX + nCharW * nTextScale > m_nWidth
	    || nY + nCharH * nTextScale > m_nHeight)
	{
		return;
	}

	for (unsigned cy = 0; cy < nCharH; cy++)
	{
		CCharGenerator::TPixelLine Line = Font.GetPixelLine (ch, cy);
		for (unsigned sy = 0; sy < nTextScale; sy++)
		{
			u32 *p = Row (nBuffer, nY + cy * nTextScale + sy) + nX;
			for (unsigned cx = 0; cx < nCharW; cx++)
			{
				u32 nColor = Font.GetPixel (cx, Line) ? nFG : nBG;
				for (unsigned sx = 0; sx < nTextScale; sx++)
				{
					*p++ = nColor;
				}
			}
		}
	}
}

void CVideoOut::DrawText (unsigned nBuffer, unsigned nCol, unsigned nRow, const char *pText,
			  u32 nFG, u32 nBG)
{
	unsigned nCharW = m_Font.GetCharWidth () * m_nTextScale;
	unsigned nCharH = m_Font.GetCharHeight () * m_nTextScale;
	for (unsigned i = 0; pText[i] != '\0' && nCol + i < GetColumns (); i++)
	{
		DrawChar (nBuffer, (nCol + i) * nCharW, nRow * nCharH, pText[i], nFG, nBG,
			  m_nTextScale, m_Font);
	}
}

void CVideoOut::DrawGame (const u32 *pSource, boolean bFilter)
{
	for (unsigned y = 0; y < GAME_H; y++)
	{
		DrawGameLineTo (m_nBack, pSource, y, m_pLine, m_pDark, bFilter);
	}
}

void CVideoOut::DrawGameLineTo (unsigned nBuffer, const u32 *pSource, unsigned y, u32 *pLine, u32 *pDark,
				boolean bFilter)
{
	const unsigned M = m_nM;
	const unsigned nGameW = GAME_W * M;

	// scanlines: bottom rows of each group darker (2 of 4 at 4x, 1 of 2 or 3)
	// (COMPOSITE: none; the game lines come through DrawNtscLine, this draws
	// the menus over the paused game)
	const unsigned nDarkRows = bFilter && m_nFilter != FILTER_NONE && m_nFilter != FILTER_COMPOSITE
				   ? (M >= 4 ? M / 2 : (M >= 2 ? 1 : 0)) : 0;

	// expand one source line horizontally into cached memory, converting
	// the game's XRGB8888 (R high) to the framebuffer order if needed
	const u32 *pSrc = pSource + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
	u32 *pDst = pLine;
	if (m_bRLow)
	{
		for (unsigned x = 0; x < GAME_W; x++)
		{
			u32 p = pSrc[x];
			u32 c = 0xFF000000u | (p & 0x0000FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
			for (unsigned s = 0; s < M; s++)
			{
				*pDst++ = c;
			}
		}
	}
	else
	{
		for (unsigned x = 0; x < GAME_W; x++)
		{
			u32 c = 0xFF000000u | pSrc[x];
			for (unsigned s = 0; s < M; s++)
			{
				*pDst++ = c;
			}
		}
	}

	unsigned nDstY = m_nGameY + y * M;

	if (nDarkRows)
	{
		// shifts and masks only (75% = 50% + 25%)
		const u32 *pL = pLine;
		u32 *pD = pDark;
		switch (m_nFilter)
		{
		case FILTER_SCAN_LIGHT:
			for (unsigned x = 0; x < nGameW; x++)
			{
				u32 c = pL[x];
				pD[x] = 0xFF000000u | (((c >> 1) & 0x007F7F7Fu) + ((c >> 2) & 0x003F3F3Fu));
			}
			break;
		case FILTER_SCAN_STRONG:
			for (unsigned x = 0; x < nGameW; x++)
			{
				pD[x] = 0xFF000000u | ((pL[x] >> 2) & 0x003F3F3Fu);
			}
			break;
		default:
			for (unsigned x = 0; x < nGameW; x++)
			{
				pD[x] = 0xFF000000u | ((pL[x] >> 1) & 0x007F7F7Fu);
			}
			break;
		}
	}
	for (unsigned s = 0; s < M; s++)
	{
		const u32 *pSrcLine = s >= M - nDarkRows ? pDark : pLine;
		u32 *pRow = Row (nBuffer, nDstY + s) + m_nGameX;
		memcpy (pRow, pSrcLine, nGameW * 4);
		if (!bFilter)
		{
			// the menus: where they show the background, the screen's own
			// grid (BEZEL GRID or FULL), which the UI cannot draw; else black
			const u32 *pGrid = m_pGridPic + (y * M + s) * nGameW;
			const boolean bGrid = (m_nBezel & BEZEL_GRID) != 0;
			for (unsigned x = 0; x < GAME_W; x++)
			{
				if (pSrc[x] != GRID_HOLE)
				{
					continue;
				}
				if (bGrid)
				{
					memcpy (pRow + x * M, pGrid + x * M, M * 4);
				}
				else
				{
					for (unsigned i = 0; i < M; i++)
					{
						pRow[x * M + i] = 0xFF000000u;
					}
				}
			}
		}
	}
	FbFlush ();			// the line is in memory before the HVS reads it
}

void CVideoOut::DrawNtscLine (unsigned y, const u32 *pNtsc, unsigned nNtscW, u32 *pLine)
{
	const unsigned M = m_nM;
	const unsigned nGameW = GAME_W * M;
	const unsigned nBuffer = SharedLoad (m_bRacing) ? m_nBack ^ 1 : m_nBack;	// core 2

	// linear interpolation, 16.16: output pixel x samples the filtered line at
	// (x + 0.5) * nNtscW / nGameW - 0.5
	const u32 nStep = (nNtscW << 16) / nGameW;
	s32 nPos = (s32) (nStep / 2) - 0x8000;
	for (unsigned x = 0; x < nGameW; x++, nPos += nStep)
	{
		unsigned i = nPos < 0 ? 0 : (unsigned) nPos >> 16;
		u32 f = nPos < 0 ? 0 : ((unsigned) nPos >> 8) & 0xFF;	// weight of the next pixel
		u32 p = pNtsc[i];
		if (f && i + 1 < nNtscW)
		{
			u32 q = pNtsc[i + 1];
			u32 rb = (((p & 0xFF00FFu) * (256 - f) + (q & 0xFF00FFu) * f) >> 8) & 0xFF00FFu;
			u32 g = (((p & 0x00FF00u) * (256 - f) + (q & 0x00FF00u) * f) >> 8) & 0x00FF00u;
			p = rb | g;
		}
		pLine[x] = m_bRLow ? 0xFF000000u | (p & 0x0000FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16)
				   : 0xFF000000u | p;
	}

	unsigned nDstY = m_nGameY + y * M;
	for (unsigned s = 0; s < M; s++)
	{
		memcpy (Row (nBuffer, nDstY + s) + m_nGameX, pLine, nGameW * 4);
	}
	FbFlush ();			// the line is in memory before the HVS reads it
}

// ---------------------------------------------------------------- the border

// The grid at a framebuffer pixel. It is counted in game pixels from the
// screen's centre, the same towards every side, so that it is symmetric
// around the picture: the centre falls between two framebuffer pixels, and
// the +1 makes the game pixels (M framebuffer pixels) meet there
void CVideoOut::GridAt (unsigned x, unsigned y, int *pGX, int *pGY) const
{
	const unsigned W2 = m_nWidth / 2, H2 = m_nHeight / 2;
	const unsigned cx = x >= W2 ? x - W2 : W2 - 1 - x;
	const unsigned cy = y >= H2 ? y - H2 : H2 - 1 - y;
	*pGX = (int) ((cx + 1) / m_nM);
	*pGY = (int) ((cy + 1) / m_nM);
}

// the theme's grid at a framebuffer pixel: counted from the screen's middle
// without a mirror (a mirror would double the pixel where a line crosses the
// middle), so one pixel off the exact symmetry
u32 CVideoOut::GridColour (unsigned x, unsigned y) const
{
	const int W2 = (int) (m_nWidth / 2), H2 = (int) (m_nHeight / 2);
	return ThemeGrid ((int) x - W2 - m_nGridX0, (int) y - H2 - m_nGridY0, (int) m_nM, W2, H2);
}

u32 CVideoOut::GridPixel (unsigned x, unsigned y) const
{
	const u32 c = GridColour (x, y);
	return Native (RGB ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
}

u32 CVideoOut::BgPixel (unsigned x, unsigned y) const
{
	if (!(m_nBezel & BEZEL_GRID))
	{
		return 0xFF000000u;
	}
	if (m_nBezelId == 0)
	{
		return GridPixel (x, y);			// no game: the grid, whole
	}
	// around a game: how far from the picture, in game pixels
	int gx, gy;
	GridAt (x, y, &gx, &gy);
	int d = gx - GAME_W / 2;
	if (gy - GAME_H / 2 > d) d = gy - GAME_H / 2;
	u32 c;
	if (m_nFrame > 0)
	{
		// where the frame's lines are (framebuffer pixels from the centre)
		const int X = (int) x - (int) (m_nWidth / 2), Y = (int) y - (int) (m_nHeight / 2);
		const int t = ((int) m_nM + 1) / 2;
		const boolean bInX = X > -m_nFrameL - t && X < m_nFrameR + t;
		const boolean bInY = Y > -m_nFrameT - t && Y < m_nFrameB + t;
		const boolean bLine =    (bInY && ((X >= m_nFrameR && X < m_nFrameR + t) || (X <= -m_nFrameL && X > -m_nFrameL - t)))
				      || (bInX && ((Y >= m_nFrameB && Y < m_nFrameB + t) || (Y <= -m_nFrameT && Y > -m_nFrameT - t)));
		if (bLine)
		{
			c = g_Theme.nFrameColour;		// the frame: a thin line
		}
		else if (X > -m_nFrameL && X < m_nFrameR && Y > -m_nFrameT && Y < m_nFrameB)
		{
			c = ThemeGround ();			// inside the frame: empty
		}
		else if (d <= m_nFrame)
		{
			// just outside the line: the grid's lines go on up to it, lit
			c = GridColour (x, y) != ThemeGround () ? g_Theme.nFrameColour : ThemeGround ();
		}
		else
		{
			// the grid, whole; a line near the frame lit: from the frame's
			// colour back to its own, smoothstep (e / G) in integers
			const int e = d - m_nFrame - 1, G = g_Theme.nFrameGlow;
			c = GridColour (x, y);
			if (e >= G || c == ThemeGround ())
			{
				return GridPixel (x, y);
			}
			const int s = e * e * (3 * G - 2 * e), S = G * G * G;
			const u32 f = g_Theme.nFrameColour;
			u32 n = 0;
			for (unsigned sh = 0; sh <= 16; sh += 8)
			{
				const int a = (f >> sh) & 0xFF, b = (c >> sh) & 0xFF;
				n |= (u32) (a + (b - a) * s / S) << sh;
			}
			c = n;
		}
	}
	else if (d >= GRID_FADE)
	{
		return GridPixel (x, y);
	}
	else
	{
		// smoothstep (d / FADE) against the dither's threshold, in integers
		const int v = d * d * (3 * GRID_FADE - 2 * d);
		const int t = (2 * s_Bayer[gy & 3][gx & 3] + 1) * (GRID_FADE * GRID_FADE * GRID_FADE / 32);
		c = d < GRID_CLEAR || v <= t ? ThemeGround () : GridColour (x, y);
	}
	return Native (RGB ((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
}

// the whole border of a buffer (never the picture), then the game's info
void CVideoOut::FillBorder (unsigned nBuf)
{
	const unsigned gx0 = m_nGameX, gx1 = m_nGameX + GAME_W * m_nM;
	const unsigned gy0 = m_nGameY, gy1 = m_nGameY + GAME_H * m_nM;
	for (unsigned y = 0; y < m_nHeight; y++)
	{
		u32 *p = Row (nBuf, y);
		const boolean bGameRow = y >= gy0 && y < gy1;
		for (unsigned x = 0; x < m_nWidth; x++)
		{
			if (bGameRow && x == gx0)
			{
				x = gx1 - 1;
				continue;
			}
			p[x] = BgPixel (x, y);
		}
	}
	m_Text[nBuf][0].x1 = m_Text[nBuf][1].x1 = 0;
	m_nTextSum[nBuf][0] = m_nTextSum[nBuf][1] = 0;
	if ((m_nBezel & BEZEL_INFO) && m_bBands)
	{
		DrawInfoLines (nBuf);
	}
	FbFlush ();
}

void CVideoOut::SetBezel (unsigned nMode, const TBezelInfo *pInfo, unsigned nId)
{
	if (!m_bGameMode || (m_bBezelDrawn && nMode == m_nBezel && nId == m_nBezelId))
	{
		return;
	}
	m_nBezel = nMode;
	m_nBezelId = nId;
	if (pInfo)
	{
		m_Info = *pInfo;
	}
	else
	{
		memset (&m_Info, 0, sizeof m_Info);
	}
	FillBorder (0);
	FillBorder (1);
	m_bBezelDrawn = TRUE;
}

// ---------------------------------------------------------------- the text

unsigned CVideoOut::InfoWidth (const char *pText) const
{
	unsigned n = 0;
	for (const char *s = pText; *s; s++)
	{
		unsigned ch = (unsigned char) *s;
		n += s_InfoWidth[(ch >= 32 && ch < 128 ? ch : '?') - 32];
	}
	return n * m_nU;
}

unsigned CVideoOut::DrawInfoText (const TTarget &T, unsigned x, unsigned y, const char *pText, u32 nColor)
{
	const unsigned U = m_nU;
	const u32 nInk = Native (RGB ((nColor >> 16) & 0xFF, (nColor >> 8) & 0xFF, nColor & 0xFF));
	for (const char *s = pText; *s; s++)
	{
		unsigned ch = (unsigned char) *s;
		if (ch < 32 || ch >= 128) ch = '?';
		const unsigned nAdv = s_InfoWidth[ch - 32] * U;
		if (x + nAdv + U > T.nW || y + 9 * U > T.nH)
		{
			break;
		}
		for (int pass = 0; pass < 2; pass++)		// the shadow, then the glyph
		{
			const u32 c = pass == 0 ? 0xFF000000u : nInk;
			const unsigned o = pass == 0 ? U : 0;
			for (unsigned r = 0; r < 8; r++)
			{
				const unsigned char nRow = s_InfoFont[ch - 32][r];
				for (unsigned k = 0; nRow && k < 8; k++)
				{
					if (!(nRow & (0x80 >> k)))
					{
						continue;
					}
					for (unsigned sy = 0; sy < U; sy++)
					{
						u32 *p = T.pBase + (y + r * U + sy + o) * T.nPitch + x + k * U + o;
						for (unsigned sx = 0; sx < U; sx++)
						{
							p[sx] = c;
						}
					}
				}
			}
		}
		x += nAdv;
	}
	return x;
}

// "A attack   B jump   C magic"; the same name on the buttons next to each
// other once: "A B C rotate"; returns the next x
unsigned CVideoOut::DrawButtons (const TTarget &T, unsigned x, unsigned y)
{
	boolean bFirst = TRUE;
	for (unsigned i = 0; i < 6 && m_Info.pButtons[i]; )
	{
		if (m_Info.pButtons[i][0] == '\0')	// "": a button the game does
		{					// not use, no letter is drawn
			i++;
			continue;
		}
		unsigned j = i + 1;
		while (j < 6 && m_Info.pButtons[j] && strcmp (m_Info.pButtons[j], m_Info.pButtons[i]) == 0)
		{
			j++;
		}
		if (!bFirst)
		{
			x = DrawInfoText (T, x, y, "   ", INFO_GREY);
		}
		bFirst = FALSE;
		for (unsigned b = i; b < j; b++)
		{
			char Letter[3] = {(char) ('A' + b), ' ', '\0'};
			x = DrawInfoText (T, x, y, Letter, INFO_WHITE);
		}
		char Name[32];
		unsigned n = 0;
		for (const char *s = m_Info.pButtons[i]; *s && n < sizeof Name - 1; s++)
		{
			Name[n++] = *s >= 'A' && *s <= 'Z' ? (char) (*s + 32) : *s;
		}
		Name[n] = '\0';
		x = DrawInfoText (T, x, y, Name, INFO_GREY);
		i = j;
	}
	return x;
}

// BEZEL_INFO at 1080p: title, maker and year centred above the picture; the
// buttons below it from its left edge, SELECT coin at its right edge
void CVideoOut::DrawInfoLines (unsigned nBuf)
{
	const TTarget T = {Row (nBuf, 0), m_nPitchPx, m_nWidth, m_nHeight};
	const unsigned U = m_nU, nGameW = GAME_W * m_nM, nGameH = GAME_H * m_nM;
	// near the picture, inside the clear strip before the grid's first line;
	// with a frame, the text and its shadow in the middle of the strip
	unsigned nNear = INFO_NEAR * U, nNearTop = INFO_NEAR * U;
	if (m_nFrame > 0)
	{
		const int M = (int) m_nM, t = (M + 1) / 2;
		const int nStrip = m_nFrame * M + (M - t) / 2, nText = (int) ((TEXT_H + 1) * U);
		const int nGap = nStrip > nText ? (nStrip - nText) / 2 : 0;
		nNear = (unsigned) nGap;			// below: the text first, then its shadow
		nNearTop = (unsigned) (nStrip - nGap - (int) (TEXT_H * U));
	}
	const unsigned nTop = m_nGameY - TEXT_H * U - nNearTop;
	const unsigned nBottom = m_nGameY + nGameH + nNear;

	const boolean bMaker = m_Info.pMaker && m_Info.pMaker[0];
	if (m_Info.pTitle)
	{
		unsigned w = InfoWidth (m_Info.pTitle);
		if (bMaker)
		{
			w += InfoWidth ("  ") + InfoWidth (m_Info.pMaker);
		}
		unsigned x = m_nGameX + (w < nGameW ? (nGameW - w) / 2 : 0);
		x = DrawInfoText (T, x, nTop, m_Info.pTitle, INFO_WHITE);
		if (bMaker)
		{
			x = DrawInfoText (T, x, nTop, "  ", INFO_GREY);
			DrawInfoText (T, x, nTop, m_Info.pMaker, INFO_GREY);
		}
	}

	// SELECT coin only if it stays apart from the buttons (always, today)
	const unsigned x = DrawButtons (T, m_nGameX, nBottom);
	const unsigned nSelect = m_nGameX + nGameW - InfoWidth ("SELECT coin");
	if (nSelect >= x + InfoWidth ("   "))
	{
		const unsigned xs = DrawInfoText (T, nSelect, nBottom, "SELECT", INFO_WHITE);
		DrawInfoText (T, xs, nBottom, " coin", INFO_GREY);
	}
}

void CVideoOut::DrawSideText (boolean bRight, const TTextSpan *pSpans, unsigned nSpans)
{
	if (!m_bGameMode)
	{
		return;
	}
	const unsigned U = m_nU;
	const unsigned nCol = bRight ? 1 : 0;
	const unsigned nBuf = SharedLoad (m_bRacing) ? m_nBack ^ 1 : m_nBack;

	// what it says (0 = nothing): unchanged, nothing to do
	u32 nSum = 0;
	if (nSpans)
	{
		nSum = 2166136261u;
		for (unsigned i = 0; i < nSpans; i++)
		{
			nSum = (nSum ^ pSpans[i].nColor) * 16777619u;
			for (const char *s = pSpans[i].pText; *s; s++)
			{
				nSum = (nSum ^ (unsigned char) *s) * 16777619u;
			}
		}
		nSum |= 1;
	}
	if (nSum == m_nTextSum[nBuf][nCol])
	{
		return;
	}
	m_nTextSum[nBuf][nCol] = nSum;

	// the lines, character by character with their colours
	static char L[SIDE_LINES][SIDE_CHARS + 1];
	static u32 C[SIDE_LINES][SIDE_CHARS];
	unsigned nLen[SIDE_LINES] = {0};
	unsigned nLines = nSpans ? 1 : 0;
	for (unsigned i = 0; i < nSpans; i++)
	{
		for (const char *s = pSpans[i].pText; *s; s++)
		{
			unsigned l = nLines - 1;
			if (*s == '\n' && m_bSide)
			{
				if (nLines < SIDE_LINES) nLines++;
				continue;
			}
			if (*s == '\n')			// the bands: one line
			{
				for (unsigned k = 0; k < 2 && nLen[l] < SIDE_CHARS; k++)
				{
					L[l][nLen[l]] = ' ';
					C[l][nLen[l]++] = pSpans[i].nColor;
				}
				continue;
			}
			if (nLen[l] < SIDE_CHARS)
			{
				L[l][nLen[l]] = *s;
				C[l][nLen[l]++] = pSpans[i].nColor;
			}
		}
	}
	for (unsigned l = 0; l < nLines; l++)
	{
		L[l][nLen[l]] = '\0';
	}

	// too wide for the column: wrap at the last space that fits
	const unsigned nGameW = GAME_W * m_nM, nGameH = GAME_H * m_nM;
	const unsigned nMaxW = m_bSide ? m_nGameX - m_nSideGap - m_nGap : m_nWidth - 2 * m_nGap;
	for (unsigned l = 0; m_bSide && l < nLines && nLines < SIDE_LINES; l++)
	{
		if (InfoWidth (L[l]) <= nMaxW)
		{
			continue;
		}
		int nCut = -1;
		for (int k = (int) nLen[l] - 1; k > 0 && nCut < 0; k--)
		{
			if (L[l][k] == ' ')
			{
				L[l][k] = '\0';
				if (InfoWidth (L[l]) <= nMaxW)
				{
					nCut = k;
				}
				L[l][k] = ' ';
			}
		}
		if (nCut < 0)
		{
			continue;			// one long word: cut by the screen edge
		}
		for (unsigned m = nLines; m > l + 1; m--)
		{
			memcpy (L[m], L[m - 1], sizeof L[0]);
			memcpy (C[m], C[m - 1], sizeof C[0]);
			nLen[m] = nLen[m - 1];
		}
		nLines++;
		unsigned nRest = nLen[l] - (unsigned) nCut - 1;
		memmove (L[l + 1], &L[l][nCut + 1], nRest + 1);
		memmove (C[l + 1], &C[l][nCut + 1], nRest * sizeof (u32));
		nLen[l + 1] = nRest;
		L[l][nCut] = '\0';
		nLen[l] = (unsigned) nCut;
	}

	// where: the lines' box
	const unsigned nPitch = TEXT_PITCH * U;
	const unsigned nTextH = nLines * nPitch - (TEXT_PITCH - TEXT_H) * U;
	unsigned y0;
	if (m_bSide)
	{
		y0 = bRight ? m_nColTop : (m_nColBot > nTextH ? m_nColBot - nTextH : 0);
	}
	else if (bRight)
	{
		y0 = m_nGameY > TEXT_H * U ? (m_nGameY - TEXT_H * U) / 2 : 0;
	}
	else
	{
		unsigned nBelow = m_nHeight - m_nGameY - nGameH;
		y0 = m_nGameY + nGameH + (nBelow > TEXT_H * U ? (nBelow - TEXT_H * U) / 2 : 0);
	}
	unsigned nX[SIDE_LINES];
	TRect New = {m_nWidth, y0, 0, y0 + nLines * nPitch};
	for (unsigned l = 0; l < nLines; l++)
	{
		const unsigned w = InfoWidth (L[l]);
		if (!m_bSide)		nX[l] = w < m_nWidth ? (m_nWidth - w) / 2 : 0;
		else if (bRight)	nX[l] = m_nGameX + nGameW + m_nSideGap;
		else			nX[l] = m_nGameX - m_nSideGap > w ? m_nGameX - m_nSideGap - w : 0;
		if (nX[l] < New.x0) New.x0 = nX[l];
		if (nX[l] + w + U > New.x1) New.x1 = nX[l] + w + U;
	}
	if (New.x1 > m_nWidth) New.x1 = m_nWidth;
	if (New.y1 > m_nHeight) New.y1 = m_nHeight;

	// the block to copy: the old box and the new one together
	TRect &Old = m_Text[nBuf][nCol];
	TRect B = New;
	if (nLines == 0)
	{
		B = Old;
	}
	else if (Old.x1)
	{
		if (Old.x0 < B.x0) B.x0 = Old.x0;
		if (Old.y0 < B.y0) B.y0 = Old.y0;
		if (Old.x1 > B.x1) B.x1 = Old.x1;
		if (Old.y1 > B.y1) B.y1 = Old.y1;
	}
	Old = nLines ? New : TRect {0, 0, 0, 0};
	if (B.x1 <= B.x0 || B.y1 <= B.y0)
	{
		return;
	}
	if (B.y1 - B.y0 > BLOCK_H) B.y1 = B.y0 + BLOCK_H;

	// prepared apart: the border, then the text
	const unsigned nBW = B.x1 - B.x0, nBH = B.y1 - B.y0;
	for (unsigned y = 0; y < nBH; y++)
	{
		u32 *p = m_pBlock + y * nBW;
		for (unsigned x = 0; x < nBW; x++)
		{
			p[x] = BgPixel (B.x0 + x, B.y0 + y);
		}
	}
	const TTarget T = {m_pBlock, nBW, nBW, nBH};
	for (unsigned l = 0; l < nLines; l++)
	{
		unsigned x = nX[l] - B.x0;
		const unsigned y = y0 + l * nPitch - B.y0;
		for (unsigned k = 0; k < nLen[l]; k++)
		{
			char One[2] = {L[l][k], '\0'};
			x = DrawInfoText (T, x, y, One, C[l][k]);
		}
	}

	// then copied, a finished row at a time (never over the picture)
	const unsigned gx0 = m_nGameX, gx1 = m_nGameX + nGameW;
	const unsigned gy0 = m_nGameY, gy1 = m_nGameY + nGameH;
	for (unsigned y = 0; y < nBH; y++)
	{
		const unsigned yy = B.y0 + y;
		unsigned x0 = B.x0, x1 = B.x1;
		if (yy >= gy0 && yy < gy1)
		{
			if (x0 < gx1 && x1 > gx0)
			{
				continue;		// cannot happen: the columns are beside it
			}
		}
		memcpy (Row (nBuf, yy) + x0, m_pBlock + y * nBW, (x1 - x0) * 4);
	}
	FbFlush ();
}

void CVideoOut::ShowBuffer (unsigned nBuffer)
{
	m_pFrameBuffer->SetVirtualOffset (0, nBuffer * m_nHeight);
	m_nBack = nBuffer ^ 1;
}

boolean CVideoOut::Flip (void)
{
	boolean bOK = m_pFrameBuffer->SetVirtualOffset (0, m_nBack * m_nHeight);
	m_nBack ^= 1;
	return bOK;
}

boolean CVideoOut::WaitForVSync (void)
{
	return m_pFrameBuffer->WaitForVerticalSync ();
}
