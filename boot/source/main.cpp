/****************************************************************************
 * Copyright (C) 2012 FIX94
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogcsys.h>
#include <ogc/consol.h>
#include <sys/iosupport.h>
#include <unistd.h>
#include <malloc.h>

#include "Config.hpp"
#include "ChannelHandler.hpp"
#include "nl/progress.h" // newlauncher: eigener Balken statt video_tinyload.h (M11)
#include "apploader.h"
#include "patchcode.h"
#include "memory.h"
#include "utils.h"
#include "disc.h"
#include "fst.h"
#include "wdvd.h"
#include "gecko.h"

#define EXT_ADDR_CFG	((vu32*)0x90100000)
IOS_Info CurrentIOS;

/* Boot Variables */
u32 GameIOS = 0;
u32 vmode_reg = 0;
GXRModeObj *vmode = NULL;

u32 AppEntrypoint = 0;

extern "C" {
extern void __exception_closeall();
extern s32 wbfsDev;
extern u32 wbfs_part_idx;
extern FragList *frag_list;
}

struct DiagBuf {
	char buf[512];
	u32 pos;

	void putCh(char c) {
		if(pos + 1 < sizeof(buf)) buf[pos++] = c;
	}
	void putStr(const char *s) {
		while(*s) putCh(*s++);
	}
	void putU32(u32 v) {
		char tmp[16];
		u32 n = 0;
		do {
			tmp[n++] = (char)('0' + (v % 10u));
			v /= 10u;
		} while(v != 0);
		while(n > 0) putCh(tmp[--n]);
	}
	void putS32(s32 v) {
		if(v < 0) {
			putCh('-');
			putU32((u32)(-(s64)v));
			return;
		}
		putU32((u32)v);
	}
	void putHex(u32 v, u32 digits) {
		static const char kHex[] = "0123456789ABCDEF";
		for(u32 i = digits; i > 0; --i) putCh(kHex[(v >> ((i - 1u) * 4u)) & 0xFu]);
	}
};

static void diagWrite(const char *text, u32 len) {
	const devoptab_t *dev = devoptab_list[STD_OUT];
	if(dev != NULL && dev->write_r != NULL) dev->write_r(_REENT, NULL, text, len);
}

