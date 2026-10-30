/*
 * neo-pi - kernel: game list, play with USB pads and HDMI
 * audio, in-game menu with save states, automatic diagnostics in LOG.TXT.
 */
#include "kernel.h"
#include "fontdata.h"
#include "neopi.h"
#include "profile.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/new.h>
#include <circle/util.h>
#include <circle/alloc.h>
#include <circle/bcmwatchdog.h>
#include <stdarg.h>

extern "C" {
#include <stdint.h>
#include "geo.h"
#include "geo_lspc.h"
#include "geo_mixer.h"
#include "geo_neo.h"
#include "geo_ymfm.h"

int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);
int vsnprintf (char *pBuffer, size_t nSize, const char *pFormat, va_list Args);
void *neopi_zip_extract (const void *pZip, size_t nZipSize, const char *pName, size_t *pSize);
void *neopi_png_encode (const void *pRGB, int nWidth, int nHeight, size_t *pSize);
void neopi_png_free (void *p);
}

void NeopiInstallVFS (void);

#define NEOPI_DIR	"SD:/neopi"
#define BIOS_PATH	NEOPI_DIR "/neogeo.zip"
#define LOG_PATH	NEOPI_DIR "/LOG.TXT"
#define BENCH_PATH	NEOPI_DIR "/BENCH.TXT"
#define BENCH_FLAG	NEOPI_DIR "/BENCH"
#define PROFILE_PATH	NEOPI_DIR "/PROFILE.TXT"
#define LAST_PATH	NEOPI_DIR "/LAST.TXT"
#define CFG_PATH	NEOPI_DIR "/neopi.cfg"
#define PADS_PATH	NEOPI_DIR "/pads.cfg"
#define SAVE_DIR	NEOPI_DIR "/saves"
#define SHOT_DIR	NEOPI_DIR "/shots"

#define SAMPLE_RATE	48000
#define BENCH_WARMUP	1800		// 30 s of play before measuring
#define BENCH_FRAMES	3600		// then 60 s measured
#define BENCH_PROFILE_US 200		// profiler: one sample every 200 us
#define BENCH_SLOW_US	16000		// frames from 16 ms on count as slow
#define REPORT_FRAMES	3600		// one statistics line per minute of play

#define COLOR_TEXT	RGB (220, 220, 220)
#define COLOR_TITLE	RGB (255, 200, 0)
#define COLOR_OK	RGB (80, 230, 80)
#define COLOR_ERR	RGB (255, 70, 70)
#define COLOR_DIM	RGB (140, 140, 140)

CKernel *CKernel::s_pThis = 0;

enum { GmSave, GmLoad, GmReset, GmBios, GmList, GmPower, GmCount };

// ---- emulator buffers and callbacks ---------------------------------------

static u32 s_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES] __attribute__ ((aligned (64)));
static int16_t s_AudioBuf[8192] __attribute__ ((aligned (64)));
static volatile unsigned s_nAudioSamples;	// stereo frames produced in the last frame

static void AudioCallback (size_t nSamples)
{
	s_nAudioSamples = nSamples / 2;		// callback counts int16 values (L+R)
}

// Light resampler (default since 0.6.2-rc1): the core hands over the raw
// YM2610 output (about 56.3 kHz) and it is brought to 48 kHz by linear
// interpolation, carrying the position and the last sample across frames.
// Same rate ratio as Geolith's Speex path, a fraction of its cost (Speex at
// quality 7 took about 15% of the frame time on a Pi 3).
// resampler=speex in neopi.cfg goes back to Speex.
#define YM_RATE		56319		// as in geo_mixer.c
static boolean s_bSpeex = FALSE;
static u64 s_nResPos;			// 32.32 position, 0 = last sample of the previous frame
static int16_t s_ResPrev[2];
static const u64 s_nResStep = ((u64) YM_RATE << 32) / SAMPLE_RATE;

static void AudioCallbackLinear (size_t nValues)
{
	const int16_t *pIn = geo_ymfm_get_buffer ();
	unsigned nIn = (unsigned) (nValues / 2);	// stereo input samples
	unsigned nOut = 0;
	u64 nPos = s_nResPos;
	const unsigned nMaxOut = sizeof s_AudioBuf / sizeof s_AudioBuf[0] / 2;

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
		s_AudioBuf[2*nOut]   = (int16_t) (l0 + (((l1 - l0) * f) >> 15));
		s_AudioBuf[2*nOut+1] = (int16_t) (r0 + (((r1 - r0) * f) >> 15));
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
	s_nAudioSamples = nOut;
}

static void CoreLog (int nLevel, const char *pFormat, ...)
{
	if (nLevel == GEO_LOG_DBG)
	{
		return;
	}

	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);

	size_t n = strlen (Buffer);
	while (n > 0 && (Buffer[n-1] == '\n' || Buffer[n-1] == '\r'))
	{
		Buffer[--n] = '\0';
	}

	char Line[180];
	snprintf (Line, sizeof Line, "  core: %s", Buffer);
	CKernel::Get ()->Log (Line, nLevel >= GEO_LOG_WRN ? COLOR_ERR : COLOR_DIM);
}

// Neo Geo inputs (all active low), written by the game loop before geo_exec()
static volatile unsigned s_Joy[2] = {0xFF, 0xFF};
static volatile unsigned s_StatA = 0x07 | 0x18;		// coins 1/2; slots 3/4 unused (MVS)
static volatile unsigned s_StatB = 0x0F | 0x30;		// starts; no memory card

static unsigned InputJoystick (unsigned nPort)	{ return s_Joy[nPort & 1]; }
static unsigned InputStatusA (void)		{ return s_StatA; }
static unsigned InputStatusB (void)		{ return s_StatB; }
static volatile unsigned s_nTestFrames = 0;		// > 0: TEST button held (MVS service menu)
static unsigned InputSystemType (void)	{ return s_nTestFrames ? 0x40 : 0xC0; }
static unsigned InputDIPSwitches (void)	{ return 0xFF; }		// all DIP switches off
static unsigned InputUnused (void)		{ return 0xFF; }

// ---- C interface used by the shim -----------------------------------------

extern "C" void neopi_log (const char *pFormat, ...)
{
	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	CKernel::Get ()->Log (Buffer, COLOR_TEXT);
}

extern "C" void neopi_fatal (int nCode, const char *pFormat, ...)
{
	char Buffer[160];
	va_list Args;
	va_start (Args, pFormat);
	vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	CKernel::Get ()->Fatal (nCode, Buffer);
}

// ---- helpers --------------------------------------------------------------

static void FormatMs (char *pOut, size_t nSize, unsigned nMicros)
{
	snprintf (pOut, nSize, "%u.%02u", nMicros / 1000, (nMicros % 1000) / 10);
}

struct TStats
{
	unsigned nAvg, nP99, nMax, nMin;
};

static void ComputeStats (const unsigned *pValues, unsigned nCount, TStats *pStats)
{
	static unsigned Sorted[BENCH_FRAMES];
	u64 nSum = 0;
	for (unsigned i = 0; i < nCount; i++)
	{
		Sorted[i] = pValues[i];
		nSum += pValues[i];
	}
	for (unsigned i = 1; i < nCount; i++)
	{
		unsigned v = Sorted[i];
		int j = (int) i - 1;
		while (j >= 0 && Sorted[j] > v)
		{
			Sorted[j+1] = Sorted[j];
			j--;
		}
		Sorted[j+1] = v;
	}
	pStats->nAvg = (unsigned) (nSum / nCount);
	pStats->nMin = Sorted[0];
	pStats->nMax = Sorted[nCount-1];
	pStats->nP99 = Sorted[(nCount * 99) / 100];
}

// Histogram in 0.1 ms bins (0..50 ms) for per-minute reports
struct THisto
{
	enum { Bins = 500 };
	unsigned Bin[Bins];
	unsigned nCount, nMax;
	u64 nSum;

	void Reset (void) { memset (Bin, 0, sizeof Bin); nCount = nMax = 0; nSum = 0; }
	void Add (unsigned us)
	{
		unsigned b = us / 100;
		Bin[b < Bins ? b : Bins - 1]++;
		nCount++;
		nSum += us;
		if (us > nMax) nMax = us;
	}
	unsigned Avg (void) const { return nCount ? (unsigned) (nSum / nCount) : 0; }
	unsigned P99 (void) const
	{
		unsigned nTarget = nCount - nCount / 100, n = 0;
		for (unsigned b = 0; b < Bins; b++)
		{
			n += Bin[b];
			if (n >= nTarget) return b * 100 + 100;
		}
		return nMax;
	}
};

static THisto s_HistEmu, s_HistWork, s_HistPeriod;
static unsigned s_nOverBudget, s_nMissedVSync, s_nWindowFrames;
static unsigned s_nWindowStart;

static boolean HasNeoExtension (const char *pName)
{
	size_t n = strlen (pName);
	if (n < 4)
	{
		return FALSE;
	}
	const char *e = pName + n - 4;
	return    e[0] == '.'
	       && (e[1] == 'n' || e[1] == 'N')
	       && (e[2] == 'e' || e[2] == 'E')
	       && (e[3] == 'o' || e[3] == 'O');
}

int CLogSink::Write (const void *pBuffer, size_t nCount)
{
	static char Line[160];
	static unsigned nPos = 0;
	const char *p = (const char *) pBuffer;
	for (size_t i = 0; i < nCount; i++)
	{
		char c = p[i];
		if (c == '\x1b')
		{
			while (i + 1 < nCount && !((p[i+1] >= 'a' && p[i+1] <= 'z') || (p[i+1] >= 'A' && p[i+1] <= 'Z')))
			{
				i++;
			}
			i++;
			continue;
		}
		if (c == '\n' || nPos >= sizeof Line - 1)
		{
			Line[nPos] = '\0';
			if (nPos > 0 && CKernel::Get ())
			{
				CKernel::Get ()->Log (Line, COLOR_DIM);
			}
			nPos = 0;
			continue;
		}
		if (c != '\r')
		{
			Line[nPos++] = c;
		}
	}
	return (int) nCount;
}

