/*
 * neo-pi - the game loop: pads to the Neo Geo panels, one emulated frame per
 * screen frame (beam raced, or double buffered with a frame delay), the
 * in-game menu, the messages and the status line in the black bands
 */
#include "kernel.h"
#include "frame.h"
#include "mcline.h"
#include "pacing.h"
#include "racing.h"
#include "stats.h"
#include "smp.h"
#include "fontdata.h"

extern "C" {
#include "geo.h"
void *neopi_png_encode (const void *pRGB, int nWidth, int nHeight, size_t *pSize);
void neopi_png_free (void *p);
extern void (*geo_lspc_line_hook) (unsigned nScanline);	// patches/geolith-phase.py
}

void NeopiInstallVFS (void);

#define REPORT_FRAMES	3600		// one statistics line per minute of play

enum { GmSave, GmLoad, GmBios, GmOptions, GmReset, GmList, GmPower, GmCount };

// ---- the Neo Geo panels -------------------------------------------------------

// all active low, written before each geo_exec(), read by the core
#define STAT_A_IDLE	(0x07 | 0x18)		// coins 1/2; slots 3/4 unused (MVS)
#define STAT_B_IDLE	(0x0F | 0x30)		// starts; no memory card
static unsigned s_Joy[2] = {0xFF, 0xFF};
static unsigned s_StatA = STAT_A_IDLE;
static unsigned s_StatB = STAT_B_IDLE;
static unsigned s_nTestFrames = 0;		// > 0: TEST button held (MVS service menu)

// Universe BIOS menu: A+B+C held from the reset for 1.5 s. It reads them on its
// splash screen (60 frames were enough in tests, 40 were not); held longer, its
// menu would ignore the d-pad until they are released
#define BOOT_KEYS_FRAMES	90

static unsigned InputJoystick (unsigned nPort)	{ return s_Joy[nPort & 1]; }
static unsigned InputStatusA (void)		{ return s_StatA; }
static unsigned InputStatusB (void)		{ return s_StatB; }
static unsigned InputSystemType (void)		{ return s_nTestFrames ? 0x40 : 0xC0; }
static unsigned InputDIPSwitches (void)		{ return 0xFF; }	// all DIP switches off
static unsigned InputUnused (void)		{ return 0xFF; }

// nothing pressed (while the menu or the credits are open)
static void PanelsIdle (void)
{
	s_Joy[0] = s_Joy[1] = 0xFF;
	s_StatA = STAT_A_IDLE;
	s_StatB = STAT_B_IDLE;
}

void CKernel::SetupCoreOnce (void)
{
	geo_log_set_callback (CoreLog);
	NeopiInstallVFS ();

	geo_input_set_callback (0, InputJoystick);
	geo_input_set_callback (1, InputJoystick);
	geo_input_sys_set_callback (0, InputStatusA);
	geo_input_sys_set_callback (1, InputStatusB);
	geo_input_sys_set_callback (2, InputSystemType);
	geo_input_sys_set_callback (3, InputDIPSwitches);
	geo_input_sys_set_callback (4, InputUnused);
}

// ---- the loop -----------------------------------------------------------------

// Before a frame: with beam racing, wait for the moment the plan gives
// against the HDMI scan-out (returns that moment); otherwise, while playing,
// wait the frame delay after the vsync (returns 0). The time spent waiting
// does not count as work: *pT0 moves on by it.
static unsigned WaitForFrameStart (boolean bRace, boolean bPlaying, unsigned nLastVSync, unsigned *pT0)
{
	if (bRace)
	{
		u64 nStart = g_Race.pRacer->PlanFrame (Ticks ());
		unsigned tWait = CTimer::GetClockTicks ();
		while ((s64) (Ticks () - nStart) < 0)
		{
			g_Race.pRaster->Sample ();
		}
		g_Race.pRacer->FrameGo (Ticks ());
		unsigned nGoUs = CTimer::GetClockTicks ();
		*pT0 += nGoUs - tWait;
		return nGoUs;
	}
	if (bPlaying && g_Delay.nUs > 0)
	{
		unsigned tGo = nLastVSync + g_Delay.nUs;
		unsigned tWait = CTimer::GetClockTicks ();
		while ((int) (CTimer::GetClockTicks () - tGo) < 0)
		{
		}
		*pT0 += CTimer::GetClockTicks () - tWait;
	}
	return 0;
}

