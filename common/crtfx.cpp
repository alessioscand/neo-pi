//
// crtfx.cpp - TEST: CPU ports of libretro CRT shaders (see crtfx.h)
//
// Sources: libretro/slang-shaders crt/shaders/{fakelottes, crt-blurPi,
// crt-easymode, crt-aperture, hyllian/crt-hyllian-fast, crt-Ntap/crt-1tap}.slang,
// ntsc/shaders/ntsc-simple/ntsc_simple.slang, default
// parameters, curvature off. At an integer scale every weight depends only on
// the sub-column or sub-row: they are worked out once per scale.
//
#include "crtfx.h"
#include "screen.h"
#include "smp.h"
#include <math.h>

#define MAXM	8
#define MAXOW	(GAME_W * MAXM)
#define SLOTS	4
#define ARRS	6
#define TABN	16384
#define NT_TAPS	11
#define NT_PAD	5
#define NT_W	(2 * GAME_W)
#define NT_PH	(NT_W + GAME_H + 2 * NT_PAD)

static unsigned s_nFx, s_M, s_OW;
static unsigned s_rs, s_bs;
static float s_U8[256];			// 0..1
static float s_Lin[256];		// per shader: input gamma
static float s_Tab[TABN];		// per shader: a power curve, s_TabMax
static float s_TabMax;
static u8 s_Out[TABN];			// per shader: output gamma, 0..s_OutMax
static float s_OutMax;

// per sub-column (whole row of screen pixels, OW)
static short s_Tap[MAXOW];		// first source pixel
static float s_W[4][MAXOW];		// weights
static short s_Tap2[MAXOW];		// a second filter (aperture: sharp)
static float s_W2[4][MAXOW];
static short s_Tap3[MAXOW];		// a third (aperture: glow)
static float s_W3[3][MAXOW > 4096 ? MAXOW : 4096];
static float s_Mask[3][MAXOW];
// per sub-row
static int   s_RowLine[MAXM];		// first line used, relative to L
static float s_RowF[MAXM];		// weight of the second line
static int   s_RowLine2[MAXM];
static float s_RowF2[MAXM];
static float s_RowK[MAXM];		// scanline factor
static float s_RowW[3][MAXM];		// aperture: glow, 3 lines

struct TSlot
{
	int nLine;
	float *pA[ARRS];
	u16 *pQ[ARRS];
};
static TSlot s_Slot[SLOTS];
static unsigned s_nNext;
static float *s_pTmp[8];

// fixed point helpers (per shader)
static u16 s_T12[4096];			// a curve on 12 bits, 0..65535
static u32 s_RowQ[MAXM][8];		// per row factors

static u32 s_O[3][MAXOW];		// one row, per channel
static float s_FF[MAXM][2][3][MAXOW];	// fakelottes: mask x scanline ^ (1 / 2.2) x 255
static u8 s_Lit[MAXOW];
static u16 s_MQ[3][MAXOW];		// hyllian: mask ^ (1 / 2.2), 8.8			// easymode: lit channel

static u64 s_nSum, s_nMax;
static unsigned s_nCalls;

static float Clamp (float v, float a, float b)	{ return v < a ? a : (v > b ? b : v); }

static void Tables (float fGammaIn, float fTabPow, float fTabMax, float fOutPow, float fOutMul, float fOutMax)
{
	for (unsigned i = 0; i < 256; i++)
	{
		s_U8[i] = i / 255.0f;
		s_Lin[i] = powf (i / 255.0f, fGammaIn);
	}
	s_TabMax = fTabMax;
	for (unsigned i = 0; i < TABN; i++)
	{
		s_Tab[i] = powf (i * fTabMax / (TABN - 1), fTabPow);
	}
	s_OutMax = fOutMax;
	for (unsigned i = 0; i < TABN; i++)
	{
		float v = powf (i * fOutMax / (TABN - 1), fOutPow) * fOutMul * 255.0f + 0.5f;
		s_Out[i] = (u8) (v > 255.0f ? 255 : v);
	}
}

static inline float Pow (float v)
{
	if (v <= 0.0f) return 0.0f;
	if (v >= s_TabMax) v = s_TabMax;
	return s_Tab[(unsigned) (v * ((TABN - 1) / s_TabMax))];
}

static inline u32 Out (float r, float g, float b)
{
	const float k = (TABN - 1) / s_OutMax;
	unsigned ir = r <= 0 ? 0 : (r >= s_OutMax ? TABN - 1 : (unsigned) (r * k));
	unsigned ig = g <= 0 ? 0 : (g >= s_OutMax ? TABN - 1 : (unsigned) (g * k));
	unsigned ib = b <= 0 ? 0 : (b >= s_OutMax ? TABN - 1 : (unsigned) (b * k));
	return 0xFF000000u | (u32) s_Out[ir] << s_rs | (u32) s_Out[ig] << 8 | (u32) s_Out[ib] << s_bs;
}

static float Lanczos2 (float x)		// 2 sin(pi x) sin(pi x / 2) / (pi x)^2
{
	float c = 3.141592653589f * x;
	c = fabsf (c) < 1e-5f ? 1e-5f : fabsf (c);
	return 2.0f * sinf (c) * sinf (c * 0.5f) / (c * c);
}