// ---- kernel ---------------------------------------------------------------

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_CPUThrottle (CPUSpeedMaximum),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_pInput (0),
	m_pUI (0),
	m_bVideoOK (FALSE),
	m_bSDOK (FALSE),
	m_bQuiet (TRUE),		// black screen while booting; text only on errors
	m_nRepeat (0),
	m_nLines (0),
	m_pBIOS (0),
	m_nBIOSSize (0),
	m_pGame (0),
	m_nGameSize (0),
	m_pGames (0),
	m_nGames (0),
	m_nLoaded (-1),
	m_bVSync (TRUE),
	m_bStatusOn (FALSE),
	m_nClearStatus (0),
	m_nShots (0),
	m_nSystem (0),
	m_nRegion (REGION_EU),
	m_nFilter (0),
	m_bHVS (TRUE),
	m_bFilterAllowed (TRUE),
	m_bForceFilters (FALSE)
{
	s_pThis = this;
	m_LastRaw[0] = '\0';
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	m_bVideoOK = m_Video.Initialize ();

	m_Serial.Initialize (115200);

	if (!m_Logger.Initialize (&m_LogSink))
	{
		return FALSE;
	}
	if (!m_Interrupt.Initialize ())
	{
		return FALSE;
	}
	if (!m_Timer.Initialize ())
	{
		return FALSE;
	}

	return TRUE;
}

// ---- console / log --------------------------------------------------------

void CKernel::Log (const char *pLine, u32 nColor)
{
	if (m_nLines > 0 && strcmp (pLine, m_LastRaw) == 0)
	{
		m_nRepeat++;
		unsigned n = (m_nLines - 1) % MaxLines;
		snprintf (m_Lines[n], MaxLineLen, "%s (x%u)", m_LastRaw, m_nRepeat);
		if (!m_bQuiet && m_bVideoOK && !m_Video.IsGameMode () && m_nLines <= m_Video.GetRows ())
		{
			m_Video.DrawText (0, 0, m_nLines - 1, m_Lines[n], nColor, RGB (0, 0, 0));
		}
		return;
	}
	strncpy (m_LastRaw, pLine, MaxLineLen - 1);
	m_LastRaw[MaxLineLen - 1] = '\0';
	m_nRepeat = 1;

	unsigned n = m_nLines % MaxLines;
	strncpy (m_Lines[n], pLine, MaxLineLen - 1);
	m_Lines[n][MaxLineLen - 1] = '\0';
	m_LineColor[n] = nColor;
	m_nLines++;

	if (m_bQuiet)
	{
		return;
	}

	m_Serial.Write (pLine, strlen (pLine));
	m_Serial.Write ("\r\n", 2);

	if (!m_bVideoOK || m_Video.IsGameMode ())	// never write text over the game screen
	{
		return;
	}

	unsigned nRows = m_Video.GetRows ();
	if (m_nLines <= nRows)
	{
		m_Video.DrawText (0, 0, m_nLines - 1, m_Lines[n], nColor, RGB (0, 0, 0));
	}
	else
	{
		RedrawConsole ();
	}
}

void CKernel::RedrawConsole (void)
{
	unsigned nRows = m_Video.GetRows ();
	unsigned nFirst = m_nLines > nRows ? m_nLines - nRows : 0;
	m_Video.ClearRows (0, 0, nRows, RGB (0, 0, 0));
	for (unsigned i = nFirst; i < m_nLines; i++)
	{
		unsigned n = i % MaxLines;
		m_Video.DrawText (0, 0, i - nFirst, m_Lines[n], m_LineColor[n], RGB (0, 0, 0));
	}
}

void CKernel::WriteTextFile (const char *pPath, const char *pText)
{
	if (!m_bSDOK)
	{
		return;
	}
	FIL File;
	if (f_open (&File, pPath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return;
	}
	UINT nWritten;
	f_write (&File, pText, strlen (pText), &nWritten);
	f_close (&File);
}

void CKernel::SaveLog (void)
{
	static char Text[MaxLines * (MaxLineLen + 2)];
	unsigned nFirst = m_nLines > MaxLines ? m_nLines - MaxLines : 0;
	size_t nPos = 0;
	for (unsigned i = nFirst; i < m_nLines; i++)
	{
		const char *pLine = m_Lines[i % MaxLines];
		size_t nLen = strlen (pLine);
		memcpy (Text + nPos, pLine, nLen);
		nPos += nLen;
		Text[nPos++] = '\r';
		Text[nPos++] = '\n';
	}
	Text[nPos] = '\0';
	WriteTextFile (LOG_PATH, Text);
}

void CKernel::Fatal (int nCode, const char *pMessage)
{
	if (m_bQuiet || m_Video.IsGameMode ())
	{
		m_bQuiet = FALSE;
		if (m_bVideoOK && m_Video.SetConsoleMode ())
		{
			RedrawConsole ();
		}
	}

	char Line[160];
	snprintf (Line, sizeof Line, "ERROR: %s", pMessage);
	Log (Line, COLOR_ERR);

	switch (nCode)
	{
	case NEOPI_ERR_SD:
		Log ("  Check that the card is FAT32 and that the files are in its root.", COLOR_TEXT);
		break;
	case NEOPI_ERR_BIOS:
		Log ("  Copy the Neo Geo BIOS as neopi/neogeo.zip (MAME/FBNeo set).", COLOR_TEXT);
		break;
	case NEOPI_ERR_GAME:
		Log ("  Copy at least one .neo game into the neopi folder.", COLOR_TEXT);
		break;
	case NEOPI_ERR_MEM:
		Log ("  Not enough memory.", COLOR_TEXT);
		break;
	default:
		break;
	}
	snprintf (Line, sizeof Line, "The green LED blinks %d times: error code %d.", nCode, nCode);
	Log (Line, COLOR_TEXT);

	SaveLog ();

	while (1)
	{
		m_ActLED.Blink (nCode, 250, 350);
		CTimer::SimpleMsDelay (1500);
	}
}

// ---- files ----------------------------------------------------------------

u8 *CKernel::LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	size_t nSize = f_size (&File);
	u8 *pBuffer = new (nHeap) u8[nSize + 16];
	if (pBuffer == 0)
	{
		f_close (&File);
		char Msg[160];
		snprintf (Msg, sizeof Msg, "not enough memory for %s (%u MB)",
			  pPath, (unsigned) (nSize >> 20));
		Log (Msg, COLOR_ERR);
		return 0;
	}

	unsigned nStart = CTimer::GetClockTicks ();
	size_t nDone = 0;
	const size_t nChunk = 1 << 20;
	while (nDone < nSize)
	{
		size_t nWant = nSize - nDone < nChunk ? nSize - nDone : nChunk;
		UINT nRead = 0;
		if (   f_read (&File, pBuffer + nDone, nWant, &nRead) != FR_OK
		    || nRead == 0)
		{
			break;
		}
		nDone += nRead;
		if (pShowName != 0 && m_pUI != 0)
		{
			m_pUI->DrawLoading (pShowName, (unsigned) ((u64) nDone * 100 / nSize));
			ShowUIFrame (FALSE);
		}
	}
	f_close (&File);

	if (nDone != nSize)
	{
		delete [] pBuffer;
		return 0;
	}

	unsigned nMs = (CTimer::GetClockTicks () - nStart) / 1000;
	char Msg[180];
	unsigned nSpeed10 = nMs ? (unsigned) ((u64) nSize * 10 * 1000 / nMs / (1024 * 1024)) : 0;
	snprintf (Msg, sizeof Msg, "  read %s: %u KB in %u ms (%u.%u MB/s)", pPath + 4,
		  (unsigned) (nSize >> 10), nMs, nSpeed10 / 10, nSpeed10 % 10);
	Log (Msg, COLOR_OK);

	*pSize = nSize;
	return pBuffer;
}

void CKernel::ScanGames (void)
{
	m_pGames = new TGameEntry[MaxGames];
	m_nGames = 0;

	DIR Dir;
	FILINFO Info;
	if (f_opendir (&Dir, NEOPI_DIR) != FR_OK)
	{
		return;
	}
	while (   m_nGames < MaxGames
	       && f_readdir (&Dir, &Info) == FR_OK
	       && Info.fname[0] != '\0')
	{
		if ((Info.fattrib & AM_DIR) || !HasNeoExtension (Info.fname)
		    || strlen (Info.fname) >= sizeof m_pGames[0].File)
		{
			continue;
		}

		char Path[128];
		snprintf (Path, sizeof Path, "%s/%s", NEOPI_DIR, Info.fname);
		FIL File;
		u8 Header[128];
		UINT nRead = 0;
		if (f_open (&File, Path, FA_READ) != FR_OK)
		{
			continue;
		}
		f_read (&File, Header, sizeof Header, &nRead);
		f_close (&File);

		if (   nRead < sizeof Header
		    || Header[0] != 'N' || Header[1] != 'E' || Header[2] != 'O' || Header[3] != 1)
		{
			char Msg[160];
			snprintf (Msg, sizeof Msg, "  %s: invalid .neo header, skipped", Info.fname);
			Log (Msg, COLOR_ERR);
			continue;
		}

		TGameEntry &G = m_pGames[m_nGames];
		strcpy (G.File, Info.fname);
		unsigned n = 0;
		for (unsigned i = 0; i < 33 && Header[44 + i] != 0; i++)
		{
			char c = (char) Header[44 + i];
			if (c >= 'a' && c <= 'z') c -= 32;
			if (c < 32 || c > 126) c = ' ';
			G.Name[n++] = c;
		}
		while (n > 0 && G.Name[n-1] == ' ') n--;
		G.Name[n] = '\0';
		if (n == 0)
		{
			strncpy (G.Name, Info.fname, sizeof G.Name - 1);
			G.Name[sizeof G.Name - 1] = '\0';
		}
		m_nGames++;
	}
	f_closedir (&Dir);

	// sort by name
	for (unsigned i = 1; i < m_nGames; i++)
	{
		TGameEntry t = m_pGames[i];
		int j = (int) i - 1;
		while (j >= 0 && strcmp (m_pGames[j].Name, t.Name) > 0)
		{
			m_pGames[j+1] = m_pGames[j];
			j--;
		}
		m_pGames[j+1] = t;
	}
}

