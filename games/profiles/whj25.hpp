// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include "profiles/title_layout.hpp"

namespace whj25::profile {
inline constexpr unsigned sceneReadAheadBytes=52u*1024*1024;
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v30,9,"GameData/data","DLL/key.fkey","DLL/icon.key","鏡2.5.exe","ボイス/","patch.dat",
 {{"image/","画像/"},{"chara/","立ち絵/"},{"music/","音楽/"},{"se/","効果音/"}}
};
inline const whj2::StoryProfile story{
 "scenario/root.s","scenario/select.s","include",
 "avg/header.s","avg/header_replay.s","avg/footer.s","WHJ25-STORY-1",
 {"sentence","format","message","name","face","camera","bg01","bg02",
  "chara01","chara02","chara03","chara04","chara05","chara06","chara07","chara08","chara09","chara10",
  "ef01","ef02","ef03","ef04","motion","textani","music01","se01","se02","se03","movie"},
 // Filedmon is a developer marker; bg04 has no object in ProjectObjects.
 "",{},"",true,"","","",0,false,"",false,{"filedmon","bg04"}
};
inline void applySentenceTiming(whj2::Event&){}
inline constexpr char gameName[]="美少女万華鏡 -かつて少女だった君へ-";
inline constexpr char gameEnv[]="WHJ25_GAME",saveEnv[]="WHJ25_SAVES",
 rendererEnv[]="WHJ25_RENDERER",logEnv[]="WHJ25_LOG",hostSaves[]="local/whj25-saves";
inline constexpr char sdGame[]="sdmc:/switch/whj25/game";
inline constexpr char sdSaves[]="sdmc:/switch/whj25/saves";
inline constexpr char sdRenderer[]="sdmc:/switch/whj25/renderer.cfg";
inline constexpr char titleMusic[]="music/タイトル曲.ogg";
inline constexpr char cgMusic[]="music/hシーン曲.ogg";
inline constexpr char replayMusic[]="music/しっとり曲.ogg";
inline constexpr char titleStartFade[]="";
inline constexpr double cautionFade=0,cautionExitWait=0;
inline constexpr char secondMakerLogo[]="";
inline constexpr double titleStartWait=2300,titleMusicFade=500,cautionWait=4500;
inline constexpr whj2::TitleLayout title{
 {"script/title/title_back01.b","script/title/title_back01.b"},
 {"script/title/frame.b","script/title/frame.b"},
 {645,645},{500,500},{640,640},{390,420},{60,60},
 7000,500,7000,0,1,false,"*OnLock","*OnLock",
 nullptr,"01.png","02.png","script/title/button.b",
 {"script/title/button.b","script/title/button.b"},
 {"start","load","extra","option","quit"},
 {"album","memory","music","back"}, {}, {},10,-5,true,1000,100,300
};
inline constexpr bool explicitCharacterExtensions=true;
inline constexpr bool captureReplacements=false;
inline constexpr bool relativeCameraDepth=true;
inline constexpr bool extendedCameraFields=true;
inline constexpr bool separateLayerTiming=true;
inline constexpr bool stackedHistory=true;
inline constexpr bool malformedLayerPriority=true;
inline constexpr bool deferredSceneChanges=true;
inline constexpr bool extendedCgRoots=true;
inline constexpr char extrasFlag[]="#spopenflag";
inline constexpr bool localizedHistory=true;
inline constexpr int musicTrackCount=7;
inline constexpr unsigned storyAudioTracks=4;
inline constexpr float storyMusicGain=1.f;
inline constexpr char mixerSignature[]="WHJ25-mixer-1";
}
