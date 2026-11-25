// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>

namespace whj2 {
using TitleButtonPositions=std::array<std::array<std::array<int,2>,5>,2>;
struct TitleOpeningAudio {
 const char* opening=nullptr;
 const char* middle=nullptr;
 unsigned middleAt=0,musicAt=0;
 unsigned openingFade=0,middleFadeIn=0,middleFadeOut=0;
};
struct TitleModelTexture {
 const char*container=nullptr;
 const char*source=nullptr;
 const char*member=nullptr;
};
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
 const TitleButtonPositions* positions=nullptr;
 const char* buttonOff="01.png";
 const char* buttonOn="02.png";
 const char* quitButton="script/title/button.b";
 std::array<const char*,2> buttonArt{"script/title/button.b","script/title/button.b"};
 std::array<const char*,5> mainFiles{"start","load","album","option","quit"};
 std::array<const char*,4> extraFiles{"CG","Memory","Sound","Back"};
 TitleOpeningAudio audio{};
 std::array<TitleModelTexture,2> modelTextures{};
 int extraLastYOffset=0,extraFrameXOffset=0;
 bool exitUsesMotions=false;
 double fadeInMs=0,buttonStaggerMs=0,backdropExitDelayMs=0;
 bool frameBackdrop=false,albumAlwaysAvailable=false;
 bool holdIntroLastFrame=false;
 double startBlackFadeAtMs=0,startBlackFadeMs=0;
 const char* buttonPush=nullptr;
 const char* confirmSound=nullptr;
};
}