void CKernel::RunGame (void)
{
	PlayStart ();
	m_pInput->RestartDiagnostics ();		// log the pad reports of this game too
	m_ActLED.On ();			// steady while playing (no firmware call every second)

	unsigned nLastVSync = CTimer::GetClockTicks ();
	unsigned nNextDeadline = nLastVSync + 16667;
	unsigned nTotalFrames = 0;

	g_Stats.Reset (nLastVSync);
	DelayStatsReset ();
	DelayRestart ();
	g_Race.bGame = g_Race.bReady && McEnabled ();
	if (g_Race.bGame)
	{
		// a game that gave up on beam racing keeps the double buffer until it is left:
		// the next game started from the list tries again
		g_Race.pRacer->Rearm ();
		g_Race.pRacer->SetGeometry (m_Video.GetGameY (), m_Video.GetCPUScale (), m_Video.GetHVSFactor ());
		g_Race.pRacer->Restart ();
		g_Race.pRacer->ResetStats ();
	}
	{
		unsigned nOld;
		m_pInput->TakeChange (&nOld);	// a press from the list does not count
	}
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

			int nPad = m_Play.bMenu ? -1 : m_pInput->GetUnmappedPad ();
			if (nPad >= 0)
			{
				// new pad during the game: pause and set it up
				m_pUI->SnapshotGame (g_VideoBuf);
				m_Video.SetRacing (FALSE);
				RunMapping ((unsigned) nPad, TRUE);
				ResetGameStats ();
				m_Play.nPrev[0] = m_Play.nPrev[1] = ~0u;
				unsigned nOld;
				m_pInput->TakeChange (&nOld);	// presses during the set-up do not count
			}
		}

		const boolean bRace = g_Race.bGame && !m_Play.bMenu;
		m_Video.SetRacing (bRace);
		const unsigned nGoUs = WaitForFrameStart (bRace, !m_Play.bMenu, nLastVSync, &t0);
		t1 = t0;
		boolean bOddFrame = FALSE;	// slow for its own reasons: not a measure of the game

		// read controls as late as possible, right before emulating the frame
		unsigned nChangeTicks;
		boolean bChange = m_pInput->TakeChange (&nChangeTicks);
		u32 Pad[2] = {m_pInput->Get (0), m_pInput->Get (1)};

		if (!m_Play.bMenu)
		{
			TPlayInput In;
			PlayReadPanels (Pad, &In);
			if (In.bOpenCredits)
			{
				// Select + D: credits over the paused game (B closes)
				PanelsIdle ();
				m_pUI->SnapshotGame (g_VideoBuf);
				m_Video.SetRacing (FALSE);
				RunCredits ();
				PlayWaitRelease ();
				m_Play.nToast = 0;
				m_Play.nClearToast = m_nClearStatus = 2;
				ResetGameStats ();
				continue;
			}
			if (In.bOpenMenu)
			{
				PlayOpenMenu ();
			}
			else
			{
				t1 = PlayFrame (bRace, In.bScreenshot, &t0, &bOddFrame);
			}
		}
		if (m_Play.bMenu && !PlayMenu (Pad))
		{
			return;					// GAME LIST
		}
		PlayDrawBands ();

		unsigned t2 = CTimer::GetClockTicks ();
		const boolean bRaced = bRace && !m_Play.bMenu;	// already on screen: no flip, no vsync
		if (!bRaced)
		{
			m_Video.Flip ();
		}
		unsigned t3 = CTimer::GetClockTicks ();

		if (bRaced)
		{
			nNextDeadline = t3 + 16667;	// the next frame start paces the loop
		}
		else if (m_bVSync)
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

		if (g_Stats.bSkipFrame)
		{
			g_Stats.bSkipFrame = FALSE;	// spans the pause: not a real frame time
		}
		else if (!m_Play.bMenu)
		{
			g_Stats.Emu.Add (t1 - t0);
			g_Stats.Work.Add (t2 - t0);
			g_Stats.Period.Add (nPeriod);
			if (bRaced)
			{
				// pad report -> the HVS starts composing the top of the picture
				if (bChange)
				{
					g_Stats.Lag.Add (nGoUs - nChangeTicks
							 + (unsigned) g_Race.pRaster->TicksToUs ((s64) g_Race.pRacer->GetPadToPicture ()));
				}
			}
			else
			{
				g_Stats.Slack.Add (t4 - t3);
				if (!bOddFrame)
				{
					DelayUpdate (t3 - t0, nPeriod > 25000);
				}
				if (bChange)
				{
					g_Stats.Lag.Add (t4 - nChangeTicks);
				}
			}
			if (t3 - t0 > 16000) g_Stats.nOverBudget++;
			if (nPeriod > 25000) g_Stats.nMissedVSync++;
			g_Stats.nFrames++;
			if (g_Stats.nFrames >= REPORT_FRAMES)
			{
				ReportWindow (FALSE);
				m_pInput->FlushDiagnostics ();
			}
		}

		if (!m_Play.bMenu && t2 - t0 > m_Play.nWorkMax)
		{
			m_Play.nWorkMax = t2 - t0;
		}
		if (++m_Play.nFrames == 60)
		{
			PlayStatusLine (t4, &nNextDeadline);
		}
	}
}