void CKernel::GamePath (char *pOut, size_t nSize, const char *pExt) const
{
	char Base[64];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';
	snprintf (pOut, nSize, "%s/%s.%s", SAVE_DIR, Base, pExt);
}

// ---- core -----------------------------------------------------------------

void CKernel::SetupCoreOnce (void)
{
	geo_log_set_callback (CoreLog);
	NeopiInstallVFS ();

	geo_lspc_set_buffer (s_VideoBuf);
	geo_mixer_set_buffer (s_AudioBuf);
	geo_mixer_set_callback (AudioCallback);

	geo_input_set_callback (0, InputJoystick);
	geo_input_set_callback (1, InputJoystick);
	geo_input_sys_set_callback (0, InputStatusA);
	geo_input_sys_set_callback (1, InputStatusB);
	geo_input_sys_set_callback (2, InputSystemType);
	geo_input_sys_set_callback (3, InputDIPSwitches);
	geo_input_sys_set_callback (4, InputUnused);
}

void CKernel::SaveGameData (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	static const char *Ext[3] = {"nv", "srm", "mcr"};
	for (unsigned i = 0; i < 3; i++)
	{
		char Path[160];
		GamePath (Path, sizeof Path, Ext[i]);
		geo_savedata_save (i, Path);
	}
}

void CKernel::UnloadGame (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	SaveGameData ();
	geo_deinit ();
	geo_bios_unload ();
	delete [] m_pGame;
	m_pGame = 0;
	m_nLoaded = -1;
}

// Palette and video RAM as at power-on (all zero): otherwise, while the BIOS
// runs its tests after the reset, the chip keeps showing what the previous
// game left there. Done with the same writes the 68000 would make.
void CKernel::ClearVideoMemory (void)
{
	for (unsigned nBank = 0; nBank < 2; nBank++)
	{
		geo_lspc_palram_bank (nBank);
		for (u32 nAddr = 0; nAddr < 0x2000; nAddr += 2)
		{
			geo_lspc_palram_wr16 (nAddr, 0);
		}
	}
	geo_lspc_palram_bank (0);

	geo_lspc_vrammod_wr (1);
	geo_lspc_vramaddr_wr (0x0000);			// lower VRAM, 32K words
	for (unsigned i = 0; i < 0x8000; i++)
	{
		geo_lspc_vram_wr (0);
	}
	geo_lspc_vramaddr_wr (0x8000);			// upper VRAM, 2K words
	for (unsigned i = 0; i < 0x800; i++)
	{
		geo_lspc_vram_wr (0);
	}
	geo_lspc_vramaddr_wr (0);
	geo_lspc_vrammod_wr (0);
}

boolean CKernel::LoadGame (unsigned nIndex)
{
	UnloadGame ();

	const TGameEntry &G = m_pGames[nIndex];
	char Path[128], Line[180];
	snprintf (Path, sizeof Path, "%s/%s", NEOPI_DIR, G.File);
	snprintf (Line, sizeof Line, "Game: %s (%s)", G.Name, G.File);
	Log (Line, COLOR_TITLE);

	m_pGame = LoadFile (Path, &m_nGameSize, HEAP_ANY, m_bQuiet ? G.Name : 0);
	if (m_pGame == 0)
	{
		Log ("  read failed", COLOR_ERR);
		return FALSE;
	}

	geo_mixer_deinit ();
	geo_set_system (m_nSystem == 0 ? SYSTEM_MVS : SYSTEM_AES);
	geo_set_region ((int) m_nRegion);
	geo_mixer_set_rate (SAMPLE_RATE);
	geo_mixer_init ();
	geo_mixer_set_raw (s_bSpeex ? 0 : 1);
	geo_mixer_set_callback (s_bSpeex ? AudioCallback : AudioCallbackLinear);
	s_nResPos = 0;
	s_ResPrev[0] = s_ResPrev[1] = 0;
	geo_init ();
	geo_set_region ((int) m_nRegion);
	ClearVideoMemory ();
	snprintf (Line, sizeof Line, "  system %s, region %s", m_nSystem == 0 ? "MVS" : "AES", RegionName (m_nRegion));
	Log (Line, COLOR_DIM);
	if (!geo_bios_load_mem (m_pBIOS, m_nBIOSSize))
	{
		Fatal (NEOPI_ERR_BIOS, "the required BIOS (MVS/AES, region) is missing from neogeo.zip");
	}
	if (!geo_neo_load (m_pGame, m_nGameSize))
	{
		Log ("  invalid .neo file", COLOR_ERR);
		geo_deinit ();
		geo_bios_unload ();
		delete [] m_pGame;
		m_pGame = 0;
		return FALSE;
	}
	m_nLoaded = (int) nIndex;
	m_nShots = 0;					// screenshot numbers: next free one

	static const char *Ext[3] = {"nv", "srm", "mcr"};
	for (unsigned i = 0; i < 3; i++)
	{
		char SavePath[160];
		GamePath (SavePath, sizeof SavePath, Ext[i]);
		geo_savedata_load (i, SavePath);
	}

	geo_lspc_set_palette (0);		// resistor network palette (as on hardware)
	geo_reset (1);

	WriteTextFile (LAST_PATH, G.File);
	return TRUE;
}

// ---- video helpers ----------------------------------------------------------

void CKernel::ShowUIFrame (boolean bWait)
{
	m_Video.DrawGame (m_pUI->GetBuffer ());
	m_Video.DrawBandText (TRUE, 0, 0);		// no status line or message over menus
	m_Video.DrawBandText (FALSE, 0, 0);
	m_Video.Flip ();
	if (!bWait)
	{
		return;
	}
	if (m_bVSync)
	{
		m_Video.WaitForVSync ();
	}
	else
	{
		CTimer::SimpleusDelay (16667);
	}
}

