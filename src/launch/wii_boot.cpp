#include "launch/wii_boot.hpp"

#include <gccore.h>

#include <cstring>

#include "booter_cfg.h"
#include "codehandler_info.h"
#include "core/dol.hpp"
#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"
#include "game_booter_dol.h"
#include "launch/chainloader.hpp"
#include "platform/log.hpp"

namespace nl::launch {

namespace {

inline void* at(u32 addr) { return reinterpret_cast<void*>(addr); }

static_assert(NL_WII_CFG_ADDR > kDolBufferAddr && NL_FRAG_LIST_ADDR + sizeof(::FragList) <= kBooterAddr,
              "the_CFG und Liste müssen im reservierten DOL-Puffer liegen");
static_assert(NL_GCT_ADDR + NL_GCT_MAX <= kBooterAddr, "GCT-Puffer überlappt den App-Booter");
// M15: Preflight-Grenze (core) = Codeliste des gebauten Handlers (codelist…codelistend).
static_assert(NL_CH_CODELIST_END - NL_CH_CODELIST == kWiiCodelistBytes, "kWiiCodelistBytes passt nicht zum Codehandler");
static_assert(kWiiCodelistBytes <= NL_GCT_MAX, "GCT-Puffer kleiner als die Codeliste");
static_assert(NL_WIP_ADDR + NL_WIP_MAX * sizeof(::WIP_Code) <= kBooterAddr, "WIP-Liste überlappt den App-Booter");
static_assert(NL_WIP_MAX == kWipMaxEntries, "kWipMaxEntries passt nicht zu NL_WIP_MAX");
static_assert(sizeof(::WIP_Code) == 12, "WIP_Code-Layout weicht von upstream ab");

}  // namespace

Result<void> loadDolBytesToBuffer(std::span<const u8> dol) {
    if (auto ok = checkDolImage(dol, NL_WII_CFG_ADDR - kDolBufferAddr); !ok.isOk()) return Result<void>::err(ok.error());
    std::memcpy(at(kDolBufferAddr), dol.data(), dol.size());
    return Result<void>::ok();
}

Result<LaunchPlan> prepareWiiBoot(const WiiBootPlan& plan, std::string_view title) {
    const bool isUsb = plan.source == WiiBootSource::Usb;
    if (plan.id6.size() != 6 || (isUsb && plan.fragBytes.size() != sizeof(::FragList))) {
        return Result<LaunchPlan>::err({ErrorCode::InvalidData, "WiiBootPlan"});
    }

    the_CFG cfg;
    std::memset(&cfg, 0, sizeof cfg);
    std::memcpy(cfg.gameID, plan.id6.data(), 6);
    if (isUsb) {
        cfg.fragments = reinterpret_cast<::FragList*>(NL_FRAG_LIST_ADDR);
        cfg.wbfsDevice = plan.usb ? WBFS_DEVICE_USB : WBFS_DEVICE_SDHC;
        cfg.wbfsPart = 0;
        cfg.GameBootType = TYPE_WII_WBFS_EXT;
    } else {
        cfg.fragments = nullptr;
        cfg.wbfsDevice = 0;
        cfg.wbfsPart = 0;
        cfg.GameBootType = TYPE_WII_DISC;
    }
    cfg.JustDanceGame = title.find("Just Dance") != std::string_view::npos;
    cfg.IOS.Version = plan.iosSlot;
    cfg.IOS.Type = IOS_TYPE_D2X;  // auch bei Unverified: der Booter versucht nur BlockIOSReload
    cfg.IOS.Base = static_cast<u8>(plan.cios.base);
    cfg.BootType = TYPE_WII_GAME;
    // M14: Spieloptionen; ohne Optionen sind das die kNl*-Standards aus booter_cfg.h (Test game_options).
    const WiiCfgFields& o = plan.options.cfg;
    cfg.configbytes[0] = o.language;
    cfg.configbytes[1] = kNlConfigByteUnpatched;
    cfg.vidMode = o.vidMode;
    cfg.patchVidMode = o.patchVidMode;
    cfg.vipatch = o.vipatch;
    cfg.countryString = o.countryString;
    cfg.patchregion = o.patchregion != 0;
    cfg.aspectRatio = o.aspectRatio;
    cfg.deflicker = o.deflicker;
    cfg.videoWidth = o.videoWidth;
    cfg.patchFix480p = o.patchFix480p != 0;
    cfg.private_server = o.privateServer;
    cfg.returnTo = o.returnTo;
    // M15: gewählte Codes; ohne Codes bleiben cheats/codelist leer und hooktype 0 (kein Sprung in den Handler).
    if (!plan.gct.empty()) {
        if (plan.gct.size() > kWiiCodelistBytes) return Result<LaunchPlan>::err({ErrorCode::InvalidData, "GCT"});
        std::memcpy(at(NL_GCT_ADDR), plan.gct.data(), plan.gct.size());
        DCFlushRange(at(NL_GCT_ADDR), plan.gct.size());
        cfg.cheats = static_cast<u8*>(at(NL_GCT_ADDR));
        cfg.cheatSize = static_cast<u32>(plan.gct.size());
        cfg.codelist = at(NL_CH_CODELIST);
        cfg.codelistend = static_cast<u8*>(at(NL_CH_CODELIST_END));
        cfg.hooktype = plan.hookType;
    }
    // M15: WIP-Patches; leer → wip_list NULL, set_wip_list() im Booter tut dann nichts.
    if (!plan.wip.empty()) {
        if (plan.wip.size() > NL_WIP_MAX) return Result<LaunchPlan>::err({ErrorCode::InvalidData, "WIP"});
        auto* list = static_cast<::WIP_Code*>(at(NL_WIP_ADDR));
        for (std::size_t i = 0; i < plan.wip.size(); ++i) {
            list[i].offset = plan.wip[i].offset;
            list[i].srcaddress = plan.wip[i].original;
            list[i].dstaddress = plan.wip[i].replacement;
        }
        DCFlushRange(list, static_cast<u32>(plan.wip.size() * sizeof(::WIP_Code)));
        cfg.wip_list = list;
        cfg.wip_count = static_cast<u32>(plan.wip.size());
    }
    cfg.debugger = 0;

    std::memcpy(at(NL_WII_CFG_ADDR), &cfg, sizeof cfg);
    DCFlushRange(at(NL_WII_CFG_ADDR), sizeof cfg);
    if (isUsb) {
        std::memcpy(at(NL_FRAG_LIST_ADDR), plan.fragBytes.data(), plan.fragBytes.size());
        DCFlushRange(at(NL_FRAG_LIST_ADDR), sizeof(::FragList));
    }

    if (auto loaded = loadDolBytesToBuffer({game_booter_dol, game_booter_dol_size}); !loaded.isOk()) {
        return Result<LaunchPlan>::err(loaded.error());
    }
    LaunchPlan booter;
    booter.dolPath = "game_booter";
    booter.dolSize = static_cast<u32>(game_booter_dol_size);
    booter.args = {"game_booter"};
    auto block = packArgvBlock(booter.args, kArgsAddr);
    if (!block.isOk()) return Result<LaunchPlan>::err(block.error());
    booter.argvBlock = std::move(block).value();
    return Result<LaunchPlan>::ok(std::move(booter));
}

void bootWiiGame(const LaunchPlan& booter, u8 iosSlot) {
    ISFS_Deinitialize();  // nand_titles hält ISFS offen; vor dem IOS-Wechsel schließen
    log::line("NL_IOS_RELOAD %u", static_cast<unsigned>(iosSlot));
    IOS_ReloadIOS(iosSlot);
    if (IOS_GetVersion() != static_cast<s32>(iosSlot)) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    chainload(booter);  // SYS_SHUTDOWN darin lädt kein IOS nach: das cIOS bleibt aktiv
}

}  // namespace nl::launch
