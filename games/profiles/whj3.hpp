// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include "profiles/title_layout.hpp"

namespace whj3::profile {
// The chapter 8 full-size DPNG expands to 118.3 MiB before any texture upload.
inline constexpr unsigned sceneReadAheadBytes=144u*1024*1024;
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v30,10,"GameData/data","DLL/key.fkey","DLL/icon.key","WHJ3.exe","ボイス/","patch.dat",
 {{"image/","画像/"},{"chara/","立ち絵/"},{"music/","音楽/"},{"se/","効果音/"},
  {"script/albummode/thumimage/","thumbnail/album/"},
  {"script/memorymode/thumimage/","thumbnail/memory/"}}
};
inline const whj2::StoryProfile story{
 "scenario/root.s","scenario/select.s","include",
 "avg/header.s","avg/header_replay.s","avg/footer.s","WHJ3-STORY-1",
 {"sentence","format","message","name","face","bg01","bg02",
  "chara01","chara02","chara03","chara04","chara05","chara06","chara07","chara08","chara09","chara10",
  "music01","se01","se02","se03","movie",
  "ef01","ef02","ef03","ef04","effect","camera","textani"},
 "",{},"avg/preselect.s",true,"","","",0,false,"replayinclude.s",true
};
inline void applySentenceTiming(whj2::Event&){}
inline constexpr char gameName[]="美少女万華鏡 -神が造りたもうた少女たち-";
inline constexpr char gameEnv[]="WHJ3_GAME",saveEnv[]="WHJ3_SAVES",
 rendererEnv[]="WHJ3_RENDERER",logEnv[]="WHJ3_LOG",hostSaves[]="local/whj3-saves";
inline constexpr char sdGame[]="sdmc:/switch/whj3/game";
inline constexpr char sdSaves[]="sdmc:/switch/whj3/saves";
inline constexpr char sdRenderer[]="sdmc:/switch/whj3/renderer.cfg";
inline constexpr char titleMusic[]="music/安寧(タイトル).ogg";
inline constexpr char cgMusic[]="music/感動.ogg";
inline constexpr char replayMusic[]="music/安寧(タイトル).ogg";
inline constexpr char titleStartFade[]="^sentence,fade:overlap:500";
inline constexpr double cautionFade=0,cautionExitWait=0;
inline constexpr char secondMakerLogo[]="";
inline constexpr double titleStartWait=2000,titleMusicFade=500,cautionWait=10000;
inline constexpr whj2::TitleLayout title{
 {"script/title/title_back01.b","script/title/title_back02.b"},
 {"script/title/frame.b","script/title/frame.b"},
 {0,0},{0,0},{1176,1176},{177,177},{60,60},
 21500,500,21500,10,120,true,"*OnLock","*OnLock",
 nullptr,"01.png","02.png","script/title/button.b",
 {"script/title/button.b","script/title/button02.b"},
 {"start","load","extra","option","quit"},
 {"album","memory","music","back"},
 {"script/title/馬車.ogg","script/title/雪原を舞う風.ogg",7000,14500,500,2000,5000},
 {{{"script/title/title_back01.b#Back#model01","bg.png","背景"},
   {"script/title/title_back02.b#Back#model01","bg.png","裏タイトル"}}}
};
inline constexpr bool explicitCharacterExtensions=true;
inline constexpr bool captureReplacements=true;
inline constexpr bool relativeCameraDepth=true;
inline constexpr bool extendedCameraFields=true;
inline constexpr bool separateLayerTiming=true;
inline constexpr bool stackedHistory=false;
inline constexpr bool malformedLayerPriority=false;
inline constexpr bool deferredSceneChanges=true;
inline constexpr bool extendedCgRoots=true;
inline constexpr char extrasFlag[]="#spopenflag";
inline constexpr bool localizedHistory=true;
inline constexpr int musicTrackCount=29;
inline constexpr unsigned storyAudioTracks=4;
inline constexpr float storyMusicGain=1.f;
inline constexpr char mixerSignature[]="WHJ3-mixer-1";
}