void CKernel::PlayStart (void)
{
	TPlay &P = m_Play;
	memset (&P, 0, sizeof P);
	P.nMenuPrev = ~0u;
	P.nMenuMsgColor = UI_OK;
	P.nFpsColor = UI_WHITE;
	P.nLoadColor = UI_OK;
	P.nTempColor = UI_WHITE;
	P.nWinStart = CTimer::GetClockTicks ();
	PlayWaitRelease ();			// buttons still held from the list
}

// buttons still held (from the list, a menu, the credits) are ignored until
// they are released
void CKernel::PlayWaitRelease (void)
{
	for (unsigned p = 0; p < 2; p++)
	{
		m_Play.nPrev[p] = ~0u;
		m_Play.bSelectCombo[p] = FALSE;
	}
}

// The pads to the Neo Geo panels (s_Joy, s_StatA, s_StatB), and what Select
// and the menu key ask for. Select + A: screenshot, + B: lazy finger, + C:
// status line, + D: credits; held alone for one second: the menu; a short
// press: a coin (MVS) or Select (AES).
void CKernel::PlayReadPanels (const u32 Pad[2], TPlayInput *pIn)
{
	TPlay &P = m_Play;
	pIn->bOpenMenu = pIn->bOpenCredits = pIn->bScreenshot = FALSE;
	u32 Eff[2] = {0, 0};
	for (unsigned p = 0; p < 2; p++)
	{
		u32 n = Pad[p];
		if (P.nPrev[p] == ~0u)
		{
			// buttons still held from the list or the menu: ignore until released
			if (n == 0)
			{
				P.nPrev[p] = 0;
			}
			s_Joy[p] = 0xFF;
			continue;
		}

		u32 nNew = n & ~P.nPrev[p];

		if (n & BTN_SELECT)
		{
			if (nNew & (BTN_A | BTN_B | BTN_C | BTN_D))
			{
				P.bSelectCombo[p] = TRUE;
			}
			if (nNew & BTN_A)
			{
				pIn->bScreenshot = TRUE;
			}
			if (nNew & BTN_B)
			{
				P.bLazy[p] = !P.bLazy[p];
				snprintf (P.Toast, sizeof P.Toast, "%sLAZY FINGER %s",
					  p == 0 ? "" : "P2 ", P.bLazy[p] ? "ON" : "OFF");
				P.nToast = 60;
				Log (P.bLazy[p] ? "  lazy finger on" : "  lazy finger off", COLOR_TEXT);
			}
			if (nNew & BTN_C)
			{
				m_bStatusOn = !m_bStatusOn;
				m_nClearStatus = 2;
			}
			if (nNew & BTN_D)
			{
				pIn->bOpenCredits = TRUE;
			}
			if (!P.bSelectCombo[p] && ++P.nSelectHeld[p] >= 60)
			{
				pIn->bOpenMenu = TRUE;
				P.bSelectCombo[p] = TRUE;
			}
		}
		else
		{
			if ((P.nPrev[p] & BTN_SELECT) && !P.bSelectCombo[p])
			{
				P.nCoin[p] = 8;
			}
			P.bSelectCombo[p] = FALSE;
			P.nSelectHeld[p] = 0;
		}
		if (nNew & BTN_MENU)
		{
			pIn->bOpenMenu = TRUE;
		}
		P.nPrev[p] = n;
		Eff[p] = n;

		// Lazy finger: a held A-D button repeats, 2 frames pressed and
		// 2 released (15 per second), starting pressed at once
		u32 nGame = n;
		static const u32 LazyBtn[4] = {BTN_A, BTN_B, BTN_C, BTN_D};
		for (unsigned b = 0; b < 4; b++)
		{
			if (!(n & LazyBtn[b]))
			{
				P.nLazyCount[p][b] = 0;
			}
			else if (P.bLazy[p] && (P.nLazyCount[p][b]++ & 3) >= 2)
			{
				nGame &= ~LazyBtn[b];
			}
		}

		// Neo Geo panel: bits 0-7 = up down left right A B C D (active low);
		// while Select is held only the joystick goes through
		u32 nPanel = (n & BTN_SELECT) ? (n & 0x0F) : nGame;
		s_Joy[p] = ~nPanel & 0xFF;
	}
	if (m_nBootKeys)
	{
		s_Joy[0] &= ~0x70u;		// A+B+C (active low)
		m_nBootKeys--;
	}

	if (pIn->bOpenMenu || pIn->bOpenCredits)
	{
		Eff[0] = Eff[1] = 0;		// nothing reaches the game while they open
	}
	unsigned nStatA = STAT_A_IDLE;
	unsigned nStatB = STAT_B_IDLE;
	if (m_nGameSystem != SYS_AES)		// MVS hardware (MVS BIOS or Universe BIOS)
	{
		if (P.nCoin[0]) { nStatA &= ~0x01u; P.nCoin[0]--; }
		if (P.nCoin[1]) { nStatA &= ~0x02u; P.nCoin[1]--; }
	}
	else
	{
		if (P.nCoin[0]) { nStatB &= ~0x02u; P.nCoin[0]--; }	// AES: P1 Select
		if (P.nCoin[1]) { nStatB &= ~0x08u; P.nCoin[1]--; }	// AES: P2 Select
	}
	if (s_nTestFrames) s_nTestFrames--;
	if ((Eff[0] & BTN_START) && !(Eff[0] & BTN_SELECT)) nStatB &= ~0x01u;
	if ((Eff[1] & BTN_START) && !(Eff[1] & BTN_SELECT)) nStatB &= ~0x04u;
	s_StatA = nStatA;
	s_StatB = nStatB;
}