// Select+A: the original Neo Geo picture (304x224, no filter) as
// neopi/shots/<game>_NNN.png; returns the file name for the message, or 0
const char *CKernel::SaveScreenshot (void)
{
	static char Name[80];
	if (m_nLoaded < 0)
	{
		return 0;
	}
	char Base[64], Path[160];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';

	// next free number (the search starts after the last one used)
	FILINFO Info;
	for (;;)
	{
		if (++m_nShots > 999)
		{
			return 0;
		}
		snprintf (Name, sizeof Name, "%s_%03u.png", Base, m_nShots);
		snprintf (Path, sizeof Path, "%s/%s", SHOT_DIR, Name);
		if (f_stat (Path, &Info) != FR_OK)
		{
			break;
		}
	}

	static u8 RGB[GAME_W * GAME_H * 3];
	u8 *d = RGB;
	for (unsigned y = 0; y < GAME_H; y++)
	{
		const u32 *s = s_VideoBuf + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
		for (unsigned x = 0; x < GAME_W; x++)
		{
			*d++ = (u8) (s[x] >> 16);
			*d++ = (u8) (s[x] >> 8);
			*d++ = (u8) s[x];
		}
	}
	size_t nSize = 0;
	void *pPng = neopi_png_encode (RGB, GAME_W, GAME_H, &nSize);
	if (pPng == 0)
	{
		return 0;
	}
	FIL File;
	boolean bOK = FALSE;
	if (f_open (&File, Path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		bOK = f_write (&File, pPng, nSize, &nWritten) == FR_OK && nWritten == nSize;
		f_close (&File);
	}
	neopi_png_free (pPng);

	char Line[200];
	snprintf (Line, sizeof Line, "  screenshot %s: %s", Name, bOK ? "saved" : "FAILED");
	Log (Line, bOK ? COLOR_TEXT : COLOR_ERR);
	return bOK ? Name : 0;
}

// ---- benchmark -------------------------------------------------------------

void CKernel::Benchmark (void)
{
	static unsigned Emu[BENCH_FRAMES], Draw[BENCH_FRAMES], Total[BENCH_FRAMES];
	u64 nSamples = 0;

	boolean bQuiet = m_bQuiet;
	ApplyVideoMode ();		// measure the real drawing path (filter, HVS)
	m_bQuiet = TRUE;

	// 1. warm-up: let the BIOS logo and the game's start-up pass
	for (unsigned i = 0; i < BENCH_WARMUP; i++)
	{
		geo_exec ();
		m_Video.DrawGame (s_VideoBuf);
	}

	// 2. measurement, with the sampling profiler running
	CProfiler *pProfiler = new CProfiler (&m_Interrupt);
	boolean bProfile = pProfiler && pProfiler->Start (BENCH_PROFILE_US);

	enum { Worst = 10 };
	unsigned WorstIdx[Worst];
	unsigned nWorst = 0;
	unsigned nSlow = 0, nOver = 0;

	for (unsigned i = 0; i < BENCH_FRAMES; i++)
	{
		if (bProfile) pProfiler->BeginFrame ();
		unsigned t0 = CTimer::GetClockTicks ();
		geo_exec ();
		unsigned t1 = CTimer::GetClockTicks ();
		m_Video.DrawGame (s_VideoBuf);
		unsigned t2 = CTimer::GetClockTicks ();
		boolean bSlow = t2 - t0 >= BENCH_SLOW_US;
		if (bProfile) pProfiler->EndFrame (bSlow);

		Emu[i] = t1 - t0;
		Draw[i] = t2 - t1;
		Total[i] = t2 - t0;
		nSamples += s_nAudioSamples;
		if (bSlow) nSlow++;
		if (Total[i] > 16896) nOver++;

		// keep the indices of the slowest frames, slowest first
		unsigned k;
		if (nWorst < Worst)
		{
			k = nWorst++;
		}
		else if (Total[i] > Total[WorstIdx[Worst - 1]])
		{
			k = Worst - 1;
		}
		else
		{
			continue;
		}
		while (k > 0 && Total[WorstIdx[k-1]] < Total[i])
		{
			WorstIdx[k] = WorstIdx[k-1];
			k--;
		}
		WorstIdx[k] = i;
	}
	if (bProfile) pProfiler->Stop ();

	m_bQuiet = bQuiet;
	m_Video.SetConsoleMode ();
	RedrawConsole ();

	TStats E, D, T;
	ComputeStats (Emu, BENCH_FRAMES, &E);
	ComputeStats (Draw, BENCH_FRAMES, &D);
	ComputeStats (Total, BENCH_FRAMES, &T);

	char a[16], b[16], c[16], d[16];
	char Report[4096];
	size_t nPos = 0;
	char Line[160];

	CMachineInfo *pInfo = CMachineInfo::Get ();
	const unsigned nFrameUs = 16896;
	unsigned nSpeed = T.nAvg ? (unsigned) ((u64) nFrameUs * 100 / T.nAvg) : 0;

	#define REPORT(color) do { Log (Line, color); \
		nPos += snprintf (Report + nPos, sizeof Report - nPos, "%s\r\n", Line); } while (0)

	snprintf (Line, sizeof Line, "neo-pi %s - %s, %u MB, CPU %u MHz, %u C",
		  NEOPI_VERSION, pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Game: %s (measured after %u s of play, for %u s)",
		  m_pGames[m_nLoaded].File, BENCH_WARMUP / 60, BENCH_FRAMES / 60);
	REPORT (COLOR_TEXT);
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	snprintf (Line, sizeof Line, "Screen: mode %s, %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_nFilter));
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Frame times (ms)            avg       p99     max     min");
	REPORT (COLOR_TITLE);
	FormatMs (a, sizeof a, E.nAvg); FormatMs (b, sizeof b, E.nP99);
	FormatMs (c, sizeof c, E.nMax); FormatMs (d, sizeof d, E.nMin);
	snprintf (Line, sizeof Line, "  emulation + audio       %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	FormatMs (a, sizeof a, D.nAvg); FormatMs (b, sizeof b, D.nP99);
	FormatMs (c, sizeof c, D.nMax); FormatMs (d, sizeof d, D.nMin);
	snprintf (Line, sizeof Line, "  drawing to HDMI         %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	FormatMs (a, sizeof a, T.nAvg); FormatMs (b, sizeof b, T.nP99);
	FormatMs (c, sizeof c, T.nMax); FormatMs (d, sizeof d, T.nMin);
	snprintf (Line, sizeof Line, "  total                   %7s %7s %7s %7s", a, b, c, d);
	REPORT (COLOR_TEXT);
	snprintf (Line, sizeof Line, "Top speed: %u%% of a real Neo Geo (100%% = 59.19 fps)", nSpeed);
	REPORT (nSpeed >= 150 ? COLOR_OK : COLOR_ERR);
	snprintf (Line, sizeof Line, "Frames over 16.9 ms (too slow for full speed): %u of %u (%u.%u%%)",
		  nOver, BENCH_FRAMES, nOver * 100 / BENCH_FRAMES, (nOver * 1000 / BENCH_FRAMES) % 10);
	REPORT (nOver ? COLOR_ERR : COLOR_OK);
	snprintf (Line, sizeof Line, "Slowest frames (second.frame: emulation + drawing ms):");
	REPORT (COLOR_TITLE);
	for (unsigned w = 0; w < nWorst; w += 2)
	{
		size_t n = 0;
		Line[0] = 0;
		for (unsigned j = w; j < w + 2 && j < nWorst; j++)
		{
			unsigned f = WorstIdx[j];
			FormatMs (a, sizeof a, Emu[f]); FormatMs (b, sizeof b, Draw[f]);
			n += snprintf (Line + n, sizeof Line - n, "  %3u.%02u: %s + %s",
				       f / 60, f % 60, a, b);
		}
		REPORT (COLOR_TEXT);
	}
	snprintf (Line, sizeof Line, "Audio: %u stereo samples per frame at %u Hz, resampler %s",
		  (unsigned) (nSamples / BENCH_FRAMES), SAMPLE_RATE, s_bSpeex ? "speex" : "linear");
	REPORT (COLOR_DIM);
	// same game + same build options must give the same picture after the
	// same number of frames: used to check compiler optimisations
	u32 nHash = 2166136261U;
	for (unsigned i = 0; i < GAME_SRC_PITCH * GAME_SRC_LINES; i++)
	{
		nHash = (nHash ^ s_VideoBuf[i]) * 16777619U;
	}
	snprintf (Line, sizeof Line, "Check: picture after %u frames = %08X",
		  BENCH_WARMUP + BENCH_FRAMES, nHash);
	REPORT (COLOR_DIM);
	if (bProfile)
	{
		snprintf (Line, sizeof Line, "Profile: %u samples (%u in frames over %u ms) -> PROFILE.TXT",
			  pProfiler->GetTotal (), pProfiler->GetSlow (), BENCH_SLOW_US / 1000);
	}
	else
	{
		snprintf (Line, sizeof Line, "Profile: not available");
	}
	REPORT (COLOR_DIM);

	WriteTextFile (BENCH_PATH, Report);
	if (bProfile)
	{
		const size_t nSize = 1024 * 1024;
		char *pText = new char[nSize];
		if (pText)
		{
			pProfiler->Format (pText, nSize);
			WriteTextFile (PROFILE_PATH, pText);
			delete [] pText;
		}
	}
	delete pProfiler;
	Log ("Results saved to neopi/BENCH.TXT and neopi/PROFILE.TXT", COLOR_OK);
	SaveLog ();

	geo_reset (1);		// play from a clean boot
}

// ---- statistics -------------------------------------------------------------

void CKernel::ReportWindow (boolean bFinal)
{
	if (s_nWindowFrames < 60)
	{
		return;
	}
	unsigned nElapsed = CTimer::GetClockTicks () - s_nWindowStart;
	unsigned nFPS100 = (unsigned) ((u64) s_nWindowFrames * 100 * 1000000 / nElapsed);
	char ea[16], ep[16], em[16], wa[16], wp[16], pm[16];
	FormatMs (ea, sizeof ea, s_HistEmu.Avg ());
	FormatMs (ep, sizeof ep, s_HistEmu.P99 ());
	FormatMs (em, sizeof em, s_HistEmu.nMax);
	FormatMs (wa, sizeof wa, s_HistWork.Avg ());
	FormatMs (wp, sizeof wp, s_HistWork.P99 ());
	FormatMs (pm, sizeof pm, s_HistPeriod.nMax);
	unsigned qmin, qmax, und;
	int adj;
	m_Audio.GetStats (&qmin, &qmax, &und, &adj);

	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line,
		  "[%s] %u frames, FPS %u.%02u | emu %s/%s/%s ms | emu+draw %s/%s ms (>16ms: %u)",
		  bFinal ? "partial" : "1 min", s_nWindowFrames, nFPS100 / 100, nFPS100 % 100,
		  ea, ep, em, wa, wp, s_nOverBudget);
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line,
		  "   video %s x%u %s | max period %s ms, missed vsyncs %u | audio %u-%u, underruns %u, adj %d | %u MHz %u C",
		  Tag, m_Video.GetScale (), FilterShort (m_Video.GetFilter ()),
		  pm, s_nMissedVSync, qmin, qmax, und, adj,
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);

	s_HistEmu.Reset ();
	s_HistWork.Reset ();
	s_HistPeriod.Reset ();
	s_nOverBudget = s_nMissedVSync = s_nWindowFrames = 0;
	s_nWindowStart = CTimer::GetClockTicks ();
}

// ---- configuration, system menu, power off ---------------------------------

const char *CKernel::FilterShort (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCAN-L";
	case FILTER_SCAN_MEDIUM: return "SCAN-M";
	case FILTER_SCAN_STRONG: return "SCAN-S";
	default:		 return "NOFLT";
	}
}

const char *CKernel::FilterName (unsigned nFilter)
{
	switch (nFilter)
	{
	case FILTER_SCAN_LIGHT:	 return "SCANLINES_LIGHT";
	case FILTER_SCAN_MEDIUM: return "SCANLINES";
	case FILTER_SCAN_STRONG: return "SCANLINES_STRONG";
	default:		 return "NONE";
	}
}

// "HVS4", "HVS2" or "CPU": how the picture is being enlarged right now
void CKernel::VideoModeTag (char *pOut, size_t nSize)
{
	if (m_Video.GetHVSFactor () > 1)
	{
		snprintf (pOut, nSize, "HVS%u", m_Video.GetHVSFactor ());
	}
	else
	{
		snprintf (pOut, nSize, "CPU");
	}
}

void CKernel::ApplyVideoMode (void)
{
	if (!m_Video.SetGameMode (m_nFilter, m_bHVS))
	{
		Fatal (NEOPI_ERR_FB, "cannot set up the game framebuffer");
	}
	char Tag[8];
	VideoModeTag (Tag, sizeof Tag);
	char Line[MaxLineLen];
	snprintf (Line, sizeof Line, "Video: mode %s, HDMI %ux%u, framebuffer %ux%u, scale %ux (HVS %ux, CPU %ux), filter %s%s",
		  Tag, m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetFBWidth (), m_Video.GetFBHeight (), m_Video.GetScale (),
		  m_Video.GetHVSFactor (), m_Video.GetScale () / m_Video.GetHVSFactor (),
		  FilterName (m_Video.GetFilter ()), m_bHVS ? "" : " (scaler=cpu)");
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "Video: pixel order %s (%u), red in the %s byte%s",
		  m_Video.GetPixelOrder () ? "RGB" : "BGR", m_Video.GetPixelOrder (),
		  m_Video.IsRedLow () ? "low" : "high", "");
	Log (Line, COLOR_TEXT);
}

const char *CKernel::RegionName (unsigned nRegion)
{
	switch (nRegion)
	{
	case REGION_US:	return "USA";
	case REGION_JP:	return "JAPAN";
	case REGION_AS:	return "ASIA";
	default:	return "EUROPE";
	}
}

void CKernel::StateExt (char *pExt, unsigned nSlot) const
{
	pExt[0] = 's';
	pExt[1] = m_nSystem == 0 ? 't' : 'a';		// separate states for MVS and AES
	pExt[2] = (char) ('1' + nSlot);
	pExt[3] = '\0';
}

