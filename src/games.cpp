/*
 * neo-pi - games: the list of .neo files, loading one with the BIOS, its save
 * data (backup RAM, cartridge RAM, memory card)
 */
#include "kernel.h"
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

void CKernel::SaveGameData (void)
{
	if (m_nLoaded < 0)
	{
		return;
	}
	for (unsigned i = 0; i < 3; i++)
	{
		char Path[160];
		GamePath (Path, sizeof Path, SaveExt (i));
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

// 68000 and Z80 work RAM as at power-on (all zero). Geolith never clears them:
// it expects one game per run, and its reset is the board's RESET button, which
// keeps the RAM. Without this, a game loaded from the list starts with the RAM
// of the previous one, and the Universe BIOS, finding its own data there, takes
// it for a soft reset and keeps that game's active region instead of reading
// its settings from the backup RAM (which is loaded per game and is right).
// Also used by BIOS SETTINGS with UNIBIOS. The backup RAM is not touched.
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
	// Geolith trusts the sizes in the header and takes the ROMs one after the
	// other from byte 4096: a truncated or damaged file would be read past its
	// end (garbage on screen, or worse). Same layout as geo_neo_load(), so a
	// file it reads correctly always passes
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
		geo_savedata_load (i, SavePath);
	}

	geo_lspc_set_palette (0);		// resistor network palette (as on hardware)
	ClearWorkRAM ();			// a game always starts as at power-on
	geo_reset (1);
	FrameRephase ();

	WriteTextFile (LAST_PATH, G.File);
	return TRUE;
}