// the game pauses under the in-game menu
void CKernel::PlayOpenMenu (void)
{
	TPlay &P = m_Play;
	P.bMenu = TRUE;
	P.nMenuSel = 0;
	P.MenuMsg[0] = '\0';
	P.nMenuPrev = ~0u;
	P.nToast = 0;
	PanelsIdle ();
	char Path[160];
	StatePath (Path, sizeof Path);
	FILINFO Info;
	P.bSaved = f_stat (Path, &Info) == FR_OK;
	m_pUI->SnapshotGame (g_VideoBuf);
	m_Video.SetRacing (FALSE);
	SaveGameData ();
	m_pInput->FlushDiagnostics ();
	ReportWindow (TRUE);
	SaveLog ();
}

// One emulated frame (beam raced or not), its sound and its picture; returns
// when the emulation ended (t1)
unsigned CKernel::PlayFrame (boolean bRace, boolean bScreenshot, unsigned *pT0, boolean *pOddFrame)
{
	TPlay &P = m_Play;
	if (bScreenshot)
	{
		*pOddFrame = TRUE;
		// the frame on screen, before emulating the next one
		const char *pFile = SaveScreenshot ();
		snprintf (P.Toast, sizeof P.Toast, pFile ? "SCREENSHOT SAVED  %s" : "SCREENSHOT FAILED", pFile);
		P.nToast = 60;
	}
	if (bRace)
	{
		FrameSetRaced (TRUE);
		g_Race.nWaitTicks = 0;
		geo_lspc_line_hook = RaceLineHook;
	}
	geo_exec ();
	FrameSync ();
	if (bRace)
	{
		geo_lspc_line_hook = 0;
		FrameSetRaced (FALSE);
		if (*pOddFrame)
		{
			g_Race.pRacer->Restart ();	// the screenshot took the time
		}
		else
		{
			g_Race.pRacer->FrameEnd ();
		}
		g_Race.pRaster->EndFrame ();
		// the waits for the beam are not work: as if the frame had started later
		*pT0 += (unsigned) g_Race.pRaster->TicksToUs ((s64) g_Race.nWaitTicks);
		if (g_Race.pRacer->Failed ())
		{
			g_Race.bGame = FALSE;
			Log ("Beam racing: rows late in too many frames, double buffer for this game", COLOR_ERR);
		}
		if (!McEnabled ())
		{
			// core 1 stopped: the lines are no longer drawn as they come
			g_Race.bGame = FALSE;
			m_Video.SetRacing (FALSE);
			m_Video.DrawGame (g_VideoBuf);
			m_Video.Flip ();
			Log ("Beam racing: off, single core from now on", COLOR_ERR);
		}
	}
	unsigned t1 = CTimer::GetClockTicks ();
	m_Audio.WriteFrame (g_AudioBuf, g_nAudioFrames);
	FrameDraw ();
	return t1;
}

