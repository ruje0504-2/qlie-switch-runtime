// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include "profiles/settings_layout.hpp"
#include "profiles/title_layout.hpp"
#include <string_view>

namespace whj6::profile {
inline constexpr unsigned sceneReadAheadBytes=52u*1024*1024;
inline constexpr std::array<unsigned,2> sceneSize{1920,1080};
inline constexpr int sceneVideoMaxHeight=2700;
inline constexpr int settingsPromptShift=-75,settingsTextShift=-58,settingsVoiceWaitY=894;
inline constexpr whj2::SettingsLayout settingsLayout{{{
 {320,(780+settingsTextShift)*2.f/3,300,0,-6,0},{320,(620+settingsTextShift)*2.f/3,300,0,-6,0},
 {820,146.67f,300,0,-6,0},{820,173.33f,300,0,-6,0},{820,200,300,0,-6,0},
 {820,226.67f,300,0,-6,0},{820,253.33f,300,0,-6,0},{820,280,300,0,-6,0},
 {320,(660+settingsTextShift)*2.f/3,300,0,-6,0}
}}, {176.67f,203.33f,230,256.67f,283.33f}};
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v31,8,"GameData/data","","","","ボイス/","patch.dat",
 {{"image/","画像/"},{"chara/","立ち絵/"},{"music/","音楽/"},{"se/","効果音/"},
  {"script/config/default.txt","script/config/valuedefault.txt"},
  {"script/albummode/thumimage/","thumbnail/album/"},
  {"script/memorymode/thumimage/","thumbnail/memory/"}}
};
inline const whj2::StoryProfile story{
 "scenario/root.s","scenario/select.s","include",
 "avg/header.s","avg/header_replay.s","avg/footer.s","WHJ6-STORY-1",
 {"sentence","format","message","name","face","bg01","bg02",
  "chara01","chara02","chara03","chara04","chara05","chara06","chara07","chara08","chara09","chara10",
  "music01","se01","se02","se03","sebgv","movie",
  "ef01","ef02","ef03","ef04","effect","camera","textani"},
 "",{},"avg/preselect.s",true,"scenario/name/name.s","avg2/header.s","scenario/ed_movie.s",64,true,
 "",true,{"autosave","charaon"}
};
inline void applySentenceTiming(whj2::Event&){}
inline bool emptyAnimationSlot(std::string_view path,unsigned slot,size_t slots){
 // These animations use the index after their last bitmap for an empty layer.
 return slot==slots&&(path.rfind("image/effect_c/",0)==0||
  path=="script/albummode/button_album.b"||path=="script/memorymode/button_memory.b");
}
inline constexpr char gameName[]="美少女万華鏡異聞 雪女";
inline constexpr char gameEnv[]="WHJ6_GAME",saveEnv[]="WHJ6_SAVES",
 rendererEnv[]="WHJ6_RENDERER",logEnv[]="WHJ6_LOG",hostSaves[]="local/whj6-saves";
inline constexpr char sdGame[]="sdmc:/switch/whj6/game";
inline constexpr char sdSaves[]="sdmc:/switch/whj6/saves";
inline constexpr char sdRenderer[]="sdmc:/switch/whj6/renderer.cfg";
inline constexpr char titleMusic[]="music/13.白銀の世界.ogg";
inline constexpr char cgMusic[]="music/07.夜のとばり.ogg";
inline constexpr char replayMusic[]="music/07.夜のとばり.ogg";
inline constexpr char titleStartFade[]="^sentence,fade:overlap:1000";
inline constexpr double titleStartWait=1100,titleMusicFade=750,cautionWait=10000;
inline constexpr auto title=[] {
 whj2::TitleLayout layout;
 layout.back={"script/title/back01.b","script/title/back01.b"};
 layout.frame={"script/title/frame.b","script/title/frame.b"};
 layout.frameX={0,0};layout.frameY={0,0};
 layout.buttonX={1630,1630};layout.buttonY={495,495};layout.buttonStep={100,100};
 layout.introMs=8800;layout.returnBackdropMs=7500;
 layout.alternateHits=0;layout.alternateRange=1;
 layout.asyncBackdropVideo=true;
 layout.lockedEnter="*OnLockEnable";layout.lockedIdle="*OnLockOffMouse";
 layout.buttonOff="_off.png";layout.buttonOn="_on.png";
 layout.buttonPush="_push.png";layout.confirmSound="misc/Sys_OnClickTitle.ogg";
 return layout;
}();
inline constexpr double cautionFade=450,cautionExitWait=500;
inline constexpr char secondMakerLogo[]="script/makerlogo/seikeiproduction.ogv";
inline constexpr bool explicitCharacterExtensions=false,captureReplacements=false,relativeCameraDepth=false;
inline constexpr bool extendedCameraFields=true,separateLayerTiming=true,stackedHistory=false;
inline constexpr bool malformedLayerPriority=false,deferredSceneChanges=false,localizedHistory=false;
inline constexpr int musicTrackCount=24;
inline constexpr bool extendedCgRoots=true;
inline constexpr char extrasFlag[]="#spopenflag";
inline constexpr unsigned storyAudioTracks=5;
inline constexpr float storyMusicGain=1.f;
inline constexpr char mixerSignature[]="WHJ6-mixer-1";
}
