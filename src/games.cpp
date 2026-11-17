/*
 * neo-pi - games: the .neo list, loading with the BIOS, save data
 */
#include "kernel.h"
#include "gametable.h"
#include "frame.h"
#include <circle/new.h>

extern "C" {
#include "geo.h"
#include "geo_lspc.h"
#include "geo_m68k.h"
#include "geo_z80.h"
#include "geo_mixer.h"
#include "geo_neo.h"
}

// a number kept in BCD (0x56 = 56), 0 if a nibble is not a decimal digit
static unsigned BCDToNumber (unsigned nBCD)
{
	unsigned nNumber = 0;
	for (int nShift = 28; nShift >= 0; nShift -= 4)
	{
		const unsigned nDigit = (nBCD >> nShift) & 0xF;
		if (nDigit > 9)
		{
			return 0;
		}
		nNumber = nNumber * 10 + nDigit;
	}
	return nNumber;
}

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
		// maker and year, under the list and above the picture: "SNK 1992"
		char Maker[18];
		n = 0;
		for (unsigned i = 0; i < 17 && Header[77 + i] != 0; i++)
		{
			char c = (char) Header[77 + i];
			if (c >= 'a' && c <= 'z') c -= 32;
			Maker[n++] = c >= 32 && c <= 126 ? c : ' ';
		}
		while (n > 0 && Maker[n-1] == ' ') n--;
		Maker[n] = '\0';
		const unsigned nYear = Header[28] | Header[29] << 8;
		if (nYear >= 1980 && nYear < 2100)
		{
			snprintf (G.Info, sizeof G.Info, "%s %u", Maker, nYear);
		}
		else
		{
			snprintf (G.Info, sizeof G.Info, "%s", Maker);
		}
		// the NGH number picks the button names (gametable.cpp); the
		// header keeps it in BCD, one decimal digit a nibble: Art of
		// Fighting 2, NGH-056, is 0x56
		const unsigned nBCD =   Header[40]       | Header[41] << 8
				      | Header[42] << 16 | (unsigned) Header[43] << 24;
		G.nGame = NeoButtonsFind (BCDToNumber (nBCD));
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

// save data of the loaded game: MVS memory (backup RAM), cartridge RAM, memory
// card. The Universe BIOS keeps its own settings for each game in the backup
// RAM, so it has a file of its own (.unv) and never mixes with the MVS BIOS's.
const char *CKernel::SaveExt (unsigned nType) const
{
	static const char *Ext[3] = {"nv", "srm", "mcr"};
	if (nType == 0 && m_nGameSystem == SYS_UNI)
	{
		return "unv";
	}
	return Ext[nType < 3 ? nType : 0];
}

void CKernel::GamePath (char *pOut, size_t nSize, const char *pExt) const
{
	char Base[64];
	strcpy (Base, m_pGames[m_nLoaded].File);
	size_t n = strlen (Base);
	if (n > 4) Base[n - 4] = '\0';
	snprintf (pOut, nSize, "%s/%s.%s", SAVE_DIR, Base, pExt);
}

// the save state: one per game and system (s + a / t / u + 1)
void CKernel::StatePath (char *pOut, size_t nSize) const
{
	char Ext[4];
	Ext[0] = 's';
	Ext[1] = m_nGameSystem == SYS_AES ? 'a' : (m_nGameSystem == SYS_UNI ? 'u' : 't');
	Ext[2] = '1';
	Ext[3] = '\0';
	GamePath (pOut, nSize, Ext);
}

// what each file on the card holds, so that an unchanged one is not written
// again (nSize 0 = not known: the next save writes it)
static const unsigned s_MemType[3] = {GEO_MEMTYPE_NVRAM, GEO_MEMTYPE_CARTRAM, GEO_MEMTYPE_MEMCARD};
static u8 s_OnCardNv[SIZE_64K], s_OnCardSrm[SIZE_8K], s_OnCardMcr[SIZE_2K];
static u8 *const s_OnCard[3] = {s_OnCardNv, s_OnCardSrm, s_OnCardMcr};
static const size_t s_OnCardMax[3] = {SIZE_64K, SIZE_8K, SIZE_2K};
static size_t s_nOnCard[3];