// The in-game menu, one frame of it; FALSE: GAME LIST was chosen
boolean CKernel::PlayMenu (const u32 Pad[2])
{
	TPlay &P = m_Play;

	// item list (BIOS SETTINGS only on MVS hardware)
	unsigned Ids[GmCount], nIds = 0;
	Ids[nIds++] = GmSave;		// RESUME is the B button
	Ids[nIds++] = GmLoad;
	if (m_nGameSystem != SYS_AES) Ids[nIds++] = GmBios;
	Ids[nIds++] = GmOptions;
	Ids[nIds++] = GmReset;
	Ids[nIds++] = GmList;
	Ids[nIds++] = GmPower;
	if (P.nMenuSel >= nIds) P.nMenuSel = 0;

	u32 n = Pad[0] | Pad[1];
	u32 nNew = P.nMenuPrev == ~0u ? 0 : (n & ~P.nMenuPrev);
	P.nMenuPrev = n;
	unsigned nId = Ids[P.nMenuSel];

	if (nNew & BTN_UP)	{ P.nMenuSel = (P.nMenuSel + nIds - 1) % nIds; P.MenuMsg[0] = '\0'; }
	if (nNew & BTN_DOWN)	{ P.nMenuSel = (P.nMenuSel + 1) % nIds; P.MenuMsg[0] = '\0'; }

	if (nNew & BTN_B)
	{
		P.bMenu = FALSE;		// resume
	}
	else if (nNew & BTN_A)
	{
		char Path[160];
		StatePath (Path, sizeof Path);

		switch (nId)
		{
		case GmSave:
		{
			boolean bOK = geo_state_save (Path);
			if (bOK)
			{
				P.bSaved = TRUE;
				snprintf (P.MenuMsg, sizeof P.MenuMsg, "SAVED");
				P.nMenuMsgColor = UI_OK;
			}
			else
			{
				snprintf (P.MenuMsg, sizeof P.MenuMsg, "SAVE FAILED");
				P.nMenuMsgColor = UI_ERR;
			}
			Log (bOK ? "  save state: saved" : "  save state: FAILED", COLOR_TEXT);
			break;
		}

		case GmLoad:
			if (!P.bSaved)
			{
				break;			// disabled until something is saved
			}
			if (geo_state_load (Path))
			{
				FrameRephase ();	// states from 0.6.4 too
				Log ("  save state: loaded", COLOR_TEXT);
				P.bMenu = FALSE;
			}
			else
			{
				snprintf (P.MenuMsg, sizeof P.MenuMsg, "LOAD FAILED");
				P.nMenuMsgColor = UI_ERR;
			}
			break;

		case GmReset:
			geo_reset (1);
			FrameRephase ();
			Log ("  reset", COLOR_TEXT);
			P.bMenu = FALSE;
			break;

		case GmBios:
			if (m_nGameSystem == SYS_UNI)
			{
				// Universe BIOS: reset with A+B+C held on the splash
				// screen, which opens its main menu. The work RAM is
				// cleared first, as at power-on: otherwise, during the
				// game's start-up, the BIOS sees its own data there,
				// takes it as a soft reset and skips the splash screen
				ClearWorkRAM ();
				geo_reset (1);
				FrameRephase ();
				m_nBootKeys = BOOT_KEYS_FRAMES;
				Log ("  BIOS SETTINGS: reset with A+B+C (Universe BIOS menu)", COLOR_TEXT);
			}
			else
			{
				s_nTestFrames = 10;		// press the MVS TEST button
				Log ("  BIOS SETTINGS: TEST button", COLOR_TEXT);
			}
			P.bMenu = FALSE;
			break;

		case GmOptions:
			// over the paused game; B goes back to this menu
			RunSystemMenu (TRUE);
			P.nMenuPrev = ~0u;
			break;

		case GmList:
			SaveGameData ();
			if (m_bReloadGame)
			{
				UnloadGame ();		// SYSTEM or REGION changed while playing
			}
			SaveLog ();
			m_Video.SetRacing (FALSE);
			return FALSE;

		case GmPower:
			if (PowerOffCountdown ())
			{
				PowerOff ();
			}
			P.nMenuPrev = ~0u;
			break;
		}
	}
	if (!P.bMenu)
	{
		PlayWaitRelease ();		// ignore buttons until released
		ResetGameStats ();
		m_Video.DrawGame (g_VideoBuf);
		return TRUE;
	}

	static const char *Labels[GmCount] =
	{
		"SAVE STATE", "LOAD STATE", "BIOS SETTINGS",
		"OPTIONS", "RESET", "GAME LIST", "POWER OFF"
	};
	TMenuItem Items[GmCount];
	for (unsigned i = 0; i < nIds; i++)
	{
		Items[i].pLabel = Labels[Ids[i]];
		Items[i].pValue = 0;
		Items[i].bDisabled = Ids[i] == GmLoad && !P.bSaved;
	}
	const char *pMsg = P.MenuMsg;
	u32 nMsgColor = P.nMenuMsgColor;
	if (!P.MenuMsg[0] && Ids[P.nMenuSel] == GmBios)
	{
		pMsg = "RESTARTS THE GAME";
		nMsgColor = UI_VALUE;		// a note, like APPLIES TO NEXT GAME
	}
	static const TButtonHint Hints[2] = {{'A', "SELECT"}, {'B', "RESUME"}};
	m_pUI->DrawPanelMenu (TRUE, m_pGames[m_nLoaded].Name, Items, nIds, P.nMenuSel, pMsg, nMsgColor,
			      Hints, 2, 0);
	FrameDrawPicture (m_pUI->GetBuffer ());
	return TRUE;
}

