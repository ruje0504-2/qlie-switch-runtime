// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>

namespace whj2 {
struct TitleLayout {
 std::array<const char*,2> back{"script/title/title_back01.b","script/title/title_back02.b"};
 std::array<const char*,2> frame{"script/title/frame.b","script/title/frame.b"};
 std::array<int,2> frameX{1070,1070}, frameY{450,450};
 std::array<int,2> buttonX{1080,1080}, buttonY{310,330}, buttonStep{70,80};
 double introMs=5000, buttonEntranceMs=500, returnBackdropMs=6000;
 unsigned alternateHits=5, alternateRange=100;
 bool alternateRequiresExtras=false;
 const char* lockedEnter="*OnLock";
 const char* lockedIdle="*OnLock";
};
}