void CKernel::LoadConfig (void)
{
	FIL File;
	char Text[256];
	UINT nRead = 0;
	if (f_open (&File, CFG_PATH, FA_READ) != FR_OK)
	{
		return;
	}
	f_read (&File, Text, sizeof Text - 1, &nRead);
	f_close (&File);
	Text[nRead] = '\0';

	if (strstr (Text, "system=AES"))	m_nSystem = 1;
	if (strstr (Text, "region=USA"))	m_nRegion = REGION_US;
	if (strstr (Text, "region=JAPAN"))	m_nRegion = REGION_JP;
	if (strstr (Text, "region=ASIA"))	m_nRegion = REGION_AS;
	if (strstr (Text, "filter=TV"))				m_nFilter = FILTER_SCAN_MEDIUM;	// removed in 0.6.0
	else if (strstr (Text, "filter=SCANLINES_LIGHT"))	m_nFilter = FILTER_SCAN_LIGHT;
	else if (strstr (Text, "filter=SCANLINES_STRONG"))	m_nFilter = FILTER_SCAN_STRONG;
	else if (strstr (Text, "filter=SCANLINES"))		m_nFilter = FILTER_SCAN_MEDIUM;
	if (strstr (Text, "scaler=cpu"))	m_bHVS = FALSE;
	if (strstr (Text, "filters=on"))	m_bFilterAllowed = m_bForceFilters = TRUE;	// e.g. to try them on a Pi 3
	if (!m_bFilterAllowed)			m_nFilter = FILTER_NONE;
	// USB pads are polled every 1 ms (like usbhid.jspoll=1 on Linux);
	// usbpoll=native keeps the interval requested by the device
	if (strstr (Text, "usbpoll=native"))	g_nUSBInterruptPollMs = 0;
	// resampler=speex: Geolith's own high quality resampler (heavier)
	if (strstr (Text, "resampler=speex"))	s_bSpeex = TRUE;

	char Line[140];
	snprintf (Line, sizeof Line, "Settings: system %s, region %s, filter %s, USB polling %s, resampler %s",
		  m_nSystem == 0 ? "MVS" : "AES", RegionName (m_nRegion), FilterName (m_nFilter),
		  g_nUSBInterruptPollMs ? "1 ms" : "native", s_bSpeex ? "speex" : "linear");
	Log (Line, COLOR_TEXT);
}

void CKernel::SaveConfig (void)
{
	char Text[200];
	snprintf (Text, sizeof Text, "system=%s\r\nregion=%s\r\nfilter=%s\r\n%s",
		  m_nSystem == 0 ? "MVS" : "AES", RegionName (m_nRegion),
		  FilterName (m_nFilter), m_bHVS ? "" : "scaler=cpu\r\n");
	if (m_bForceFilters)
	{
		strcat (Text, "filters=on\r\n");
	}
	if (g_nUSBInterruptPollMs == 0)
	{
		strcat (Text, "usbpoll=native\r\n");
	}
	if (s_bSpeex)
	{
		strcat (Text, "resampler=speex\r\n");
	}
	WriteTextFile (CFG_PATH, Text);
}

void CKernel::RunSystemMenu (void)
{
	enum { SmSystem, SmRegion, SmFilter, SmControls, SmPower, SmCount };
	static const unsigned Regions[4] = {REGION_EU, REGION_US, REGION_JP, REGION_AS};

	unsigned nSel = 0;
	u32 nPrev = ~0u;
	unsigned nOldSystem = m_nSystem, nOldRegion = m_nRegion, nOldFilter = m_nFilter;
	const char *pMsg = "";
	unsigned nFrame = 0;

	CMachineInfo *pInfo = CMachineInfo::Get ();
	char Footer[48];
	snprintf (Footer, sizeof Footer, "NEO-PI " NEOPI_VERSION "  PI %u  %u C", pInfo->GetModelMajor (),
		  m_CPUThrottle.GetTemperature ());

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
		}
		if (nFrame % 120 == 0)
		{
			snprintf (Footer, sizeof Footer, "NEO-PI " NEOPI_VERSION "  PI %u  %u C", pInfo->GetModelMajor (),
				  m_CPUThrottle.GetTemperature ());
		}

		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		nPrev = n;

		if (nNew & BTN_UP)	{ nSel = (nSel + SmCount - 1) % SmCount; pMsg = ""; }
		if (nNew & BTN_DOWN)	{ nSel = (nSel + 1) % SmCount; pMsg = ""; }
		int nDir = (nNew & (BTN_A | BTN_RIGHT)) ? 1 : ((nNew & BTN_LEFT) ? -1 : 0);

		if (nDir != 0)
		{
			switch (nSel)
			{
			case SmSystem:
				m_nSystem ^= 1;
				break;

			case SmRegion: {
				unsigned i = 0;
				while (i < 4 && Regions[i] != m_nRegion) i++;
				i = (i + 4 + (unsigned) nDir) % 4;
				m_nRegion = Regions[i];
				break; }

			case SmFilter:
				if (m_bFilterAllowed)
				{
					m_nFilter = (m_nFilter + FILTER_COUNT + (unsigned) nDir) % FILTER_COUNT;
					ApplyVideoMode ();
				}
				else if (nNew & BTN_A)
				{
					pMsg = "SCANLINES NEED A PI 4";
				}
				break;

			case SmControls:
				if (nNew & BTN_A)
				{
					int nPad = ChoosePad ();
					if (nPad >= 0)
					{
						RunMapping ((unsigned) nPad, m_nLoaded >= 0);
					}
					else if (m_pInput->GetPadCount () == 0)
					{
						pMsg = "CONNECT A USB GAMEPAD";
					}
					nPrev = ~0u;
				}
				break;

			case SmPower:
				if ((nNew & BTN_A) && PowerOffCountdown ())
				{
					PowerOff ();
				}
				nPrev = ~0u;
				break;
			}
		}

		if (nNew & BTN_B)
		{
			break;
		}

		static const char *Labels[SmCount] = {"SYSTEM", "REGION", "SCANLINES", "CONTROLS", "POWER OFF"};
		TMenuItem Items[SmCount];
		for (unsigned i = 0; i < SmCount; i++)
		{
			Items[i].pLabel = Labels[i];
			Items[i].pValue = 0;
			Items[i].bDisabled = FALSE;
		}
		Items[SmSystem].pValue = m_nSystem == 0 ? "MVS" : "AES";
		Items[SmRegion].pValue = RegionName (m_nRegion);
		static const char *FilterLabels[FILTER_COUNT] = {"OFF", "LIGHT", "MEDIUM", "STRONG"};
		Items[SmFilter].pValue = m_bFilterAllowed ? FilterLabels[m_nFilter] : "PI 4 ONLY";
		Items[SmFilter].bDisabled = !m_bFilterAllowed;

		const char *pShow = pMsg;
		if (!pShow[0] && (m_nSystem != nOldSystem || m_nRegion != nOldRegion))
		{
			pShow = "APPLIES TO NEXT GAME";
		}
		static const TButtonHint Hints[2] = {{'A', "CHANGE"}, {'B', "BACK"}};
		m_pUI->DrawPanelMenu (m_nLoaded >= 0, "OPTIONS", Items, SmCount, nSel, pShow, UI_VALUE,
				      Hints, 2, Footer);
		ShowUIFrame ();
	}

	if (m_nSystem != nOldSystem || m_nRegion != nOldRegion || m_nFilter != nOldFilter)
	{
		SaveConfig ();
		char Line[120];
		snprintf (Line, sizeof Line, "New settings: system %s, region %s, filter %s",
			  m_nSystem == 0 ? "MVS" : "AES", RegionName (m_nRegion), FilterName (m_nFilter));
		Log (Line, COLOR_TEXT);
	}
	if ((m_nSystem != nOldSystem || m_nRegion != nOldRegion) && m_nLoaded >= 0)
	{
		UnloadGame ();		// the BIOS changes: reload the game next time
	}
}

// ---- control mapping --------------------------------------------------------

void CKernel::LoadPads (void)
{
	static char Text[4096];
	FIL File;
	UINT nRead = 0;
	if (f_open (&File, PADS_PATH, FA_READ) == FR_OK)
	{
		f_read (&File, Text, sizeof Text - 1, &nRead);
		f_close (&File);
	}
	Text[nRead] = '\0';
	m_pInput->LoadMaps (Text);
}

void CKernel::SavePads (void)
{
	static char Text[4096];
	m_pInput->FormatMaps (Text, sizeof Text);
	WriteTextFile (PADS_PATH, Text);
}

// CONTROLS from the SYSTEM menu: which pad? (the only one, or the one that is pressed)
int CKernel::ChoosePad (void)
{
	unsigned nCount = m_pInput->GetPadCount ();
	if (nCount == 0)
	{
		return -1;
	}
	if (nCount == 1)
	{
		for (unsigned i = 0; i < MAX_PADS; i++)
		{
			if (m_pInput->IsPadConnected (i))
			{
				return (int) i;
			}
		}
	}

	boolean bReleased = FALSE;
	for (unsigned f = 0; f < 600; f++)		// 10 seconds
	{
		int nActive = m_pInput->GetActivePad ();
		if (!bReleased)
		{
			bReleased = nActive < 0;
		}
		else if (nActive >= 0)
		{
			return nActive;
		}
		if (m_pInput->GetKeyboard () & BTN_MENU)
		{
			break;
		}
		m_pUI->DrawMessage ("CONTROLS", "PRESS A BUTTON ON THE PAD", "YOU WANT TO SET UP");
		ShowUIFrame ();
	}
	return -1;
}