// black bands: the status line at the bottom, messages at the top (only while
// playing)
void CKernel::PlayDrawBands (void)
{
	TPlay &P = m_Play;
	if (m_bStatusOn && !P.bMenu && P.bStatusReady)
	{
		const CScreen::TTextSpan Spans[] =
		{
			{P.StFps, P.nFpsColor}, {"   LOAD ", UI_GREY}, {P.StLoad, P.nLoadColor},
			{"   ", UI_GREY}, {P.StTemp, P.nTempColor},
			{g_Race.bGame ? "   RACE " : "   DLY ", UI_GREY}, {P.StDelay, UI_WHITE},
			{"   ", UI_GREY}, {P.StRes, UI_WHITE}, {P.StScale, UI_GREY}
		};
		m_Video.DrawBandText (FALSE, Spans, sizeof Spans / sizeof Spans[0]);
		m_nClearStatus = 2;
	}
	else if (m_nClearStatus)
	{
		m_Video.DrawBandText (FALSE, 0, 0);
		m_nClearStatus--;
	}
	if (P.nToast && !P.bMenu)
	{
		const CScreen::TTextSpan Span = {P.Toast, UI_WHITE};
		m_Video.DrawBandText (TRUE, &Span, 1);
		P.nToast--;
		P.nClearToast = 2;
	}
	else if (P.nClearToast)
	{
		m_Video.DrawBandText (TRUE, 0, 0);
		P.nClearToast--;
	}
}

