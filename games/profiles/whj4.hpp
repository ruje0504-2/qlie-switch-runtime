// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include "profiles/title_layout.hpp"

namespace whj4::profile {
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v31,9,"GameData/data","reskey.fkey","","","ボイス/","patch.dat",
 {{"image/","画像/"},{"chara/","立ち絵/"},{"music/","音楽/"},{"se/","効果音/"},
  {"script/config/default.txt","script/config/valuedefault.txt"},
  {"script/albummode/thumimage/","thumbnail/album/"},
  {"script/memorymode/thumimage/","thumbnail/memory/"}}
};
inline const whj2::StoryProfile story{
 "scenario/root.s","scenario/select.s","include",
 "avg/header.s","avg/header_replay.s","avg/footer.s","WHJ4-STORY-1",
 {"sentence","format","message","face","bg01","bg02",
  "chara01","chara02","chara03","chara04","chara05","chara06",
  "music01","se01","se02","se03","sebgv","movie",
  "ef01","ef02","ef03","ef04","effect","camera","textani"},
 "",{},"avg/preselect.s",true
};
inline void applySentenceTiming(whj2::Event&){}
inline constexpr char gameName[] = "美少女万華鏡 -罪と罰の少女-";
inline constexpr char gameEnv[] = "WHJ4_GAME", saveEnv[] = "WHJ4_SAVES",
 rendererEnv[] = "WHJ4_RENDERER", logEnv[] = "WHJ4_LOG", hostSaves[] = "local/whj4-saves";
inline constexpr char sdGame[] = "sdmc:/switch/whj4/game";
inline constexpr char sdSaves[] = "sdmc:/switch/whj4/saves";
inline constexpr char sdRenderer[] = "sdmc:/switch/whj4/renderer.cfg";
inline constexpr char titleMusic[] = "music/24苦しみの曲.ogg";
inline constexpr char cgMusic[] = "music/06情事2.ogg";
inline constexpr char replayMusic[] = "music/05情事1.ogg";
inline constexpr char titleStartFade[] = "^sentence,fade:overlap:1000";
inline constexpr double titleStartWait = 1100, titleMusicFade = 750, cautionWait = 10000;
inline constexpr whj2::TitleLayout title{
 {"script/title/back01.b","script/title/back02.b"},
 {"script/title/frame01.png","script/title/frame02.png"},
 {1050,960},{50,0},{1130,1136},{160,197},{70,70},
 10000,500,20000,10,120,true,"*OnLockEnable","*OnLockOffMouse"
};
inline constexpr char secondMakerLogo[] = "";
inline constexpr bool extendedCgRoots=true;
inline constexpr char extrasFlag[]="#spopenflag";
inline constexpr unsigned storyAudioTracks=5;
inline constexpr float storyMusicGain=1.f;
inline constexpr char mixerSignature[]="WHJ4-mixer-2";
}