// Asks for A B C D START SELECT in turn on one pad (directions are automatic),
// then saves on its own. A button already used is ignored; Esc on the
// keyboard cancels.
boolean CKernel::RunMapping (unsigned nSlot, boolean bOverGame)
{
	static const char *Names[6] = {"A", "B", "C", "D", "START", "SELECT / COIN"};
	const unsigned nSteps = MAP_COUNT - MAP_FIRST_BUTTON;

	TBind Binds[MAP_COUNT];
	memset (Binds, 0, sizeof Binds);
	static char Values[6][16];
	const char *pValues[6];
	for (unsigned i = 0; i < 6; i++)
	{
		Values[i][0] = '\0';
		pValues[i] = Values[i];
	}

	unsigned nStep = 0;
	boolean bWaitRelease = TRUE;		// e.g. A still held from the SYSTEM menu
	u32 nKeyPrev = ~0u;
	unsigned nFrame = 0;

	char Line[120];
	snprintf (Line, sizeof Line, "Controls: setting up pad %u (%04x:%04x)",
		  nSlot + 1, m_pInput->GetVendor (nSlot), m_pInput->GetProduct (nSlot));
	Log (Line, COLOR_TEXT);

	char Title[32], Footer[40];
	snprintf (Title, sizeof Title, "CONTROLS  PLAYER %u", m_pInput->GetPlayerOfPad (nSlot) + 1);
	snprintf (Footer, sizeof Footer, "PAD %04X:%04X", m_pInput->GetVendor (nSlot),
		  m_pInput->GetProduct (nSlot));

	while (nStep < nSteps)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
			if (!m_pInput->IsPadConnected (nSlot))
			{
				Log ("  pad unplugged: set-up cancelled", COLOR_TEXT);
				return FALSE;
			}
		}

		u32 nKey = m_pInput->GetKeyboard ();
		u32 nKeyNew = nKeyPrev == ~0u ? 0 : (nKey & ~nKeyPrev);
		nKeyPrev = nKey;
		if (nKeyNew & BTN_MENU)
		{
			Log ("  set-up cancelled", COLOR_TEXT);
			return FALSE;
		}

		if (bWaitRelease)
		{
			bWaitRelease = !m_pInput->IsReleased (nSlot);
		}
		else
		{
			int nButton = m_pInput->CaptureButton (nSlot);
			if (nButton >= 0)
			{
				boolean bUsed = FALSE;
				for (unsigned i = 0; i < nStep; i++)
				{
					if (Binds[MAP_FIRST_BUTTON + i].nIndex == (u8) nButton)
					{
						bUsed = TRUE;
					}
				}
				if (!bUsed)
				{
					TBind &B = Binds[MAP_FIRST_BUTTON + nStep];
					B.nType = BindButton;
					B.nIndex = (u8) nButton;
					B.nValue = 0;
					CInput::DescribeBind (B, Values[nStep], sizeof Values[nStep]);
					nStep++;
				}
				bWaitRelease = TRUE;
			}
		}

		m_pUI->DrawMapping (bOverGame, Title, (int) nStep, pValues, Footer);
		ShowUIFrame ();
	}

	m_pInput->SetMap (nSlot, Binds);
	SavePads ();
	for (unsigned i = 0; i < nSteps; i++)
	{
		snprintf (Line, sizeof Line, "  %s = %s", Names[i], Values[i]);
		Log (Line, COLOR_TEXT);
	}
	Log ("  controls saved to neopi/pads.cfg", COLOR_TEXT);
	SaveLog ();

	// "SAVED" for one second
	for (unsigned f = 0; f < 60; f++)
	{
		m_pUI->DrawMapping (bOverGame, Title, (int) nSteps, pValues, Footer);
		ShowUIFrame ();
	}
	return TRUE;
}

boolean CKernel::PowerOffCountdown (void)
{
	// 3 seconds, B cancels; drawn over whatever the UI buffer shows
	static u32 Saved[GAME_SRC_PITCH * GAME_SRC_LINES];
	memcpy (Saved, m_pUI->GetBuffer (), sizeof Saved);
	u32 nPrev = ~0u;
	for (unsigned f = 0; f < 180; f++)
	{
		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		nPrev = n;
		if (nNew & BTN_B)
		{
			memcpy (m_pUI->GetBuffer (), Saved, sizeof Saved);
			Log ("  power off cancelled", COLOR_TEXT);
			return FALSE;
		}
		char Line[24];
		snprintf (Line, sizeof Line, "POWER OFF IN %u", 3 - f / 60);
		memcpy (m_pUI->GetBuffer (), Saved, sizeof Saved);
		static const TButtonHint Cancel = {'B', "CANCEL"};
		m_pUI->DrawBox (Line, "", &Cancel);
		ShowUIFrame ();
	}
	return TRUE;
}

void CKernel::PowerOff (void)
{
	Log ("Power off: saving data and halting", COLOR_TITLE);
	SaveGameData ();
	SaveLog ();
	f_mount (0, "SD:", 0);
	m_bSDOK = FALSE;

	// black picture, then halt (the firmware stops the HDMI output)
	m_Video.Clear (0, RGB (0, 0, 0));
	m_Video.Clear (1, RGB (0, 0, 0));
	m_Video.ShowBuffer (0);
	CTimer::SimpleMsDelay (300);

	CBcmWatchdog Watchdog;
	Watchdog.Restart (CBcmWatchdog::PartitionHalt);
	while (1)
	{
	}
}

// ---- game list ------------------------------------------------------------

unsigned CKernel::RunList (unsigned nSel)
{
	u32 nPrev = ~0u;		// require a release before acting
	unsigned nRepeat = 0;
	unsigned nFrame = 0;

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
			m_pInput->FlushDiagnostics ();

			// a pad never seen before: set up its controls first
			int nPad = m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				RunMapping ((unsigned) nPad, m_nLoaded >= 0);
				nPrev = ~0u;
				continue;
			}
		}

		u32 n = m_pInput->GetAny ();
		u32 nNew = n & ~nPrev;

		// up/down with auto-repeat, left/right = page
		u32 nMove = nNew;
		if (n & (BTN_UP | BTN_DOWN))
		{
			if (++nRepeat > 20 && nRepeat % 4 == 0)
			{
				nMove |= n & (BTN_UP | BTN_DOWN);
			}
		}
		else
		{
			nRepeat = 0;
		}
		if ((nMove & BTN_UP) && nSel > 0)			nSel--;
		if ((nMove & BTN_DOWN) && nSel + 1 < m_nGames)		nSel++;
		if (nNew & BTN_LEFT)	nSel = nSel >= 8 ? nSel - 8 : 0;
		if (nNew & BTN_RIGHT)	nSel = nSel + 8 < m_nGames ? nSel + 8 : m_nGames - 1;

		if (nNew & (BTN_A | BTN_START))
		{
			return nSel;
		}
		if (nNew & BTN_D)
		{
			RunSystemMenu ();
			n = ~0u;		// wait for release
		}
		nPrev = n;

		const char *pMsg = m_pInput->GetPadCount () == 0 ? "CONNECT A USB GAMEPAD" : "";
		m_pUI->DrawList (m_pGames, m_nGames, nSel, pMsg, m_nLoaded >= 0);
		ShowUIFrame ();
	}
}

// ---- game loop ------------------------------------------------------------

// statistics windows restart after a pause (menu, credits)
void CKernel::ResetGameStats (void)
{
	s_nWindowStart = CTimer::GetClockTicks ();
	s_HistEmu.Reset (); s_HistWork.Reset (); s_HistPeriod.Reset ();
	s_nOverBudget = s_nMissedVSync = s_nWindowFrames = 0;
}

// Credits over the paused game; B (or Esc) goes back to the game
void CKernel::RunCredits (void)
{
	Log ("  credits", COLOR_TEXT);
	u32 nPrev = ~0u;
	for (unsigned nFrame = 0; ; nFrame++)
	{
		if (nFrame % 15 == 0)
		{
			m_pInput->UpdateDevices ();	// a new pad is set up after the credits
		}
		u32 n = m_pInput->GetAny ();
		u32 nNew = nPrev == ~0u ? 0 : (n & ~nPrev);
		if (nPrev == ~0u)
		{
			if (n == 0) nPrev = 0;	// wait for Select and D to be released
		}
		else
		{
			nPrev = n;
		}
		if (nNew & (BTN_B | BTN_MENU))
		{
			return;
		}
		m_pUI->DrawCredits (NEOPI_VERSION);
		ShowUIFrame ();
	}
}

