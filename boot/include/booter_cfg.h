/****************************************************************************
 * Übergabeformat Launcher → Game-Booter (M9, siehe boot/UPSTREAM.md).
 *
 * Die Typen stammen unverändert aus WiiFlow Lite (resources/wiiflow_game_booter/source:
 * Config.hpp, frag.h, cios.h, types.h, wip.h), Copyright (C) 2012 FIX94 u. a., GPLv3 oder später.
 * Hier zusammengezogen, damit Launcher und Booter dieselbe Definition übersetzen; die upstream-Header
 * binden diese Datei ein.
 ****************************************************************************/
#ifndef NL_BOOTER_CFG_H
#define NL_BOOTER_CFG_H

#include <gctypes.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifndef ATTRIBUTE_PACKED
#define ATTRIBUTE_PACKED __attribute__((packed))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Speicher (Plan M9, Global Constraints) ------------------------------------------------- */
#define NL_WII_CFG_ADDR   0x93200000u /* the_CFG; der Booter liest hier statt über den Zeiger bei 0x90100000 */
#define NL_FRAG_LIST_ADDR 0x93201000u /* FragList in voller Größe (sizeof(FragList)) */
/* M15: Übergabepuffer der GCT (the_CFG.cheats). Der Booter kopiert sie früh in eigenen Speicher und beim Start
 * in die Codeliste hinter dem Codehandler (codelist…codelistend in MEM1); der Handler liest nie aus MEM2. */
#define NL_GCT_ADDR       0x93240000u
#define NL_GCT_MAX        0x10000u
/* M15: WIP-Liste (the_CFG.wip_list, WIP_Code-Einträge). set_wip_list() kopiert sie gleich nach dem Lesen von the_CFG
 * in den Booter-Heap; offset zählt im Datenstrom der main.dol-Sektionen (wip.c do_wip_code). */
#define NL_WIP_ADDR       0x93250000u
#define NL_WIP_MAX        1024u

/* --- types.h ---------------------------------------------------------------------------------- */
enum
{
	TYPE_WII_DISC = 0,
	TYPE_WII_WBFS,
	TYPE_WII_WBFS_EXT,
};

enum
{
    TYPE_WII_GAME = 0,
    TYPE_GC_GAME,
    TYPE_CHANNEL,
    TYPE_PLUGIN,
    TYPE_HOMEBREW,
    TYPE_END
};
#define NoGameID(x)			(x == TYPE_PLUGIN || x == TYPE_HOMEBREW)

enum
{
	IOS_TYPE_D2X = 0,
	IOS_TYPE_WANIN,
	IOS_TYPE_HERMES,
	IOS_TYPE_KWIIRK,
	IOS_TYPE_NORMAL_IOS,
	IOS_TYPE_STUB,
};
#define CustomIOS(x)		(x != IOS_TYPE_NORMAL_IOS && x != IOS_TYPE_STUB)

/* --- Gerätekonstanten: source/libwbfs/libwbfs.h:12–13 (gesetzt in source/loader/wbfs.c:216) ---- */
enum
{
	WBFS_DEVICE_USB = 1,	/* USB device */
	WBFS_DEVICE_SDHC		/* SDHC device */
};

/* --- frag.h ----------------------------------------------------------------------------------- */
// worst case wbfs fragmentation scenario:
// 9GB (dual layer) / 2mb (wbfs sector size) = 4608
#define MAX_FRAG 20000
// max that ehcmodule_frag will allow at the moment is about:
// 40000/4/3-1 = 21844

typedef struct
{
	u32 offset; // file offset, in sectors unit
	u32 sector;
	u32 count;
} Fragment;

typedef struct
{
	u32 size; // num sectors
	u32 num;  // num fragments
	u32 maxnum;
	Fragment frag[MAX_FRAG];
} FragList;

/* --- cios.h ----------------------------------------------------------------------------------- */
typedef struct _IOS_Info {
	u32 Revision;
	u32 SubRevision;
	u8 Version;
	u8 Type;
	u8 Base;
} IOS_Info;

