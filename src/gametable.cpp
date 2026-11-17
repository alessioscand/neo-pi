/*
 * neo-pi - buttons A..D by NGH number, written by hand (checked against
 * Arcade-History); a game not here shows none
 */
#include "gametable.h"

struct TNeoButtons
{
	unsigned	 nNGH;
	const char	*pButtons[NEO_BUTTONS];	// 0 after the last, "" = not used
};

// sorted by NGH number
static const TNeoButtons s_Buttons[] =
{
	// nam1975: NAM-1975
	{  1, {"machine gun", "special weapon", "evade"}},
	// bstars: Baseball Stars Professional
	{  2, {"pitch/bat", "advance", "bunt", "time out"}},
	// tpgolf: Top Player's Golf
	{  3, {"shot/club", "club/mode", "", "score card"}},
	// maglord: Magician Lord
	{  5, {"shot", "jump"}},
	// ridhero: Riding Hero
	{  6, {"accelerate", "brake", "turbo"}},
	// alpham2: Alpha Mission II / ASO II - Last Guardian
	{  7, {"missile/laser", "armor attack", "armor menu"}},
	// ncombat: Ninja Combat
	{  9, {"attack/magic", "jump", "special"}},
	// cyberlip: Cyber-Lip
	{ 10, {"shoot", "jump", "select weapon"}},
	// superspy: The Super Spy
	{ 11, {"punch/weapon", "kick", "select weapon"}},
	// mutnat: Mutation Nation
	{ 14, {"attack", "jump"}},
	// kotm: King of the Monsters
	{ 16, {"punch", "kick", "run"}},
	// sengoku: Sengoku / Sengoku Denshou
	{ 17, {"attack", "jump", "change"}},
	// burningf: Burning Fight
	{ 18, {"attack", "jump attack", "middle attack", "hard kick"}},
	// lbowling: League Bowling
	{ 19, {"bowl"}},
	// gpilots: Ghost Pilots
	{ 20, {"shoot", "bomb"}},
	// joyjoy: Puzzled / Joy Joy Kid
	{ 21, {"turn block", "L-ball"}},
	// bjourney: Blue's Journey / Raguy
	{ 22, {"throw enemy", "jump", "change size"}},
	// quizdais: Quiz Daisousa Sen - The Last Count Down
	{ 23, {"answer", "answer", "answer", "answer"}},
	// lresort: Last Resort
	{ 24, {"shoot", "drone control"}},
	// eightman: Eight Man
	{ 25, {"punch", "jump", "8 Man bomber"}},
	// legendos: Legend of Success Joe / Ashitano Joe Densetsu
	{ 29, {"punch", "sway/guard"}},
	// 2020bb: 2020 Super Baseball
	{ 30, {"swing/pitch", "steal/base", "bunt", "time out"}},
	// socbrawl: Soccer Brawl
	{ 31, {"kick/shoot", "pass"}},
	// roboarmy: Robo Army
	{ 32, {"punch", "jump", "kick"}},
	// fatfury1: Fatal Fury - King of Fighters / Garou Densetsu - shukum
	{ 33, {"punch", "kick", "throw"}},
	// fbfrenzy: Football Frenzy
	{ 34, {"kick/boost", "pass A/switch", "pass B", "pass C"}},
	// crsword: Crossed Swords
	{ 37, {"slash", "thrust/magic"}},
	// trally: Thrash Rally
	{ 38, {"accelerate", "brake"}},
	// kotm2: King of the Monsters 2 - The Next Thing
	{ 39, {"punch", "kick", "jump"}},
	// sengoku2: Sengoku 2 / Sengoku Denshou 2
	{ 40, {"slice", "sever", "jump", "change"}},
	// bstars2: Baseball Stars 2
	{ 41, {"swing/pitch", "steal/base", "bunt", "time out"}},
	// quizdai2: Quiz Meitantei Neo & Geo - Quiz Daisousa Sen part 2
	{ 42, {"answer", "answer", "answer", "answer"}},
	// 3countb: 3 Count Bout / Fire Suplex
	{ 43, {"punch", "kick", "jump", "fall"}},
	// aof: Art of Fighting / Ryuuko no Ken
	{ 44, {"punch", "kick", "throw", "taunt"}},
	// samsho: Samurai Shodown / Samurai Spirits
	{ 45, {"light slash", "medium slash", "light kick", "medium kick"}},
	// tophuntr: Top Hunter - Roddy & Cathy
	{ 46, {"punch", "jump", "change planes"}},
	// fatfury2: Fatal Fury 2 / Garou Densetsu 2 - arata-naru tatakai
	{ 47, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// androdun: Andro Dunos
	{ 49, {"shoot", "change weapon"}},
	// ncommand: Ninja Commando
	{ 50, {"shoot", "somersault", "ninja powers"}},
	// viewpoin: Viewpoint
	{ 51, {"shoot", "bomb"}},
	// ssideki: Super Sidekicks / Tokuten Ou
	{ 52, {"shoot", "pass"}},
	// wh1: World Heroes
	{ 53, {"punch", "kick", "throw"}},
	// kof94: The King of Fighters '94
	{ 55, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// aof2: Art of Fighting 2 / Ryuuko no Ken 2
	{ 56, {"punch", "kick", "throw", "taunt"}},
	// wh2: World Heroes 2
	{ 57, {"punch", "kick", "throw"}},
	// fatfursp: Fatal Fury Special / Garou Densetsu Special
	{ 58, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// savagere: Savage Reign / Fu'un Mokushiroku - kakutou sousei
	{ 59, {"punch", "kick", "weapon", "line change"}},
	// fightfev: Fight Fever
	{ 60, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// ssideki2: Super Sidekicks 2 - The World Championship / Tokuten Ou
	{ 61, {"shoot/low pass", "high pass", "short kick"}},
	// spinmast: Spin Master / Miracle Adventure
	{ 62, {"shoot", "jump", "bomb"}},
	// samsho2: Samurai Shodown II / Shin Samurai Spirits - Haohmaru jig
	{ 63, {"light slash", "medium slash", "light kick", "medium kick"}},
	// wh2j: World Heroes 2 Jet
	{ 64, {"punch", "kick", "taunt"}},
	// wjammers: Windjammers / Flying Power Disc
	{ 65, {"throw/catch", "lob"}},
	// karnovr: Karnov's Revenge / Fighter's History Dynamite
	{ 66, {"weak punch", "strong punch", "weak kick", "strong kick"}},
	// gururin: Gururin
	{ 67, {"rotate left", "rotate right"}},
	// pspikes2: Power Spikes II
	{ 68, {"serve/spike", "toss/hit"}},
	// fatfury3: Fatal Fury 3 - Road to the Final Victory / Garou Denset
	{ 69, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// zupapa: Zupapa!
	{ 70, {"punch/toss", "jump"}},
	// panicbom: Panic Bomber
	{ 73, {"rotate"}},
	// aodk: Aggressors of Dark Kombat / Tsuukai GANGAN Koushinkyoku
	{ 74, {"punch", "kick", "jump"}},
	// sonicwi2: Aero Fighters 2 / Sonic Wings 2
	{ 75, {"shoot", "bomb"}},
	// zedblade: Zed Blade / Operation Ragnarok
	{ 76, {"shoot", "bomb"}},
	// galaxyfg: Galaxy Fight - Universal Warriors
	{ 78, {"light attack", "medium attack", "strong attack", "taunt"}},
	// strhoop: Street Hoop / Street Slam / Dunk Dream
	{ 79, {"shoot/dunk", "pass/steal"}},
	// quizkof: Quiz King of Fighters
	{ 80, {"answer", "answer", "answer", "answer"}},
	// ssideki3: Super Sidekicks 3 - The Next Glory / Tokuten Ou 3 - eik
	{ 81, {"shoot/low pass", "high pass", "short pass"}},
	// doubledr: Double Dragon (Neo-Geo)
	{ 82, {"light attack", "medium attack", "hard attack",
	       "extra attack"}},
	// pbobblen: Puzzle Bobble / Bust-A-Move (Neo-Geo)
	{ 83, {"shoot"}},
	// kof95: The King of Fighters '95
	{ 84, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// tws96: Tecmo World Soccer '96
	{ 86, {"short pass/slide", "long shot", "long pass"}},
	// samsho3: Samurai Shodown III / Samurai Spirits - Zankurou Musouke
	{ 87, {"light slash", "medium slash", "strong slash", "kick"}},
	// stakwin: Stakes Winner / Stakes Winner - GI kinzen seihae no mich
	{ 88, {"reins", "whip"}},
	// pulstar: Pulstar
	{ 89, {"shoot", "drone control"}},
	// whp: World Heroes Perfect
	{ 90, {"weak punch", "medium punch", "weak kick", "medium kick"}},
	// kabukikl: Far East of Eden - Kabuki Klash / Tengai Makyou - Shin 
	{ 92, {"weak slash", "medium slash", "weak kick", "medium kick"}},
	// neobombe: Neo Bomberman
	{ 93, {"bomb/throw", "special", "stop bomb"}},
	// gowcaizr: Voltage Fighter - Gowcaizer / Choujin Gakuen Gowcaizer
	{ 94, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// rbff1: Real Bout Fatal Fury / Real Bout Garou Densetsu
	{ 95, {"punch", "kick", "power attack", "line change"}},
	// aof3: Art of Fighting 3 - The Path of the Warrior / Art of Fighti
	{ 96, {"punch", "kick", "blow away", "taunt"}},
	// sonicwi3: Aero Fighters 3 / Sonic Wings 3
	{ 97, {"shoot", "bomb"}},
	// turfmast: Neo Turf Masters / Big Tournament Golf
	{200, {"shot", "hook", "slice"}},
	// mslug: Metal Slug - Super Vehicle-001
	{201, {"shoot", "jump", "grenade"}},
	// puzzledp: Puzzle De Pon!
	{202, {"shoot"}},
	// mosyougi: Syougi No Tatsujin - Master of Syougi
	{203, {"grab", "cancel"}},
	// marukodq: Chibi Marukochan Deluxe Quiz
	{206, {"answer", "answer", "answer", "answer"}},
	// neomrdo: Neo Mr. Do!
	{207, {"attack"}},
	// sdodgeb: Super Dodge Ball / Kunio no Nekketsu Toukyuu Densetsu
	{208, {"shoot/catch", "pass/dodge", "jump", "feint"}},
	// goalx3: Goal! Goal! Goal!
	{209, {"shoot", "pass", "short kick"}},
	// overtop: Over Top
	{212, {"accelerate", "reduce speed"}},
	// neodrift: Neo Drift Out - New Technology
	{213, {"accelerate", "brake"}},
	// kof96: The King of Fighters '96
	{214, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// ssideki4: The Ultimate 11 - The SNK Football Championship / Tokut
	{215, {"shoot/low pass", "high pass", "short pass"}},
	// kizuna: Kizuna Encounter - Super Tag Battle / Fu'un Super Tag Bat
	{216, {"punch", "kick", "weapon", "tag"}},
	// ninjamas: Ninja Master's - haoh-ninpo-cho
	{217, {"weak punch", "strong punch", "weak kick", "strong kick"}},
	// ragnagrd: Ragnagard / Shin-Oh-Ken
	{218, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// pgoal: Pleasure Goal / Futsal - 5 on 5 Mini Soccer
	{219, {"shoot", "pass", "trick play", "save"}},
	// magdrop2: Magical Drop II
	{221, {"catch", "throw"}},
	// samsho4: Samurai Shodown IV - Amakusa's Revenge / Samurai Spirits
	{222, {"light slash", "medium slash", "strong slash", "kick"}},
	// rbffspec: Real Bout Fatal Fury Special / Real Bout Garou Densetsu
	{223, {"punch", "kick", "power attack", "line change"}},
	// twinspri: Twinkle Star Sprites
	{224, {"shoot", "bomb"}},
	// wakuwak7: Waku Waku 7
	{225, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// stakwin2: Stakes Winner 2
	{227, {"reins", "whip", "go sign"}},
	// ghostlop: Ghostlop
	{228, {"red ball", "blue ball"}},
	// breakers: Breakers
	{230, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// miexchng: Money Puzzle Exchanger / Money Idol Exchanger
	{231, {"catch coins", "throw coins"}},
	// kof97: The King of Fighters '97
	{232, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// magdrop3: Magical Drop III
	{233, {"catch", "throw", "lower"}},
	// lastblad: The Last Blade / Bakumatsu Roman - Gekka no Kenshi
	{234, {"weak slash", "strong slash", "kick", "repel"}},
	// irrmaze: The Irritating Maze / Ultra Denryu Iraira Bou
	{236, {"view"}},
	// popbounc: Pop 'n Bounce / Gapporin
	{237, {"shoot"}},
	// shocktro: Shock Troopers
	{238, {"shoot", "evade", "special", "change player"}},
	// blazstar: Blazing Star
	{239, {"shoot", "break attack"}},
	// rbff2: Real Bout Fatal Fury 2 - The Newcomers / Real Bout Garou D
	{240, {"punch", "kick", "power attack", "line change"}},
	// mslug2: Metal Slug 2 - Super Vehicle-001/II
	{241, {"shoot", "jump", "grenade"}},
	// kof98: The King of Fighters '98 - The Slugfest / King of Fighters
	{242, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// lastbld2: The Last Blade 2 / Bakumatsu Roman - Dai Ni Maku Gekka 
	{243, {"weak slash", "strong slash", "kick", "repel"}},
	// neocup98: Neo-Geo Cup '98 - The Road to the Victory
	{244, {"shoot/low pass", "high pass", "short pass"}},
	// breakrev: Breakers Revenge
	{245, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// shocktr2: Shock Troopers - 2nd Squad
	{246, {"shoot", "bomb", "jump"}},
	// flipshot: Battle Flip Shot
	{247, {"special", "slide", "provoke"}},
	// pbobbl2n: Puzzle Bobble 2 / Bust-A-Move Again (Neo-Geo)
	{248, {"shoot"}},
	// ctomaday: Captain Tomaday
	{249, {"left punch", "right punch", "formation"}},
	// mslugx: Metal Slug X - Super Vehicle-001
	{250, {"shoot", "jump", "grenade"}},
	// kof99: The King of Fighters '99 - Millennium Battle
	{251, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// ganryu: Ganryu / Musashi Ganryuki
	{252, {"sword", "jump", "chain"}},
	// garou: Garou - Mark of the Wolves
	{253, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// s1945p: Strikers 1945 Plus
	{254, {"shoot", "support"}},
	// preisle2: Prehistoric Isle 2
	{255, {"shoot", "bomb"}},
	// mslug3: Metal Slug 3
	{256, {"shoot", "jump", "grenade"}},
	// kof2000: The King of Fighters 2000
	{257, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// bangbead: Bang Bead
	{259, {"swing", "threat/sliding", "miracle swing"}},
	// nitd: Nightmare in the Dark
	{260, {"fireball", "jump", "fireball"}},
	// sengoku3: Sengoku 3 / Sengoku Densho 2001
	{261, {"weapon", "blast", "jump", "projectile"}},
	// kof2001: The King of Fighters 2001
	{262, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// mslug4: Metal Slug 4
	{263, {"shoot", "jump", "grenade", "Metal Slug attack"}},
	// rotd: Rage of the Dragons
	{264, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// kof2002: The King of Fighters 2002
	{265, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// matrim: Matrimelee / Shin Gouketsuji Ichizoku Toukon
	{266, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// pnyaa: Pochi and Nyaa
	{267, {"turn left", "turn right", "spiky nyan"}},
	// ms5pcb: Metal Slug 5 (JAMMA PCB)
	{268, {"shoot", "jump", "grenade", "Metal Slug attack"}},
	// svcpcb: SNK vs. Capcom - SVC Chaos
	{269, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// samsho5: Samurai Shodown V / Samurai Spirits Zero
	{270, {"light slash", "medium slash", "kick", "dodge"}},
	// kf2k3pcb: The King of Fighters 2003 (Japan, JAMMA PCB)
	{271, {"weak punch", "weak kick", "strong punch", "strong kick"}},
	// samsh5sp: Samurai Shodown V Special / Samurai Spirits Zero Specia
	{272, {"light slash", "medium slash", "kick", "dodge"}},
};

static const unsigned s_nButtons = sizeof s_Buttons / sizeof s_Buttons[0];

int NeoButtonsFind (unsigned nNGH)
{
	if (nNGH == 0)			// carts with no number (prototypes,
	{				// bootlegs, homebrew)
		return -1;
	}
	unsigned nLow = 0, nHigh = s_nButtons;	// binary search: sorted table
	while (nLow < nHigh)
	{
		const unsigned nMid = (nLow + nHigh) / 2;
		if (s_Buttons[nMid].nNGH < nNGH)
		{
			nLow = nMid + 1;
		}
		else
		{
			nHigh = nMid;
		}
	}
	return nLow < s_nButtons && s_Buttons[nLow].nNGH == nNGH ? (int) nLow : -1;
}

const char *const *NeoButtons (int nIndex)
{
	return s_Buttons[nIndex].pButtons;
}
