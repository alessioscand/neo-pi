/*
 * neo-pi - menus: game list, OPTIONS, CONTROLS, power off countdown, credits
 */
#include "kernel.h"
#include "frame.h"

void CKernel::ShowUIFrame (boolean bWait, const char *pTopMessage)
{
	FrameDrawPicture (m_pUI->GetBuffer ());
	if (pTopMessage && pTopMessage[0])
	{
		// in the black band above the picture, like the in-game messages
		const CVideoOut::TTextSpan Span = {pTopMessage, UI_ERR};
		m_Video.DrawBandText (TRUE, &Span, 1);
	}
	else
	{
		m_Video.DrawBandText (TRUE, 0, 0);	// no status line or message over menus
	}
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
			RunSystemMenu (FALSE);
			n = ~0u;		// wait for release
		}
		nPrev = n;

		const char *pMsg = m_pInput->GetPadCount () == 0 ? "CONNECT A USB GAMEPAD" : "";
		m_pUI->DrawList (m_pGames, m_nGames, nSel, "", m_nLoaded >= 0);
		ShowUIFrame (TRUE, pMsg);
	}
}

void CKernel::RunSystemMenu (boolean bInGame)
{
	enum { SmSystem, SmRegion, SmFilter, SmControls, SmPower, SmCount };
	static const unsigned Regions[4] = {REGION_EU, REGION_US, REGION_JP, REGION_AS};

	unsigned nSel = 0;
	u32 nPrev = ~0u;
	unsigned nOldSystem = m_nSystem, nOldRegion = m_nRegion, nOldFilter = m_nFilter;
	const char *pMsg = "";
	unsigned nFrame = 0;

	// "Raspberry Pi 3 Model B+" -> "PI 3 B+"
	char Model[32];
	const char *pName = CMachineInfo::Get ()->GetMachineName ();
	if (strncmp (pName, "Raspberry ", 10) == 0) pName += 10;
	unsigned nLen = 0;
	for (; *pName && nLen < sizeof Model - 1; pName++)
	{
		if (strncmp (pName, "Model ", 6) == 0) pName += 6;
		char c = *pName;
		Model[nLen++] = c >= 'a' && c <= 'z' ? (char) (c - 32) : c;
	}
	Model[nLen] = '\0';
	char Footer[48];
	snprintf (Footer, sizeof Footer, "NEO-PI " PROJECT_VERSION "  %s", Model);

	while (1)
	{
		if (nFrame++ % 15 == 0)
		{
			m_pInput->UpdateDevices ();
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
				// MVS, AES, UNIBIOS (only with uni-bios_4_0.rom in neogeo.zip)
				do
				{
					m_nSystem = (m_nSystem + 3 + (unsigned) nDir) % 3;
				}
				while (m_nSystem == SYS_UNI && !m_bHasUni);
				break;

			case SmRegion: {
				if (m_nSystem == SYS_UNI)
				{
					break;		// set in the Universe BIOS's own menu
				}
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
					pMsg = "NEEDS MULTICORE";
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
		Items[SmSystem].pValue = SystemName ();
		Items[SmRegion].pValue = m_nSystem == SYS_UNI ? "SET IN UNIBIOS" : RegionName (m_nRegion);
		Items[SmRegion].bDisabled = m_nSystem == SYS_UNI;
		static const char *FilterLabels[FILTER_COUNT] = {"OFF", "LIGHT", "MEDIUM", "STRONG", "COMPOSITE"};
		// not allowed only on a Pi 3 with multicore=off
		// ("MULTICORE ONLY" does not fit next to SCANLINES)
		Items[SmFilter].pValue = m_bFilterAllowed ? FilterLabels[m_nFilter] : "MULTICORE OFF";
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
			  SystemName (), RegionName (m_nRegion), FilterName (m_nFilter));
		Log (Line, COLOR_TEXT);
	}
	if ((m_nSystem != nOldSystem || m_nRegion != nOldRegion) && m_nLoaded >= 0)
	{
		if (bInGame)
		{
			m_bReloadGame = TRUE;	// the game goes on; reloaded from the list
		}
		else
		{
			UnloadGame ();		// the BIOS changes: reload the game next time
		}
	}
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
		m_pUI->DrawCredits (PROJECT_VERSION);
		ShowUIFrame ();
	}
}