/* --- wip.h ------------------------------------------------------------------------------------ */
typedef struct
{
	u32 offset;
	u32 srcaddress;
	u32 dstaddress;
} WIP_Code;

/* --- Config.hpp ------------------------------------------------------------------------------- */
struct the_CFG {
	/* needed for wii games */
	char gameID[7];
	FragList *fragments;
	s32 wbfsDevice;
	u32 wbfsPart;
	u8 GameBootType;
	WIP_Code *wip_list;
	u32 wip_count;
	u32 *gameconf;
	u32 gameconfsize;
	void *codelist;
	u8 *codelistend;
	bool JustDanceGame;
	bool patchregion;
	/* needed for channels */
	u64 title;
	bool use_dol;
	/* needed for both channels and wii games */
	IOS_Info IOS;
	u8 BootType;
	u8 configbytes[2];// [0] used for language. [1] not used
	u8 countryString;
	u8 vidMode;
	u8 patchVidMode;
	u8 vipatch;
	s8 aspectRatio;
	bool patchFix480p;
	u8 deflicker;
	u8 videoWidth;
	u8 private_server;
	char server_addr[24];
	u8 *cheats;
	u32 cheatSize;
	u8 debugger;
	u32 hooktype;
	u32 returnTo;
	bool use_led;
} ATTRIBUTE_PACKED;

/* --- Standardwerte des Launchers (Plan M9, Tabelle the_CFG) mit ihrer upstream-Herkunft ---------- */
enum
{
	kNlVidModeSystem = 1,          /* boot disc.c:164 Disc_SelectVMode: „case 1: // SYSTEM“ */
	kNlConfigByteUnpatched = 0xCD, /* source/menu/menu_game_boot.cpp:35 setLanguage: 0xCD = Sprache nicht patchen */
	kNlAspectRatioGame = -1,       /* menu_game_boot.cpp:1114: Einstellung 0 („Spielstandard“) − 1 */
	kNlDeflickerGame = 0,          /* menu_game_boot.cpp:1121–1122: Standard 0 */
	kNlVideoWidthGame = 0,         /* menu_game_boot.cpp:1124–1125: Standard 0 */
};
/* JustDanceGame: menu_game_boot.cpp:1099–1101 — wahr, wenn der Titel „Just Dance“ enthält. */

/* Layout ist Teil der Schnittstelle: 32-Bit-Zeiger, gepackt (PowerPC, gezählt aus den Feldern oben). */
#ifdef GEKKO
#ifdef __cplusplus
static_assert(sizeof(struct the_CFG) == 121, "the_CFG-Layout weicht von upstream ab");
static_assert(sizeof(FragList) == 12 + MAX_FRAG * 12, "FragList-Layout weicht von upstream ab");
#else
_Static_assert(sizeof(struct the_CFG) == 121, "the_CFG-Layout weicht von upstream ab");
_Static_assert(sizeof(FragList) == 12 + MAX_FRAG * 12, "FragList-Layout weicht von upstream ab");
#endif
#endif
/* M15: GCT-Puffer hinter the_CFG und FragList, ohne Überlappung. */
#ifdef __cplusplus
static_assert(NL_FRAG_LIST_ADDR + sizeof(FragList) <= NL_GCT_ADDR, "GCT-Puffer überlappt die FragList");
static_assert(NL_GCT_ADDR + NL_GCT_MAX <= NL_WIP_ADDR, "WIP-Liste überlappt den GCT-Puffer");
#else
_Static_assert(NL_FRAG_LIST_ADDR + sizeof(FragList) <= NL_GCT_ADDR, "GCT-Puffer überlappt die FragList");
_Static_assert(NL_GCT_ADDR + NL_GCT_MAX <= NL_WIP_ADDR, "WIP-Liste überlappt den GCT-Puffer");
#endif

#ifdef __cplusplus
}
#endif

#endif /* NL_BOOTER_CFG_H */