static float Curve (float x, float sharp)	// crt-easymode's curve_distance
{
	float xs = x >= 0.5f ? 1.0f : 0.0f;
	float d = 0.25f - (x - xs) * (x - xs);
	float c = 0.5f - sqrtf (d > 0 ? d : 0) * (x < 0.5f ? 1.0f : (x > 0.5f ? -1.0f : 0.0f));
	return x + (c - x) * sharp;
}


// ---- setup -------------------------------------------------------------------

void CrtFxSetup (unsigned nFx, unsigned M, boolean bRLow)
{
	if (M > MAXM) M = MAXM;
	if (M < 2) nFx = CRTFX_NONE;
	s_rs = bRLow ? 0 : 16;
	s_bs = bRLow ? 16 : 0;
	if (nFx == s_nFx && M == s_M)
	{
		return;
	}
	if (!s_Slot[0].pA[0])
	{
		for (unsigned i = 0; i < SLOTS; i++)
			for (unsigned a = 0; a < ARRS; a++)
			{
				s_Slot[i].pA[a] = new float[MAXOW];
				s_Slot[i].pQ[a] = new u16[MAXOW];
			}
		for (unsigned a = 0; a < 8; a++)
			s_pTmp[a] = new float[MAXOW];
	}
	SharedStore (s_nFx, (unsigned) CRTFX_NONE);
	s_M = M;
	s_OW = GAME_W * M;
	const unsigned OW = s_OW;
	for (unsigned i = 0; i < SLOTS; i++) s_Slot[i].nLine = -100;

	for (unsigned j = 0; j < OW; j++)
		for (unsigned c = 0; c < 3; c++)
			s_Mask[c][j] = 1.0f;

	switch (nFx)
	{
	case CRTFX_HYLLIAN:
		// MASK_INTENSITY 0.5, gamma 2.4 / 2.2, BRIGHTBOOST 1.5, SCANLINES 0.72
		Tables (1.0f, 2.4f, 1.25f, 1.0f / 2.2f, 1.0f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float sx = (j + 0.5f) / M - 0.49999f;
			float b = floorf (sx), f = sx - b;
			s_Tap[j] = (short) (b - 1);
			float f2 = f * f, f3 = f2 * f;		// Catmull-Rom
			s_W[0][j] = -0.5f * f3 + f2 - 0.5f * f;
			s_W[1][j] = 1.5f * f3 - 2.5f * f2 + 1.0f;
			s_W[2][j] = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
			s_W[3][j] = 0.5f * f3 - 0.5f * f2;
			int mf = (int) floorf (j + 0.5f - 0.49999f * M);
			boolean odd = (mf & 1) != 0;
			s_Mask[0][j] = s_Mask[2][j] = odd ? 0.5f : 1.0f;
			s_Mask[1][j] = odd ? 1.0f : 0.5f;
			for (unsigned c = 0; c < 3; c++)
				s_MQ[c][j] = s_Mask[c][j] < 1.0f ? 187 : 256;	// 0.5 ^ (1 / 2.2)
		}
		for (unsigned s = 0; s < M; s++)
		{
			float fy = (s + 0.5f) / M;
			float d1 = Clamp (1.5f - 0.72f - fabsf (fy - 0.5f), 0.0f, 1.0f);
			s_RowK[s] = d1 * d1 * (3.0f + 1.5f - 2.0f * d1);
			s_RowLine[s] = 0;
			s_RowQ[s][0] = (u32) (powf (s_RowK[s], 1.0f / 2.2f) * 255.0f + 0.5f);
		}
		for (unsigned i = 0; i < 4096; i++)		// v ^ 2.4 ^ (1 / 2.2)
			s_T12[i] = (u16) (powf (i / 4095.0f, 2.4f / 2.2f) * 65535.0f + 0.5f);
		break;

	case CRTFX_EASYMODE:
		// dilation on (c * c), gamma 2.0 / (1 + 1) = 1, output 1 / 1.8 x 1.2,
		// mask 0.3 RGB, sharpness H 0.5, V 1.0, beam 1.5, brightness 0.35..0.65
		Tables (2.0f, 1.0f, 1.0f, 1.0f / 1.8f, 1.2f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float px = (j + 0.5f) / M - 0.5f;
			float b = floorf (px), d = px - b;
			s_Tap[j] = (short) (b - 1);
			float cx = Curve (d, 0.25f);
			float w0 = Lanczos2 (1.0f + cx), w1 = Lanczos2 (cx), w2 = Lanczos2 (1.0f - cx), w3 = Lanczos2 (2.0f - cx);
			float n = w0 + w1 + w2 + w3;
			s_W[0][j] = w0 / n; s_W[1][j] = w1 / n; s_W[2][j] = w2 / n; s_W[3][j] = w3 / n;
			unsigned dot = j % 3;
			s_Lit[j] = (u8) dot;
			for (unsigned c = 0; c < 3; c++) s_Mask[c][j] = c == dot ? 1.0f : 0.7f;
		}
		for (unsigned s = 0; s < M; s++)
		{
			float py = (s + 0.5f) / M - 0.5f;
			float b = floorf (py);
			s_RowLine[s] = (int) b;
			s_RowF[s] = Curve (py - b, 1.0f);
			float cs = cosf (2.0f * 3.141592653589f * (s + 0.5f) / M) * 0.5f + 0.5f;
			s_RowK[s] = 1.0f - powf (cs, 1.5f);
			s_RowQ[s][0] = (u32) (s_RowF[s] * 256.0f + 0.5f);

		}
		break;

	case CRTFX_APERTURE:
		// gamma 2.4 / 2.4, brightness 1.5, glow 0.5 x 0.5, halation 0.1,
		// diffusion 0.05, mask 2 colours 0.3, beam 0.5..1.5 shape 2.5, sharp 1 / 3
		Tables (2.4f, 1.0f / 2.5f, 1.0f, 1.0f / 2.4f, 1.0f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float pcx = (j + 0.5f) / M;
			// soft: Lanczos at the source's pixels, 2 taps (pairs summed)
			float p = pcx - 0.5f, b = floorf (p), d = p - b;
			s_Tap[j] = (short) b;
			float w0 = Lanczos2 (d + 1), w1 = Lanczos2 (d), w2 = Lanczos2 (d - 1), w3 = Lanczos2 (d - 2);
			float n = w0 + w1 + w2 + w3;
			s_W[0][j] = (w0 + w1) / n; s_W[1][j] = (w2 + w3) / n;
			// sharp: the same at 3x, nearest source pixels
			p = pcx * 3.0f - 0.5f; b = floorf (p); d = p - b;
			int t1 = (int) floorf ((b + 0.5f) / 3.0f), t2 = (int) floorf ((b + 1.5f) / 3.0f);
			s_Tap2[j] = (short) t1;
			w0 = Lanczos2 (d + 1); w1 = Lanczos2 (d); w2 = Lanczos2 (d - 1); w3 = Lanczos2 (d - 2);
			n = w0 + w1 + w2 + w3;
			s_W2[0][j] = (w0 + w1) / n; s_W2[1][j] = (w2 + w3) / n;
			s_W2[2][j] = (float) (t2 - t1);			// 0 or 1
			// glow: gaussian over 3 pixels
			float dist = -(pcx - floorf (pcx) - 0.5f);
			s_Tap3[j] = (short) (floorf (pcx) - 1);
			float g[3], gs = 0;
			for (int k = 0; k < 3; k++) { float x = (dist + k - 1) / 0.5f; g[k] = exp2f (-x * x); gs += g[k]; }
			for (int k = 0; k < 3; k++) s_W3[k][j] = g[k] / gs;
			boolean i0 = (j % 2) == 0;		// (1,0,1) / (0,1,0) at 0.3: x1.3 / x0.7
			s_Mask[0][j] = s_Mask[2][j] = i0 ? 1.3f : 0.7f;
			s_Mask[1][j] = i0 ? 0.7f : 1.3f;
		}
		for (unsigned s = 0; s < M; s++)
		{
			float off = (M % 2) ? 0.0f : 0.5f / M;
			float cvh = (s + 0.5f) / M - off;		// in L's line
			float b = floorf (cvh), fr = cvh - b;
			s_RowLine[s] = (int) b;
			s_RowK[s] = fabsf (2.0f * fr - 1.0f);		// |2 xx - 1|
			float dist = -(fr - 0.5f), g[3], gs = 0;
			for (int k = 0; k < 3; k++) { float x = (dist + k - 1) / 0.5f; g[k] = exp2f (-x * x); gs += g[k]; }
			for (int k = 0; k < 3; k++) s_RowW[k][s] = g[k] / gs;
		}
		break;

	case CRTFX_FAKELOTTES:
		// shadowMask 1, maskDark 0.5, maskLight 1.5, gamma 2.5 / 2.2,
		// scanlines 0.95 + 0.40 sin
		Tables (1.0f, 2.5f, 1.0f, 1.0f / 2.2f, 1.0f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float x = (j + 0.5f) / M - 0.5f, b = floorf (x);
			s_Tap[j] = (short) b;
			s_W[1][j] = x - b;
		}
		for (unsigned s = 0; s < M; s++)
		{
			float y = (s + 0.5f) / M - 0.5f, b = floorf (y);
			s_RowLine[s] = (int) b;
			s_RowF[s] = y - b;
			s_RowK[s] = 0.95f + 0.40f * sinf (2.0f * 3.141592653589f * ((s + 0.5f) / M - 0.25f));
			s_RowQ[s][0] = (u32) (s_RowF[s] * 256.0f + 0.5f);
			for (unsigned d = 0; d < 2; d++)
				for (unsigned l = 0; l < 2; l++)
				{
					float v = (d ? 0.5f : 1.5f) * (l ? 1.5f : 0.5f) * s_RowK[s];
					s_RowQ[s][1 + d * 2 + l] = (u32) (powf (v, 1.0f / 2.2f) * 255.0f + 0.5f);
				}
		}
		for (unsigned sr = 0; sr < M; sr++)
			for (unsigned par = 0; par < 2; par++)
				for (unsigned j = 0; j < OW; j++)
				{
					unsigned odd = (j % 6) < 3, dark = ((par + odd) & 1) == 0, lit = j % 3;
					for (unsigned ch = 0; ch < 3; ch++)
						s_FF[sr][par][ch][j] = (float) s_RowQ[sr][1 + dark * 2 + (ch == lit)];
				}
		break;

	case CRTFX_ONETAP:
		// MIN_THICK 0.1, MAX_THICK 0.95, H_SMOOTH 0.8, V_SMOOTH 1, SUBPX_POS 0.31,
		// THICK_FALLOFF 0.45; linear filtering at the sigmoid's x
		Tables (1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float sx = (j + 0.5f) / M - 0.5f, b = floorf (sx), x = 2.0f * (sx - b) - 1.0f;
			const float hs = 6.0f + (1.0f - 6.0f) * 0.8f;
			s_Tap[j] = (short) b;
			s_W[1][j] = 0.5f + 0.5f * hs * x / sqrtf (1.0f + (hs * hs - 1.0f) * x * x);
		}
		for (unsigned s = 0; s < M; s++)
		{
			float y = (s + 0.5f) / M - 0.31f, b = floorf (y);
			s_RowLine[s] = (int) b;				// L or L-1
			float lo = y - 0.5f / M, hi = y + 0.5f / M;	// beam window
			s_RowK[s] = floorf (hi) - floorf (lo);
			s_RowF[s] = hi - floorf (hi);
			s_RowF2[s] = lo - floorf (lo);
		}
		for (unsigned i = 0; i < 4096; i++)		// beam width
		{
			float w = 0.1f + (0.95f - 0.1f) * powf (i / 4095.0f, 0.45f);
			s_W3[0][i] = w > 1.0f ? 1.0f : w;
		}
		break;

	case CRTFX_NTSCSIMPLE:
		// SNES mode, composite cable: 11 taps at 2x, luma Blackman 0.24,
		// chroma 0.06, gain 1.5; no scanlines, linear 2x -> M
		Tables (1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float x = (j + 0.5f) * 2.0f / M - 0.5f, b = floorf (x);
			s_Tap[j] = (short) b;
			s_W[1][j] = x - b;
		}
		{
			float sy = 0, sc = 0;
			for (unsigned n = 0; n < NT_TAPS; n++)
			{
				const float tau = 6.283185307f;
				float wy = (0.5f - 0.24f) - 0.5f * cosf (tau * n / NT_TAPS) + 0.24f * cosf (2 * tau * n / NT_TAPS);
				float wc = (0.5f - 0.06f) - 0.5f * cosf (tau * n / NT_TAPS) + 0.06f * cosf (2 * tau * n / NT_TAPS);
				s_W[0][n] = wy; s_W[2][n] = wc; sy += wy; sc += wc;
			}
			for (unsigned n = 0; n < NT_TAPS; n++) { s_W[0][n] /= sy; s_W[2][n] = s_W[2][n] / sc * 1.5f; }
			for (unsigned k = 0; k < NT_PH; k++)	// phase (k - pad) x pi x 0.6666
			{
				float ph = ((float) k - NT_PAD) * 3.141592653589f * 0.6666f;
				s_W2[0][k] = cosf (ph);
				s_W2[1][k] = sinf (ph);
			}
		}
		break;

	case CRTFX_BLURPI:
		// scanlineGain 0.30, rgbExtraGain 0.10, blurGain 0.15, blurRadius 1.5
		Tables (1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
		for (unsigned j = 0; j < OW; j++)
		{
			float x = (j + 0.5f) / M - 0.5f, b = floorf (x);
			s_Tap[j] = (short) b;   s_W[1][j] = x - b;
			x -= 1.5f; b = floorf (x);
			s_Tap2[j] = (short) b;  s_W2[1][j] = x - b;
			x += 3.0f; b = floorf (x);
			s_Tap3[j] = (short) b;  s_W3[1][j] = x - b;
		}
		for (unsigned s = 0; s < M; s++)
		{
			float y = (s + 0.5f) / M - 0.5f, b = floorf (y);
			s_RowLine[s] = (int) b;  s_RowF[s] = y - b;
			y += 1.5f; b = floorf (y);
			s_RowLine2[s] = (int) b; s_RowF2[s] = y - b;
			s_RowQ[s][0] = (u32) (s_RowF[s] * 256.0f + 0.5f);
			s_RowQ[s][1] = (u32) (s_RowF2[s] * 256.0f + 0.5f);
		}
		break;

	default:
		nFx = CRTFX_NONE;
		break;
	}
	SharedStore (s_nFx, nFx);
}

unsigned CrtFxActive (void)
{
	return SharedLoad (s_nFx);
}

static inline boolean NoLag (unsigned nFx)
{
	return nFx == CRTFX_HYLLIAN || nFx == CRTFX_ONETAP || nFx == CRTFX_NTSCSIMPLE;
}

boolean CrtFxLag (void)
{
	unsigned n = SharedLoad (s_nFx);
	return n != CRTFX_NONE && !NoLag (n);
}

static unsigned s_nField;		// ntsc-simple: frame parity

// ---- one source line, horizontally ---------------------------------------------

static const u32 *s_pFrame;
static int s_nNewest;

static inline const u32 *SrcLine (int L)
{
	if (L < 0) L = 0;
	if (L > s_nNewest) L = s_nNewest;
	return s_pFrame + (GAME_SRC_Y + L) * GAME_SRC_PITCH + GAME_SRC_X;
}

static inline unsigned Px (int x)
{
	return x < 0 ? 0 : (x >= (int) GAME_W ? GAME_W - 1 : (unsigned) x);
}

#define CH(p, c)	(((p) >> (16 - 8 * (c))) & 0xFF)
#define PAD	3
static float s_SP[3][GAME_W + 2 * PAD];		// one source line, planar, edges repeated

static void Planar (const u32 *p, const float *pTab)
{
	for (unsigned x = 0; x < GAME_W; x++)
	{
		u32 c = p[x];
		s_SP[0][x + PAD] = pTab[(c >> 16) & 0xFF];
		s_SP[1][x + PAD] = pTab[(c >> 8) & 0xFF];
		s_SP[2][x + PAD] = pTab[c & 0xFF];
	}
	for (unsigned ch = 0; ch < 3; ch++)
		for (unsigned i = 0; i < PAD; i++)
		{
			s_SP[ch][i] = s_SP[ch][PAD];
			s_SP[ch][GAME_W + PAD + i] = s_SP[ch][GAME_W + PAD - 1];
		}
}

static void HLine (int L, float **pA, u16 **pQ)
{
	const u32 *p = SrcLine (L);
	const unsigned OW = s_OW;
	switch (s_nFx)
	{
	case CRTFX_HYLLIAN:		// cubic in gamma space; v ^ 2.4 ^ (1 / 2.2) x mask ^ (1 / 2.2)
		Planar (p, s_U8);
		for (unsigned ch = 0; ch < 3; ch++)
		{
			const float *sp = s_SP[ch] + PAD;
			u16 *q = pQ[ch];
			for (unsigned j = 0; j < OW; j++)
			{
				const float *t = sp + s_Tap[j];
				float v = s_W[0][j] * t[0] + s_W[1][j] * t[1] + s_W[2][j] * t[2] + s_W[3][j] * t[3];
				unsigned i = v <= 0 ? 0 : (v >= 1 ? 4095 : (unsigned) (v * 4095.0f + 0.5f));
				q[j] = (u16) ((s_T12[i] * s_MQ[ch][j]) >> 8);
			}
		}
		break;

	case CRTFX_EASYMODE:		// Lanczos on c * c, clamped to the middle pair
		Planar (p, s_Lin);
		for (unsigned ch = 0; ch < 3; ch++)
		{
			const float *sp = s_SP[ch] + PAD;
			float *q = pA[ch];
			for (unsigned j = 0; j < OW; j++)
			{
				const float *t = sp + s_Tap[j];
				float m1 = t[1], m2 = t[2];
				float v = s_W[0][j] * t[0] + s_W[1][j] * m1 + s_W[2][j] * m2 + s_W[3][j] * t[3];
				float lo = m1 < m2 ? m1 : m2, hi = m1 < m2 ? m2 : m1;
				q[j] = Clamp (v, lo, hi);
			}
		}
		break;

	case CRTFX_APERTURE:		// glow, horizontally (the line's own data: AperLine)
		Planar (p, s_Lin);
		for (unsigned ch = 0; ch < 3; ch++)
		{
			const float *sp = s_SP[ch] + PAD;
			float *a = pA[ch];
			for (unsigned j = 0; j < OW; j++)
			{
				const float *t = sp + s_Tap3[j];
				a[j] = s_W3[0][j] * t[0] + s_W3[1][j] * t[1] + s_W3[2][j] * t[2];
			}
		}
		break;

	case CRTFX_FAKELOTTES:		// bilinear, gamma space
		Planar (p, s_U8);
		for (unsigned ch = 0; ch < 3; ch++)
		{
			const float *sp = s_SP[ch] + PAD;
			float *a = pA[ch];
			for (unsigned j = 0; j < OW; j++)
			{
				const float *t = sp + s_Tap[j];
				a[j] = t[0] + (t[1] - t[0]) * s_W[1][j];
			}
		}
		break;

	case CRTFX_ONETAP:		// signal ^ 2 and beam width
		Planar (p, s_U8);
		for (unsigned ch = 0; ch < 3; ch++)
		{
			const float *sp = s_SP[ch] + PAD;
			float *a = pA[ch], *w = pA[3 + ch];
			for (unsigned j = 0; j < OW; j++)
			{
				const float *t = sp + s_Tap[j];
				float v = t[0] + (t[1] - t[0]) * s_W[1][j];
				a[j] = v * v;
				w[j] = s_W3[0][(unsigned) (v * 4095.0f + 0.5f)];
			}
		}
		break;

	case CRTFX_NTSCSIMPLE:		// modulate at 2x, demodulate, filter; then 2x -> M
	{
		static float Y[NT_W + 2 * NT_PAD], C[NT_W + 2 * NT_PAD], S[NT_W + 2 * NT_PAD], Out[3][NT_W + 1];
		const int L0 = L < 0 ? 0 : L;
		const float sg = s_nField ? -1.0f : 1.0f;	// + pi on odd frames
		for (int i = 0; i < (int) NT_W + 2 * NT_PAD; i++)
		{
			int x = i - NT_PAD;
			u32 c = p[Px (x >> 1)];
			float r = s_U8[(c >> 16) & 0xFF], g = s_U8[(c >> 8) & 0xFF], b = s_U8[c & 0xFF];
			float yy = 0.299f * r + 0.587f * g + 0.114f * b;
			float ii = 0.596f * r - 0.274f * g - 0.322f * b;
			float qq = 0.211f * r - 0.523f * g + 0.312f * b;
			unsigned k = (unsigned) (i + L0);		// phase (x + L) x cycles
			float cs = s_W2[0][k] * sg, sn = s_W2[1][k] * sg;
			float sig = yy + ii * cs + qq * sn;
			Y[i] = sig; C[i] = sig * cs; S[i] = sig * sn;
		}
		for (unsigned x = 0; x < NT_W; x++)
		{
			float y = 0, ci = 0, cq = 0;
			for (unsigned n = 0; n < NT_TAPS; n++)
			{
				y += Y[x + n] * s_W[0][n];
				ci += C[x + n] * s_W[2][n];
				cq += S[x + n] * s_W[2][n];
			}
			float r = y + 0.956f * ci + 0.621f * cq;
			float g = y - 0.272f * ci - 0.647f * cq;
			float b = y - 1.106f * ci + 1.703f * cq;
			Out[0][x] = Clamp (r, 0, 1) * 255.0f;
			Out[1][x] = Clamp (g, 0, 1) * 255.0f;
			Out[2][x] = Clamp (b, 0, 1) * 255.0f;
		}
		for (unsigned ch = 0; ch < 3; ch++)
		{
			Out[ch][NT_W] = Out[ch][NT_W - 1];
			u16 *q = pQ[ch];
			for (unsigned j = 0; j < OW; j++)
			{
				int t = s_Tap[j];
				const float *o = Out[ch] + (t < 0 ? 0 : t);
				float v = t < 0 ? o[0] : o[0] + (o[1] - o[0]) * s_W[1][j];
				q[j] = (u16) (v + 0.5f);
			}
		}
		break;
	}

	case CRTFX_BLURPI:		// x 128 (x 255): centre + left/right (same rows); centre for below
		Planar (p, s_U8);
		for (unsigned ch = 0; ch < 3; ch++)
		{
			const float *sp = s_SP[ch] + PAD;
			u16 *q = pQ[ch], *q2 = pQ[3 + ch];
			for (unsigned j = 0; j < OW; j++)
			{
				const float *t = sp + s_Tap[j], *l = sp + s_Tap2[j], *r = sp + s_Tap3[j];
				float cc = t[0] + (t[1] - t[0]) * s_W[1][j];
				float ll = l[0] + (l[1] - l[0]) * s_W2[1][j];
				float rr = r[0] + (r[1] - r[0]) * s_W3[1][j];
				q[j] = (u16) ((0.97625f * cc + 0.0375f * (ll + rr)) * (255.0f * 128.0f) + 0.5f);
				q2[j] = (u16) (0.0375f * cc * (255.0f * 128.0f) + 0.5f);
			}
		}
		break;
	}
}

static TSlot *Get (int L)
{
	if (L < 0) L = 0;
	if (L > s_nNewest) L = s_nNewest;
	for (unsigned i = 0; i < SLOTS; i++)
		if (s_Slot[i].nLine == L)
			return &s_Slot[i];
	TSlot *pS = &s_Slot[s_nNext];
	s_nNext = (s_nNext + 1) % SLOTS;
	pS->nLine = L;
	HLine (L, pS->pA, pS->pQ);
	return pS;
}

// aperture: the line's own soft / sharp picture and beam widths
static void AperLine (int L)
{
	Planar (SrcLine (L), s_Lin);
	for (unsigned ch = 0; ch < 3; ch++)
	{
		const float *sp = s_SP[ch] + PAD;
		float *c0 = s_pTmp[ch];
		for (unsigned j = 0; j < s_OW; j++)
		{
			const float *t = sp + s_Tap[j], *t2 = sp + s_Tap2[j];
			float soft = s_W[0][j] * t[0] + s_W[1][j] * t[1];
			float sharp = s_W2[0][j] * t2[0] + s_W2[1][j] * t2[(int) s_W2[2][j]];
			float pr = soft * sharp;
			c0[j] = pr > 0 ? sqrtf (pr) : 0.0f;
			s_pTmp[3 + ch][j] = 1.0f / (0.5f + Pow (soft));
		}
	}
}

// ---- rows ----------------------------------------------------------------------

static inline u32 Pack (u32 r, u32 g, u32 b)
{
	return 0xFF000000u | r << s_rs | g << 8 | b << s_bs;
}

static inline u32 Sat (u32 v)	{ return v > 255 ? 255 : v; }

static void PackRow (u32 *__restrict pDst)
{
	const u32 *__restrict r = s_O[0], *__restrict g = s_O[1], *__restrict b = s_O[2];
	const unsigned rs = s_rs, bs = s_bs;
	for (unsigned j = 0; j < s_OW; j++)
		pDst[j] = 0xFF000000u | r[j] << rs | g[j] << 8 | b[j] << bs;
}

static void Rows (int L, TCrtRowFn pRow, void *pCtx)
{
	const unsigned M = s_M, OW = s_OW;
	if (s_nFx == CRTFX_APERTURE)
	{
		AperLine (L);
	}
	if (s_nFx == CRTFX_NTSCSIMPLE)		// all rows alike
	{
		u16 **Q = Get (L)->pQ;
		for (unsigned ch = 0; ch < 3; ch++)
			for (unsigned j = 0; j < OW; j++)
				s_O[ch][j] = Q[ch][j];
		for (unsigned s = 0; s < M; s++)	// (no reads from the framebuffer)
			PackRow (pRow (pCtx, L * M + s));
		return;
	}
	for (unsigned s = 0; s < M; s++)
	{
		u32 *pDst = pRow (pCtx, L * M + s);
		if (s_nFx == CRTFX_HYLLIAN && s >= (M + 1) / 2)	// rows are symmetric: done
		{
			break;
		}
		switch (s_nFx)
		{
		case CRTFX_ONETAP:
		{
			float **A = Get (L + s_RowLine[s])->pA;
			const float na = s_RowK[s], fh = s_RowF[s] - 0.5f, fl = s_RowF2[s] - 0.5f, k = 255.0f * 255.0f * M;
			for (unsigned ch = 0; ch < 3; ch++)
			{
				const float *__restrict a = A[ch], *__restrict w = A[3 + ch];
				u32 *__restrict o = s_O[ch];
				for (unsigned j = 0; j < OW; j++)
				{
					float wd = w[j], h = 0.5f * wd;
					float th = fh + h, tl = fl + h;
					th = th < 0 ? 0 : (th > wd ? wd : th);
					tl = tl < 0 ? 0 : (tl > wd ? wd : tl);
					float area = na * wd + th - tl;
					float v = sqrtf (a[j] * area * k);
					o[j] = (u32) ((v > 255.0f ? 255.0f : v) + 0.5f);
				}
			}
			PackRow (pDst);
			break;
		}
		case CRTFX_HYLLIAN:
		{
			u16 **Q = Get (L)->pQ;
			const u32 k = s_RowQ[s][0];
			for (unsigned ch = 0; ch < 3; ch++)
			{
				const u16 *__restrict q = Q[ch];
				u32 *__restrict o = s_O[ch];
				for (unsigned j = 0; j < OW; j++)
				{
					u32 v = (q[j] * k) >> 16;
					o[j] = v > 255 ? 255 : v;
				}
			}
			PackRow (pDst);
			if (M - 1 - s != s)
			{
				PackRow (pRow (pCtx, L * M + M - 1 - s));
			}
			break;
		}
		case CRTFX_EASYMODE:
		{
			float **A = Get (L + s_RowLine[s])->pA, **B = Get (L + s_RowLine[s] + 1)->pA;
			const float f = s_RowF[s], sw = s_RowK[s];
			const float *__restrict a0 = A[0], *__restrict a1 = A[1], *__restrict a2 = A[2];
			const float *__restrict b0 = B[0], *__restrict b1 = B[1], *__restrict b2 = B[2];
			const float *__restrict m0 = s_Mask[0], *__restrict m1 = s_Mask[1], *__restrict m2 = s_Mask[2];
			u32 *__restrict o0 = s_O[0], *__restrict o1 = s_O[1], *__restrict o2 = s_O[2];
			for (unsigned j = 0; j < OW; j++)
			{
				float r = a0[j] + (b0[j] - a0[j]) * f;
				float g = a1[j] + (b1[j] - a1[j]) * f;
				float b = a2[j] + (b2[j] - a2[j]) * f;
				float mx = r > g ? r : g;
				mx = mx > b ? mx : b;
				float br = (mx + 0.2126f * r + 0.7152f * g + 0.0722f * b) * 0.5f;
				br = br < 0.35f ? 0.35f : (br > 0.65f ? 0.65f : br);
				float k = sw + (1.0f - sw) * br;
				// (x) ^ (1 / 1.8) x 1.2: sqrt, then u ^ 1.111 as a cubic
				float u, y;
				u = sqrtf (r * k * m0[j]);
				y = u * (0.77054346f + u * (0.3851755f - 0.15856206f * u)) * (1.2f * 255.0f);
				o0[j] = (u32) ((y > 255.0f ? 255.0f : y) + 0.5f);
				u = sqrtf (g * k * m1[j]);
				y = u * (0.77054346f + u * (0.3851755f - 0.15856206f * u)) * (1.2f * 255.0f);
				o1[j] = (u32) ((y > 255.0f ? 255.0f : y) + 0.5f);
				u = sqrtf (b * k * m2[j]);
				y = u * (0.77054346f + u * (0.3851755f - 0.15856206f * u)) * (1.2f * 255.0f);
				o2[j] = (u32) ((y > 255.0f ? 255.0f : y) + 0.5f);
			}
			PackRow (pDst);
			break;
		}
		case CRTFX_APERTURE:
		{
			int l0 = L + s_RowLine[s];
			float **G0 = Get (l0 - 1)->pA, **G1 = Get (l0)->pA, **G2 = Get (l0 + 1)->pA;
			const float w0 = s_RowW[0][s], w1 = s_RowW[1][s], w2 = s_RowW[2][s], as = s_RowK[s];
			for (unsigned ch = 0; ch < 3; ch++)
			{
				const float *__restrict g0 = G0[ch], *__restrict g1 = G1[ch], *__restrict g2 = G2[ch];
				const float *__restrict c0 = s_pTmp[ch], *__restrict ibp = s_pTmp[3 + ch], *__restrict m = s_Mask[ch];
				u32 *__restrict o = s_O[ch];
				for (unsigned j = 0; j < OW; j++)
				{
					float glow = w0 * g0[j] + w1 * g1[j] + w2 * g2[j];
					float ib = ibp[j];
					float t = 1.0f - as * ib;
					t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
					float col = c0[j] * t * t * (3.0f - 2.0f * t) * ib;
					glow -= col;
					glow = glow < 0.0f ? 0.0f : (glow > 1.0f ? 1.0f : glow);
					col += glow * glow * 0.1f;
					float x = (col * m[j] + glow * 0.05f) * 1.5f;		// BRIGHTNESS
					x = x > 1.0f ? 1.0f : x;
					// x ^ (1 / 2.4) = w ^ (5 / 3), w = x ^ (1 / 4), as a cubic
					float w = sqrtf (sqrtf (x));
					float y = w * (0.13752100f + w * (1.10003058f - 0.24060162f * w)) * 255.0f;
					o[j] = (u32) ((y > 255.0f ? 255.0f : y) + 0.5f);
				}
			}
			PackRow (pDst);
			break;
		}
		case CRTFX_FAKELOTTES:
		{
			float **A = Get (L + s_RowLine[s])->pA, **B = Get (L + s_RowLine[s] + 1)->pA;
			const float f = s_RowF[s];
			const unsigned par = (L * M + s) & 1;
			for (unsigned ch = 0; ch < 3; ch++)
			{
				const float *__restrict a = A[ch], *__restrict b = B[ch], *__restrict F = s_FF[s][par][ch];
				u32 *__restrict o = s_O[ch];
				for (unsigned j = 0; j < OW; j++)
				{
					float x = a[j] + (b[j] - a[j]) * f;
					float y = x * (0.72467291f + x * (0.45745973f - 0.18540175f * x));	// x ^ (2.5 / 2.2)
					float v = y * F[j];
					v = v > 255.0f ? 255.0f : v;
					o[j] = (u32) (v + 0.5f);
				}
			}
			PackRow (pDst);
			break;
		}
		case CRTFX_BLURPI:
		{
			u16 **A = Get (L + s_RowLine[s])->pQ, **B = Get (L + s_RowLine[s] + 1)->pQ;
			u16 **C = Get (L + s_RowLine2[s])->pQ, **D = Get (L + s_RowLine2[s] + 1)->pQ;
			const int f = (int) s_RowQ[s][0], f2 = (int) s_RowQ[s][1];
			const int k = ((L * M + s) & 1) ? 256 : 179;		// 0.7 on even rows
			for (unsigned ch = 0; ch < 3; ch++)
			{
				const u16 *__restrict a = A[ch], *__restrict b = B[ch], *__restrict c = C[3 + ch], *__restrict d = D[3 + ch];
				u32 *__restrict o = s_O[ch];
				for (unsigned j = 0; j < OW; j++)
				{
					int v = a[j] + (((int) b[j] - (int) a[j]) * f >> 8) + c[j] + (((int) d[j] - (int) c[j]) * f2 >> 8);
					v = (v * k) >> 15;
					o[j] = (u32) (v < 0 ? 0 : (v > 255 ? 255 : v));
				}
			}
			PackRow (pDst);
			break;
		}
		}
	}
}

void CrtFxLine (const u32 *pFrame, unsigned y, TCrtRowFn pRow, void *pCtx)
{
	if (!SharedLoad (s_nFx))
	{
		return;
	}
	u64 t0 = Ticks ();
	s_pFrame = pFrame;
	s_nNewest = (int) y;
	const unsigned nFx = SharedLoad (s_nFx);
	if (y == 0)
	{
		for (unsigned i = 0; i < SLOTS; i++) s_Slot[i].nLine = -100;
		s_nField ^= 1;
	}
	if (NoLag (nFx))
	{
		for (unsigned i = 0; i < SLOTS; i++)
			if (s_Slot[i].nLine == (int) y) s_Slot[i].nLine = -100;
		Rows ((int) y, pRow, pCtx);
	}
	else if (y > 0)
	{
		// lines below the newest were clamped to it: forget them
		for (unsigned i = 0; i < SLOTS; i++)
			if (s_Slot[i].nLine >= (int) y) s_Slot[i].nLine = -100;
		Rows ((int) y - 1, pRow, pCtx);
	}
	if (y == GAME_H - 1 && !NoLag (nFx))
	{
		Rows ((int) y, pRow, pCtx);
	}
	u64 d = Ticks () - t0;
	s_nSum += d;
	if (d > s_nMax) s_nMax = d;
	s_nCalls++;
}

void CrtFxTakeStats (unsigned *pSumUs, unsigned *pMaxUs, unsigned *pCalls)
{
	u64 f = TicksFreq ();
	*pSumUs = (unsigned) (s_nSum * 1000000 / f);
	*pMaxUs = (unsigned) (s_nMax * 1000000 / f);
	*pCalls = s_nCalls;
	s_nSum = 0;
	s_nMax = 0;
	s_nCalls = 0;
}