// Once a second: the status line's values (FPS, the worst frame of the second
// against 16.7 ms, temperature every 4 s, frame delay or pads to picture)
void CKernel::PlayStatusLine (unsigned nNow, unsigned *pNextDeadline)
{
	TPlay &P = m_Play;
	unsigned nElapsed = nNow - P.nWinStart;
	unsigned nFPS100 = (unsigned) (6000ULL * 1000000 / nElapsed);

	if (m_bVSync && nFPS100 > 7500)
	{
		m_bVSync = FALSE;
		*pNextDeadline = CTimer::GetClockTicks () + 16667;
		Log ("firmware vsync does not block: switching to timer pacing", COLOR_ERR);
	}

	if (nFPS100 >= 5995)
		snprintf (P.StFps, sizeof P.StFps, "%u FPS", (nFPS100 + 50) / 100);
	else
		snprintf (P.StFps, sizeof P.StFps, "%u.%u FPS", nFPS100 / 100, (nFPS100 % 100) / 10);
	P.nFpsColor = nFPS100 < 5990 ? UI_WARN : UI_WHITE;
	unsigned nLoad = P.nWorkMax * 100 / 16667;
	snprintf (P.StLoad, sizeof P.StLoad, "%u%%", nLoad);
	P.nLoadColor = nLoad > 95 ? UI_ERR : (nLoad >= 75 ? UI_WARN : UI_OK);
	if (P.nSeconds++ % 4 == 0)
	{
		P.nTemp = m_CPUThrottle.GetTemperature ();
	}
	snprintf (P.StTemp, sizeof P.StTemp, "%u" FONT_DEGREE "C", P.nTemp);
	P.nTempColor = P.nTemp > 75 ? UI_ERR : (P.nTemp > 65 ? UI_WARN : UI_WHITE);
	snprintf (P.StRes, sizeof P.StRes, "%uP", m_Video.GetHeight ());
	if (g_Race.bGame)
	{
		unsigned us = (unsigned) g_Race.pRaster->TicksToUs ((s64) g_Race.pRacer->GetPadToPicture ());
		snprintf (P.StDelay, sizeof P.StDelay, "%u.%u", us / 1000, (us % 1000) / 100);
	}
	else
	{
		snprintf (P.StDelay, sizeof P.StDelay, "%u.%u", g_Delay.nUs / 1000, (g_Delay.nUs % 1000) / 100);
	}
	unsigned k = m_Video.GetHVSFactor (), S = m_Video.GetScale ();
	snprintf (P.StScale, sizeof P.StScale, " %uX %s", S,
		  k == S ? "HVS" : (k == 1 ? "CPU" : "CPU+HVS"));
	P.bStatusReady = TRUE;

	m_CPUThrottle.Update ();	// does its firmware calls every 4 s
	P.nFrames = 0;
	P.nWorkMax = 0;
	P.nWinStart = CTimer::GetClockTicks ();
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
		const u32 *s = g_VideoBuf + (GAME_SRC_Y + y) * GAME_SRC_PITCH + GAME_SRC_X;
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