void CKernel::RunGame (void)
{
	const char *pName = m_pGames[m_nLoaded].Name;

	// status line (Select+C), refreshed once per second
	char StFps[16] = "", StLoad[8] = "", StTemp[8] = "", StRes[8] = "", StScale[16] = "";
	u32 nFpsColor = UI_WHITE, nLoadColor = UI_OK, nTempColor = UI_WHITE;
	boolean bStatusReady = FALSE;

	// message in the top band (screenshot), in frames
	char Toast[64] = "";
	unsigned nToast = 0, nClearToast = 0;

	boolean bMenu = FALSE;
	unsigned nMenuSel = 0;
	boolean bSaved = FALSE;			// one save state per game
	char MenuMsg[40] = "";
	u32 nMenuMsgColor = UI_OK;

	u32 nPrev[2] = {~0u, ~0u};		// wait for release after the list
	boolean bSelectCombo[2] = {FALSE, FALSE};
	unsigned nCoin[2] = {0, 0};
	unsigned nSelectHeld[2] = {0, 0};	// frames Select has been held alone
	boolean bLazy[2] = {FALSE, FALSE};	// lazy finger (turbo on A-D), Select+B
	unsigned nLazyCount[2][4] = {{0}};	// frames each of A-D has been held
	u32 nMenuPrev = ~0u;
	m_pInput->RestartDiagnostics ();		// log the pad reports of this game too

	unsigned nFrames = 0;
	unsigned nWinStart = CTimer::GetClockTicks ();
	unsigned nWorkMax = 0;
	unsigned nLastVSync = CTimer::GetClockTicks ();
	unsigned nNextDeadline = nLastVSync + 16667;
	unsigned nTotalFrames = 0;

	s_HistEmu.Reset (); s_HistWork.Reset (); s_HistPeriod.Reset ();
	s_nOverBudget = s_nMissedVSync = s_nWindowFrames = 0;
	s_nWindowStart = nLastVSync;
	{
		unsigned a, b, c;
		int d;
		m_Audio.GetStats (&a, &b, &c, &d);	// reset audio statistics
	}

	while (1)
	{
		unsigned t0 = CTimer::GetClockTicks ();
		unsigned t1 = t0;

		if (nTotalFrames % 30 == 0)
		{
			m_pInput->UpdateDevices ();

			int nPad = bMenu ? -1 : m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				// new pad during the game: pause and set it up
				m_pUI->SnapshotGame (s_VideoBuf);
				RunMapping ((unsigned) nPad, TRUE);
				nPrev[0] = nPrev[1] = ~0u;
			}
		}

		// read controls as late as possible, right before emulating the frame
		u32 Pad[2] = {m_pInput->Get (0), m_pInput->Get (1)};

		if (!bMenu)
		{
			boolean bOpenMenu = FALSE, bOpenCredits = FALSE, bScreenshot = FALSE;
			u32 Eff[2] = {0, 0};
			for (unsigned p = 0; p < 2; p++)
			{
				u32 n = Pad[p];
				if (nPrev[p] == ~0u)
				{
					// buttons still held from the list or the menu: ignore until released
					if (n == 0)
					{
						nPrev[p] = 0;
					}
					s_Joy[p] = 0xFF;
					continue;
				}

				u32 nNew = n & ~nPrev[p];

				if (n & BTN_SELECT)
				{
					// Select + A: screenshot, + B: lazy finger, + C: status line, + D: credits
					if (nNew & (BTN_A | BTN_B | BTN_C | BTN_D))
					{
						bSelectCombo[p] = TRUE;
					}
					if (nNew & BTN_A)
					{
						bScreenshot = TRUE;
					}
					if (nNew & BTN_B)
					{
						bLazy[p] = !bLazy[p];
						snprintf (Toast, sizeof Toast, "%sLAZY FINGER %s",
							  p == 0 ? "" : "P2 ", bLazy[p] ? "ON" : "OFF");
						nToast = 60;
						Log (bLazy[p] ? "  lazy finger on" : "  lazy finger off", COLOR_TEXT);
					}
					if (nNew & BTN_C)
					{
						m_bStatusOn = !m_bStatusOn;
						m_nClearStatus = 2;
					}
					if (nNew & BTN_D)
					{
						bOpenCredits = TRUE;
					}
					// Select held alone for one second: menu (no coin)
					if (!bSelectCombo[p] && ++nSelectHeld[p] >= 60)
					{
						bOpenMenu = TRUE;
						bSelectCombo[p] = TRUE;
					}
				}
				else
				{
					if ((nPrev[p] & BTN_SELECT) && !bSelectCombo[p])
					{
						nCoin[p] = 8;		// short press: coin (MVS) / Select (AES)
					}
					bSelectCombo[p] = FALSE;
					nSelectHeld[p] = 0;
				}
				if (nNew & BTN_MENU)
				{
					bOpenMenu = TRUE;
				}
				nPrev[p] = n;
				Eff[p] = n;

				// Lazy finger: a held A-D button repeats, 2 frames pressed and
				// 2 released (15 per second), starting pressed at once
				u32 nGame = n;
				static const u32 LazyBtn[4] = {BTN_A, BTN_B, BTN_C, BTN_D};
				for (unsigned b = 0; b < 4; b++)
				{
					if (!(n & LazyBtn[b]))
					{
						nLazyCount[p][b] = 0;
					}
					else if (bLazy[p] && (nLazyCount[p][b]++ & 3) >= 2)
					{
						nGame &= ~LazyBtn[b];
					}
				}

				// Neo Geo panel: bits 0-7 = up down left right A B C D (active low);
				// while Select is held only the joystick goes through
				u32 nPanel = (n & BTN_SELECT) ? (n & 0x0F) : nGame;
				s_Joy[p] = ~nPanel & 0xFF;
			}

			if (bOpenMenu || bOpenCredits)
			{
				Eff[0] = Eff[1] = 0;		// nothing reaches the game while they open
			}
			unsigned nStatA = 0x07 | 0x18;
			unsigned nStatB = 0x0F | 0x30;
			if (m_nSystem == 0)
			{
				if (nCoin[0]) { nStatA &= ~0x01u; nCoin[0]--; }
				if (nCoin[1]) { nStatA &= ~0x02u; nCoin[1]--; }
			}
			else
			{
				if (nCoin[0]) { nStatB &= ~0x02u; nCoin[0]--; }	// AES: P1 Select
				if (nCoin[1]) { nStatB &= ~0x08u; nCoin[1]--; }	// AES: P2 Select
			}
			if (s_nTestFrames) s_nTestFrames--;
			if ((Eff[0] & BTN_START) && !(Eff[0] & BTN_SELECT)) nStatB &= ~0x01u;
			if ((Eff[1] & BTN_START) && !(Eff[1] & BTN_SELECT)) nStatB &= ~0x04u;
			s_StatA = nStatA;
			s_StatB = nStatB;

			if (bOpenCredits)
			{
				// Select + D: credits over the paused game (B closes)
				s_Joy[0] = s_Joy[1] = 0xFF;
				s_StatA = 0x07 | 0x18;
				s_StatB = 0x0F | 0x30;
				m_pUI->SnapshotGame (s_VideoBuf);
				RunCredits ();
				nPrev[0] = nPrev[1] = ~0u;
				bSelectCombo[0] = bSelectCombo[1] = FALSE;
				nToast = 0;
				nClearToast = m_nClearStatus = 2;
				ResetGameStats ();
				continue;
			}
			if (bOpenMenu)
			{
				bMenu = TRUE;
				nMenuSel = 0;
				MenuMsg[0] = '\0';
				nMenuPrev = ~0u;
				nToast = 0;
				s_Joy[0] = s_Joy[1] = 0xFF;
				s_StatA = 0x07 | 0x18;
				s_StatB = 0x0F | 0x30;
				{
					char Path[160], Ext[8];
					StateExt (Ext, 0);
					GamePath (Path, sizeof Path, Ext);
					FILINFO Info;
					bSaved = f_stat (Path, &Info) == FR_OK;
				}
				m_pUI->SnapshotGame (s_VideoBuf);
				SaveGameData ();
				m_pInput->FlushDiagnostics ();
				ReportWindow (TRUE);
				SaveLog ();
			}
			else
			{
				if (bScreenshot)
				{
					// the frame on screen, before emulating the next one
					const char *pFile = SaveScreenshot ();
					snprintf (Toast, sizeof Toast, pFile ? "SCREENSHOT SAVED  %s" : "SCREENSHOT FAILED",
						  pFile);
					nToast = 60;
				}
				geo_exec ();
				t1 = CTimer::GetClockTicks ();
				m_Audio.WriteFrame (s_AudioBuf, s_nAudioSamples);
				m_Video.DrawGame (s_VideoBuf);
			}
		}

		if (bMenu)
		{
			// item list (BIOS SETTINGS only on MVS)
			unsigned Ids[GmCount], nIds = 0;
			Ids[nIds++] = GmSave;		// RESUME is the B button
			Ids[nIds++] = GmLoad;
			Ids[nIds++] = GmReset;
			if (m_nSystem == 0) Ids[nIds++] = GmBios;
			Ids[nIds++] = GmList;
			Ids[nIds++] = GmPower;
			if (nMenuSel >= nIds) nMenuSel = 0;

			u32 n = Pad[0] | Pad[1];
			u32 nNew = nMenuPrev == ~0u ? 0 : (n & ~nMenuPrev);
			nMenuPrev = n;
			unsigned nId = Ids[nMenuSel];

			if (nNew & BTN_UP)	{ nMenuSel = (nMenuSel + nIds - 1) % nIds; MenuMsg[0] = '\0'; }
			if (nNew & BTN_DOWN)	{ nMenuSel = (nMenuSel + 1) % nIds; MenuMsg[0] = '\0'; }

			if (nNew & BTN_B)
			{
				bMenu = FALSE;		// resume
			}
			else if (nNew & BTN_A)
			{
				char Path[160], Ext[8];
				StateExt (Ext, 0);
				GamePath (Path, sizeof Path, Ext);

				switch (nId)
				{
				case GmSave:
					if (geo_state_save (Path))
					{
						bSaved = TRUE;
						snprintf (MenuMsg, sizeof MenuMsg, "SAVED");
						nMenuMsgColor = UI_OK;
					}
					else
					{
						snprintf (MenuMsg, sizeof MenuMsg, "SAVE FAILED");
						nMenuMsgColor = UI_ERR;
					}
					Log (bSaved ? "  save state: saved" : "  save state: FAILED", COLOR_TEXT);
					break;

				case GmLoad:
					if (!bSaved)
					{
						break;			// disabled until something is saved
					}
					if (geo_state_load (Path))
					{
						Log ("  save state: loaded", COLOR_TEXT);
						bMenu = FALSE;
					}
					else
					{
						snprintf (MenuMsg, sizeof MenuMsg, "LOAD FAILED");
						nMenuMsgColor = UI_ERR;
					}
					break;

				case GmReset:
					geo_reset (1);
					Log ("  reset", COLOR_TEXT);
					bMenu = FALSE;
					break;

				case GmBios:
					s_nTestFrames = 10;		// press the MVS TEST button
					Log ("  BIOS SETTINGS: TEST button", COLOR_TEXT);
					bMenu = FALSE;
					break;

				case GmList:
					SaveGameData ();
					SaveLog ();
					return;

				case GmPower:
					if (PowerOffCountdown ())
					{
						PowerOff ();
					}
					nMenuPrev = ~0u;
					break;
				}

			}
			if (!bMenu)
			{
				nPrev[0] = nPrev[1] = ~0u;	// ignore buttons until released
				bSelectCombo[0] = bSelectCombo[1] = FALSE;
				ResetGameStats ();
			}

			if (bMenu)
			{
				static const char *Labels[GmCount] =
				{
					"SAVE STATE", "LOAD STATE", "RESET",
					"BIOS SETTINGS", "GAME LIST", "POWER OFF"
				};
				TMenuItem Items[GmCount];
				for (unsigned i = 0; i < nIds; i++)
				{
					Items[i].pLabel = Labels[Ids[i]];
					Items[i].pValue = 0;
					Items[i].bDisabled = Ids[i] == GmLoad && !bSaved;
						}
				const char *pMsg = MenuMsg;
				u32 nMsgColor = nMenuMsgColor;
				if (!MenuMsg[0] && Ids[nMenuSel] == GmBios)
				{
					pMsg = "RESTARTS THE GAME";
					nMsgColor = UI_DIM;
				}
				static const TButtonHint Hints[2] = {{'A', "SELECT"}, {'B', "RESUME"}};
				m_pUI->DrawPanelMenu (TRUE, pName, Items, nIds, nMenuSel, pMsg, nMsgColor,
						      Hints, 2, 0);
				m_Video.DrawGame (m_pUI->GetBuffer ());
			}
			else
			{
				m_Video.DrawGame (s_VideoBuf);
			}
		}

		// black bands: status line at the bottom, messages at the top (only while playing)
		if (m_bStatusOn && !bMenu && bStatusReady)
		{
			const CVideoOut::TTextSpan Spans[] =
			{
				{StFps, nFpsColor}, {"   LOAD ", UI_GREY}, {StLoad, nLoadColor},
				{"   ", UI_GREY}, {StTemp, nTempColor},
				{"   ", UI_GREY}, {StRes, UI_WHITE}, {StScale, UI_GREY}
			};
			m_Video.DrawBandText (FALSE, Spans, sizeof Spans / sizeof Spans[0]);
			m_nClearStatus = 2;
		}
		else if (m_nClearStatus)
		{
			m_Video.DrawBandText (FALSE, 0, 0);
			m_nClearStatus--;
		}
		if (nToast && !bMenu)
		{
			const CVideoOut::TTextSpan Span = {Toast, UI_WHITE};
			m_Video.DrawBandText (TRUE, &Span, 1);
			nToast--;
			nClearToast = 2;
		}
		else if (nClearToast)
		{
			m_Video.DrawBandText (TRUE, 0, 0);
			nClearToast--;
		}
		unsigned t2 = CTimer::GetClockTicks ();
		m_Video.Flip ();
		unsigned t3 = CTimer::GetClockTicks ();

		if (m_bVSync)
		{
			m_Video.WaitForVSync ();
		}
		else
		{
			while ((int) (CTimer::GetClockTicks () - nNextDeadline) < 0)
			{
			}
			nNextDeadline += 16667;
		}
		unsigned t4 = CTimer::GetClockTicks ();
		unsigned nPeriod = t4 - nLastVSync;
		nLastVSync = t4;
		nTotalFrames++;

		if (!bMenu)
		{
			s_HistEmu.Add (t1 - t0);
			s_HistWork.Add (t2 - t0);
			s_HistPeriod.Add (nPeriod);
			if (t3 - t0 > 16000) s_nOverBudget++;
			if (nPeriod > 25000) s_nMissedVSync++;
			s_nWindowFrames++;
			if (s_nWindowFrames >= REPORT_FRAMES)
			{
				ReportWindow (FALSE);
				m_pInput->FlushDiagnostics ();
			}
		}

		// status line, once per second
		if (!bMenu && t2 - t0 > nWorkMax)
		{
			nWorkMax = t2 - t0;
		}
		if (++nFrames == 60)
		{
			unsigned nElapsed = t4 - nWinStart;
			unsigned nFPS100 = (unsigned) (6000ULL * 1000000 / nElapsed);

			if (m_bVSync && nFPS100 > 7500)
			{
				m_bVSync = FALSE;
				nNextDeadline = CTimer::GetClockTicks () + 16667;
				Log ("firmware vsync does not block: switching to timer pacing", COLOR_ERR);
			}

			// 60 FPS / 59.8 FPS, load = worst frame of the second vs 16.7 ms
			if (nFPS100 >= 5995)
				snprintf (StFps, sizeof StFps, "%u FPS", (nFPS100 + 50) / 100);
			else
				snprintf (StFps, sizeof StFps, "%u.%u FPS", nFPS100 / 100, (nFPS100 % 100) / 10);
			nFpsColor = nFPS100 < 5990 ? UI_WARN : UI_WHITE;
			unsigned nLoad = nWorkMax * 100 / 16667;
			snprintf (StLoad, sizeof StLoad, "%u%%", nLoad);
			nLoadColor = nLoad > 95 ? UI_ERR : (nLoad >= 75 ? UI_WARN : UI_OK);
			unsigned nTemp = m_CPUThrottle.GetTemperature ();
			snprintf (StTemp, sizeof StTemp, "%u" FONT_DEGREE "C", nTemp);
			nTempColor = nTemp > 75 ? UI_ERR : (nTemp > 65 ? UI_WARN : UI_WHITE);
			snprintf (StRes, sizeof StRes, "%uP", m_Video.GetHeight ());
			unsigned k = m_Video.GetHVSFactor (), S = m_Video.GetScale ();
			snprintf (StScale, sizeof StScale, " %uX %s", S,
				  k == S ? "HVS" : (k == 1 ? "CPU" : "CPU+HVS"));
			bStatusReady = TRUE;

			m_CPUThrottle.Update ();
			m_ActLED.On ();
			nFrames = 0;
			nWorkMax = 0;
			nWinStart = CTimer::GetClockTicks ();
		}
		else if (nFrames == 30)
		{
			m_ActLED.Off ();
		}
	}
}