// the save data in memory is now what the card holds
static void RememberOnCard (unsigned nType)
{
	size_t nSize = 0;
	const void *pData = geo_mem_ptr (s_MemType[nType], &nSize);
	s_nOnCard[nType] = 0;
	if (pData && nSize > 0 && nSize <= s_OnCardMax[nType])
	{
		memcpy (s_OnCard[nType], pData, nSize);
		s_nOnCard[nType] = nSize;
	}
}

// only what changed since it was read or written
void CKernel::SaveGameData (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	for (unsigned i = 0; i < 3; i++)
	{
		size_t nSize = 0;
		const void *pData = geo_mem_ptr (s_MemType[i], &nSize);
		if (pData && nSize == s_nOnCard[i] && memcmp (pData, s_OnCard[i], nSize) == 0)
		{
			continue;
		}
		char Path[160];
		GamePath (Path, sizeof Path, SaveExt (i));
		if (geo_savedata_save (i, Path) == 1)
		{
			RememberOnCard (i);
		}
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

// 68000 and Z80 work RAM zeroed as at power-on (Geolith's reset keeps it, and
// the Universe BIOS would take the last game's RAM for a soft reset). Also
// for BIOS SETTINGS; the backup RAM is kept.
void CKernel::ClearWorkRAM (void)
{
	memset ((void *) geo_m68k_ram_ptr (), 0, 0x10000);
	memset ((void *) geo_z80_ram_ptr (), 0, 0x800);
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
	m_nGameSystem = m_nSystem;
	m_bReloadGame = FALSE;
	geo_set_system (m_nSystem == SYS_AES ? SYSTEM_AES : (m_nSystem == SYS_UNI ? SYSTEM_UNI : SYSTEM_MVS));
	geo_set_region ((int) m_nRegion);
	geo_mixer_set_rate (SAMPLE_RATE);
	geo_mixer_init ();
	FrameAudioStart (m_bSpeex);
	geo_init ();
	geo_set_div68k (m_nCpuSpeed != CPU_TURBO);	// TURBO: the 68000 without its clock divider
	geo_set_region ((int) m_nRegion);
	ClearVideoMemory ();
	snprintf (Line, sizeof Line, "  system %s, region %s", SystemName (), RegionName (m_nRegion));
	Log (Line, COLOR_DIM);
	if (!geo_bios_load_mem (m_pBIOS, m_nBIOSSize))
	{
		// back to the list: SYSTEM or REGION can be changed in OPTIONS
		snprintf (Line, sizeof Line, "  no BIOS for %s, region %s, in neogeo.zip",
			  SystemName (), RegionName (m_nRegion));
		Log (Line, COLOR_ERR);
		geo_deinit ();
		geo_bios_unload ();
		delete [] m_pGame;
		m_pGame = 0;
		return FALSE;
	}
	// Geolith trusts the header's sizes (ROMs from byte 4096): check them
	// against the file, with geo_neo_load()'s layout
	const u8 *h = (const u8 *) m_pGame;
	u64 nNeed = 4096;
	for (unsigned i = 4; i <= 24; i += 4)
	{
		nNeed += h[i] | (h[i+1] << 8) | (h[i+2] << 16) | ((u32) h[i+3] << 24);
	}
	if (m_nGameSize < nNeed)
	{
		snprintf (Line, sizeof Line, "  .neo file too short: %u bytes, the header needs %llu",
			  (unsigned) m_nGameSize, (unsigned long long) nNeed);
		Log (Line, COLOR_ERR);
		geo_deinit ();
		geo_bios_unload ();
		delete [] m_pGame;
		m_pGame = 0;
		return FALSE;
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
	m_nBootKeys = 0;
	m_nShots = 0;					// screenshot numbers: next free one

	for (unsigned i = 0; i < 3; i++)
	{
		char SavePath[160];
		GamePath (SavePath, sizeof SavePath, SaveExt (i));
		if (geo_savedata_load (i, SavePath) == 1)
		{
			RememberOnCard (i);
		}
		else
		{
			s_nOnCard[i] = 0;		// not on the card yet
		}
	}

	geo_lspc_set_palette (0);		// resistor network palette (as on hardware)
	ClearWorkRAM ();			// a game always starts as at power-on
	geo_reset (1);
	FrameRephase ();

	WriteTextFile (LAST_PATH, G.File);
	return TRUE;
}
