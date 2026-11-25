// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include "profiles/title_layout.hpp"

namespace whj5::profile {
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v31,10,"GameData/data","reskey.fkey","","","ボイス/","patch.dat",
 {{"image/","画像/"},{"chara/","立ち絵/"},{"music/","音楽/"},{"se/","効果音/"},
  {"script/config/default.txt","script/config/valuedefault.txt"},
  {"script/albummode/thumimage/","thumbnail/album/"},
  {"script/memorymode/thumimage/","thumbnail/memory/"}}
};
inline const whj2::StoryProfile story{
 "scenario/root.s","scenario/select.s","include",
 "avg/header.s","avg/header_replay.s","avg/footer.s","WHJ5-STORY-1",
 {"sentence","format","message","name","face","bg01","bg02",
  "chara01","chara02","chara03","chara04","chara05","chara06","chara07","chara08","chara09","chara10",
  "music01","se01","se02","se03","sebgv","movie",
  "ef01","ef02","ef03","ef04","effect","camera","textani"},
 "",{},"avg/preselect.s",true,"scenario/name/name.s","avg2/header.s","scenario/ed_movie.s",64,true
};
inline void applySentenceTiming(whj2::Event&){}
inline constexpr char gameName[] = "美少女万華鏡 -理與迷宮的少女-";
inline constexpr char gameEnv[] = "WHJ5_GAME", saveEnv[] = "WHJ5_SAVES",
 rendererEnv[] = "WHJ5_RENDERER", logEnv[] = "WHJ5_LOG", hostSaves[] = "local/whj5-saves";
inline constexpr char sdGame[] = "sdmc:/switch/whj5/game";
inline constexpr char sdSaves[] = "sdmc:/switch/whj5/saves";
inline constexpr char sdRenderer[] = "sdmc:/switch/whj5/renderer.cfg";
inline constexpr char titleMusic[] = "music/16狐の曲.ogg";
inline constexpr char cgMusic[] = "music/23情事2の曲.ogg";
inline constexpr char replayMusic[] = "music/22情事1の曲.ogg";
inline constexpr char titleStartFade[] = "^sentence,fade:overlap:1000";
inline constexpr double titleStartWait = 1100, titleMusicFade = 750, cautionWait = 10000;
inline constexpr whj2::TitleButtonPositions titleButtons{{
 {{{570,604},{571,673},{370,641},{770,640},{1252,691}}},
 {{{571,604},{372,639},{569,676},{769,638},{0,0}}}
}};
inline constexpr whj2::TitleLayout title{
 {"script/title/back01.b","script/title/back01.b"},
 {"script/title/frame.b","script/title/frame.b"},
 {0,0},{0,0},{570,570},{604,604},{70,70},
 19000,500,19000,0,1,false,"*OnLockEnable","*OnLockOffMouse",
 &titleButtons,"_off.png","_on.png","script/title/button_quit.b"
};
inline constexpr double cautionFade = 450, cautionExitWait = 500;
inline constexpr char secondMakerLogo[] = "script/makerlogo/seikeiproduction.ogv";
inline constexpr bool extendedCgRoots=true;
inline constexpr char extrasFlag[]="#spopenflag";
inline constexpr unsigned storyAudioTracks=5;
inline constexpr float storyMusicGain=1.f;
inline constexpr char mixerSignature[]="WHJ5-mixer-1";
}
