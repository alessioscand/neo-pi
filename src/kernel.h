/*
 * neo-pi - kernel: game list, play with USB pads and HDMI audio, in-game menu
 * with save states, automatic diagnostics in LOG.TXT
 *
 * The class is implemented by area: kernel.cpp (start-up, power off),
 * console.cpp, games.cpp, settings.cpp, menus.cpp, gameloop.cpp, bench.cpp,
 * report.cpp, racing.cpp.
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/cputhrottle.h>
#include <circle/device.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>
#include <circle/util.h>
#include <stdint.h>
#include "video.h"
#include "input.h"
#include "audio.h"
#include "ui.h"
#include "project.h"

extern "C" {
int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);
#include "geo.h"		// REGION_*, SYSTEM_*
}

#define NEOPI_DIR	"SD:/neopi"
#define BIOS_PATH	NEOPI_DIR "/neogeo.zip"
#define UNIBIOS_ROM	"uni-bios_4_0.rom"	// Universe BIOS, in neogeo.zip (MAME/FBNeo sets)
#define LOG_PATH	NEOPI_DIR "/LOG.TXT"
#define BENCH_FLAG	NEOPI_DIR "/BENCH"
#define BENCH_PATH	NEOPI_DIR "/BENCH.TXT"
#define PROFILE_PATH	NEOPI_DIR "/PROFILE.TXT"
#define MCTEST_FLAG	NEOPI_DIR "/MCTEST"
#define MCTEST_PATH	NEOPI_DIR "/MCTEST.TXT"
#define LAST_PATH	NEOPI_DIR "/LAST.TXT"
#define CFG_PATH	NEOPI_DIR "/neopi.cfg"
#define PADS_PATH	NEOPI_DIR "/pads.cfg"
#define SAVE_DIR	NEOPI_DIR "/saves"
#define SHOT_DIR	NEOPI_DIR "/shots"

#define SAMPLE_RATE	48000
#ifndef BENCH_WARMUP
#define BENCH_WARMUP	1800		// 30 s of play before measuring
#endif
#ifndef BENCH_FRAMES
#define BENCH_FRAMES	3600		// then 60 s measured
#endif
#ifndef MCTEST_FRAMES
#define MCTEST_FRAMES	3600
#endif

#define COLOR_TEXT	RGB (220, 220, 220)
#define COLOR_TITLE	RGB (255, 200, 0)
#define COLOR_OK	RGB (80, 230, 80)
#define COLOR_ERR	RGB (255, 70, 70)
#define COLOR_DIM	RGB (140, 140, 140)

// OPTIONS > SYSTEM
#define SYS_MVS		0
#define SYS_AES		1
#define SYS_UNI		2		// MVS hardware with the Universe BIOS

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

// Receives Circle's own log messages and forwards them to the console
class CLogSink : public CDevice
{
public:
	int Write (const void *pBuffer, size_t nCount);
};

// the core's messages, to the console (geo_log_set_callback)
void CoreLog (int nLevel, const char *pFormat, ...);

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

	// console (screen during boot + serial + LOG.TXT)
	void Log (const char *pLine, u32 nColor);
	void Fatal (int nCode, const char *pMessage);
	void SaveLog (void);

	static CKernel *Get (void) { return s_pThis; }

private:
	// kernel.cpp
	void StartCores (void);
	void LoadBIOS (void);
	unsigned LastPlayed (void);
	void PowerOff (void);

	// console.cpp
	void RedrawConsole (void);
	void WriteTextFile (const char *pPath, const char *pText);

	// games.cpp
	u8 *LoadFile (const char *pPath, size_t *pSize, int nHeap, const char *pShowName);
	void ScanGames (void);
	boolean LoadGame (unsigned nIndex);
	void UnloadGame (void);
	void SaveGameData (void);
	const char *SaveExt (unsigned nType) const;
	void GamePath (char *pOut, size_t nSize, const char *pExt) const;
	void StatePath (char *pOut, size_t nSize) const;
	static void ClearWorkRAM (void);
	void ClearVideoMemory (void);

	// settings.cpp
	const char *SystemName (void) const;
	static const char *RegionName (unsigned nRegion);
	static const char *FilterName (unsigned nFilter);
	static const char *FilterShort (unsigned nFilter);
	void VideoModeTag (char *pOut, size_t nSize);
	void ApplyVideoMode (void);
	void LoadConfig (void);
	void SaveConfig (void);
	void LoadPads (void);
	void SavePads (void);

	// menus.cpp
	void ShowUIFrame (boolean bWait = TRUE, const char *pTopMessage = 0);
	unsigned RunList (unsigned nSel);
	void RunSystemMenu (boolean bInGame);
	int ChoosePad (void);
	boolean RunMapping (unsigned nSlot, boolean bOverGame);
	boolean PowerOffCountdown (void);
	void RunCredits (void);

	// gameloop.cpp
	struct TPlayInput
	{
		boolean bOpenMenu, bOpenCredits, bScreenshot;
	};
	void SetupCoreOnce (void);
	void RunGame (void);
	void PlayStart (void);
	void PlayWaitRelease (void);
	void PlayReadPanels (const u32 Pad[2], TPlayInput *pIn);
	void PlayOpenMenu (void);
	unsigned PlayFrame (boolean bRace, boolean bScreenshot, unsigned *pT0, boolean *pOddFrame);
	boolean PlayMenu (const u32 Pad[2]);
	void PlayDrawBands (void);
	void PlayStatusLine (unsigned nNow, unsigned *pNextDeadline);
	const char *SaveScreenshot (void);

	// bench.cpp
	void Benchmark (void);
	void McTest (unsigned nSel);

	// report.cpp, racing.cpp
	void ResetGameStats (void);
	void ReportWindow (boolean bFinal);
	void SetupBeamRacing (void);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogSink		m_LogSink;
	CLogger			m_Logger;
	CCPUThrottle		m_CPUThrottle;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;
	CVideoOut		m_Video;	// the screen (HDMI): used through CScreen only
	CAudioOut		m_Audio;
	CInput			*m_pInput;
	CUI			*m_pUI;

	enum { MaxLines = 400, MaxLineLen = 140, MaxGames = 256 };

	boolean			m_bVideoOK;
	boolean			m_bSDOK;
	boolean			m_bQuiet;	// UI/emulation running: console not drawn

	// console lines
	char			m_Lines[MaxLines][MaxLineLen];
	u32			m_LineColor[MaxLines];
	unsigned		m_nLines;
	char			m_LastRaw[MaxLineLen];
	unsigned		m_nRepeat;

	// BIOS and games
	u8			*m_pBIOS;
	size_t			 m_nBIOSSize;
	boolean			 m_bHasUni;	// uni-bios_4_0.rom is in neogeo.zip
	u8			*m_pGame;
	size_t			 m_nGameSize;
	TGameEntry		*m_pGames;
	unsigned		 m_nGames;
	int			 m_nLoaded;	// index in m_pGames, -1 = none
	unsigned		 m_nGameSystem;	// the system the loaded game runs with
	boolean			 m_bReloadGame;	// SYSTEM or REGION changed while playing
	unsigned		 m_nShots;	// last screenshot number used
	unsigned		 m_nBootKeys;	// > 0: player 1's A+B+C held (Universe BIOS menu)

	// configuration (neopi.cfg)
	unsigned		 m_nSystem;	// SYS_MVS, SYS_AES, SYS_UNI
	unsigned		 m_nRegion;	// REGION_*
	unsigned		 m_nFilter;	// FILTER_*
	boolean			 m_bHVS;	// FALSE: scaler=cpu
	boolean			 m_bSpeex;	// resampler=speex
	boolean			 m_bMultiCore;	// FALSE: multicore=off
	boolean			 m_bFilterAllowed;

	// frame pacing and the status line
	boolean			 m_bVSync;	// FALSE: the firmware's vsync does not block
	boolean			 m_bStatusOn;
	unsigned		 m_nClearStatus;

	// RunGame
	struct TPlay
	{
		boolean	 bMenu;			// in-game menu open
		unsigned nMenuSel;
		u32	 nMenuPrev;
		boolean	 bSaved;		// a save state exists (one per game)
		char	 MenuMsg[40];
		u32	 nMenuMsgColor;

		// per player
		u32	 nPrev[2];		// ~0: wait until released
		boolean	 bSelectCombo[2];	// Select used with another button
		unsigned nCoin[2];		// frames left of a coin press
		unsigned nSelectHeld[2];	// frames Select has been held alone
		boolean	 bLazy[2];		// lazy finger (turbo on A-D), Select+B
		unsigned nLazyCount[2][4];	// frames each of A-D has been held

		// message in the top band (screenshot), in frames
		char	 Toast[64];
		unsigned nToast, nClearToast;

		// status line (Select+C), refreshed once per second
		char	 StFps[16], StLoad[8], StTemp[8], StRes[8], StScale[16], StDelay[8];
		u32	 nFpsColor, nLoadColor, nTempColor;
		boolean	 bStatusReady;
		unsigned nFrames, nSeconds, nTemp, nWinStart, nWorkMax;
	};
	TPlay			 m_Play;

	static CKernel		*s_pThis;
};

#endif
