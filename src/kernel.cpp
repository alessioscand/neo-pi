/*
 * neo-pi - kernel: start-up, the main flow (game list, game, list...), power off
 */
#include "kernel.h"
#include "frame.h"
#include "mcline.h"
#include "fbmap.h"
#include "tvcore.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/alloc.h>
#include <circle/bcmwatchdog.h>

extern "C" {
void *neopi_zip_extract (const void *pZip, size_t nZipSize, const char *pName, size_t *pSize);
}

CKernel *CKernel::s_pThis = 0;

// cores 1 and 2: the video chip's lines (mcline.h) and COMPOSITE (tvcore.h);
// core 3 stays idle
class CVideoCore : public CMultiCoreSupport
{
public:
	CVideoCore (void) : CMultiCoreSupport (CMemorySystem::Get ()) {}
	void Run (unsigned nCore)
	{
		if (nCore == 1)
		{
			FbSetMair ();			// the framebuffer's write-combining type
			McWorker ();
		}
		else if (nCore == 2)
		{
			FbSetMair ();
			TvWorkerRun ();
		}
	}
};

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
	m_nLines (0),
	m_nRepeat (0),
	m_pBIOS (0),
	m_nBIOSSize (0),
	m_bHasUni (FALSE),
	m_pGame (0),
	m_nGameSize (0),
	m_pGames (0),
	m_nGames (0),
	m_nLoaded (-1),
	m_nGameSystem (SYS_MVS),
	m_bReloadGame (FALSE),
	m_nShots (0),
	m_nBootKeys (0),
	m_nSystem (SYS_MVS),
	m_nRegion (REGION_EU),
	m_nFilter (0),
	m_bHVS (TRUE),
	m_bSpeex (FALSE),
	m_bMultiCore (TRUE),
	m_bFilterAllowed (TRUE),
	m_bVSync (TRUE),
	m_bStatusOn (FALSE),
	m_nClearStatus (0)
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

TShutdownMode CKernel::Run (void)
{
	if (!m_bVideoOK)
	{
		Fatal (PROJECT_ERR_FB, "cannot set up the HDMI output");
	}

	char Line[180];
	Log ("neo-pi - bare metal Neo Geo emulator", COLOR_TITLE);
	snprintf (Line, sizeof Line, "Version %s, built %s %s", PROJECT_VERSION, __DATE__, __TIME__);
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
		Fatal (PROJECT_ERR_SD, "SD card not initialised");
	}
	if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
	{
		Fatal (PROJECT_ERR_SD, "cannot read the SD card file system");
	}
	m_bSDOK = TRUE;
	f_mkdir (SAVE_DIR);
	f_mkdir (SHOT_DIR);
	m_bFilterAllowed = pInfo->GetModelMajor () >= 4;
	FrameSetup (&m_Video);
	LoadConfig ();
	// multicore on a Pi 3: core 0 has the margin for the scanlines (here, not
	// in LoadConfig: it returns at once when neopi.cfg does not exist yet)
	if (m_bMultiCore)			m_bFilterAllowed = TRUE;
	if (!m_bFilterAllowed)			m_nFilter = FILTER_NONE;
	StartCores ();

	LoadBIOS ();

	// games
	ScanGames ();
	snprintf (Line, sizeof Line, "Games found: %u", m_nGames);
	Log (Line, m_nGames ? COLOR_OK : COLOR_ERR);
	if (m_nGames == 0)
	{
		Fatal (PROJECT_ERR_GAME, "no valid .neo file in the neopi folder");
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
		Log ("HDMI audio: OK (48 kHz)", COLOR_OK);
	}
	else
	{
		Log ("HDMI audio: not available, playing without sound", COLOR_ERR);
	}

	SetupCoreOnce ();

	unsigned nSel = LastPlayed ();
	FILINFO Info;
	if (f_stat (MCTEST_FLAG, &Info) == FR_OK)
	{
		McTest (nSel);
		CTimer::SimpleMsDelay (8000);
	}
	if (f_stat (BENCH_FLAG, &Info) == FR_OK)
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
	SetupBeamRacing ();
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
			m_pUI->DrawMessage ("CANNOT LOAD GAME", 0, "SEE neopi/LOG.TXT");
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

// multicore=off keeps everything on core 0
void CKernel::StartCores (void)
{
	if (!m_bMultiCore)
	{
		Log ("Multicore: off (multicore=off), single core", COLOR_TEXT);
		return;
	}
	CVideoCore *pVideoCore = new CVideoCore;
	boolean bStarted = pVideoCore && pVideoCore->Initialize ();
	unsigned nWait = CTimer::GetClockTicks ();
	while (bStarted && !McAlive () && CTimer::GetClockTicks () - nWait < 200000)
	{
	}
	if (bStarted && McAlive ())
	{
		FrameLinesToScreen (TRUE);
		McEnable (TRUE);
		Log ("Multicore: video chip lines drawn on core 1", COLOR_TEXT);
		while (!TvWorkerAlive () && CTimer::GetClockTicks () - nWait < 200000)
		{
		}
		Log (TvWorkerAlive () ? "Multicore: core 2 ready for COMPOSITE"
				      : "Multicore: core 2 not running, COMPOSITE on core 1",
		     TvWorkerAlive () ? COLOR_TEXT : COLOR_ERR);
	}
	else
	{
		Log (bStarted ? "Multicore: core 1 started but is not running, single core"
			      : "Multicore: core 1 did not start, single core", COLOR_ERR);
	}
	SaveLog ();
}

// neogeo.zip, with the Universe BIOS if it is there, and the font of the screens
void CKernel::LoadBIOS (void)
{
	m_pBIOS = LoadFile (BIOS_PATH, &m_nBIOSSize, HEAP_LOW, 0);
	if (m_pBIOS == 0)
	{
		Fatal (PROJECT_ERR_BIOS, "neopi/neogeo.zip not found or unreadable");
	}
	m_pUI = new CUI;
	size_t nFixSize = 0;
	size_t nUniSize = 0;
	void *pUni = neopi_zip_extract (m_pBIOS, m_nBIOSSize, UNIBIOS_ROM, &nUniSize);
	m_bHasUni = pUni != 0;
	free (pUni);
	Log (m_bHasUni ? "BIOS: " UNIBIOS_ROM " found, SYSTEM can be UNIBIOS"
		       : "BIOS: no " UNIBIOS_ROM " in neogeo.zip", COLOR_TEXT);
	if (m_nSystem == SYS_UNI && !m_bHasUni)
	{
		m_nSystem = SYS_MVS;
		Log ("  system=UNIBIOS in neopi.cfg but no Universe BIOS: MVS", COLOR_ERR);
	}
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
}

// the game the list starts on: the last one played (LAST.TXT)
unsigned CKernel::LastPlayed (void)
{
	unsigned nSel = 0;
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
	return nSel;
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
