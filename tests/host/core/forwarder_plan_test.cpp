#include <doctest/doctest.h>

#include "core/forwarder_plan.hpp"
#include "core/wii_title.hpp"

using namespace nl;

TEST_CASE("isSelfChannelTitle recognizes UNLA and ignores other titles") {
    CHECK(isSelfChannelTitle(0x00010001554E4C41ull));
    CHECK(isChannelTitle(kSelfChannelTitleId));
    CHECK_FALSE(isSelfChannelTitle(0x0001000148415858ull));  // HAXX
    CHECK_FALSE(isSelfChannelTitle(0x0001000248414141ull));
}

TEST_CASE("forwarderCandidates, forwarderArgv, and hasForwarderFlag work as specified") {
    auto cands = forwarderCandidates();
    REQUIRE(cands.size() == 2);
    CHECK(cands[0] == "sd:/apps/newlauncher/boot.dol");
    CHECK(cands[1] == "usb:/apps/newlauncher/boot.dol");

    auto args = forwarderArgv("sd:/apps/newlauncher/boot.dol");
    REQUIRE(args.size() == 2);
    CHECK(args[0] == "sd:/apps/newlauncher/boot.dol");
    CHECK(args[1] == "--from-forwarder");

    const char* argvYes[] = {"sd:/apps/newlauncher/boot.dol", "--from-forwarder"};
    const char* argvNo[] = {"sd:/apps/newlauncher/boot.dol", "--remote"};
    CHECK(hasForwarderFlag(2, argvYes));
    CHECK_FALSE(hasForwarderFlag(2, argvNo));
    CHECK_FALSE(hasForwarderFlag(0, nullptr));
}