the_CFG normalCFG;
int main()
{
	InitGecko();
	gprintf("WiiFlow External Booter by FIX94\n");
	memcpy(&normalCFG, (void*)NL_WII_CFG_ADDR, sizeof(the_CFG)); // newlauncher: feste Adresse statt Zeiger bei EXT_ADDR_CFG
	VIDEO_Init();// libogc
	video_init();// tinyload progress bar
	prog10();

	configbytes[0] = normalCFG.configbytes[0];// game language 0 - 9 or 0xCD = not patched
	configbytes[1] = normalCFG.configbytes[1];// not used
#if NL_BOOTER_CHEATS
	hooktype = normalCFG.hooktype;
#else
	hooktype = 0; // newlauncher: ohne Codehandler (M11) keine Hooks, siehe boot/UPSTREAM.md
#endif
	debuggerselect = normalCFG.debugger;
	CurrentIOS = normalCFG.IOS;
	set_wip_list(normalCFG.wip_list, normalCFG.wip_count);
	app_gameconfig_set(normalCFG.gameconf, normalCFG.gameconfsize);
	ocarina_set_codes(normalCFG.codelist, normalCFG.codelistend, normalCFG.cheats, normalCFG.cheatSize);
	copy_frag_list(normalCFG.fragments);
	wbfsDev = normalCFG.wbfsDevice;
	wbfs_part_idx = normalCFG.wbfsPart;
	prog10();

	s32 rWdvd = 0, rBlk = 0, rUsb = 0, rOpen = 0, rPart = 0, rOpenPart = 0;
	if(normalCFG.BootType == TYPE_WII_GAME)
	{
		rWdvd = WDVD_Init();
		if(rWdvd >= 0 && CurrentIOS.Type == IOS_TYPE_D2X)
		{
			// only for wii games - no known channels require block IOS Reload
			rBlk = BlockIOSReload();
			gprintf("Block IOS Reload using d2x %s.\n", rBlk < 0 ? "failed" : "succeeded");
		}
		if(rWdvd >= 0)
		{
			if(normalCFG.GameBootType == TYPE_WII_DISC)
			{
				// USB-Modus abschalten: Fehler ignorieren wie upstream (nicht jeder cIOS kennt das ioctl).
				s32 rOff = Disc_SetUSB(NULL, false);
				gprintf("Disc_SetUSB(NULL) = %i\n", rOff);
				if(CurrentIOS.Type == IOS_TYPE_HERMES)
					Hermes_Disable_EHC();
				if(normalCFG.vidMode > 1) //forcing a video mode
					normalCFG.patchVidMode = 1; //always normal patching
			}
			else
			{
				rUsb = Disc_SetUSB((u8*)normalCFG.gameID, normalCFG.GameBootType == TYPE_WII_WBFS_EXT);
				if(CurrentIOS.Type == IOS_TYPE_HERMES)
					Hermes_shadow_mload();
			}
		}
		prog(20);
		/* Clear Disc ID */
		memset((u8*)Disc_ID, 0, 32);
		if(rWdvd >= 0 && rUsb >= 0)
			rOpen = Disc_Open(normalCFG.GameBootType);// sets Disc_ID
		if(rWdvd >= 0 && rUsb >= 0 && rOpen >= 0)
		{
			u32 offset = 0;
			rPart = Disc_FindPartition(&offset);
			if(rPart >= 0)
				rOpenPart = WDVD_OpenPartition(offset, &GameIOS);
		}
		if(rWdvd >= 0 && rUsb >= 0 && rOpen >= 0 && rPart >= 0 && rOpenPart >= 0)
		{
			Disc_SetLowMem(normalCFG.JustDanceGame);
			if(normalCFG.vidMode == 5)
				normalCFG.patchVidMode = 1; //progressive mode requires this
			vmode = Disc_SelectVMode(normalCFG.vidMode, &vmode_reg);// requires Disc_ID[3]
			AppEntrypoint = Apploader_Run(normalCFG.vidMode, vmode, normalCFG.vipatch, normalCFG.countryString, normalCFG.patchVidMode, normalCFG.aspectRatio, normalCFG.returnTo,  
							normalCFG.patchregion, normalCFG.private_server, normalCFG.server_addr, normalCFG.videoWidth, normalCFG.patchFix480p, normalCFG.deflicker, wbfsDev == 2, normalCFG.BootType);
		}
		WDVD_Close();
	}
	else if(normalCFG.BootType == TYPE_CHANNEL)
	{
		ISFS_Initialize();
		AppEntrypoint = LoadChannel(normalCFG.title, normalCFG.use_dol, &GameIOS);// sets Disc_ID
		ISFS_Deinitialize();
		vmode = Disc_SelectVMode(normalCFG.vidMode, &vmode_reg);// requires Disc_ID[3]
		PatchChannel(normalCFG.vidMode, vmode, normalCFG.vipatch, normalCFG.countryString, normalCFG.patchVidMode, normalCFG.aspectRatio,
					normalCFG.returnTo, normalCFG.private_server, normalCFG.server_addr, normalCFG.videoWidth, normalCFG.patchFix480p, normalCFG.deflicker, normalCFG.BootType);
	}
	gprintf("Entrypoint: %08x, Requested Game IOS: %i\n", AppEntrypoint, GameIOS);
	if(rWdvd < 0 || rUsb < 0 || rOpen < 0 || rPart < 0 || rOpenPart < 0 || AppEntrypoint == 0)
	{
		const u8 *d = (const u8*)Disc_ID;
		DiagBuf b = {};
		b.putStr("  IOS="); b.putS32(IOS_GetVersion());
		b.putStr(" (cfg="); b.putU32(normalCFG.IOS.Version);
		b.putStr(" type="); b.putU32(normalCFG.IOS.Type);
		b.putStr(") dev="); b.putS32(wbfsDev);
		b.putStr(" frags="); b.putU32(frag_list ? frag_list->num : 0u);
		b.putStr("\n  wdvd="); b.putS32(rWdvd);
		b.putStr(" blk="); b.putS32(rBlk);
		b.putStr(" usb="); b.putS32(rUsb);
		b.putStr(" open="); b.putS32(rOpen);
		b.putStr(" part="); b.putS32(rPart);
		b.putStr(" openp="); b.putS32(rOpenPart);
		b.putStr("\n  disc=");
		for(int i = 0; i < 6; ++i) b.putHex(d[i], 2);
		b.putStr(" gameIOS="); b.putU32(GameIOS);
		b.putStr(" entry="); b.putHex(AppEntrypoint, 8);
		b.buf[b.pos] = 0;

		volatile u32 *hdr = (volatile u32*)(0x932F0000u | 0x40000000u);
		volatile u8 *dst = (volatile u8*)((0x932F0000u + 8u) | 0x40000000u);
		hdr[0] = 0;
		for(u32 i = 0; i < b.pos; ++i) dst[i] = (u8)b.buf[i];
		dst[b.pos] = 0;
		hdr[1] = b.pos;
		hdr[0] = 0x4E4C4352u;  // "NLCR"

		GXRModeObj *rm = VIDEO_GetPreferredMode(NULL);
		void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rm));
		CON_Init(fb, 20, 20, rm->fbWidth, rm->xfbHeight, rm->fbWidth * VI_DISPLAY_PIX_SZ);
		VIDEO_Configure(rm);
		VIDEO_SetNextFramebuffer(fb);
		VIDEO_SetBlack(FALSE);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		if(rm->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
		static const char kHead[] =
			"\n\n  Spiel konnte nicht gestartet werden.\n"
			"  The game could not be started.\n\n";
		// -4 aus WDVD_SetFragList: das d2x-EHCI-Modul bekommt die Platte nicht initialisiert.
		// Es fragt nur USB-Port 0 ab (Hardware-Abnahme M20, R6).
		static const char kUsbPort[] =
			"  USB-Platte in den anderen USB-Port stecken (cIOS liest nur Port 0).\n"
			"  Plug the USB drive into the other USB port (cIOS only reads port 0).\n\n";
		static const char kFoot[] =
			"\n\n  RESET druecken, um zurueckzukehren.\n"
			"  Press RESET to return.\n";
		diagWrite(kHead, sizeof(kHead) - 1u);
		if(rUsb == -4) diagWrite(kUsbPort, sizeof(kUsbPort) - 1u);
		diagWrite(b.buf, b.pos);
		diagWrite(kFoot, sizeof(kFoot) - 1u);
		while(!SYS_ResetButtonDown()) VIDEO_WaitVSync();
		exit(0);
	}
	setprog(320);

	/* Set Disc ID for WiiRD - must be set after ocarina stuff is done */
	memcpy((void*)0x80001800, (void*)Disc_ID, 8);
	
	/* Error 002 Fix (thanks WiiPower and uLoader) */
	*Current_IOS = (GameIOS << 16) | 0xffff;
	
	if(!isForwarder)
		*Apploader_IOS = (GameIOS << 16) | 0xffff;
	
	/* Flush everything */
	DCFlushRange((void*)0x80000000, 0x3f00);
	
	/* Enable front LED if requested */
	if(normalCFG.use_led) *HW_GPIOB_OUT |= 0x20;

	/* Set an appropriate video mode */
	Disc_SetVMode(vmode, vmode_reg);

	/* Shutdown IOS subsystems */
	u32 level = IRQ_Disable();
	__IOS_ShutdownSubsystems();
	__exception_closeall();

	/* Originally from tueidj - taken from NeoGamma (thx) */
	*(vu32*)0xCC003024 = 1;

	if(AppEntrypoint == 0x3400)
	{
		if(hooktype)
		{
			asm volatile (
				"lis %r3, returnpoint@h\n"
				"ori %r3, %r3, returnpoint@l\n"
				"mtlr %r3\n"
				"lis %r3, 0x8000\n"
				"ori %r3, %r3, 0x18A8\n"
				"nop\n"
				"mtctr %r3\n"
				"bctr\n"
				"returnpoint:\n"
				"bl DCDisable\n"
				"bl ICDisable\n"
				"li %r3, 0\n"
				"mtsrr1 %r3\n"
				"lis %r4, AppEntrypoint@h\n"
				"ori %r4,%r4,AppEntrypoint@l\n"
				"lwz %r4, 0(%r4)\n"
				"mtsrr0 %r4\n"
				"rfi\n"
			);
		}
		else
		{
			asm volatile (
				"isync\n"
				"lis %r3, AppEntrypoint@h\n"
				"ori %r3, %r3, AppEntrypoint@l\n"
				"lwz %r3, 0(%r3)\n"
				"mtsrr0 %r3\n"
				"mfmsr %r3\n"
				"li %r4, 0x30\n"
				"andc %r3, %r3, %r4\n"
				"mtsrr1 %r3\n"
				"rfi\n"
			);
		}
	}
	else if(hooktype)
	{
		asm volatile (
			"lis %r3, AppEntrypoint@h\n"
			"ori %r3, %r3, AppEntrypoint@l\n"
			"lwz %r3, 0(%r3)\n"
			"mtlr %r3\n"
			"lis %r3, 0x8000\n"
			"ori %r3, %r3, 0x18A8\n"
			"nop\n"
			"mtctr %r3\n"
			"bctr\n"
		);
	}
	else
	{
		asm volatile (
			"lis %r3, AppEntrypoint@h\n"
			"ori %r3, %r3, AppEntrypoint@l\n"
			"lwz %r3, 0(%r3)\n"
			"mtlr %r3\n"
			"blr\n"
		);
	}
	IRQ_Restore(level);

	return 0;
}
