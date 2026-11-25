// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "story.hpp"
#include "profiles/title_layout.hpp"
#include "profiles/settings_layout.hpp"

namespace whj1::profile {
// The original Moonlight clip is 74.7 MiB after stereo PCM decoding.
inline constexpr unsigned sceneReadAheadBytes=80u*1024*1024;
inline const qlie::ArchiveLayout archives{
 qlie::PackVersion::v30,8,"GameData/data","DLL/key.fkey","DLL/icon.key",
 "美少女万華鏡－１－.exe","voice/","patch.dat",
 {{"avg/","library/avg/"},{"avg/uppermenu/","library/uppermenu/"},{"chara/","charaimage/"},{"face/","charaface/"},{"se/","sound/"},
  {"script/memorymode/","script/replaymode/"},
  {"script/memorymode/サムネイル_no_image.png","script/replaymode/サムネイル_no_memory.png"},
  {"script/memorymode/fade.png","script/replaymode/fadeimage.png"},
  {"script/albummode/viewquit_n.png","script/albummode/viewback_n.png"},
  {"script/caution/caution.b","script/makerlogo/caution.b"},
  {"movie/logo.ogv","movie/ロゴ.ogv"},
  {"movie/kaleidoscope.ogv","movie/万華鏡.ogv"},
  {"movie/万華鏡.ogv","movie/kaleidoscope.ogv"},
  {"system/font/外字.of","system/font/外字登録.of"},
  {"misc/sys_onclick.wav","misc/sys_onclick.ogg"},
  {"misc/sys_onmouse.wav","misc/sys_onmouse.ogg"},
  {"misc/sys_cancel.wav","misc/sys_cancel.ogg"},
  {"script/config/タイトルへ戻る_off.png","library/uppermenu/タイトルへ戻る_off.png"},
  {"script/config/タイトルへ戻る_on.png","library/uppermenu/タイトルへ戻る_on.png"},
  {"script/config/ゲームの終了_off.png","library/uppermenu/ゲームの終了_off.png"},
  {"script/config/ゲームの終了_on.png","library/uppermenu/ゲームの終了_on.png"},
  {"script/saveload/タイトルへ戻る_off.png","library/uppermenu/タイトルへ戻る_off.png"},
  {"script/saveload/ゲームの終了_off.png","library/uppermenu/ゲームの終了_off.png"}}
};
inline constexpr whj2::LegacyAvgLayout legacyLayout{
 "windowframe_window.b",261,603,824,120,24,
 0,720,
 285,567,24,0xff5555,
 "^face,x:200,y:720,align:center:bottom,mode:auto,pri:1015",
 "charaface/","library/avg/window/faceanimation.b",200,
 "5",0x00b6,0x01f7,
 {{{"#1_route_a_f","#game_aclearflag"},{"#1_route_b_f","#game_bclearflag"}}},
 "movie/logo.ogv",{"movie/万華鏡.ogv","movie/kaleidoscope.ogv"},
 "movie/ndou99_1_01.ogv",5000,81000
};
inline const whj2::StoryProfile story{
 "scenario/ks_01/start.s","","",
 "library/avg/header.s","","library/avg/footer.s","WHJ1-STORY-1",
 {"sentence","format","message","face","bg01","bg02","bg03",
  "chara01","chara02","chara03","chara04","motion","ef01",
  "music01","se01","se02","se03","movie"},
 "",{},"",false,"","","",0,false,"",false,{},&legacyLayout
};
inline void applySentenceTiming(whj2::Event&){}
inline const char* transitionCaptureBitmap(const whj2::Event& event){
 if(event.file=="scenario/ks_01/kk_main_01.s"&&event.line==508&&
    event.value=="^sentence,fade:anime:1000:Zoom,wait:click:1000:0")
  return "image/n17_1_01.png";
 return nullptr;
}
inline constexpr char gameName[]="美少女万華鏡 -呪われし伝說の少女-";
inline constexpr char gameEnv[]="WHJ1_GAME",saveEnv[]="WHJ1_SAVES",
 rendererEnv[]="WHJ1_RENDERER",logEnv[]="WHJ1_LOG",hostSaves[]="local/whj1-saves";
inline constexpr char sdGame[]="sdmc:/switch/whj1/game";
inline constexpr char sdSaves[]="sdmc:/switch/whj1/saves";
inline constexpr char sdRenderer[]="sdmc:/switch/whj1/renderer.cfg";
inline constexpr char titleMusic[]="music/nbgm04.ogg";
inline constexpr char cgMusic[]="music/nbgm01.ogg";
inline constexpr char replayMusic[]="music/nbgm01.ogg";
inline constexpr char titleStartFade[]="^sentence,fade:anime:1000:script/title/test_01b.b";
inline constexpr double cautionFade=0,cautionExitWait=0;
inline constexpr char secondMakerLogo[]="";
inline constexpr double titleStartWait=1500,titleMusicFade=500,cautionWait=5250;
inline constexpr whj2::TitleLayout title{
 {"script/title/1_タイトル画面ムービー.b","script/title/bm1_title.b"},
 {"script/title/extention_花びら01.b","script/title/extention_花びら01.b"},
 {0,0},{0,0},{1020,1020},{348,378},{66,64},
 6000,500,6000,1,16,false,"*OnLock","*OnLock",
 nullptr,"_off.png","_on.png","script/title/bm1_title_button.b",
 {"script/title/bm1_title_button.b","script/title/bm1_title_button.b"},
 {"start","load","album","option","quit"},
 {"cg_mode","memory_mode","sound_mode","back"},{},{},0,0,false,0,0,0,true,true,true,500,500
};
inline constexpr whj2::SettingsLayout settingsLayout{{{
 {355,198,245,-6,-10,5},{355,314,245,-6,-10,5},
 {662,164,465,-8,-11,5},{662,243,465,-8,-11,5},{662,322,465,-8,-11,5},
 {662,401,465,-8,-11,5},{662,480,465,-8,-11,5},{662,559,465,-8,-11,5},
 {355,373,245,-6,-10,5}
}}, {225,304,383,462,541}, true};
inline constexpr bool explicitCharacterExtensions=true;
inline constexpr bool captureReplacements=false;
inline constexpr bool relativeCameraDepth=false;
inline constexpr bool extendedCameraFields=false;
inline constexpr bool separateLayerTiming=true;
inline constexpr bool stackedHistory=true;
inline constexpr bool malformedLayerPriority=false;
inline constexpr bool deferredSceneChanges=true;
inline constexpr bool extendedCgRoots=true;
inline constexpr const char*extrasFlag=nullptr;
inline constexpr bool localizedHistory=true;
inline constexpr int musicTrackCount=16;
inline constexpr unsigned storyAudioTracks=5;
inline constexpr float storyMusicGain=1.f;
inline constexpr char mixerSignature[]="WHJ1-mixer-1";
}