// ---- main flow ------------------------------------------------------------

TShutdownMode CKernel::Run (void)
{
	if (!m_bVideoOK)
	{
		Fatal (NEOPI_ERR_FB, "cannot set up the HDMI output");
	}

	char Line[180];
	Log ("neo-pi - bare metal Neo Geo emulator", COLOR_TITLE);
	snprintf (Line, sizeof Line, "Version %s, built %s %s", NEOPI_VERSION, __DATE__, __TIME__);
	Log (Line, COLOR_DIM);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_CPUThrottle.SetSpeed (CPUSpeedMaximum);
	snprintf (Line, sizeof Line, "%s, %u MB RAM, CPU %u MHz, temperature %u C",
		  pInfo->GetMachineName (), pInfo->GetRAMSize (),
		  m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetTemperature ());
	Log (Line, COLOR_TEXT);
	snprintf (Line, sizeof Line, "HDMI %ux%u, pixel order %s", m_Video.GetWidth (), m_Video.GetHeight (),
		  m_Video.GetPixelOrder () ? "RGB" : "BGR");
	Log (Line, COLOR_TEXT);

	// SD card
	if (!m_EMMC.Initialize ())
	{
		Fatal (NEOPI_ERR_SD, "SD card not initialised");
	}
	if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
	{
		Fatal (NEOPI_ERR_SD, "cannot read the SD card file system");
	}
	m_bSDOK = TRUE;
	f_mkdir (SAVE_DIR);
	f_mkdir (SHOT_DIR);
	m_bFilterAllowed = CMachineInfo::Get ()->GetModelMajor () >= 4;
	LoadConfig ();

	// BIOS + console font
	m_pBIOS = LoadFile (BIOS_PATH, &m_nBIOSSize, HEAP_LOW, 0);
	if (m_pBIOS == 0)
	{
		Fatal (NEOPI_ERR_BIOS, "neopi/neogeo.zip not found or unreadable");
	}
	m_pUI = new CUI;
	size_t nFixSize = 0;
	void *pFix = neopi_zip_extract (m_pBIOS, m_nBIOSSize, "sfix.sfix", &nFixSize);
	if (pFix)
	{
		m_pUI->SetFont ((const u8 *) pFix, nFixSize);
		free (pFix);
	}
	else
	{
		Log ("  sfix.sfix not found in the BIOS: fallback font", COLOR_ERR);
	}

	// games
	ScanGames ();
	snprintf (Line, sizeof Line, "Games found: %u", m_nGames);
	Log (Line, m_nGames ? COLOR_OK : COLOR_ERR);
	if (m_nGames == 0)
	{
		Fatal (NEOPI_ERR_GAME, "no valid .neo file in the neopi folder");
	}

	// USB and audio
	Log ("USB...", COLOR_TEXT);
	if (!m_USBHCI.Initialize ())
	{
		Log ("  USB controller not initialised", COLOR_ERR);
	}
	m_pInput = new CInput (&m_USBHCI, &m_DeviceNameService);
	LoadPads ();
	m_pInput->UpdateDevices ();

	if (m_Audio.Initialize (&m_Interrupt))
	{
		Log ("Audio HDMI: OK (48 kHz)", COLOR_OK);
	}
	else
	{
		Log ("HDMI audio: not available, playing without sound", COLOR_ERR);
	}

	SetupCoreOnce ();

	// last played game
	unsigned nSel = 0;
	{
		FIL File;
		char Last[64];
		UINT nRead = 0;
		if (f_open (&File, LAST_PATH, FA_READ) == FR_OK)
		{
			f_read (&File, Last, sizeof Last - 1, &nRead);
			f_close (&File);
			Last[nRead] = '\0';
			for (unsigned i = 0; i < m_nGames; i++)
			{
				if (strcmp (m_pGames[i].File, Last) == 0)
				{
					nSel = i;
				}
			}
		}
	}

	FILINFO Info;
	boolean bBench = f_stat (BENCH_FLAG, &Info) == FR_OK;
	if (bBench)
	{
		if (LoadGame (nSel))
		{
			Benchmark ();
		}
		CTimer::SimpleMsDelay (8000);		// time to read the results on screen
	}
	SaveLog ();

	// from here on the screen belongs to the UI
	ApplyVideoMode ();
	SaveLog ();
	m_bQuiet = TRUE;

	boolean bDirect = m_nGames == 1;
	while (1)
	{
		if (!bDirect)
		{
			nSel = RunList (nSel);
		}
		bDirect = FALSE;

		if (m_nLoaded != (int) nSel && !LoadGame (nSel))
		{
			m_pUI->DrawMessage ("CANNOT LOAD GAME", m_pGames[nSel].Name, "SEE neopi/LOG.TXT");
			SaveLog ();
			for (unsigned i = 0; i < 180; i++)
			{
				ShowUIFrame ();
			}
			continue;
		}
		SaveLog ();
		RunGame ();
		m_bQuiet = TRUE;
	}

	return ShutdownHalt;
}
