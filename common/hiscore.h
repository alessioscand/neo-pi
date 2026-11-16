/*
 * Hiscores and battery RAM, kept on the card
 *
 * CHiscore does what MAME's hiscore plugin does (hiscore.dat): once the game
 * has set its table up in RAM, the table of the .hi file is put over it; the
 * table is written back when it changed. The .hi files are MAME's own format.
 *
 * CBatteryRam keeps the RAM a board keeps with its battery (as MAME's NVRAM:
 * settings, bookkeeping, the scores of the games that keep them there),
 * loaded when the game starts and written when it changed.
 *
 * The core is reached through a few functions (THiCore, TNvCore); the paths
 * are the caller's, and so is the log: each call says what happened.
 */
#ifndef HISCORE_H
#define HISCORE_H

#include <circle/types.h>
#include <stddef.h>

enum TCardResult
{
	CardNothing,		// nothing to do (no table, not ready, not changed)
	CardLoaded,		// the card's copy is in the game
	CardBlank,		// none on the card yet
	CardSaved,
	CardFailed		// could not be written
};

struct THiCore
{
	size_t	(*pSize) (void);		// bytes of the table (0 = no hiscores)
	int	(*pReady) (void);		// has the game set its table up?
	void	(*pGet) (u8 *pDst);
	void	(*pPut) (const u8 *pSrc);
};

struct TNvCore
{
	size_t	(*pSize) (void);		// 0 = nothing kept
	u8 *	(*pData) (void);
};

class CHiscore
{
public:
	CHiscore (void);
	~CHiscore (void);

	void Start (const THiCore *pCore, const char *pPath);	// a game is loaded or reset
	void Stop (void);
	TCardResult Tick (void);	// after each frame: CardLoaded / CardBlank once
	TCardResult Save (void);	// when it differs from the card's and the game's own

private:
	enum { Off, Waiting, Active };
	enum { GuardFrames = 600 };	// about 10 seconds, too short for a game to be played
	const THiCore *m_pCore;
	unsigned m_nState;
	unsigned m_nGuard;		// frames left watching for the game's own set-up
	size_t m_nSize;
	u8 *m_pDefault;			// the game's own table (power-on)
	u8 *m_pCard;			// what the .hi file holds
	u8 *m_pNow;
	boolean m_bCard;		// ... if there is one
	char m_Path[160];
};

class CBatteryRam
{
public:
	CBatteryRam (void);
	~CBatteryRam (void);

	TCardResult Start (const TNvCore *pCore, const char *pPath);	// CardLoaded / CardBlank
	TCardResult Save (void);	// CardSaved / CardFailed when it changed
	void Clear (void);		// as taking the battery out: RAM at zero, file deleted

private:
	const TNvCore *m_pCore;
	size_t m_nSize;
	u8 *m_pCard;			// what the file holds (or will)
	char m_Path[160];
};

#endif
