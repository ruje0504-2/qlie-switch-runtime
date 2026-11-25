#if defined(WHJ2_WITH_VULKAN) || defined(WHJ2_WITH_DEKO3D)
#define WHJ2_WITH_GPU
#include "diagnostics.h"
#include "gpu_raster.hpp"
#endif
#ifdef WHJ2_WITH_VULKAN
#include "vulkan_raster.hpp"
#endif
#ifdef WHJ2_WITH_DEKO3D
#include "deko3d_raster.hpp"
#endif
#ifdef __SWITCH__
#include <fstream>
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
// Incremental native frontend: graphics/audio integration remains in progress.
#include "profile.hpp"
#include "ui_locale.hpp"
#include "story.hpp"
#ifdef WHJ2_MAC_GAME_PICKER
#include "mac_game_access.h"
#endif
#include "audio.hpp"
#include "switch_threads.hpp"
#include "switch_storage.hpp"
#include "save_slot_write.hpp"
#include "scene_images.hpp"
#include "message_text.hpp"
#include "scene_transition.hpp"
#include "sentence_wait.hpp"
#include "state_io.hpp"
#include "movie.hpp"
#include "joycon.hpp"
#include "voice.hpp"
#include "history.hpp"
#include "upper_menu.hpp"
#include "game_menu.hpp"
#include "auto_play.hpp"
#include "frame_pacer.hpp"
#include "fps_overlay.hpp"
#include "title_screen.hpp"
#include "choice_screen.hpp"
#include "progress.hpp"
#include "music_screen.hpp"
#include "replay_screen.hpp"
#include "cg_screen.hpp"
#include <sstream>
#include <chrono>
#include <algorithm>
extern "C" {
#include "font.h"
}
#include <SDL.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <charconv>
#include <tuple>
#ifdef WHJ2_WITH_GPU
static std::string selectedRenderer(){
 if(const char* mode=std::getenv(game_profile::rendererEnv);mode&&*mode)return mode;
#ifdef __SWITCH__
 std::string name;
 if(std::ifstream file{game_profile::sdRenderer};file>>name)return name;
 return "deko3d";
#else
 return "cpu";
#endif
}
#endif
static std::vector<uint32_t> utf8(const std::string&s){std::vector<uint32_t>o;
 for(size_t p=0;p<s.size();){unsigned c=uint8_t(s[p++]),n=0;
 if(c>=240){c&=7;
 n=3;
 }else if(c>=224){c&=15;
 n=2;
 }else if(c>=192){c&=31;
 n=1;
 }for(unsigned i=0;i<n;i++){if(p==s.size())throw std::runtime_error("Truncated UTF8");
 c=(c<<6)|(uint8_t(s[p++])&63);
 }o.push_back(c);
 }return o;
 }
namespace whj2 {
struct PlaybackState {
 std::string text,name,body,speaker,voice,page;
 std::array<float,5> volumes{};
 std::array<bool,5> loops{};
 std::array<unsigned,5> times{},types{};
 bool bgvMode=false;
 whj2::SentenceWait wait;
 whj2::AutoPlay autoPlay;
};
struct HistoryCheckpoint {
 size_t row=0;
 PlaybackState state;
 qlie::Bytes story,scene,audio;
};
using HistoryCheckpoints=std::deque<std::shared_ptr<const HistoryCheckpoint>>;
struct PreparedHistoryCheckpoints {
 HistoryCheckpoints checkpoints;
 uint64_t storyHash=0,historyHash=0;

 explicit PreparedHistoryCheckpoints(const qlie::Bytes&bytes){
  if(bytes.empty())return;
  whj2::StateReader reader{bytes};
  if(reader.str()!="QLIE-history-checkpoints-1")throw std::runtime_error("History checkpoint version");
  storyHash=reader.n();historyHash=reader.n();
  auto count=reader.n();
  if(count>game_profile::story.historyJumpLimit)
   throw std::runtime_error("Invalid history checkpoint count");
  while(count--){
   const auto row=reader.n();
   if(!checkpoints.empty()&&row<=checkpoints.back()->row)
    throw std::runtime_error("Invalid history checkpoint row");
   auto checkpoint=std::make_shared<HistoryCheckpoint>();
   checkpoint->row=size_t(row);
   checkpoint->story=reader.blob();
   auto&state=checkpoint->state;
   for(auto*value:{&state.text,&state.name,&state.body,&state.speaker,&state.voice,&state.page})
    *value=reader.str();
   for(unsigned i=0;i<game_profile::storyAudioTracks;++i){
    state.volumes[i]=reader.f();
    state.loops[i]=reader.n()!=0;state.times[i]=unsigned(reader.n());state.types[i]=unsigned(reader.n());
   }
   state.bgvMode=reader.n()!=0;
   state.wait.restore(reader.blob());
   state.autoPlay.restore(reader.blob());
   checkpoint->scene=reader.blob();
   checkpoint->audio=reader.blob();
   checkpoints.push_back(std::move(checkpoint));
  }
  reader.end();
 }
};
struct PreparedSceneRestore {
 explicit PreparedSceneRestore(const SceneImages&live):images(live.makeRestoreTarget()){}
 std::unique_ptr<SceneImages> images;
 std::unique_ptr<SceneTransition> transition;
 std::optional<PreparedHistoryCheckpoints> history;
 void read(qlie::Assets&assets,const qlie::Bytes&state){
  images->restore(state);
  images->prepareRestoredPortraitGradients();
  transition=std::make_unique<SceneTransition>(assets);
 }
 void read(qlie::Assets&assets,const std::string&slot){
  read(assets,qlie::file(slot+".images"));
  if(std::filesystem::exists(slot+".transition"))transition->restore(qlie::file(slot+".transition"));
  if(game_profile::story.historyJumpLimit){
   try{
    if(std::filesystem::exists(slot+".checkpoints"))history.emplace(qlie::file(slot+".checkpoints"));
   }catch(const std::exception&e){QLIE_LOGF("LOAD optional history checkpoints: %s\n",e.what());}
  }
 }
};
}
struct Frontend {
#ifdef __SWITCH__
 whj2::JoyCon joycon;
#endif
 qlie::Assets assets;
 whj2::UiLocale ui{assets};
 whj2::Story story;
 whj2::Progress progress{game_profile::extrasFlag};
 std::unique_ptr<whj2::ProgressWriter> progressWriter;
 std::unique_ptr<whj2::Audio>audio;
 std::unique_ptr<whj2::SceneImages>images;
 std::unique_ptr<whj2::AssetReadAhead>readAhead,soundReadAhead;
 std::unique_ptr<whj2::SceneTransition>transition;
 whj2::SentenceWait sentenceWait;
 whj2::AutoPlay autoPlay;
 std::unique_ptr<whj2::MessageText>messageText;
 whj2::MessageText::Surface messageSurface;
 std::unique_ptr<whj2::SceneImages>messageCursor;
 std::string messageCursorMotion="None";
 size_t lastMessageVisible=SIZE_MAX;
 std::string pendingBody,pendingSpeaker,pendingVoice;
 bool pendingHistoryStack=false;
 std::optional<whj2::Event> pendingScene;
 std::vector<std::string> pendingSceneAssets;
 qlie::Bytes outgoingScene;
 bool checkedOutgoingScene=false;

 std::chrono::steady_clock::time_point pendingSceneSince;
 bool lastFrameHadOverlay=false;
 bool fieldRequested=false,fieldPending=false;
 unsigned fieldParity=0;
 qlie::Bytes fieldHistory;
 std::unique_ptr<whj2::Movie>movie;
 std::unique_ptr<whj2::VoiceResolver>voices;
 std::unique_ptr<whj2::UpperMenu>upperMenu;
 std::unique_ptr<whj2::SceneImages>sceneSkipOverlay;
 unsigned sceneSkipSteps=0;
 bool sceneSkipFirstFrame=false;
 std::unique_ptr<whj2::TitleScreen>title;
 std::optional<bool> preparedTitleAlternate;
 std::unique_ptr<whj2::ChoiceScreen>choiceScreen;
 std::unique_ptr<whj2::MusicScreen>musicScreen;
 std::unique_ptr<whj2::CgScreen>cgScreen;
 bool cgOpening=false;
 std::unique_ptr<whj2::ReplayScreen>replayScreen;bool replayActive=false;unsigned replayPage=0;
 enum class ExtrasDestination {Title,CG,Music,Replay,Playback};
 static constexpr double PanelZoomMs=whj2::GameMenu::PanelZoomMs;
 struct ExtrasExit {ExtrasDestination destination;unsigned entry;bool alternateBackdrop=false;};
 std::optional<ExtrasExit> extrasExit;
 double extrasElapsed=PanelZoomMs;
#ifdef QLIE_PROFILE_WHJ5
 whj2::Settings settings{assets,true};
#else
 whj2::Settings settings{assets,false,gameSettingsLayout.classic};
#endif
 whj2::GameMenu gameMenu{assets,settings};
 enum class MenuFadeKind {Page,Load,Title,Quit};
 struct MenuFade {
  MenuFadeKind kind;
  whj2::Image previous;
  double elapsed=0;
  double duration()const{return kind==MenuFadeKind::Page?200:500;}
 };
 std::optional<MenuFade> menuFade;
 std::vector<whj2::HistoryEntry>history;
 using PlaybackState=whj2::PlaybackState;
 using HistoryCheckpoint=whj2::HistoryCheckpoint;
 using HistoryCheckpoints=whj2::HistoryCheckpoints;
 HistoryCheckpoints historyCheckpoints;
 std::shared_ptr<const HistoryCheckpoint> pendingHistoryLoad;
 size_t requestedHistoryRow=0;
 bool historyVoicePlayed=false;
 int historyViewOffset=-1;
 std::unique_ptr<whj2::MessageText> historyText;
 struct HistoryRow {
  size_t entry=SIZE_MAX;
  uint64_t lastUse=0;
  int firstLine=-1,maxOffset=0;
  bool hasVoice=false;
  whj2::Image body;
 };
 // Four visible rows and four recently visited rows retain their texture versions.
 std::array<HistoryRow,8> historyRows;
 uint64_t historyRowUse=0;
 bool normalSkip=false,skipReadOnly=true,leftTrigger=false,rightTrigger=false,currentPageWasRead=false;std::string currentPageKey;
 unsigned selectedSlot=1,pendingLoadSlot=0;unsigned skipStart=0;unsigned autoPhase=0,autoPhaseFrame=0,titlePhase=0;
 bool manualSlotsScanned=false;unsigned latestManualSlot=1;int saveThumbnailPriority=999;
 bool fingerActive=false,fingerUiDrag=false;SDL_FingerID fingerId=0;float fingerX=0,fingerY=0;qlie::Bytes gestureStory;
 bool startTransitionTest=false,skipTitleExitForTest=false,titleStarting=false,startMidCaptured=false,startEndCaptured=false;double titleStartElapsed=0,titleAudioElapsed=0;
 bool movieOnly=false,movieCaptured=false,touchOnlyTest=false,settingsTest=false,autoTest=false,fpsTest=false,titleTest=false,extraTest=false,musicTest=false,openingTest=false,viewTest=false,replayTest=false,cgTest=false,cgLoopTest=false,replayReturnTest=false,readSkipTest=false;
 unsigned opening=0;double openingElapsed=0;bool cautionLeaving=false,openingLogoCaptured=false;uint64_t openingTitleStarted=0;unsigned openingTitleCaptures=0;float titleAxis=0,titleAxisY=0,titlePointer=640;
 uint64_t mutedNonzero=0;
 qlie::Bytes touchSavedStory;std::string touchSavedText;
 std::map<std::string,std::map<std::string,std::string>>audioTemplates;
 static constexpr unsigned defaultMusicFadeMs=1000;
 std::array<float,5>volumes{{game_profile::storyMusicGain,1.f,1.f,1.f,1.f}};
 std::array<bool,5>loops{{true,false,false,false,false}};
 std::array<unsigned,5>audioTimes{{defaultMusicFadeMs,0,0,0,0}};
 std::array<unsigned,5>audioTypes{{0,2,2,2,1}};
 bool bgvMode=false,bgvDucked=false;
 std::string voiceActor;
 float bgvUserGain=1,voiceUserGain=1;
 void updateVoiceGains(){
  if(!settings.voiceActorCount())return;
  const auto found=story.state.vars.find("__bgv_actor");
  const float bgvGain=bgvMode&&found!=story.state.vars.end()?settings.characterVoiceGain(found->second):1;
  const float speechGain=settings.characterVoiceGain(voiceActor);
  if(bgvGain!=bgvUserGain){audio->setChannelUserGain(whj2::Audio::StoryBgvChannel,bgvGain);bgvUserGain=bgvGain;}
  if(speechGain!=voiceUserGain){audio->setChannelUserGain(3,speechGain);voiceUserGain=speechGain;}
 }
 void clearBackgroundVoice(){
  bgvMode=bgvDucked=false;
  story.state.vars.erase("__bgv_actor");
  updateVoiceGains();
 }
 static std::string dialogueVoiceActor(const std::string&speaker){
  std::string_view name=speaker;
  if(name.rfind("【",0)==0){name.remove_prefix(3);name=name.substr(0,name.find("】"));}
  return std::string(name.substr(0,name.find("＠")));
 }
 static int audioTrack(const std::string&command){
  constexpr const char* names[]={"music01","se01","se02","se03","sebgv"};
  for(unsigned i=0;i<game_profile::storyAudioTracks;++i)if(command==names[i])return int(i);
  return -1;
 }
 void updateBackgroundVoice(){
  if constexpr (game_profile::storyAudioTracks>3){
   const bool duck=bgvMode&&audio->playing(3);
   if(duck==bgvDucked)return;
   bgvDucked=duck;
   audio->volume(whj2::Audio::StoryBgvChannel,duck?0:volumes[4],duck?250:1000);
  }
 }
 KFont*font=nullptr;
 std::unique_ptr<KFont,decltype(&kfont_close)> localizedFont{nullptr,kfont_close};
 KFont* storyFont() const {return localizedFont?localizedFont.get():font;}
 SDL_Window*window=nullptr;
 SDL_Renderer*renderer=nullptr;
 SDL_Texture*texture=nullptr;
#ifdef WHJ2_WITH_GPU
 std::unique_ptr<whj2::GpuRaster> gpu;
#endif
 qlie::Bytes pixels;
 whj2::RenderTarget*frameTarget=nullptr;
 enum class GlyphFace { Interface, Dialogue };
 struct CachedGlyph {KFontGlyphBounds bounds{};whj2::Image image;};
 std::map<std::tuple<bool,uint32_t,unsigned,unsigned,uint32_t>,CachedGlyph>uiGlyphs;
 struct SlotPreview {bool exists=false,damaged=false;whj2::Image thumbnail;std::vector<uint32_t>excerpt;};
 std::map<unsigned,SlotPreview>slotPreviews;
 std::string text,name,status="",saveDir;
 bool menu=true,done=false,hidden=false,fpsVisible=false;
 int focus=0;
 unsigned advances=0,panelDraws=0;
 whj2::FramePacer framePacer;
 whj2::FpsOverlay fpsDisplay;
 std::map<SDL_JoystickID,SDL_GameController*>controllers;
 Frontend(const std::string&game,const std::string&saves):assets(game,game_profile::archives),story(assets,game_profile::story),pixels(1280*720*4),saveDir(saves){unsigned subsystems=SDL_INIT_AUDIO|SDL_INIT_EVENTS;
 bool nativeRequested=false;
#ifdef WHJ2_WITH_GPU
 const std::string gpuMode=selectedRenderer();
#if defined(__SWITCH__) && !defined(WHJ2_WITH_VULKAN)
 if(gpuMode=="vulkan")throw std::runtime_error("This build has deko3d and CPU renderers");
#endif
#ifdef __SWITCH__
 nativeRequested=gpuMode!="cpu";
#endif
#endif
 if(!nativeRequested)subsystems|=SDL_INIT_VIDEO;
#ifndef __SWITCH__
 subsystems|=SDL_INIT_GAMECONTROLLER;
#endif
 if constexpr (!qlie::diagnostics){
#ifndef __SWITCH__
  // The Mac SDL2 compatibility library also forwards SDL3's own logs.
  SDL_LogSetOutputFunction([](void*,int,SDL_LogPriority,const char*){},nullptr);
#endif
  SDL_LogSetAllPriority(SDL_NUM_LOG_PRIORITIES);
 }
 if(SDL_Init(subsystems))throw std::runtime_error(SDL_GetError());
 if(!nativeRequested){window=SDL_CreateWindow(ui.text(game_profile::gameName),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);if(!window)throw std::runtime_error(SDL_GetError());}
#ifdef WHJ2_WITH_DEKO3D
 if(nativeRequested&&gpuMode!="vulkan"){
  if(gpuMode!="deko3d")QLIE_LOGF("Unknown renderer '%s', selecting deko3d\n",gpuMode.c_str());
  gpu=std::make_unique<whj2::Deko3dRaster>();
 }
#endif
#ifdef WHJ2_WITH_VULKAN
#ifdef __SWITCH__
 if(nativeRequested&&!gpu)gpu=std::make_unique<whj2::VulkanRaster>();
#else
 if(gpuMode=="vulkan")gpu=std::make_unique<whj2::VulkanRaster>();
#endif
#endif

 bool nativeGpu=false;
#ifdef WHJ2_WITH_GPU
 nativeGpu=gpu&&gpu->nativePresentation();
#endif
 if(!nativeGpu){
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
 if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!renderer)throw std::runtime_error(SDL_GetError());
 SDL_RenderSetLogicalSize(renderer,1280,720);
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,1280,720);
 }
 font=kfont_open(nullptr,ui.simplified());
 if(!font||(!nativeGpu&&!texture))throw std::runtime_error("Font/texture initialization failed");
 kfont_set_antialias(font,4);
 kfont_set_style(font,2,0x302020);
 if(ui.simplified()){
  const auto data=assets.read("localization/zh-cn/arshanghaisonggbpro_lt.otf");
  localizedFont.reset(kfont_open_memory(data.data(),data.size(),1));
  if(!localizedFont)throw std::runtime_error("Chinese story font initialization failed");
  kfont_set_antialias(localizedFont.get(),4);
  kfont_set_style(localizedFont.get(),2,0x302020);
 }
 std::filesystem::create_directories(saveDir);
 {
  std::istringstream values(assets.text("avg/setting.txt"));std::string line;
  while(std::getline(values,line)){auto pos=line.find('=');if(pos!=line.npos&&whj2::trim(line.substr(0,pos))=="SaveThumPriority")saveThumbnailPriority=std::stoi(whj2::trim(line.substr(pos+1)));}
 }
 audio=std::make_unique<whj2::Audio>();
 images=std::make_unique<whj2::SceneImages>(assets);
 // Keep the opening cutscene and the preceding images resident together.
 readAhead=std::make_unique<whj2::AssetReadAhead>(1,"asset-read-ahead",game_profile::sceneReadAheadBytes);
 soundReadAhead=std::make_unique<whj2::AssetReadAhead>(2,"audio-decode",48u*1024*1024,whj2::AssetReadAhead::Priority::Background);
 images->setReadAhead(readAhead.get());audio->setReadAhead(soundReadAhead.get());
 messageText=std::make_unique<whj2::MessageText>(assets,true);
 messageCursor=std::make_unique<whj2::SceneImages>(assets);
 messageCursor->command("^ui_cursor,file:avg/window/Click.b,x:1070,y:670,pri:1550,motion:None");
 historyText=std::make_unique<whj2::MessageText>(*messageText);
 gameMenu.prepareHistory(game_profile::story.historyJumpLimit!=0);
 gameMenu.preparePanels();
 whj2::MessageText preview(assets);
 if(gameSettingsLayout.classic){
  preview.start(ui.simplified()?"[spd,0]文本示例":"[spd,0]テキスト　サンプル");
  gameMenu.setWindowPreview(preview.render(storyFont(),{100,50,10,10,0xffffff}));
 }else{
  preview.start(ui.text("[spd,0]「サンプルテキスト」"));
  gameMenu.setWindowPreview(preview.render(font,{400,50,20,20,0xffffff}));
 }
 transition=std::make_unique<whj2::SceneTransition>(assets);
 transition->setReadAhead(readAhead.get());
 movie=std::make_unique<whj2::Movie>(assets,*audio);
#ifdef WHJ2_WITH_GPU
 if(gpu){
  const bool nativeVideo=gpu->supportsVideoSurfaces();
  movie->setNativeSurfaces(nativeVideo);
  images->setNativeVideoSurfaces(nativeVideo);
 }
#endif
 voices=std::make_unique<whj2::VoiceResolver>(assets);
#if defined(__SWITCH__) && defined(QLIE_PROFILE_WHJ5)
 upperMenu=std::make_unique<whj2::UpperMenu>(assets,false);
#else
 upperMenu=std::make_unique<whj2::UpperMenu>(assets);
#endif
 title=std::make_unique<whj2::TitleScreen>(assets,game_profile::title);
 choiceScreen=std::make_unique<whj2::ChoiceScreen>(assets);
 musicScreen=std::make_unique<whj2::MusicScreen>(assets,*audio,settings,font,game_profile::musicTrackCount);
 replayScreen=std::make_unique<whj2::ReplayScreen>(assets,progress);
 cgScreen=std::make_unique<whj2::CgScreen>(assets,progress,font,game_profile::extendedCgRoots);
 cgScreen->setReadAhead(readAhead.get());
#ifdef WHJ2_WITH_GPU
 if(gpu){
  cgScreen->setNativeVideoSurfaces(gpu->supportsVideoSurfaces());
  if(gpu->nativePresentation())cgScreen->setViewCapture([this]{
   qlie::Bytes frame(1280*720*4);
   gpu->begin(0);
   cgScreen->draw(*gpu);
   gpu->end(frame);
   return frame;
  });
 }
#endif
 if(std::filesystem::exists(saveDir+"/system.progress"))progress.restore(qlie::file(saveDir+"/system.progress"));
 if(std::filesystem::exists(saveDir+"/settings.cfg"))settings.restore(qlie::file(saveDir+"/settings.cfg"));
 applySettings(false);
 prepareExtras();
 if(!game_profile::story.legacyAvg)for(auto type:{"music","sound"}){std::istringstream in(assets.text(std::string("avg/imoscripter_template_")+type+".txt"));
 std::string l;
 while(std::getline(in,l)){auto p=l.find(61);
 if(p!=l.npos)audioTemplates[type][whj2::trim(l.substr(0,p))]=whj2::trim(l.substr(p+1));
 }}
 if(readAhead){
  movie->setReadAhead(readAhead.get());
  movie->warmFile(makerLogoFile());
  if(game_profile::secondMakerLogo[0])movie->warmFile(game_profile::secondMakerLogo);
  preparedTitleAlternate=title->prepareOpening(readAhead.get(),progress.extras());
  if(game_profile::title.albumAlwaysAvailable)cgScreen->warmOpening();
  constexpr auto intro=game_profile::title.audio;
  if(intro.opening)audio->warmFile(assets,intro.opening);
  if(intro.middle)audio->warmFile(assets,intro.middle);
  if(game_profile::titleMusic[0])audio->warmFile(assets,game_profile::titleMusic);
 }
 opening=1;images->command("^ui_caution,file:script/caution/caution.b,x:0,y:0");
 QLIE_LOG_LINE(<<"OPENING caution\n");
 }
 ~Frontend(){
 progressWriter.reset();
 movie->setReadAhead(nullptr);
 audio->setReadAhead(nullptr);
 readAhead.reset();
 soundReadAhead.reset();
#ifdef WHJ2_WITH_GPU
 gpu.reset();
#endif
cgScreen.reset();replayScreen.reset();musicScreen.reset();movie.reset();
 for(auto&c:controllers)SDL_GameControllerClose(c.second);
 audio.reset();
 kfont_close(font);
 if(texture)SDL_DestroyTexture(texture);
 if(renderer)SDL_DestroyRenderer(renderer);
 if(window)SDL_DestroyWindow(window);
 SDL_Quit();
 }
 void persistProgress(){
  auto path=saveDir+"/system.progress";
  // Installed-title SaveData uses a shared commit across progress and slots.
  if(path.rfind("save:/",0)==0){qlie::write_file(path,progress.save());return;}
  if(!progressWriter)progressWriter=std::make_unique<whj2::ProgressWriter>(std::move(path));
  progressWriter->schedule(progress.save());
 }
 void flushProgress(){if(progressWriter)progressWriter->flush();}
 void syncProgress(){if(progress.capture(story))persistProgress();}
 void leaveCaution(){
  if(game_profile::cautionFade>0&&!cautionLeaving){cautionLeaving=true;openingElapsed=0;}
  else makerLogo();
 }
 void advanceCaution(double ms){
  openingElapsed+=ms;
  if(cautionLeaving){
   if(openingElapsed>=game_profile::cautionExitWait)makerLogo();
  }else{
   images->update(ms);
   if(openingElapsed>=game_profile::cautionWait)leaveCaution();
  }
 }
 unsigned cautionOpacity()const{
  if(!cautionLeaving)return 255;
  return unsigned(255*(1-std::clamp(openingElapsed/game_profile::cautionFade,0.,1.)));
 }
 const char* makerLogoFile()const{
  const auto* legacy=game_profile::story.legacyAvg;
  return legacy?legacy->makerLogo:"script/makerlogo/omegastar.ogv";
 }
 void makerLogo(){cautionLeaving=false;opening=2;images->clear();movie->open(makerLogoFile());QLIE_LOG_LINE(<<"OPENING maker-logo\n");}
 void nextMakerLogo(){
  if(opening==2&&game_profile::secondMakerLogo[0]){
   opening=3;
   movie->open(game_profile::secondMakerLogo);
  }else showTitle();
 }
 void warmNextScene(bool openingHint=false,bool endingWait=false){
  if(!readAhead)return;
  enum class Kind { Image, Audio, Movie, Rule };
  struct Hint {
   std::string path;
   Kind kind;
   std::optional<whj2::SceneImages::BackgroundBlur> blur;
   std::optional<whj2::SceneImages::BitmapFilter> gradient;
  };
  std::vector<Hint> hints;
  whj2::SceneImages::CharacterPrefixes futurePortraits;
  std::vector<std::string> sceneKeys,soundKeys,predictedFaces;
  constexpr size_t hintsPerWorker=6;
  auto add=[&](const std::string&path,Kind kind,std::optional<whj2::SceneImages::BackgroundBlur> blur={},
               std::optional<whj2::SceneImages::BitmapFilter> gradient={}){
   auto&keys=kind==Kind::Audio?soundKeys:sceneKeys;
   auto key=gradient?gradient->key():kind==Kind::Rule?whj2::SceneTransition::ruleKey(path):qlie::canonical(path);
   if(keys.size()<hintsPerWorker&&std::find(keys.begin(),keys.end(),key)==keys.end()){
    keys.push_back(key);hints.push_back({path,kind,std::move(blur),std::move(gradient)});
   }
  };
  std::vector<std::string> upcoming;
  std::string imminentMovie;
  try{upcoming=story.upcomingAssetCommands(openingHint);}
  catch(const std::runtime_error&e){QLIE_LOGF("LOAD read-ahead script: %s\n",e.what());return;}
  for(auto&line:upcoming){
   if(sceneKeys.size()==hintsPerWorker&&soundKeys.size()==hintsPerWorker)break;
   if(line.rfind("％",0)==0){
    if(soundKeys.size()<hintsPerWorker&&(!normalSkip||voices->forcePlay)){
     // Speculative hints stay off the filesystem. Playback still resolves
     // loose overrides before archives, and their decode goes to the worker.
     auto path=voices->packedHint(line);
     if(!path.empty())add(path,Kind::Audio);
    }
    continue;
   }
   auto comma=line.find(',');auto cmd=line.substr(0,comma);
   if(cmd=="^movie"){
    auto path=movie->assetPath(line.substr(comma+1));
    if(assets.exists(path)){
     if(endingWait&&imminentMovie.empty())imminentMovie=path;
     else add(path,Kind::Movie);
    }
   }else if(cmd=="^sentence"){
    if(normalSkip)continue;
    try{for(const auto&path:transition->ruleAssets(line))add(path,Kind::Rule);}
    catch(const std::exception&){/* The live command reports invalid rules. */}
   }else if(!cmd.empty()&&audioTrack(cmd.substr(1))>=0){
    const auto fields=whj2::split(line.substr(comma+1),',');
    for(const auto&part:fields)if(part.rfind("file:",0)==0){
     auto name=part.substr(5);if(name.empty()||name=="none"||name.find('$')!=name.npos)continue;
     auto path=game_profile::story.legacyAvg&&name.rfind("voice/",0)==0?name:
      std::string(cmd=="^music01"?"music/":"se/")+name;if(!assets.inPack(path))path+=".ogg";
     if(assets.inPack(path)){
      if(line.find(",sndfilter:bathroom2")!=line.npos)path="@bathroom2:"+path;
      for(const auto&field:fields){
       if(field=="pan:0")path="@pan-left:"+path;
       if(field=="pan:100")path="@pan-right:"+path;
      }
      add(path,Kind::Audio);
     }
    }
   }else{
    if(auto blur=images->backgroundBlur(line)){
     auto key=blur->key();
     add(key,Kind::Image,std::move(blur));
    }
    std::optional<whj2::SceneImages::BitmapFilter> gradient;
    auto paths=images->warmPaths(line,&futurePortraits,&gradient,&predictedFaces);
    if(gradient&&!gradient->result)add({},Kind::Image,{},std::move(gradient));
    for(const auto&path:paths)add(path,Kind::Image);
   }
  }
  // A predicted face must not displace an image explicitly used by the next scene.
  for(const auto&path:predictedFaces)add(path,Kind::Image);
  movie->primeFile(imminentMovie);
  readAhead->retain(sceneKeys);
  soundReadAhead->retain(soundKeys);
  for(auto&h:hints){
   if(h.gradient)images->warmBitmapFilter(*h.gradient);
   else if(h.blur)images->warmBackgroundBlur(*h.blur);
   else if(h.kind==Kind::Movie)movie->warmFile(h.path);
   else if(h.kind==Kind::Audio)audio->warmFile(assets,h.path);
   else if(h.kind==Kind::Rule)transition->warmRule(h.path);
   else images->warmFile(h.path);
  }
 }
 void prepareExtras(){
  if(!progress.extras())return;
  musicScreen->prepare();
  if(!game_profile::title.albumAlwaysAvailable)cgScreen->prepare();
  replayScreen->prepare();
 }
 void playTitleTheme(){if(game_profile::titleMusic[0])audio->playFile(assets,0,game_profile::titleMusic,true,255.f/256,0,0);}
 void updateTitleAudio(double elapsed,bool skipped=false){
  constexpr auto intro=game_profile::title.audio;
  if(!intro.opening||titleAudioElapsed>=intro.musicAt)return;
  const auto previous=titleAudioElapsed;
  titleAudioElapsed=skipped?intro.musicAt:previous+elapsed;
  if(skipped){
   audio->stop(8);audio->stop(9);playTitleTheme();
  }else{
   if(previous<intro.middleAt&&titleAudioElapsed>=intro.middleAt){
    audio->volume(8,0,intro.openingFade,true);
    audio->playFile(assets,9,intro.middle,false,255.f/256,intro.middleFadeIn,0);
   }
   if(titleAudioElapsed>=intro.musicAt){
    audio->volume(9,0,intro.middleFadeOut,true);playTitleTheme();
   }
  }
 }
 void showTitle(){cancelPendingScene();story.state.vars.erase("__scene_skip_active");sceneSkipOverlay.reset();historyCheckpoints.clear();menuFade.reset();titleStarting=false;titleStartElapsed=0;transition->reset();normalSkip=false;currentPageKey.clear();extrasExit.reset();cgScreen->close();replayActive=false;replayScreen->close();if(musicScreen->visible())musicScreen->close();opening=0;images->clear();syncProgress();prepareExtras();menu=true;hidden=false;focus=0;upperMenu->close();gameMenu.close();
  const bool alternate=preparedTitleAlternate?*preparedTitleAlternate:title->rollAlternate();
  preparedTitleAlternate.reset();
  title->reset(alternate,progress.extras());audio->stopAll();clearBackgroundVoice();
  titleAudioElapsed=0;
  constexpr auto intro=game_profile::title.audio;
  if(intro.opening){
   audio->playFile(assets,8,intro.opening,false,255.f/256,0,0);
   audio->warmFile(assets,intro.middle);
   if(game_profile::titleMusic[0])audio->warmFile(assets,game_profile::titleMusic);
  }else playTitleTheme();
  warmNextScene(true);
 }
 void invalidateHistoryRows(){historyRows={};historyViewOffset=-1;}
 void replaceHistory(std::vector<whj2::HistoryEntry> entries={}){
  history=std::move(entries);
  historyCheckpoints.clear();
  invalidateHistoryRows();
 }
 size_t historyIndex(int row)const{
  const auto offset=size_t(gameMenu.historyOffset+row);
  return offset<history.size()?history.size()-1-offset:SIZE_MAX;
 }
 std::shared_ptr<const HistoryCheckpoint> historyCheckpoint(size_t row)const{
  auto found=std::find_if(historyCheckpoints.begin(),historyCheckpoints.end(),
                         [row](const auto& checkpoint){return checkpoint->row==row;});
  return found==historyCheckpoints.end()?nullptr:*found;
 }
 void recordHistoryCheckpoint(){
  if(name.empty()||history.empty()||!story.historyJumpEnabled())return;
  auto checkpoint=std::make_shared<HistoryCheckpoint>();
  checkpoint->row=history.size()-1;
  auto&state=checkpoint->state;
  checkpoint->story=story.save();
  state.text=text;state.name=name;state.body=pendingBody;state.speaker=pendingSpeaker;
  state.voice=pendingVoice;state.page=currentPageKey;
  state.volumes=volumes;state.loops=loops;state.times=audioTimes;state.types=audioTypes;
  state.bgvMode=bgvMode;state.wait=sentenceWait;state.autoPlay=autoPlay;
  // Scene and mixer saves contain resource names and clocks. The restored
  // page enters via the load fade, so no transition framebuffer is retained.
  checkpoint->scene=images->save();
  checkpoint->audio=audio->save();
  if(!historyCheckpoints.empty()&&historyCheckpoints.back()->row==checkpoint->row)historyCheckpoints.pop_back();
  historyCheckpoints.push_back(std::move(checkpoint));
  if(historyCheckpoints.size()>game_profile::story.historyJumpLimit)historyCheckpoints.pop_front();
 }
 qlie::Bytes saveHistoryCheckpoints(uint32_t storyHash,uint32_t historyHash)const{
  whj2::StateWriter writer;
  writer.str("QLIE-history-checkpoints-1");
  writer.n(storyHash);writer.n(historyHash);
  writer.n(historyCheckpoints.size());
  for(const auto&checkpoint:historyCheckpoints){
   const auto&state=checkpoint->state;
   writer.n(checkpoint->row);
   writer.blob(checkpoint->story);
   for(const auto*value:{&state.text,&state.name,&state.body,&state.speaker,&state.voice,&state.page})
    writer.str(*value);
   for(unsigned i=0;i<game_profile::storyAudioTracks;++i){
    writer.f(state.volumes[i]);
    writer.n(state.loops[i]);writer.n(state.times[i]);writer.n(state.types[i]);
   }
   writer.n(state.bgvMode);
   writer.blob(state.wait.save());
   writer.blob(state.autoPlay.save());
   writer.blob(checkpoint->scene);
   writer.blob(checkpoint->audio);
  }
  return std::move(writer.bytes);
 }
 void applySettings(bool persist=true){
 audio->setUserGains(settings.gains());updateVoiceGains();images->setWindowAlpha(unsigned(settings.get("WindowAlpha")));messageText->setSpeed(settings.get("TextSpeed"));if(!text.empty())refreshMessage();if(persist)qlie::write_file(saveDir+"/settings.cfg",settings.save());gameMenu.settingsDirty=false;}
 void drawDialogueSpeaker(){
  if(images->messageVisible()){
   const auto style=images->speakerStyle();
   line(whj2::messageDisplayName(name),style.x,style.y,style.size,style.color,nullptr,GlyphFace::Dialogue);
  }
 }
 void drawGlyph(uint32_t cp,int x,int y,unsigned width,unsigned height,uint32_t color,
                 whj2::RenderQueue*queue=nullptr,GlyphFace face=GlyphFace::Interface){
 auto* selected=face==GlyphFace::Dialogue?storyFont():font;
 if(!frameTarget&&!queue){kfont_draw(selected,newImage(),cp,x,y,width,height,color);return;}
 auto key=std::make_tuple(selected!=font,cp,width,height,color);
 auto at=uiGlyphs.find(key);
 if(at==uiGlyphs.end()){
  CachedGlyph glyph;
  if(kfont_glyph_bounds(selected,cp,width,height,&glyph.bounds))return;
  auto& b=glyph.bounds;
  glyph.image={b.width,b.height,qlie::Bytes(size_t(b.width)*b.height*4)};
  if(b.width&&b.height){
   KImage dst{0,0,b.width,b.height,size_t(b.width)*4,glyph.image.pixels.data()};
   kfont_draw(selected,&dst,cp,-b.left,-b.top,width,height,color);
  }
  glyph.image.version.publish();
  at=uiGlyphs.emplace(key,std::move(glyph)).first;
 }
 auto&glyph=at->second;x+=glyph.bounds.left;y+=glyph.bounds.top;
 if(queue)queue->image(glyph.image,whj2::Affine2D{1,0,0,1,float(x),float(y)},0,255,0,0xffffff);
 else frameTarget->font(glyph.image,x,y);
 }
 void line(const std::string&s,int x,int y,int size=26,uint32_t color=0xffffff,
           whj2::RenderQueue*queue=nullptr,GlyphFace face=GlyphFace::Interface){
 int start=x;
 for(auto cp:utf8(s)){int width=cp<128?size/2:size;
 if(cp==10||x+width>1230){x=start;
 y+=size+10;
 if(cp==10)continue;
 }if(y+size>710)break;
 drawGlyph(cp,x,y,width,size,color,queue,face);
 x+=width;
 }}
 std::string expandAudio(std::string s,const std::string&type){std::vector<std::pair<std::string,std::string>>values(audioTemplates[type].begin(),audioTemplates[type].end());
 std::sort(values.begin(),values.end(),[](const auto&a,const auto&b){return a.first.size()>b.first.size();});
 for(unsigned pass=0;pass<12;pass++){bool changed=false;
 for(auto&v:values){size_t p=0;
 while((p=s.find(v.first,p))!=s.npos){s.replace(p,v.first.size(),v.second);
 p+=v.second.size();
 changed=true;
 }}if(!changed)return s;
 }throw std::runtime_error("Audio template recursion");
 }
 bool audioCommand(const std::string&line){auto p=line.find(44);
 std::string cmd=line.substr(1,p-1);
 const auto ch=audioTrack(cmd);
 if(ch<0)return false;
 constexpr unsigned channels[]={0,1,2,whj2::Audio::StoryEffect3Channel,whj2::Audio::StoryBgvChannel};
 const auto output=channels[ch];
 std::string type=ch==0?"music":"sound";
 auto tokens=whj2::split(expandAudio(p==line.npos?"":line.substr(p+1),type),44);
 std::map<std::string,std::string>attrs;
 for(auto&t:tokens){if(t.empty())continue;
 auto c=t.find(58);
 if(c==t.npos)throw std::runtime_error("Unknown audio template "+t);
 auto key=t.substr(0,c),value=t.substr(c+1);
 if(ch==0&&key=="clear"&&value=="def"){
  // Imo_Music_Excute combines the defaults at the clear token.
  attrs["file"]="none";
  attrs["loop"]="true";
  attrs["vol"]="100";
  attrs["time"]=std::to_string(defaultMusicFadeMs);
  attrs["voltype"]="bgm";
 }else attrs[std::move(key)]=std::move(value);
 }
 for(auto&v:attrs){
  if(v.first=="file"||v.first=="loop"||v.first=="vol"||v.first=="voltype"||v.first=="time"||
     v.first=="clear"||v.first=="oncetime")continue;
  if(game_profile::storyAudioTracks>3){
   if(v.first=="pan"&&(v.second=="50"||
      (attrs.count("file")&&(v.second=="0"||v.second=="100"))))continue;
   if(v.first=="sndfilter"&&(v.second=="none"||v.second=="bathroom2"))continue;
   if(v.first=="delay")continue;
   if(v.first=="trans"&&v.second=="continue"&&!attrs.count("file"))continue;
   if(v.first=="id"||v.first=="mode")continue;
  }
  throw std::runtime_error("Unsupported audio property "+v.first);
 }
 // Imo_Sound_Refresh_Main resets per-file playback properties before overrides.
 if(ch!=0&&(attrs.count("file")||(attrs.count("clear")&&attrs["clear"]=="def"))){
  loops[ch]=false;volumes[ch]=1;audioTimes[ch]=0;
  if(attrs.count("clear"))audioTypes[ch]=cmd=="sebgv"?1:2;
 }
 if(attrs.count("clear")){audio->volume(output,0,0,true);if(ch==0)audio->volume(whj2::Audio::MusicPreviousChannel,0,0,true);
 if(ch==4)clearBackgroundVoice();
 return true;
 }if(attrs.count("loop"))loops[ch]=attrs["loop"]=="true"||attrs["loop"]=="infinity";
 if(attrs.count("vol"))volumes[ch]=std::stof(attrs["vol"])/100.f*(ch==0?game_profile::storyMusicGain:1.f);
 if(attrs.count("time"))audioTimes[ch]=unsigned(std::stoul(attrs["time"]));
 if(attrs.count("voltype")){auto type=attrs["voltype"];if(type=="bgm")audioTypes[ch]=0;else if(type=="voice")audioTypes[ch]=1;else if(type=="se")audioTypes[ch]=2;else QLIE_LOG_LINE(<<"PENDING audio volume type "<<type<<"\n");}
 if(ch==4){
  if(attrs.count("mode"))bgvMode=attrs["mode"]=="bgv";
  if(settings.voiceActorCount()&&attrs.count("id"))
   story.state.vars["__bgv_actor"]=attrs["id"];
  updateVoiceGains();
 }
 const unsigned ms=attrs.count("oncetime")?unsigned(std::stoul(attrs["oncetime"])):audioTimes[ch];
 const float gain=ch==4&&bgvMode&&audio->playing(3)?0:volumes[ch];
 if(attrs.count("file")){auto f=attrs["file"];
 if(f=="none"){if(ch==0)audio->stopMusic(ms,audioTypes[ch]);else audio->volume(output,0,ms,true);
 return true;
 }if(ch!=0&&normalSkip&&!loops[ch]){audio->stop(output);return true;}
 std::string path=game_profile::story.legacyAvg&&f.rfind("voice/",0)==0?f:(ch==0?"music/":"se/")+f;
 if(!assets.exists(path))path+=".ogg";
 if(attrs.count("sndfilter")&&attrs.at("sndfilter")=="bathroom2")path="@bathroom2:"+path;
 const auto pan=attrs.find("pan");
 if(pan!=attrs.end()&&pan->second!="50")path=(pan->second=="0"?"@pan-left:":"@pan-right:")+path;
 if(ch==0)audio->playMusicFile(assets,path,loops[ch],volumes[ch],ms,audioTypes[ch],attrs.count("time")||attrs.count("oncetime"));
 else audio->playFile(assets,output,path,loops[ch],gain,ms,audioTypes[ch],
                      attrs.count("delay")?unsigned(std::stoul(attrs["delay"])):0);
 }else audio->volume(output,gain,ms);
 return true;
 }
 void playSceneSounds(){
 for(auto&event:images->takeSoundEvents()){
  if(event.stop)audio->volume(event.channel,0,0,true);
  else audio->play(event.channel,std::move(event.bytes),event.loop,event.gain,0,event.source,event.category);
 }
 }
 void playVoice(const std::string&id,const std::string&speaker,bool historyReplay=false){
 if(!historyReplay&&!voices->forcePlay&&normalSkip){audio->stop(3);return;}
 if(id.empty())return;
 auto match=voices->resolve(id);
 if(!match){if constexpr (qlie::diagnostics){QLIE_LOG_LINE(<<"VOICE unresolved id="<<id);for(auto&candidate:voices->candidates(id))QLIE_LOG_LINE(<<" candidate="<<candidate);QLIE_LOG_LINE(<<"\n");}if(voices->fileErrors)throw std::runtime_error("Voice file not found: "+id);return;}
 if(settings.voiceActorCount()){
  voiceActor=dialogueVoiceActor(speaker);
  updateVoiceGains();
 }
 audio->playFile(assets,3,match->request,false,voices->gain,0,1);
 QLIE_LOGF("VOICE requested id=%s path=%s source=%s offset=%llu stored_bytes=%llu loose=%d\n",
              id.c_str(),match->request.c_str(),match->source.path.c_str(),
              static_cast<unsigned long long>(match->source.offset),static_cast<unsigned long long>(match->source.size),
              int(match->source.loose));
 }
 void refreshMessage(bool show=false){
  // A branch may clear the old message layer and defer loading the next
  // one. Rebuilding it here would consume the file its command awaits.
  if(pendingScene)return;
  const auto started=qlie::diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
  images->setMessageImage(messageText->renderIncremental(storyFont(),images->messageStyle(),messageSurface),show);
  lastMessageVisible=messageText->visible();
  if constexpr (qlie::diagnostics){
   const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
   if(elapsed>=1000.0/60)QLIE_LOGF("TEXT render_ms=%.3f glyphs=%zu page=%s\n",elapsed,lastMessageVisible,currentPageKey.c_str());
  }
 }
 void setMessageCursor(const char*motion){
  if(messageCursorMotion==motion)return;
  messageCursorMotion=motion;
  messageCursor->command("^ui_cursor,motion:"+messageCursorMotion);
 }
 void updateMessageCursor(double ms){
  if(menu||hidden||pendingScene||movie->playing()||story.state.awaiting||!images->messageVisible()||
     !sentenceWait.waitingForMessage()||!messageText->finished()){
   setMessageCursor("None");
   return;
  }
  const char*motion=normalSkip?"Skip":autoPlay.enabled?"Auto":"Click";
  if(messageCursorMotion!=motion)setMessageCursor(motion);
  else if(!gameMenu.visible()&&!upperMenu->visible())messageCursor->update(ms);
 }
 void recordCurrentRead(){if(!menu&&!hidden&&!gameMenu.visible()&&!movie->playing()&&images->messageVisible()&&!currentPageKey.empty()&&messageText->finished()&&progress.markRead(currentPageKey))persistProgress();}
 void toggleAuto(){if(menuFadeLocksInput()||sceneSkipActive())return;if(menu||pendingScene||movie->playing()||gameMenu.visible()||upperMenu->visible()||story.state.awaiting)return;autoPlay.enabled=!autoPlay.enabled;normalSkip=false;hidden=false;uiSound("Sys_OnClick");}
 bool waitingOnFade()const{return pendingScene&&pendingScene->value.rfind("^sentence,",0)==0;}
 void toggleSkip(){if(menuFadeLocksInput()||sceneSkipActive())return;if(normalSkip){normalSkip=false;return;}if(menu||(pendingScene&&!waitingOnFade())||movie->playing()||gameMenu.visible()||upperMenu->visible()||story.state.awaiting)return;autoPlay.enabled=false;skipReadOnly=settings.get("SkipMode")==1;normalSkip=!skipReadOnly||progress.wasRead(currentPageKey);if(normalSkip){autoPlay.enabled=false;hidden=false;}}

 bool deferSceneUntil(whj2::Event&event,std::vector<std::string> waiting){
  if(std::all_of(waiting.begin(),waiting.end(),[this](const auto&key){return readAhead->ready(key);}))return false;
  QLIE_LOGF("LOAD scene_defer %s:%u resources=%zu\n",
               event.file.c_str(),event.line,waiting.size());
  pendingScene=std::move(event);pendingSceneAssets=std::move(waiting);
  if constexpr (qlie::diagnostics) pendingSceneSince=std::chrono::steady_clock::now();
  return true;
 }
 bool deferSceneCommand(whj2::Event&event,const std::vector<std::string>&resources,
                        const std::optional<whj2::SceneImages::BackgroundBlur>&blur,
                        const std::optional<whj2::SceneImages::BitmapFilter>&gradient={}){
  std::vector<std::string> waiting;
  if(gradient&&!gradient->result){
   images->warmBitmapFilter(*gradient,true);
   waiting.push_back(gradient->key());
  }
  for(const auto&path:resources){
   images->warmFile(path,true);
   waiting.push_back(qlie::canonical(path));
  }
  if(blur){
   images->warmBackgroundBlur(*blur,true);
   waiting.push_back(blur->key());
  }
  return deferSceneUntil(event,std::move(waiting));
 }
 void resetOutgoingScene(){outgoingScene.clear();checkedOutgoingScene=false;}
 void cancelPendingScene(){
  resetOutgoingScene();
  for(const auto&key:pendingSceneAssets)readAhead->discard(key);
  pendingSceneAssets.clear();
  pendingScene.reset();
 }
 bool resumeSceneCommand(){
  if(!pendingScene||gameMenu.visible()||upperMenu->visible())return false;
  // An outgoing capture can be waiting for a frame rather than a file.
  if(pendingSceneAssets.empty()){next(true);return !pendingScene;}
  if(normalSkip&&waitingOnFade()){
   for(const auto&key:pendingSceneAssets)readAhead->discard(key);
  }else if(!std::all_of(pendingSceneAssets.begin(),pendingSceneAssets.end(),
                       [this](const auto&path){return readAhead->ready(path);}))return false;
  QLIE_LOGF("LOAD scene_ready %s:%u wait_ms=%.3f\n",pendingScene->file.c_str(),pendingScene->line,
               std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pendingSceneSince).count());
  next(true);return true;
 }
 std::optional<qlie::Bytes> captureScene(){
   bool masks=false,surfaces=false;
#ifdef WHJ2_WITH_GPU
   const bool native=gpu&&gpu->nativePresentation()&&!transition->needsCanvas();
   if(native){masks=gpu->supportsMasks();surfaces=gpu->supportsVideoSurfaces();}
#endif
   auto queue=images->captureQueue(masks,surfaces);
   if(queue.waitingForVideoFrame()){images->takeShownFiles();return {};}
   qlie::Bytes scene(1280*720*4);
#ifdef WHJ2_WITH_GPU
   if(native){
    constexpr uint32_t background=0xff141923u;
    if(transition->isActive()||!queue.captureAlignedBitmap(scene,1280,720,background)){
     gpu->begin(background);queue.draw(*gpu,1280,720);transition->blend(*gpu);gpu->end(scene);
    }
    return scene;
   }
#endif
   for(size_t i=0;i<scene.size();i+=4){scene[i]=35;scene[i+1]=25;scene[i+2]=20;scene[i+3]=255;}
   queue.draw(scene,1280,720);transition->blend(scene);
   return scene;
 }
 bool enterPlayerName(){
  if constexpr (qlie::diagnostics){
   if(const char* value=std::getenv("WHJ5_TEST_NAME"))return story.setPlayerName(value);
  }
  for(;;){
   std::string nameInput;
#ifdef __SWITCH__
   SwkbdConfig keyboard;
   if(R_FAILED(swkbdCreate(&keyboard,0)))throw std::runtime_error("Cannot open name input");
   swkbdConfigMakePresetDefault(&keyboard);
   swkbdConfigSetHeaderText(&keyboard,"请输入您的名字");
   swkbdConfigSetGuideText(&keyboard,"最多 10 个字");
   swkbdConfigSetStringLenMax(&keyboard,10);
   swkbdConfigSetStringLenMin(&keyboard,1);
   char input[64]{};
   const Result result=swkbdShow(&keyboard,input,sizeof(input));
   swkbdClose(&keyboard);
   if(R_FAILED(result))return false;
   nameInput=input;
#elif defined(WHJ2_MAC_GAME_PICKER)
   std::unique_ptr<char,decltype(&std::free)> input(whj2_mac_choose_player_name(),std::free);
   if(!input)return false;
   nameInput=input.get();
#else
   throw std::runtime_error("Name input requires the Switch or Mac frontend");
#endif
   if(story.setPlayerName(std::move(nameInput)))return true;
  }
 }
 void syncScenePan(){
  const auto pan=story.state.vars.find("__view_pan");
  if(pan==story.state.vars.end()){images->setViewPan("",0,0);return;}
  const auto value=whj2::split(pan->second,',');
  if(value.size()!=3)throw std::runtime_error("Malformed view pan state");
  images->setViewPan(value[0],std::stof(value[1]),std::stof(value[2]));
 }
 bool sceneSkipActive()const{
  return game_profile::story.sceneSkip&&story.state.vars.count("__scene_skip_active")!=0;
 }
 bool sceneSkipRunning()const{return sceneSkipActive()&&!story.state.awaiting;}
 bool sceneSkipAvailable()const{
  if(!game_profile::story.sceneSkip||menu||replayActive||opening||pendingScene||
     story.state.finished||story.state.awaiting||movie->playing()||sceneSkipActive())return false;
  const auto section=story.state.vars.find("__scene_checkpoint");
  return section!=story.state.vars.end()&&progress.visited(section->second);
 }
 void beginSceneSkip(){
  if(!sceneSkipAvailable())return;
  story.state.vars["__scene_skip_active"]="1";
  sceneSkipSteps=0;sceneSkipFirstFrame=true;
  normalSkip=false;autoPlay.enabled=false;hidden=false;
  upperMenu->close();transition->reset();sentenceWait.cancel();
  audio->stopAll();clearBackgroundVoice();
  readAhead->retain({});
  currentPageKey.clear();
  text.clear();name.clear();messageText->start("");
  clearSceneSkipStage();
 }
 void clearSceneSkipStage(){
  images->clear();
  images->setAlternateMessageWindow(story.state.vars.count("__avg2_active")!=0);
 }
 void finishSceneSkip(){
  story.state.vars.erase("__scene_skip_active");
  sceneSkipOverlay.reset();
  audio->stopAll();
  history.push_back({"","＊＊＊シーンスキップ＊＊＊",""});
  invalidateHistoryRows();
 }
 void advanceSceneSkip(){
  if(!sceneSkipRunning()||gameMenu.visible()||menuFadeLocksInput())return;
  if(sceneSkipFirstFrame){sceneSkipFirstFrame=false;return;}
  // Bound work between input and presentation. Suppressed events do not
  // decode assets or increment dialogue and voice history.
  for(unsigned i=0;i<256;i++){
   auto event=story.next();
   ++sceneSkipSteps;
   const auto&kind=event.type;
   if(kind=="choice"||kind=="name-entry"||kind=="create"||kind=="finish"||kind=="finished"){
    if(kind=="create"||kind=="finish"||kind=="finished")finishSceneSkip();
    else clearSceneSkipStage();
    story.state.pending.push_front(std::move(event));
    next();
    return;
   }
  }
 }
 template<typename Target>
 void drawSceneSkip(Target&target){
  if(!sceneSkipOverlay){
   sceneSkipOverlay=images->makeRestoreTarget();
   const auto*base=story.state.vars.count("__avg2_active")?"avg2/":"avg/";
   sceneSkipOverlay->command(std::string("^ui_skip,file:")+base+"sceneskip.b,x:0,y:0");
  }
  sceneSkipOverlay->draw(target);
  whj2::RenderQueue queue;
  line("skip..."+std::to_string(sceneSkipSteps&~63u),0,0,18,0x888888,&queue);
  queue.draw(target,1280,720);
 }
 template<typename Target>
 void drawUpperMenu(Target&target){
  upperMenu->draw(target);
  if(!upperMenu->sectionSkipVisible())return;
  whj2::RenderQueue queue;
  line(ui.simplified()?"跳过场景":"シーンスキップ",744,12,16,
       upperMenu->sectionSkipSelected()?0xffff88:0xffffff,&queue);
  queue.draw(target,1280,720);
 }
 void next(bool resume=false){if(menu||(story.state.finished&&story.state.pending.empty())||(!resume&&pendingScene))return;
  using LoadClock=std::chrono::steady_clock;
  struct PageTiming {
   whj2::AssetReadAhead* loader;
   whj2::AssetReadAhead::Stats start;
   LoadClock::time_point begin;
   double capture=0;
   ~PageTiming(){
    if(!qlie::diagnostics||!loader)return;
    double ms=std::chrono::duration<double,std::milli>(LoadClock::now()-begin).count();
    if(ms<1000.0/60)return;
    auto end=loader->stats();
    QLIE_LOGF("LOAD page_ms=%.3f capture_ms=%.3f ready=%u waited=%u urgent=%u cold=%u\n",
                 ms,capture,end.ready-start.ready,end.waited-start.waited,end.urgent-start.urgent,end.cold-start.cold);
   }
 } timing{qlie::diagnostics?readAhead.get():nullptr,readAhead->stats(),qlie::diagnostics?LoadClock::now():LoadClock::time_point{}};
 if(!resume){resetOutgoingScene();setMessageCursor("None");recordCurrentRead();audio->discardQueued(3);currentPageKey.clear();images->beginPage();pendingBody.clear();pendingSpeaker.clear();pendingVoice.clear();pendingHistoryStack=false;}
 auto&body=pendingBody;auto&speaker=pendingSpeaker;auto&voiceId=pendingVoice;
 for(unsigned guard=0;guard<10000;guard++){
 auto e=pendingScene?std::move(*pendingScene):story.next();
 pendingScene.reset();pendingSceneAssets.clear();
 syncProgress();
 if(e.type=="view-pan"){syncScenePan();continue;}
 if(e.type=="name-entry"){
  if(!enterPlayerName()){showTitle();return;}
  if(sceneSkipRunning())return;
  continue;
 }
 if(e.type=="choice"){normalSkip=false;sentenceWait.cancel();resetOutgoingScene();transition->present();focus=0;choiceScreen->show(story.state.choices,storyFont());
 text=body.empty()?ui.text("選択してください"):body;
 messageText->start(text);messageText->reveal();refreshMessage(true);
 name=speaker;
 images->selectFace(speaker,voiceId);
 return;
 }if(e.type=="interlude"||e.type=="visual-wait"){
  resetOutgoingScene();transition->present();
  if(e.type=="interlude"){
   text.clear();name.clear();messageText->start("");refreshMessage();
  }
  sentenceWait.page(false);
  warmNextScene();
  return;
 }if(e.type=="page"){if(body.empty())continue;
 const bool continuesHistory=(story.state.vars.count("__history_continue")||(game_profile::stackedHistory&&pendingHistoryStack))&&!history.empty();
 if(continuesHistory){speaker=history.back().speaker;voiceId=history.back().voice;}
 if(auto face=images->selectFace(speaker,voiceId,true)){
  if(deferSceneCommand(e,{*face},{}))return;
  images->selectFace(speaker,voiceId);
 }
 resetOutgoingScene();transition->present();
 text=body;
  currentPageKey=qlie::canonical(e.file)+":"+std::to_string(e.line);currentPageWasRead=progress.wasRead(currentPageKey);if(skipReadOnly&&!currentPageWasRead)normalSkip=false;
 messageText->start(text);autoPlay.page(settings.get("AutoClickWait"));refreshMessage(true);
 name=speaker;
 story.state.vars.erase("__history_continue");
 if(continuesHistory){history.back().text+=text;invalidateHistoryRows();}
 else {history.push_back({name,text,voiceId});playVoice(voiceId,name);}
 pendingHistoryStack=false;
 sentenceWait.page(true,audio->playing(3));
 recordHistoryCheckpoint();
 advances++;warmNextScene();
 QLIE_LOGF("PAGE %s:%u\n",e.file.c_str(),e.line);
 return;
 }if(e.type=="finish"||e.type=="finished"){if(replayActive){openReplay(replayPage);return;}showTitle();
 text="";
 status=ui.text("物語の終了地点に到達しました。");
 return;
 }if(e.type=="text"){
  if(e.value.rfind("【",0)==0){speaker=e.value;
 continue;
 }
  if(e.value.rfind("％",0)==0){voiceId=e.value;
 continue;
 }
  if(!body.empty())body+="\n";
 body+=e.value;
 continue;
 }if(game_profile::stackedHistory&&e.type=="command"&&(e.value=="^format,log:stack"||e.value=="^format,clear:def")){
  pendingHistoryStack=e.value=="^format,log:stack";
  continue;
 }if(e.type=="command"&&e.value.rfind("^movie,",0)==0){
 resetOutgoingScene();
 movie->open(e.value.substr(7));
 text="";
 name="";
 return;
 }
 const bool characterFade=e.type=="legacy-character-fade";
 if(characterFade||(e.type=="command"&&e.value.rfind("^sentence",0)==0)){
  // AVG reserves the automatic portrait dissolve only when an explicit fade
  // has not already captured the scene. It never adds a script wait.
  if(characterFade&&(normalSkip||transition->awaitingPresentation()))continue;
  game_profile::applySentenceTiming(e);
  if(!normalSkip){
   std::vector<std::string> rules;
   for(const auto&path:transition->ruleAssets(e.value)){
    transition->warmRule(path,true);
    rules.push_back(whj2::SceneTransition::ruleKey(path));
   }
   if(deferSceneUntil(e,std::move(rules)))return;
  }
  qlie::Bytes snapshot;
  if(!normalSkip&&transition->needsCapture(e.value)){
   const auto start=qlie::diagnostics?LoadClock::now():LoadClock::time_point{};
   if(!outgoingScene.empty())snapshot=std::move(outgoingScene);
   else{
    auto captured=captureScene();
    if(!captured){pendingScene=std::move(e);return;}
    snapshot=std::move(*captured);
   }
   if(qlie::diagnostics)timing.capture+=std::chrono::duration<double,std::milli>(LoadClock::now()-start).count();
  }
  transition->commandWithCapture(e.value,[&snapshot]{return std::move(snapshot);},
                                 characterFade?nullptr:&sentenceWait,!normalSkip);continue;
 }if(e.type=="wait"||e.type=="click-wait"){
  const bool clickable=e.type=="click-wait";
  std::optional<double> timeout=e.value.empty()?0.:std::stod(e.value);
  if(game_profile::story.legacyAvg&&clickable&&(e.value.empty()||*timeout<0))timeout.reset();
  sentenceWait.native(clickable,timeout);resetOutgoingScene();transition->present();
  warmNextScene(false,e.type=="wait"&&e.file==game_profile::story.endingMovieScript);return;
 }if(e.type=="command"&&e.value.rfind("^savefilter",0)==0){auto fields=whj2::split(e.value,',');if(fields.size()!=2||fields[1]!="0")throw std::runtime_error("Unsupported save thumbnail filter");continue;
 }if(e.type=="audio-volume"){
  auto fields=whj2::split(e.value,',');
  if(fields.size()!=3||fields[0]!="\\v")throw std::runtime_error("Invalid voice volume event");
  audio->volume(3,std::stof(fields[1])/256.f,unsigned(std::stoul(fields[2])));continue;
 }if(e.type=="audio-clear"){
  if(e.value.rfind("dsnd[",0)!=0||e.value.back()!=']')throw std::runtime_error("Invalid direct sound scope");
  size_t used=0;auto index=std::stoul(e.value.substr(5,e.value.size()-6),&used);
  if(index>=16||used!=e.value.size()-6)throw std::runtime_error("Direct sound channel out of range");
  audio->stop(8+unsigned(index));continue;
 }if(e.type=="command"){
  if(e.value=="^format,log:continue"||e.value=="^format,log:stack"){
   story.state.vars["__history_continue"]="1";continue;
  }
  if(e.value=="^format,clear:def"){
   story.state.vars.erase("__history_continue");continue;
  }
  if(audioCommand(e.value))continue;
  std::optional<whj2::SceneImages::BitmapFilter> gradient;
  auto resources=images->warmPaths(e.value,nullptr,&gradient);
  auto blur=images->backgroundBlur(e.value);
  if(deferSceneCommand(e,resources,blur,gradient))return;
  if(gradient)images->takeBitmapFilter(*gradient);
  if constexpr(game_profile::deferredSceneChanges){
   // AVG captures at each explicit fade, including consecutive fades in one page.
   const bool nativeFadePending=game_profile::story.legacyAvg&&transition->awaitingPresentation();
   if(!normalSkip&&!nativeFadePending&&!checkedOutgoingScene&&images->changesCapturedScene(e.value)){
    if(story.beforeNextPresentation(!body.empty(),[this](const auto&next){
        return next.value.rfind("^sentence,",0)==0&&transition->needsCapture(next.value);
       })){
     const auto start=qlie::diagnostics?LoadClock::now():LoadClock::time_point{};
     auto captured=captureScene();
     if(!captured){pendingScene=std::move(e);return;}
     outgoingScene=std::move(*captured);
     if(qlie::diagnostics)timing.capture+=std::chrono::duration<double,std::milli>(LoadClock::now()-start).count();
     QLIE_LOG_LINE(<<"FADE capture before "<<e.file<<":"<<e.line<<"\n");
    }
    checkedOutgoingScene=true;
   }
  }
  const bool sceneCommand=images->command(e.value);
  if(gradient)images->installBitmapFilter(*gradient);
  if(blur)images->installBackgroundBlur(*blur);
  for(const auto&path:resources)readAhead->release(qlie::canonical(path));
  if(sceneCommand){playSceneSounds();continue;}
 }
 if(e.type=="create"||e.type=="free"){
 if(game_profile::story.sceneSkip&&!story.state.replay&&e.type=="create"){
  auto key="scene:"+qlie::canonical(e.file)+":"+std::to_string(e.line);
  auto&section=story.state.vars["__scene_checkpoint"];
  if(!section.empty()&&progress.markSceneComplete(section))persistProgress();
  section=std::move(key);
 }
 resetOutgoingScene();
 story.state.vars.erase("__history_continue");audio->stopAll();clearBackgroundVoice();
 images->setAlternateMessageWindow(story.state.vars.count("__avg2_active")!=0);
 images->clear();
 syncScenePan();
 transition->reset();
 sentenceWait=whj2::SentenceWait{};
 messageText->start("");
 }if(e.type=="command"||e.type=="create"||e.type=="free"||e.type=="wait"){
  // Preserve every unrendered event in the frontend trace; these are explicitly
  // pending rendering work, never counted as multimedia acceptance.
  QLIE_LOG_LINE(<<"PENDING-PRESENT "<<e.type<<" "<<e.file<<":"<<e.line<<"\n");
 continue;
 }throw std::runtime_error("Unhandled frontend event "+e.type);
 }throw std::runtime_error("Frontend event budget");
 }
 static constexpr unsigned AutoSaveBase=121,AutoSaveCount=5;
 unsigned newestManualSlot(){
  if(manualSlotsScanned)return latestManualSlot;
  latestManualSlot=1;
  auto latest=std::filesystem::file_time_type::min();
  for(const auto&entry:std::filesystem::directory_iterator(saveDir)){
   const auto name=entry.path().filename().string();
   if((name.size()!=12&&name.size()!=13)||name.compare(0,4,"slot")||name.compare(name.size()-6,6,".story"))continue;
   unsigned slot=0;
   const char*end=name.data()+name.size()-6;
   auto parsed=std::from_chars(name.data()+4,end,slot);
   if(parsed.ec!=std::errc{}||parsed.ptr!=end||slot==0||slot>=AutoSaveBase||(name.size()==13&&slot<100))continue;
   const auto time=entry.last_write_time();
   if(time>latest||(time==latest&&slot<latestManualSlot)){latest=time;latestManualSlot=slot;}
  }
  manualSlotsScanned=true;
  return latestManualSlot;
 }
 std::string slotPrefix(unsigned slot=1)const{if(slot>=AutoSaveBase){if(slot>=AutoSaveBase+AutoSaveCount)throw std::runtime_error("Auto save slot range");char number[32];std::snprintf(number,sizeof(number),"/autosave/slot%02u",slot-AutoSaveBase+1);return saveDir+number;}char number[16];std::snprintf(number,sizeof(number),"/slot%02u",slot);return saveDir+number;}
 bool save(unsigned slot=1){
  if(replayActive||menu||movie->playing())return false;
  if(pendingScene){status=ui.text("場面の準備中です。");return false;}
  try{
   if(slot<AutoSaveBase)flushProgress();
   whj2::SaveSlotWrite output(slotPrefix(slot));
   qlie::Bytes scene(1280*720*4,0);
   images->drawThumbnail(scene,float(saveThumbnailPriority));
   transition->blend(scene);
   qlie::Bytes thumb(160*90*4);
   for(unsigned y=0;y<90;y++)for(unsigned x=0;x<160;x++)for(unsigned c=0;c<4;c++)
    thumb[(y*160+x)*4+c]=scene[((y*8)*1280+x*8)*4+c];
   output.write(".thumbnail",thumb);
   output.write(".text",qlie::Bytes(text.begin(),text.end()));
   output.write(".name",qlie::Bytes(name.begin(),name.end()));
   output.write(".images",images->save());
   output.write(".transition",transition->save());
   whj2::StateWriter flow;
   flow.str(pendingHistoryStack?"WHJ2-flow-3":"WHJ2-flow-2");
   const auto waitState=sentenceWait.save();
   flow.str(std::string(waitState.begin(),waitState.end()));
   flow.str(pendingBody);
   flow.str(pendingSpeaker);
   flow.str(pendingVoice);
   flow.str(currentPageKey);
   if(pendingHistoryStack)flow.n(1);
   output.write(".flow",flow.bytes);
   output.write(".message",messageText->save());
   output.write(".auto",autoPlay.save());
   if(story.state.awaiting)output.write(".choice",choiceScreen->save());
   output.write(".audio",audio->save());
   whj2::StateWriter mixer;
   mixer.str(game_profile::mixerSignature);
   for(unsigned i=0;i<game_profile::storyAudioTracks;++i){
    mixer.f(volumes[i]);
    mixer.n(loops[i]);
    mixer.n(audioTimes[i]);
    mixer.n(audioTypes[i]);
   }
   if constexpr (game_profile::storyAudioTracks>3)mixer.n(bgvMode);
   output.write(".mixer",mixer.bytes);
   const auto historyData=whj2::saveHistory(history);
   const auto storyData=story.save();
   output.write(".history",historyData);
   if(game_profile::story.historyJumpLimit)
    output.write(".checkpoints",saveHistoryCheckpoints(qlie::hash(storyData),qlie::hash(historyData)));
   output.write(".story",storyData);
   output.publish();
  }catch(const std::exception&e){
   status=ui.text("保存できませんでした。");
   QLIE_LOGF("SAVE slot %u failed: %s\n",slot,e.what());
   return false;
  }
  slotPreviews.erase(slot);
  if(slot<AutoSaveBase){latestManualSlot=slot;manualSlotsScanned=true;}
  status=ui.text("現在の状態を保存しました。");
  return true;
 }
 void refreshSlotFlags(){using UI=whj2::GameMenu;if(gameMenu.panel!=UI::Save&&gameMenu.panel!=UI::Load)return;for(unsigned i=0;i<8;i++)gameMenu.populatedSlots[i]=slotPreview(unsigned(gameMenu.page)*8+i+1).exists;if(!gameMenu.asking&&gameMenu.focus>=8&&!gameMenu.populatedSlots[unsigned(gameMenu.focus)%8])gameMenu.focus%=8;}
 void deleteSlot(unsigned slot){
  if(!slot||slot>=AutoSaveBase)throw std::runtime_error("Manual delete slot range");
  bool removed=false;
  try{
   std::filesystem::remove(slotPrefix(slot)+".story");removed=true;
   for(auto ext:{".text",".name",".images",".transition",".flow",".message",".auto",".choice",".audio",".mixer",".history",".checkpoints",".thumbnail"})std::filesystem::remove(slotPrefix(slot)+ext);
   status=ui.text("データを削除しました。");
  }catch(const std::exception&e){QLIE_LOG_LINE(<<"DELETE FAIL "<<e.what()<<"\n");status=removed?ui.text("データを削除しましたが、一部のファイルが残っています。"):ui.text("削除できませんでした。");}
  try{whj2::commitSavePath(saveDir+"/");}catch(const std::exception&e){QLIE_LOG_LINE(<<"DELETE COMMIT FAIL "<<e.what()<<"\n");status=ui.text("削除結果を保存できませんでした。");}
  slotPreviews.erase(slot);if(latestManualSlot==slot)manualSlotsScanned=false;refreshSlotFlags();
 }
 void autoSaveChoice(){
  if(replayActive||menu||movie->playing()||normalSkip)return;
  unsigned chosen=AutoSaveBase;auto oldest=std::filesystem::file_time_type::max();
  for(unsigned i=0;i<AutoSaveCount;i++){unsigned slot=AutoSaveBase+i;auto path=slotPrefix(slot)+".story";auto time=std::filesystem::exists(path)?std::filesystem::last_write_time(path):std::filesystem::file_time_type::min();if(time<=oldest){oldest=time;chosen=slot;}}
  auto priorStatus=status;
  if(save(chosen)){status=priorStatus;QLIE_LOG_LINE(<<"AUTOSAVE slot="<<(chosen-AutoSaveBase+1)<<"\n");}
 }
 static constexpr auto loadAudioKey="@restore:slot";
 static constexpr auto loadSceneKey="@restore:scene";
 bool loadPending()const{return pendingLoadSlot||pendingHistoryLoad;}
 void startLoad(unsigned slot,std::shared_ptr<const HistoryCheckpoint> checkpoint={}){
  const auto path=checkpoint?std::string{}:slotPrefix(slot);
  auto target=std::make_shared<whj2::PreparedSceneRestore>(*images);
  readAhead->request(loadSceneKey,[&assets=assets,target,path,checkpoint]{
   if(checkpoint)target->read(assets,checkpoint->scene);
   else target->read(assets,path);
   whj2::WarmAsset ready;
   ready.sceneRestore=target;
   return ready;
  },true);
  soundReadAhead->request(loadAudioKey,[&assets=assets,path,checkpoint]{
   if(checkpoint)return whj2::Audio::prepareRestore(checkpoint->audio,assets);
   return whj2::Audio::prepareRestore(qlie::file(path+".audio"),assets);
  },true);
  pendingLoadSlot=slot;
  pendingHistoryLoad=std::move(checkpoint);
  status=ui.text("読み込み中…");
 }
 void cancelLoad(){
  readAhead->discard(loadSceneKey);
  soundReadAhead->discard(loadAudioKey);
  pendingLoadSlot=0;
  pendingHistoryLoad.reset();
 }
 void pollLoad(){
  if(!loadPending()||!readAhead->ready(loadSceneKey)||!soundReadAhead->ready(loadAudioKey))return;
  std::shared_ptr<whj2::PreparedAudioRestore> prepared;
  std::shared_ptr<whj2::PreparedSceneRestore> scene;
  bool preparingScene=true;
  try{
   scene=std::move(readAhead->takeReady(loadSceneKey)->sceneRestore);
   preparingScene=false;
   prepared=std::move(soundReadAhead->takeReady(loadAudioKey)->audioRestore);
  }catch(const std::exception&e){
   cancelLoad();
   if(scene)readAhead->retire(std::move(scene));
   status=preparingScene?ui.text("画面データを読み込めませんでした。"):ui.text("音声データを読み込めませんでした。");
   QLIE_LOGF("LOAD %s restore failed: %s\n",preparingScene?"scene":"audio",e.what());
   return;
  }
  const auto slot=pendingLoadSlot;
  pendingLoadSlot=0;
  auto checkpoint=std::move(pendingHistoryLoad);
  auto before=captureMenuFrame();
  if(checkpoint){
   SavedSlotState saved;
   static_cast<PlaybackState&>(saved)=checkpoint->state;
   try{saved.story=story.decodeSave(checkpoint->story);}
   catch(const std::exception&e){
    readAhead->retire(std::move(scene));
    soundReadAhead->retire(std::move(prepared));
    status=ui.text("データを読み込めませんでした。");
    QLIE_LOGF("LOAD history checkpoint story: %s\n",e.what());
    return;
   }
   saved.message=std::make_unique<whj2::MessageText>(*messageText);
   saved.message->start(saved.text);
   saved.message->reveal();
   applyLoad(std::move(saved),std::move(prepared),std::move(scene),checkpoint->row);
  }else if(!load(slot,std::move(prepared),std::move(scene)))return;
  gameMenu.close();
  beginMenuFade(MenuFadeKind::Load,std::move(before));
 }
 struct SavedSlotState:PlaybackState {
  whj2::StoryState story;
  bool historyStack=false;
  HistoryCheckpoints checkpoints;
  uint32_t storyHash=0,historyHash=0;
  std::unique_ptr<whj2::MessageText> message;
  std::unique_ptr<whj2::ChoiceScreen> choice;
  std::vector<whj2::HistoryEntry> history;
 };
 SavedSlotState readSlotState(const std::string&path){
  SavedSlotState result;
  const auto storyData=qlie::file(path+".story");
  result.story=story.decodeSave(storyData);
  if(game_profile::story.historyJumpLimit)result.storyHash=qlie::hash(storyData);
  const auto t=qlie::file(path+".text"),n=qlie::file(path+".name");
  result.text.assign(t.begin(),t.end());
  result.name.assign(n.begin(),n.end());
  // Check the saved name before adding the display brackets.
  utf8(result.name);
  const auto mixerBytes=qlie::file(path+".mixer");
  whj2::StateReader mixer{mixerBytes};
  const auto mixerVersion=mixer.str();
  const bool legacy=game_profile::storyAudioTracks==3&&mixerVersion=="WHJ2-mixer-1";
  const bool earlyWhj4=game_profile::storyAudioTracks>3&&mixerVersion=="WHJ4-mixer-1";
  if(!legacy&&!earlyWhj4&&mixerVersion!=game_profile::mixerSignature)throw std::runtime_error("Mixer version");
  for(unsigned i=0;i<game_profile::storyAudioTracks;++i){
   result.volumes[i]=mixer.f();
   result.loops[i]=mixer.n()!=0;
   result.times[i]=unsigned(mixer.n());
   result.types[i]=legacy?(i==0?0:2):unsigned(mixer.n());
  }
  if(game_profile::storyAudioTracks>3&&!earlyWhj4)result.bgvMode=mixer.n()!=0;
  mixer.end();
  if(std::filesystem::exists(path+".flow")){
   const auto bytes=qlie::file(path+".flow");
   whj2::StateReader flow{bytes};
   const auto version=flow.str();
   if(version!="WHJ2-flow-1"&&version!="WHJ2-flow-2"&&version!="WHJ2-flow-3")throw std::runtime_error("Flow state version");
   const auto wait=flow.str();
   result.wait.restore(qlie::Bytes(wait.begin(),wait.end()));
   result.body=flow.str();
   result.speaker=flow.str();
   result.voice=flow.str();
   if(version!="WHJ2-flow-1")result.page=flow.str();
   if(version=="WHJ2-flow-3")result.historyStack=flow.n()!=0;
   flow.end();
  }else{
   result.wait.page();
   result.body=result.text;
   result.speaker=result.name;
  }
  result.message=std::make_unique<whj2::MessageText>(*messageText);
  if(std::filesystem::exists(path+".message"))result.message->restore(qlie::file(path+".message"));
  else{result.message->start(result.text);result.message->reveal();}
  result.message->setSpeed(settings.get("TextSpeed"));
  if(std::filesystem::exists(path+".auto"))result.autoPlay.restore(qlie::file(path+".auto"));
  else result.autoPlay.page(settings.get("AutoClickWait"));
  if(std::filesystem::exists(path+".history")){
   const auto historyData=qlie::file(path+".history");
   result.history=whj2::restoreHistory(historyData);
   if(game_profile::story.historyJumpLimit)result.historyHash=qlie::hash(historyData);
  }
  if(result.story.awaiting){
   result.choice=std::make_unique<whj2::ChoiceScreen>(assets);
   result.choice->show(result.story.choices,storyFont());
   if(std::filesystem::exists(path+".choice"))result.choice->restore(qlie::file(path+".choice"),storyFont());
  }
  return result;
 }
 bool load(unsigned slot=1,std::shared_ptr<whj2::PreparedAudioRestore> preparedAudio={},
           std::shared_ptr<whj2::PreparedSceneRestore> preparedScene={}){
  if(movie->playing())return false;
  const auto path=slotPrefix(slot);
  SavedSlotState saved;
  try{
   if(!std::filesystem::exists(path+".story")){status=ui.text("保存データがありません。");return false;}
   saved=readSlotState(path);
   if(!preparedScene){
    preparedScene=std::make_shared<whj2::PreparedSceneRestore>(*images);
    preparedScene->read(assets,path);
   }
   if(auto&history=preparedScene->history;history&&!history->checkpoints.empty()){
    if(history->storyHash==saved.storyHash&&history->historyHash==saved.historyHash&&
       history->checkpoints.back()->row<saved.history.size()){
     saved.checkpoints=std::move(history->checkpoints);
    }else QLIE_LOGF("LOAD optional history checkpoints ignored: different save or invalid history row\n");
   }
   if(!preparedAudio)preparedAudio=whj2::Audio::prepareRestore(qlie::file(path+".audio"),assets).audioRestore;
  }catch(const std::exception&e){
   if(preparedScene)readAhead->retire(std::move(preparedScene));
   if(preparedAudio)soundReadAhead->retire(std::move(preparedAudio));
   status=ui.text("保存データを読み込めませんでした。");
   QLIE_LOGF("LOAD slot %u restore failed: %s\n",slot,e.what());
   return false;
  }
  applyLoad(std::move(saved),std::move(preparedAudio),std::move(preparedScene));
  return true;
 }
 void applyLoad(SavedSlotState saved,std::shared_ptr<whj2::PreparedAudioRestore> preparedAudio,
                 std::shared_ptr<whj2::PreparedSceneRestore> preparedScene,size_t jumpRow=SIZE_MAX){
  cancelPendingScene();
  story.state=std::move(saved.story);
  syncProgress();
  progress.apply(story);
  text=std::move(saved.text);
  name=std::move(saved.name);
  readAhead->retain({});
  readAhead->retire(std::move(images));
  readAhead->retire(std::move(transition));
  images=std::move(preparedScene->images);
  images->setAlternateMessageWindow(story.state.vars.count("__avg2_active")!=0);
  syncScenePan();
  transition=std::move(preparedScene->transition);
  images->setReadAhead(readAhead.get());
  transition->setReadAhead(readAhead.get());
  if(game_profile::story.legacyAvg){
   // Older saves could retain a media layer after its script object was deleted.
   for(const auto* object:{"th","th2"})if(!story.state.vars.count(object))
    images->removeUiLayer(std::string("ui_avg1_video_")+object);
   if(!story.state.vars.count("op_image"))images->removeUiLayer("ui_avg1_op_image");
  }
  bgvMode=saved.bgvMode;
  if(settings.voiceActorCount())voiceActor=dialogueVoiceActor(name);
  updateVoiceGains();
  audio->applyRestore(std::move(preparedAudio),assets);
  bgvDucked=bgvMode&&audio->playing(3);
  volumes=saved.volumes;
  loops=saved.loops;
  audioTimes=saved.times;
  audioTypes=saved.types;
  sentenceWait=std::move(saved.wait);
  pendingBody=std::move(saved.body);
  pendingSpeaker=std::move(saved.speaker);
  pendingVoice=std::move(saved.voice);
  pendingHistoryStack=game_profile::stackedHistory&&saved.historyStack;
  currentPageKey=std::move(saved.page);
  messageText=std::move(saved.message);
  refreshMessage();
  autoPlay=saved.autoPlay;
  if(jumpRow==SIZE_MAX){
   replaceHistory(std::move(saved.history));
   historyCheckpoints=std::move(saved.checkpoints);
  }
  else{
   history.resize(jumpRow+1);
   while(!historyCheckpoints.empty()&&historyCheckpoints.back()->row>jumpRow)historyCheckpoints.pop_back();
   invalidateHistoryRows();
  }
  historyVoicePlayed=false;
  if(menu)title->leave();
  menu=false;hidden=false;normalSkip=false;
  setMessageCursor("None");
  currentPageWasRead=progress.wasRead(currentPageKey);
  focus=0;
  if(saved.choice){
   choiceScreen=std::move(saved.choice);
   focus=choiceScreen->selection();
  }
  warmNextScene();
  status=jumpRow==SIZE_MAX?ui.text("静止画・音声位置を含む状態を読み込みました。"):"";
 }
 void finishTitleStart(){titleStarting=false;audio->stopAll();transition->reset();title->leave();replaceHistory();autoPlay.enabled=false;story.start();progress.apply(story);menu=false;name="";focus=0;QLIE_LOG_LINE(<<"TITLE START complete elapsed_ms="<<titleStartElapsed<<"\n");next();}
 void beginTitleStart(){
  transition->reset();
  if constexpr(game_profile::title.exitUsesMotions)title->beginExit();
  else{
   qlie::Bytes captured(1280*720*4,0);
   for(size_t i=3;i<captured.size();i+=4)captured[i]=255;
   title->draw(captured);
   transition->command(game_profile::titleStartFade,captured);
   transition->present();
  }
  titleStarting=true;titleStartElapsed=0;audio->volume(0,0,game_profile::titleMusicFade);
  QLIE_LOG_LINE(<<"TITLE START wait_ms="<<game_profile::titleStartWait<<'\n');
  if(skipTitleExitForTest)finishTitleStart();
 }
 void activate(){if(cgOpening)return;if(titleStarting){finishTitleStart();return;}if(normalSkip){normalSkip=false;return;}if(opening){if(opening==1)leaveCaution();else {movie->stop();nextMakerLogo();}return;}if(!menu&&hidden){hidden=false;return;}if(movie->playing()){
  if(const auto*legacy=game_profile::story.legacyAvg;
     legacy&&story.state.vars.count("__avg1_credits_clock")&&movie->elapsedMs()<legacy->creditsSkipDelay)return;
  movie->stop();
 if(movieOnly)done=true;
 else next();
 return;
 }if(menu){
 const bool introActive=!title->ready();
 auto action=title->confirm();
 if(introActive)updateTitleAudio(0,true);
 if(action==whj2::TitleScreen::None)return;
 uiSound("Sys_OnClick");
 if(action==whj2::TitleScreen::Start){beginTitleStart();}
 else if(action==whj2::TitleScreen::Load)openPanel(whj2::GameMenu::Load);
 else if(action==whj2::TitleScreen::Settings)openPanel(whj2::GameMenu::Settings);
 else if(action==whj2::TitleScreen::Extra)title->setExtraPage(true);
 else if(action==whj2::TitleScreen::Sound)openMusic();
 else if(action==whj2::TitleScreen::Memory)openReplay();
 else if(action==whj2::TitleScreen::CG)openCg();
 else if(action==whj2::TitleScreen::Back)title->setExtraPage(false);
 else if(action==whj2::TitleScreen::Quit)performUi(whj2::GameMenu::Quit);
 else QLIE_LOG_LINE(<<"PENDING-TITLE extra modes\n");
 }else if(story.state.awaiting){if(choiceScreen->confirm())uiSound("Sys_OnClick");
 }else {if(sentenceWait.blocksClick())return;
 if(settings.get("ClickVoiceStop"))audio->volume(3,0,250,true);
 if(messageText->reveal()){auto action=sentenceWait.prewaiting()?sentenceWait.confirm():whj2::SentenceWait::None;refreshMessage();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);return;}
 auto action=sentenceWait.confirm();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);}
 }
 void uiSound(const std::string&name){audio->playFile(assets,whj2::Audio::SystemChannel,"misc/"+name+".wav",false,1,0,3);}
 void performUi(whj2::GameMenu::Action action){using UI=whj2::GameMenu;
 if(action==UI::Nothing||gameMenu.closing()||menuFadeLocksInput())return;
 if(loadPending()){
  if(action!=UI::Close)return;
  cancelLoad();
  status.clear();
 }
 if(action==UI::PageChange){changeSlotPage(gameMenu.requestedPage,0);return;}
 if(action==UI::TestVolume){audio->playFile(assets,whj2::Audio::SettingsTestChannel,"script/config/soundtest.ogg",false,1,0,gameMenu.testVolumeCategory);return;}
 if(action==UI::ReplayVoice){
  const size_t offset=size_t(gameMenu.historyOffset);
  for(int row=0;row<4;row++){
   if(gameMenu.historyVoiceRow>=0&&gameMenu.historyVoiceRow!=row)continue;
   if(offset+size_t(row)>=history.size())continue;
   auto&entry=history[history.size()-1-offset-size_t(row)];
   if(entry.voice.empty()||!voices->resolve(entry.voice))continue;
   playVoice(entry.voice,entry.speaker,true);historyVoicePlayed=true;return;
  }
  return;
 }
 if(action==UI::SceneReturn){if(replayActive){if(movie->playing())movie->stop();uiSound("Sys_Cancel");openReplay(replayPage);}return;}
 if(action==UI::SettingsChanged){applySettings();uiSound("Sys_OnMouse");return;}
 uiSound(action==UI::No||action==UI::Close?"Sys_Cancel":"Sys_OnClick");
 if(action==UI::JumpHistory){
  const auto index=historyIndex(gameMenu.focus);
  if(historyCheckpoint(index)){requestedHistoryRow=index;gameMenu.ask(UI::JumpHistory);}
  return;
 }
 if(action==UI::No){gameMenu.dismissPrompt();return;}
 if(action==UI::Yes){action=gameMenu.requested;gameMenu.dismissPrompt();
 if(action==UI::JumpHistory){
  if(auto checkpoint=historyCheckpoint(requestedHistoryRow))startLoad(0,std::move(checkpoint));
  return;
 }
 if(action==UI::SectionSkip){beginSceneSkip();return;}
 if(action==UI::DeleteSlot){deleteSlot(selectedSlot);return;}
 if(action==UI::Slot){if(gameMenu.panel==UI::Load)startLoad(selectedSlot);else save(selectedSlot);return;}
 if(action==UI::ResetSettings){settings.reset();applySettings();return;}
 if(action==UI::Quit||action==UI::Title){
  beginMenuFade(action==UI::Quit?MenuFadeKind::Quit:MenuFadeKind::Title,captureMenuFrame());
  for(unsigned channel=0;channel<whj2::Audio::ChannelCount;++channel)audio->volume(channel,0,300,true);
  return;
 }}
 if(action==UI::Close){
  menuFade.reset();
  if(gameMenu.panel==UI::History){
   if(historyVoicePlayed)audio->volume(3,0,200,true);
   historyVoicePlayed=false;
  }
  gameMenu.beginClose();return;
 }
 if(action==UI::Quit||action==UI::Title||action==UI::ResetSettings){gameMenu.ask(action);return;}
 if(action==UI::DeleteSlot){selectedSlot=unsigned(gameMenu.slot());if(std::filesystem::exists(slotPrefix(selectedSlot)+".story"))gameMenu.ask(UI::DeleteSlot);else status=ui.text("保存データがありません。");return;}
 if(action==UI::Slot){selectedSlot=unsigned(gameMenu.slot());
 if(gameMenu.panel==UI::Save){if(std::filesystem::exists(slotPrefix(selectedSlot)+".story")){gameMenu.ask(UI::Slot);return;}save(selectedSlot);}
 else if(gameMenu.panel==UI::Load){auto&preview=slotPreview(selectedSlot);if(!preview.exists)status=ui.text("保存データがありません。");else if(preview.damaged)status=ui.text("保存データが不完全です。削除または上書きしてください。");else gameMenu.ask(UI::Slot);}
 }
 }
 void changeSlotPage(int page,int focus){
  if(pendingLoadSlot)return;
  if(gameMenu.page!=page){
   beginMenuFade(MenuFadeKind::Page,captureMenuFrame());
   gameMenu.page=page;
  }
  gameMenu.focus=focus;
  refreshSlotFlags();
 }
 void openPanel(whj2::GameMenu::Panel panel){if(movie->playing()||(replayActive&&(panel==whj2::GameMenu::Save||panel==whj2::GameMenu::Load)))return;if(panel==whj2::GameMenu::Save&&menu)return;
 // Reuse slot previews across panels. Save and delete invalidate their slot.
 upperMenu->close();normalSkip=false;status.clear();gameMenu.allowSceneReturn=replayActive;gameMenu.open(panel);if(panel==whj2::GameMenu::Save||panel==whj2::GameMenu::Load){auto slot=newestManualSlot()-1;gameMenu.page=int(slot/8);gameMenu.focus=int(slot%8);}refreshSlotFlags();historyVoicePlayed=false;historyViewOffset=-1;gameMenu.setHistorySize(history.size());}
 void upperAction(whj2::UpperMenu::Action action){if(action==whj2::UpperMenu::None)return;upperMenu->close();
 if(action==whj2::UpperMenu::Quit){performUi(whj2::GameMenu::Quit);return;}
 if(action==whj2::UpperMenu::Title){performUi(whj2::GameMenu::Title);return;}
 if(action==whj2::UpperMenu::SectionSkip){if(sceneSkipAvailable())gameMenu.ask(whj2::GameMenu::SectionSkip);return;}
 if(action==whj2::UpperMenu::Screen){if(!window)return;auto flags=SDL_GetWindowFlags(window);if(SDL_SetWindowFullscreen(window,(flags&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP))QLIE_LOG_LINE(<<"PENDING-UPPERMENU screen mode: "<<SDL_GetError()<<"\n");}
 }
 bool extrasVisible()const{return cgScreen->visible()||replayScreen->visible()||musicScreen->visible();}
 bool extrasReady()const{return !extrasExit&&extrasElapsed>=PanelZoomMs;}
 bool startingReplay()const{return extrasExit&&extrasExit->destination==ExtrasDestination::Playback;}
 void beginExtrasExit(ExtrasDestination destination,unsigned entry=0){
  if(!extrasReady())return;
  extrasExit=ExtrasExit{destination,entry};
  extrasElapsed=0;
  if(startingReplay()){
   qlie::Bytes captured(1280*720*4,0);
   replayScreen->draw(captured);
   transition->command("^sentence,fade:rule:250:script/memorymode/fade.png:134",captured);
   transition->present();
   return;
  }
  audio->volume(0,0,250);
  if(destination==ExtrasDestination::Title){
   extrasExit->alternateBackdrop=title->rollAlternate();
   title->warmBackdrop(extrasExit->alternateBackdrop);
  }
 }
 std::optional<whj2::Affine2D> extrasView()const{
  if(extrasReady()||startingReplay())return {};
  const auto zoom=gameMenu.panelZoom(extrasExit.has_value(),extrasElapsed);
  const float x=zoom.scaleX/100,y=zoom.scaleY/100;
  if(x==1&&y==1)return {};
  return whj2::Affine2D{x,0,0,y,640-640*x,360-360*y};
 }
 template<typename Target>
 void drawExtras(Target& target){
  if(startingReplay()){transition->blend(target);return;}
  const auto view=extrasView();
  const auto* transform=view?&*view:nullptr;
  if(cgScreen->visible())cgScreen->draw(target,transform);
  else if(replayScreen->visible())replayScreen->draw(target,transform);
  else musicScreen->draw(target,transform);
 }
 void openCg(){
  cgOpening=!cgScreen->open();
  if(cgOpening)return;
  extrasElapsed=0;
  menu=true;
  audio->stopAll();
  audio->playFile(assets,0,game_profile::cgMusic,true,255.f/256,0,0);
 }
 void openMusic(){extrasElapsed=0;musicScreen->open();}
 void openReplay(unsigned page=0){cancelPendingScene();normalSkip=false;currentPageKey.clear();replayActive=false;menu=true;hidden=false;gameMenu.close();upperMenu->close();images->clear();transition->reset();sentenceWait=whj2::SentenceWait{};replaceHistory();text.clear();name.clear();pendingBody.clear();pendingSpeaker.clear();pendingVoice.clear();autoPlay.enabled=false;messageText->start("");audio->stopAll();extrasElapsed=0;replayScreen->open(page);audio->playFile(assets,0,game_profile::replayMusic,true,255.f/256,0,0);}
 void cgAction(whj2::CgScreen::Action action){
  using CG=whj2::CgScreen;
  if(action==CG::None)return;
  uiSound(action==CG::Back?"Sys_Cancel":"Sys_OnClick");
  beginExtrasExit(action==CG::Back?ExtrasDestination::Title:action==CG::Replay?ExtrasDestination::Replay:ExtrasDestination::Music);
 }
 void replayAction(int action){
  using Replay=whj2::ReplayScreen;
  if(action==Replay::None)return;
  uiSound(action==Replay::Back?"Sys_Cancel":"Sys_OnClick");
  if(action>=0)beginExtrasExit(ExtrasDestination::Playback,unsigned(action));
  else beginExtrasExit(action==Replay::Back?ExtrasDestination::Title:action==Replay::Music?ExtrasDestination::Music:ExtrasDestination::CG);
 }
 void musicAction(whj2::MusicScreen::Destination action){
  using Music=whj2::MusicScreen;
  if(action!=Music::NoDestination)beginExtrasExit(action==Music::CG?ExtrasDestination::CG:action==Music::Replay?ExtrasDestination::Replay:ExtrasDestination::Title);
 }
 void updateExtras(double ms){
  if(!extrasExit){
   extrasElapsed=std::min(extrasElapsed+ms,PanelZoomMs);
   if(cgScreen->visible()){
    cgScreen->update(ms);
    if(cgScreen->inView()&&!fingerActive&&(titleAxis||titleAxisY))cgScreen->panDelta(titleAxis*ms*.6,titleAxisY*ms*.6);
   }else if(replayScreen->visible())replayScreen->update(ms);
   else{musicScreen->update(ms);if(musicScreen->takeSettingsChanged())applySettings();}
   return;
  }
  extrasElapsed+=ms;
  if(startingReplay())transition->update(ms);
  if(extrasElapsed<(startingReplay()?300.:PanelZoomMs))return;
  const auto exit=*extrasExit;
  extrasExit.reset();
  if(cgScreen->visible())cgScreen->close();
  else if(replayScreen->visible())replayScreen->close();
  else if(musicScreen->visible())musicScreen->close();
  switch(exit.destination){
  case ExtrasDestination::Title:
   title->returnToExtras(exit.alternateBackdrop);playTitleTheme();break;
  case ExtrasDestination::CG:openCg();break;
  case ExtrasDestination::Music:openMusic();break;
  case ExtrasDestination::Replay:openReplay();break;
  case ExtrasDestination::Playback:
   transition->reset();replayPage=replayScreen->pageIndex();
   audio->stopAll();story.startReplay(replayScreen->entry(exit.entry).script,replayScreen->entry(exit.entry).label);clearBackgroundVoice();
   progress.apply(story);replayActive=true;menu=false;hidden=false;autoPlay.enabled=false;focus=0;warmNextScene();next();
   break;
  }
 }
 void back(){if(cgOpening){cgOpening=false;return;}if(normalSkip){normalSkip=false;return;}if(extrasVisible()&&!extrasReady())return;if(cgScreen->visible()){cgAction(cgScreen->back());return;}if(replayScreen->visible()){replayAction(whj2::ReplayScreen::Back);return;}if(musicScreen->visible()){musicAction(musicScreen->back());return;}if(gameMenu.asking){performUi(whj2::GameMenu::No);return;}if(gameMenu.visible()){performUi(whj2::GameMenu::Close);return;}if(upperMenu->visible()){upperMenu->close();return;}if(!menu)hidden=!hidden;}
 SlotPreview&slotPreview(unsigned slot){
 auto at=slotPreviews.find(slot);if(at!=slotPreviews.end())return at->second;
 SlotPreview preview;
 try{
  preview.exists=std::filesystem::exists(slotPrefix(slot)+".story");
  if(preview.exists){
   for(auto ext:{".text",".name",".images",".audio",".mixer"})if(!std::filesystem::exists(slotPrefix(slot)+ext))preview.damaged=true;
   if(!preview.damaged){if(std::filesystem::exists(slotPrefix(slot)+".thumbnail")){preview.thumbnail={160,90,qlie::file(slotPrefix(slot)+".thumbnail")};if(preview.thumbnail.pixels.size()!=160*90*4)throw std::runtime_error("Thumbnail size");preview.thumbnail.version.publish();}auto content=qlie::file(slotPrefix(slot)+".text");preview.excerpt=utf8(std::string(content.begin(),content.end()));}
  }
 }catch(const std::exception&e){QLIE_LOG_LINE(<<"SAVE PREVIEW FAIL slot="<<slot<<" "<<e.what()<<"\n");preview.exists=true;preview.damaged=true;}
 if(preview.damaged){preview.thumbnail={};preview.excerpt=utf8(ui.text("保存データが不完全です"));}
 return slotPreviews.emplace(slot,std::move(preview)).first->second;
 }
 void drawPanel(){if constexpr (qlie::diagnostics) ++panelDraws;using UI=whj2::GameMenu;
 using PanelContent=std::function<void(whj2::RenderQueue&)>;
 auto submit=[&](const PanelContent&content,const PanelContent&prompt={}){
  if(frameTarget)gameMenu.draw(*frameTarget,content,prompt);
  else gameMenu.draw(pixels,content,prompt);
 };
 if(gameMenu.panel==UI::History){
  gameMenu.setHistorySize(history.size());
  gameMenu.focus=std::clamp(gameMenu.focus,0,std::max(0,gameMenu.historyRows-1));
  const int offset=gameMenu.historyOffset;
  if(historyViewOffset!=offset){
   historyViewOffset=offset;
   gameMenu.historyTextOffset.fill(0);
  }
  gameMenu.historyVoices.fill(false);
  gameMenu.historyJumps.fill(false);
  gameMenu.historyTextMax.fill(0);
  std::array<const HistoryRow*,4> visible{};
  constexpr auto historyFace=game_profile::localizedHistory?GlyphFace::Dialogue:GlyphFace::Interface;
  for(int row=0;row<gameMenu.historyRows;row++){
   const size_t index=history.size()-1-size_t(offset)-size_t(row);
   const auto&entry=history[index];
   auto cached=std::find_if(historyRows.begin(),historyRows.end(),[index](const HistoryRow&item){return item.entry==index;});
   const bool needsRow=cached==historyRows.end();
   if(needsRow){
    cached=std::min_element(historyRows.begin(),historyRows.end(),[](const HistoryRow&a,const HistoryRow&b){return a.lastUse<b.lastUse;});
    *cached=HistoryRow{};
    cached->entry=index;
    cached->hasVoice=!entry.voice.empty()&&bool(voices->resolve(entry.voice));
   }
   cached->lastUse=++historyRowUse;
   const int firstLine=gameMenu.historyTextOffset[row];
   if(cached->firstLine!=firstLine){
    static constexpr whj2::MessageStyle style{920,104,26,26,0xffffff};
    historyText->start(entry.text);
    historyText->reveal();
    if(needsRow)cached->maxOffset=int(historyText->lastPageOffset(style));
    cached->body=historyText->render(historyFace==GlyphFace::Dialogue?storyFont():font,style,size_t(firstLine));
    cached->body.version.publish();
    cached->firstLine=firstLine;
   }
   gameMenu.historyVoices[row]=cached->hasVoice;
   gameMenu.historyJumps[row]=bool(historyCheckpoint(index));
   gameMenu.historyTextMax[row]=cached->maxOffset;
   visible[row]=&*cached;
  }
  submit([&](whj2::RenderQueue&queue){
   static constexpr int rowY[]={516,367,218,64};
   for(int row=0;row<gameMenu.historyRows;row++){
    const auto&entry=history[visible[row]->entry];
    const int y=rowY[row];
    line(whj2::messageDisplayName(entry.speaker),230,y,26,0x69faff,&queue,historyFace);
    queue.image(visible[row]->body,whj2::Affine2D{1,0,0,1,230.f,float(y+30)},0,255,0,0xffffff);
   }
   if(!status.empty())line(status,30,677,18,0xffffff,&queue);
  },[&](whj2::RenderQueue&queue){
   line(ui.simplified()?"是否跳转到该文字处？":"この文章まで戻りますか？",440,275,28,0xffffff,&queue);
  });
  return;
 }
 refreshSlotFlags();
 submit([&](whj2::RenderQueue&queue){
  if(gameMenu.asking){auto prompt=gameMenu.requested==UI::Quit?ui.text("ゲームを終了しますか？"):gameMenu.requested==UI::Title?ui.text("タイトルに戻りますか？"):gameMenu.requested==UI::ResetSettings?ui.text("設定を初期状態に戻しますか？"):gameMenu.requested==UI::SectionSkip?(ui.simplified()?"跳过这个场景吗？":"このシーンをスキップしますか？"):gameMenu.requested==UI::DeleteSlot?ui.text("データを削除しますか？"):gameMenu.panel==UI::Load?ui.text("データをロードしますか？"):ui.text("データを上書き保存しますか？");line(prompt,440,275,28,0xffffff,&queue);return;}
  if(gameMenu.panel==UI::Settings){
   if(gameSettingsLayout.classic)line(ui.simplified()?"快进范围":"スキップ対象",165,431,17,0xffffff,&queue);
   if(gameMenu.allowSceneReturn)line(ui.text("回想を終了"),520,680,26,
                                   gameMenu.focus==16?0xffff88:0xffffff,&queue);
  }
  if(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load)for(int i=0;i<8;i++){
   unsigned slot=unsigned(gameMenu.page*8+i+1);
   int x=i<4?198:834,y=136+(i%4)*135;
   line("No."+std::to_string(slot),x,y,16,0xff5555,&queue);
   const auto&preview=slotPreview(slot);
   if(!preview.exists){line(ui.text("NO DATA"),x,y+24,18,0x999999,&queue);continue;}
   if(!preview.thumbnail.pixels.empty())
    queue.image(preview.thumbnail,whj2::imagePlacement(i<4?27:660,137+(i%4)*135,1,1,0,0,0,160,90),0,255,0,0xffffff);
   int px=x;
   for(size_t n=0;n<std::min(preview.excerpt.size(),size_t(23));n++){
    auto cp=preview.excerpt[n];drawGlyph(cp,px,y+22,18,18,0xffffff,&queue);px+=cp<128?9:18;
   }
  }
  if(!status.empty())line(status,30,677,18,0xffffff,&queue);
 });

 }

 KImage*newImage(){static KImage dst;dst={0,0,1280,720,1280*4,pixels.data()};return &dst;}
 void recordShown(){auto shown=images->takeShownFiles();auto cgShown=cgScreen->takeShownFiles();shown.insert(cgShown.begin(),cgShown.end());if(progress.captureFiles(shown))persistProgress();}
 void present(){recordShown();if(fpsVisible)fpsDisplay.draw(pixels);
#ifdef WHJ2_WITH_GPU
 if(gpu&&gpu->nativePresentation()){framePacer.wait();if(auto stamp=gpu->presentPixels(pixels))fpsDisplay.presented(*stamp);return;}
#endif
 const uint8_t* display=pixels.data();
 fieldParity^=1;
 if(fieldPending){
  constexpr size_t stride=1280*4;
  for(size_t y=fieldParity;y<720;y+=2)
   std::memcpy(fieldHistory.data()+y*stride,pixels.data()+y*stride,stride);
  display=fieldHistory.data();
 }else if(fieldRequested)fieldHistory=pixels;
 fieldPending=fieldRequested;
 fieldRequested=false;
 SDL_UpdateTexture(texture,nullptr,display,1280*4);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);framePacer.wait();SDL_RenderPresent(renderer);fpsDisplay.presented(SDL_GetTicks64());}
#ifdef WHJ2_WITH_GPU
 void composeNative(const whj2::RenderQueue*scene){
 if(sceneSkipRunning()){gpu->begin(0xff000000u);drawSceneSkip(*frameTarget);}
 else if(gameMenu.coversScene()){gpu->begin(0xff000000u);drawPanel();}
 else if(extrasVisible()){
  gpu->begin(0xff000000u);
  drawExtras(*frameTarget);
 }
 else if(opening==1){
  gpu->begin(0u);images->draw(*frameTarget,true);
  if(cautionLeaving)frameTarget->dim(cautionOpacity(),255);
 }
 else if(movie->hasFrame()){
  KVideoSurface surface{};
  if(!movie->surface(surface)||!gpu->beginVideoSurface(surface))gpu->beginVideo(movie->image());
  drawUpperMenu(*frameTarget);if(gameMenu.visible())drawPanel();
 }
 else{
  gpu->begin(menu?0xff000000u:0xff141923u);
  if(menu){
   if(titleStarting&&!game_profile::title.exitUsesMotions)transition->blend(*frameTarget);
   else title->draw(*frameTarget);
  }
  else{
   scene->draw(*frameTarget,1280,720);transition->blend(*frameTarget);
   if(!hidden&&gameMenu.panel!=whj2::GameMenu::History){
    images->drawMessages(*frameTarget);drawDialogueSpeaker();
    if(messageCursorMotion!="None")messageCursor->draw(*frameTarget);
    if(story.state.awaiting)choiceScreen->draw(*frameTarget);
    images->drawFaces(*frameTarget);if(images->messageVisible()){gameMenu.drawAutoButton(*frameTarget,autoPlay.enabled);gameMenu.drawSkipButton(*frameTarget,normalSkip);}
   }
   drawUpperMenu(*frameTarget);
  }
  if(gameMenu.visible())drawPanel();
 }
 }
 void drawNative(const whj2::RenderQueue*scene){
 if(uiGlyphs.size()>1024)uiGlyphs.clear();
 if(const auto shade=menuFadeShade()){
  frameTarget=gpu.get();gpu->begin(*shade?0xffffffffu:0xff000000u);
 }else if(!gameMenu.coversScene()&&(transition->needsCanvas()||transition->needsTarget())){
  composeCpu(true,scene);
  gpu->begin(pixels);frameTarget=gpu.get();
  if(fieldRequested)frameTarget->requestFieldPresentation();
 }else{
  frameTarget=gpu.get();composeNative(scene);
 }
 drawMenuFade(*frameTarget);
 recordShown();if(fpsVisible)fpsDisplay.draw(*frameTarget);frameTarget=nullptr;
 // FIFO presentation supplies the native 60 Hz limit. A second software sleep
 // before acquisition can miss the next vblank and halve the frame rate.
 if(auto stamp=gpu->present(fpsVisible)){fpsDisplay.presented(*stamp);movie->presented();}
 }
#endif
 void composeCpu(bool liveVideo=false,const whj2::RenderQueue*scene=nullptr){
 fieldRequested=false;
 if(sceneSkipRunning()){clearMenuFrame(0);drawSceneSkip(pixels);return;}
 if(gameMenu.coversScene()){clearMenuFrame(0);drawPanel();return;}
 if(extrasVisible()){
  std::fill(pixels.begin(),pixels.end(),0);
  drawExtras(pixels);
  return;
 }
 if(opening==1){
  std::fill(pixels.begin(),pixels.end(),0);fieldRequested=images->draw(pixels,liveVideo);
  if(cautionLeaving){
   const auto opacity=cautionOpacity();
   for(size_t i=0;i<pixels.size();i+=4)
    for(unsigned ch=0;ch<3;++ch)pixels[i+ch]=uint8_t(pixels[i+ch]*opacity/255);
  }
  return;
 }if(movie->hasFrame()){pixels=movie->image().pixels;
 drawUpperMenu(pixels);
 if(gameMenu.visible())drawPanel();
 return;
 }for(size_t i=0;i<pixels.size();i+=4){pixels[i]=menu?0:35;
 pixels[i+1]=menu?0:25;
 pixels[i+2]=menu?0:20;
 pixels[i+3]=255;
 }
 if(!menu){
  if(scene){scene->draw(pixels,1280,720);fieldRequested=scene->usesFieldPresentation();}
  else fieldRequested=images->drawScene(pixels,liveVideo);
  transition->captureTarget(pixels);fieldRequested=transition->blend(pixels)||fieldRequested;
 }
 if(menu){
  if(titleStarting&&!game_profile::title.exitUsesMotions)fieldRequested=transition->blend(pixels);
  else{
#ifdef WHJ2_WITH_GPU
   if(gpu){gpu->begin(pixels);title->draw(*gpu);gpu->end(pixels);}
   else
#endif
   title->draw(pixels);
  }
 }else if(!hidden&&gameMenu.panel!=whj2::GameMenu::History){images->drawMessages(pixels);
 drawDialogueSpeaker();
 if(messageCursorMotion!="None")messageCursor->draw(pixels);
 if(story.state.awaiting)choiceScreen->draw(pixels);
 }if(!menu&&!hidden&&gameMenu.panel!=whj2::GameMenu::History){images->drawFaces(pixels);if(images->messageVisible()){gameMenu.drawAutoButton(pixels,autoPlay.enabled);gameMenu.drawSkipButton(pixels,normalSkip);}}
 if(!menu)drawUpperMenu(pixels);
 if(gameMenu.visible())drawPanel();
 }
 bool menuFadeLocksInput()const{return menuFade&&menuFade->kind!=MenuFadeKind::Page;}
 std::optional<uint8_t> menuFadeShade()const{
  if(menuFade&&menuFade->kind==MenuFadeKind::Title)return 0;
  if(menuFade&&menuFade->kind==MenuFadeKind::Quit)return 255;
  return {};
 }
 void beginMenuFade(MenuFadeKind kind,qlie::Bytes before){
  if(kind==MenuFadeKind::Title&&readAhead)
   preparedTitleAlternate=title->prepareOpening(readAhead.get(),progress.extras());
  menuFade=MenuFade{kind,{1280,720,std::move(before)}};
  menuFade->previous.version.publish();
 }
 void advanceMenuFade(double ms){
  if(menuFade)menuFade->elapsed=std::min(menuFade->elapsed+ms,menuFade->duration());
 }
 void finishMenuFade(){
  if(!menuFade||menuFade->elapsed<menuFade->duration())return;
  const auto kind=menuFade->kind;
  menuFade.reset();
  if(kind==MenuFadeKind::Title){if(movie->playing())movie->stop();showTitle();}
  else if(kind==MenuFadeKind::Quit)done=true;
 }
 template<typename Target>
 void drawMenuFade(Target& target){
  if(!menuFade)return;
  const unsigned alpha=255-unsigned(menuFade->elapsed*255/menuFade->duration());
  if(!alpha)return;
  whj2::RenderQueue queue;
  queue.image(menuFade->previous,whj2::Affine2D{1,0,0,1,0,0},0,alpha,0,0xffffff);
  queue.draw(target,1280,720);
 }
 void clearMenuFrame(uint8_t shade){
  for(size_t i=0;i<pixels.size();i+=4){
   pixels[i]=pixels[i+1]=pixels[i+2]=shade;
   pixels[i+3]=255;
  }
 }
 void composeMenuFrame(bool liveVideo,const whj2::RenderQueue*scene=nullptr){
  if(const auto shade=menuFadeShade()){fieldRequested=false;clearMenuFrame(*shade);}
  else composeCpu(liveVideo,scene);
  drawMenuFade(pixels);
 }
 qlie::Bytes captureMenuFrame(){
  composeMenuFrame(false);
  auto frame=pixels;
  // The outgoing picture is already composited onto the display background.
  // Carrying its intermediate alpha would expose the new page at time zero.
  for(size_t i=3;i<frame.size();i+=4)frame[i]=255;
  return frame;
 }
 void draw(){if constexpr (qlie::diagnostics) panelDraws=0;
 const bool overlay=gameMenu.visible()||upperMenu->visible()||menuFade.has_value();
 // While waiting for media, repaint only to show or remove an overlay.
 const bool waitingForMedia=movie->loading()||pendingScene;
 if(waitingForMedia&&!overlay&&!lastFrameHadOverlay){framePacer.wait();return;}
 std::optional<whj2::RenderQueue> scene;
 if(!menu&&opening!=1&&!movie->hasFrame()&&!sceneSkipRunning()&&!gameMenu.coversScene()&&!extrasVisible()&&!menuFadeShade()){
  bool masks=false,videoSurfaces=false;
#ifdef WHJ2_WITH_GPU
  if(gpu&&gpu->nativePresentation()&&!transition->needsCanvas()&&!transition->needsTarget()){
   masks=gpu->supportsMasks();
   videoSurfaces=gpu->supportsVideoSurfaces();
  }
#endif
  scene.emplace(images->captureQueue(masks,videoSurfaces));
  // A cold decoder has no image yet. Keep the display and transition target
  // intact until every visible video layer can contribute its first frame.
  if(scene->waitingForVideoFrame()){
   images->takeShownFiles();
   framePacer.wait();
   return;
  }
 }
 lastFrameHadOverlay=overlay;
#ifdef WHJ2_WITH_GPU
 if(gpu&&gpu->nativePresentation()){drawNative(scene?&*scene:nullptr);return;}
#endif
 composeMenuFrame(true,scene?&*scene:nullptr);
 present();movie->presented();
 }
 void snapshot(const std::string&name){SDL_Surface*surface=SDL_CreateRGBSurfaceFrom(pixels.data(),1280,720,32,1280*4,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
 if(!surface||SDL_SaveBMP(surface,(saveDir+"/"+name).c_str()))throw std::runtime_error("Cannot save screenshot");
 SDL_FreeSurface(surface);
 QLIE_LOG_LINE(<<"SCREENSHOT "<<saveDir<<"/"<<name<<"\n");
 }
 bool titleViewActive()const{return menu&&!titleStarting&&!opening&&!gameMenu.visible()&&!musicScreen->visible()&&!replayScreen->visible()&&!cgScreen->visible();}
 void windowSize(int*w,int*h){if(window)SDL_GetWindowSize(window,w,h);else{*w=1280;*h=720;}}
#ifdef __SWITCH__
 bool nativeTouchDown=false;float nativeTouchX=0,nativeTouchY=0;Sint64 nativeTouchId=0;
 void pollNativeTouch(){
  HidTouchScreenState state{};bool touching=hidGetTouchScreenStates(&state,1)>0&&state.count>0;
  auto emit=[&](Uint32 type,float x,float y,Sint64 id){SDL_Event event{};event.type=type;event.tfinger.touchId=0;event.tfinger.fingerId=id;event.tfinger.x=x;event.tfinger.y=y;event.tfinger.pressure=type==SDL_FINGERUP?0.f:1.f;if(SDL_PushEvent(&event)<0)throw std::runtime_error(SDL_GetError());};
  if(nativeTouchDown&&(!touching||Sint64(state.touches[0].finger_id)!=nativeTouchId)){emit(SDL_FINGERUP,nativeTouchX,nativeTouchY,nativeTouchId);nativeTouchDown=false;}
  if(touching){auto&t=state.touches[0];float x=t.x/1280.f,y=t.y/720.f;Sint64 id=t.finger_id;if(!nativeTouchDown)emit(SDL_FINGERDOWN,x,y,id);else if(x!=nativeTouchX||y!=nativeTouchY)emit(SDL_FINGERMOTION,x,y,id);nativeTouchDown=true;nativeTouchX=x;nativeTouchY=y;nativeTouchId=id;}
 }
#endif
 void windowToLogical(int px,int py,float*x,float*y){if(renderer){SDL_RenderWindowToLogical(renderer,px,py,x,y);return;}int width,height;windowSize(&width,&height);float scale=std::min(width/1280.f,height/720.f);*x=(px-(width-1280*scale)*.5f)/scale;*y=(py-(height-720*scale)*.5f)/scale;}
 void touchTitleView(float x,float y){
  const bool titleView=titleViewActive();
  const bool storyView=!menu&&!gameMenu.visible()&&images->hasViewPan();
  if(!titleView&&!storyView)return;
  int w,h;windowSize(&w,&h);float lx,ly;
  windowToLogical(int(x*w),int(y*h),&lx,&ly);
  if(storyView){images->pointViewPan(lx,ly);return;}
  titlePointer=std::clamp(lx,0.f,1280.f);title->view(titlePointer);
 }
 void touchDrag(float x,float y,bool start){if(menuFadeLocksInput()||(extrasVisible()&&!extrasReady())){if(start)fingerUiDrag=false;return;}int w,h;windowSize(&w,&h);float lx,ly;windowToLogical(int(x*w),int(y*h),&lx,&ly);if(cgScreen->visible()){if(start)fingerUiDrag=false;if(cgScreen->inView())cgScreen->pan(lx,ly);return;}if(replayScreen->visible()){if(start)fingerUiDrag=false;return;}if(musicScreen->visible()){bool moved=musicScreen->drag(lx,ly);if(start)fingerUiDrag=moved;return;}if(start)fingerUiDrag=gameMenu.beginDrag(lx,ly);else gameMenu.drag(lx,ly);if(fingerUiDrag&&gameMenu.settingsDirty){applySettings(false);uiSound("Sys_OnMouse");}}
 void pointer(int px,int py){if(cgOpening||menuFadeLocksInput()||sceneSkipRunning())return;float x,y;
 windowToLogical(px,py,&x,&y);
 if(opening||titleStarting){activate();return;}if(menu){titlePointer=x;title->view(x);}
 if(extrasVisible()&&!extrasReady())return;
 if(cgScreen->visible()){cgAction(cgScreen->tap(x,y));return;}
 if(replayScreen->visible()){replayAction(replayScreen->tap(x,y));return;}
 if(musicScreen->visible()){musicAction(musicScreen->tap(x,y));return;}
 if(gameMenu.visible()){refreshSlotFlags();auto action=gameMenu.tap(x,y);refreshSlotFlags();performUi(action);return;}
 if(upperMenu->visible()){upperAction(upperMenu->tap(x,y));return;}
 if(pendingScene)return;
 if(!menu&&!hidden&&images->messageVisible()&&std::abs(x-1170)<=14&&std::abs(y-700)<=14){toggleAuto();return;}
 if(!menu&&!hidden&&images->messageVisible()&&std::abs(x-1200)<=14&&std::abs(y-700)<=14){toggleSkip();uiSound("Sys_OnClick");return;}
 if(menu){if(!title->ready()){activate();return;}int target=title->hit(x,y);if(!title->select(target))return;focus=title->selection();
 }else if(story.state.awaiting){if(choiceScreen->exiting()){activate();return;}int target=choiceScreen->hit(x,y);if(target<0)return;focus=target;if(choiceScreen->select(focus))uiSound("Sys_OnMouse");
 }activate();
 }
 void controllerAdded(int index){if(!SDL_IsGameController(index))return;
 auto*c=SDL_GameControllerOpen(index);
 if(!c)throw std::runtime_error(SDL_GetError());
 auto id=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
 if(controllers.count(id))SDL_GameControllerClose(c);
 else controllers[id]=c;
 QLIE_LOG_LINE(<<"INPUT controller connected id="<<id<<"\n");
 }
 void controllerKey(Uint8 button){using UI=whj2::GameMenu;
 if(cgOpening){if(button==SDL_CONTROLLER_BUTTON_B)back();return;}
 if(menuFadeLocksInput())return;
 if(button==SDL_CONTROLLER_BUTTON_BACK){fpsVisible=!fpsVisible;if(fpsVisible)fpsDisplay.restartSample();return;}
 if(sceneSkipRunning())return;
 if(pendingScene&&!gameMenu.visible()&&!upperMenu->visible()&&
    button!=SDL_CONTROLLER_BUTTON_X&&button!=SDL_CONTROLLER_BUTTON_Y&&button!=SDL_CONTROLLER_BUTTON_B)return;
 if(opening||titleStarting){if(button==SDL_CONTROLLER_BUTTON_A)activate();return;}
 if(extrasVisible()&&!extrasReady())return;
 if(cgScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)cgAction(cgScreen->confirm());else if(button==SDL_CONTROLLER_BUTTON_X)cgScreen->toggleZoom();else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)cgScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)cgScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)cgScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)cgScreen->move(1,0);return;}
 if(replayScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)replayAction(replayScreen->confirm());else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)replayScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)replayScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)replayScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)replayScreen->move(1,0);return;}
 if(musicScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)musicAction(musicScreen->confirm());else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)musicScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)musicScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)musicScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)musicScreen->move(1,0);return;}
 if(gameMenu.visible()){if(gameMenu.closing()||gameMenu.panelTransitioning())return;refreshSlotFlags();if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)performUi(gameMenu.confirm());else if(button==SDL_CONTROLLER_BUTTON_X&&gameMenu.panel==UI::History)performUi(gameMenu.jumpHistory());else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)gameMenu.move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)gameMenu.move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)gameMenu.move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)gameMenu.move(1,0);else if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER&&!gameMenu.asking&&(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load)){changeSlotPage((gameMenu.page+14)%15,gameMenu.focus%8);}else if(button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER&&!gameMenu.asking&&(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load)){changeSlotPage((gameMenu.page+1)%15,gameMenu.focus%8);}refreshSlotFlags();if(gameMenu.settingsDirty){applySettings();uiSound("Sys_OnMouse");}return;}
 if(button==SDL_CONTROLLER_BUTTON_Y&&!menu){upperMenu->offerSectionSkip(sceneSkipAvailable());upperMenu->toggle();return;}
 if(upperMenu->visible()){if(button==SDL_CONTROLLER_BUTTON_A)upperAction(upperMenu->confirm());else if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)upperMenu->move(-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)upperMenu->move(1);return;}
 if(menu){switch(button){case SDL_CONTROLLER_BUTTON_B:if(title->isExtraPage()){title->setExtraPage(false);uiSound("Sys_Cancel");}break;case SDL_CONTROLLER_BUTTON_A:activate();break;case SDL_CONTROLLER_BUTTON_DPAD_UP:case SDL_CONTROLLER_BUTTON_DPAD_LEFT:focus=title->move(-1);uiSound("Sys_OnMouse");break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:focus=title->move(1);uiSound("Sys_OnMouse");break;case SDL_CONTROLLER_BUTTON_X:openPanel(UI::Settings);break;case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:openPanel(UI::Load);break;default:break;}return;}
 switch(button){case SDL_CONTROLLER_BUTTON_A:activate();break;
 case SDL_CONTROLLER_BUTTON_B:back();break;
 case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:openPanel(UI::Save);break;
 case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:openPanel(UI::Load);break;
 case SDL_CONTROLLER_BUTTON_X:openPanel(UI::Settings);break;
 case SDL_CONTROLLER_BUTTON_DPAD_UP:
 case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
 case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
 case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:{
  if(!story.state.awaiting){
   if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)openPanel(UI::History);
   break;
  }
  const int count=int(story.state.choices.size());
  if(count&&!choiceScreen->exiting()){
   const int step=button==SDL_CONTROLLER_BUTTON_DPAD_UP||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
   focus=(choiceScreen->selection()+count+step)%count;
   if(choiceScreen->select(focus))uiSound("Sys_OnMouse");
  }
  break;
 }
 default:break;
 }}
 int run(unsigned smoke){
 std::array<bool,2> extrasZoomCaptured{};
#ifdef __SWITCH__
 whj2::logSwitchThread("main",whj2::bindSwitchThread(0));
 QLIE_LOG_LINE(<<"INPUT libnx Joy-Con and "<<(window?"SDL":"libnx")<<" touch\n");
#else
 QLIE_LOG_LINE(<<"INPUT physical joysticks="<<SDL_NumJoysticks()<<"\n");
 for(int i=0;i<SDL_NumJoysticks();i++)controllerAdded(i);
#endif
 bool cgVariantRequested=false;
 unsigned frames=0,touchLoadWaitFrames=0,touchSkipWaitFrames=0,readSkipFrame=0;skipTitleExitForTest=smoke&&!startTransitionTest;
 if(qlie::diagnostics&&smoke&&!openingTest&&!movieOnly){
  if(opening)showTitle();
  if(!titleTest){title->skipIntro();updateTitleAudio(0,true);}
 }
 uint64_t lastUpdate=SDL_GetTicks64();
 while(!done){auto now=SDL_GetTicks64();
 const bool skipAtFrameStart=normalSkip;
 audio->update();
 updateBackgroundVoice();
 gameMenu.update(double(now-lastUpdate));
 advanceMenuFade(double(now-lastUpdate));
#if defined(__SWITCH__) && QLIE_DIAGNOSTICS
 audio->reportMixerBinding();
#endif
 if(opening==1)advanceCaution(double(now-lastUpdate));
 if(titleStarting){
  const double elapsed=double(now-lastUpdate);
  titleStartElapsed+=elapsed;
  if constexpr(game_profile::title.exitUsesMotions)title->update(elapsed);
  else transition->update(elapsed);
  if(titleStartElapsed>=game_profile::titleStartWait)finishTitleStart();
 }
 if(!menu&&!fingerActive&&!gameMenu.visible()&&images->hasViewPan()){
  const float elapsed=float(now-lastUpdate)*.001f;
  images->moveViewPan(float(titleAxis)*elapsed,float(titleAxisY)*elapsed);
 }
 if(titleViewActive()&&!fingerActive){titlePointer=std::clamp(titlePointer+titleAxis*double(now-lastUpdate)*.6,0.,1280.);title->view(titlePointer);}
 if(extrasVisible())updateExtras(double(now-lastUpdate));
 else if(!opening&&menu&&!titleStarting&&!gameMenu.visible()){
  title->update(double(now-lastUpdate));
  updateTitleAudio(double(now-lastUpdate));
 }
 if(cgOpening)openCg();
 if(!menu&&!pendingScene&&!movie->playing()&&!gameMenu.visible()&&!upperMenu->visible()&&!menuFadeLocksInput()&&!sceneSkipRunning()){
 images->update(double(now-lastUpdate));
 sentenceWait.elapseSkip(double(now-lastUpdate));
 if(story.state.awaiting){choiceScreen->update(double(now-lastUpdate));if(choiceScreen->takeAutoSaveRequest())autoSaveChoice();int chosen=choiceScreen->takeConfirmed();if(chosen>=0){story.choose(unsigned(chosen));if(!sceneSkipActive())next();}}
 messageText->update(double(now-lastUpdate));if(messageText->visible()!=lastMessageVisible)refreshMessage();
 transition->update(double(now-lastUpdate));
 autoPlay.tick(double(now-lastUpdate),messageText->visible(),settings.get("AutoTextWait"),settings.get("TextSpeed"),messageText->timedCharacters(autoPlay.countedGlyphs(),messageText->visible()));
 auto waitAction=normalSkip?whj2::SentenceWait::None:sentenceWait.tick(double(now-lastUpdate),audio->playing(3));
 if(waitAction==whj2::SentenceWait::None&&sentenceWait.waitingForMessage()&&autoPlay.ready(messageText->finished(),audio->playing(3),settings.get("AutoPlayVoiceWait")))waitAction=sentenceWait.force();
 if(waitAction==whj2::SentenceWait::Next)next();else if(waitAction==whj2::SentenceWait::Resume)next(true);
 playSceneSounds();
}
#ifdef __SWITCH__
 if(!appletMainLoop()){done=true;
 break;
 }if(!window)pollNativeTouch();auto pad=joycon.poll(now);
 titleAxis=pad.viewAxis;titleAxisY=pad.viewAxisY;
 if(pad.autoToggle)toggleAuto();
 if(pad.skipToggle)toggleSkip();
 if(pad.fps)controllerKey(SDL_CONTROLLER_BUTTON_BACK);
 if(pad.upperMenu)controllerKey(SDL_CONTROLLER_BUTTON_Y);
 if(pad.confirm)controllerKey(SDL_CONTROLLER_BUTTON_A);
 if(pad.back)controllerKey(SDL_CONTROLLER_BUTTON_B);
 if(pad.menu)controllerKey(SDL_CONTROLLER_BUTTON_X);
 if(pad.save)controllerKey(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
 if(pad.load)controllerKey(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
 if(pad.horizontal)controllerKey(pad.horizontal<0?SDL_CONTROLLER_BUTTON_DPAD_LEFT:SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
 if(pad.direction)controllerKey(pad.direction<0?SDL_CONTROLLER_BUTTON_DPAD_UP:SDL_CONTROLLER_BUTTON_DPAD_DOWN);

#endif
 if(movie->playing()&&!menuFadeLocksInput()){movie->update();
 if(!movie->playing()){if(movieOnly){done=true;
 break;
 }if(opening){nextMakerLogo();}else next();
 }}SDL_Event e;
 while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)done=true;
else if(e.type==SDL_FINGERDOWN){if(!fingerActive){fingerActive=true;fingerId=e.tfinger.fingerId;fingerX=e.tfinger.x;fingerY=e.tfinger.y;touchTitleView(fingerX,fingerY);touchDrag(fingerX,fingerY,true);}}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&cgScreen->visible()&&cgScreen->inView()){touchDrag(e.tfinger.x,e.tfinger.y,false);}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&(titleViewActive()||images->hasViewPan())&&!fingerUiDrag){touchTitleView(e.tfinger.x,e.tfinger.y);}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&fingerUiDrag){touchDrag(e.tfinger.x,e.tfinger.y,false);}
 else if(e.type==SDL_FINGERUP&&fingerActive&&e.tfinger.fingerId==fingerId){fingerActive=false;
 float dx=(e.tfinger.x-fingerX)*1280,dy=(e.tfinger.y-fingerY)*720;
 if(fingerUiDrag){touchDrag(e.tfinger.x,e.tfinger.y,false);gameMenu.endDrag();fingerUiDrag=false;if(gameMenu.panel==whj2::GameMenu::Settings)applySettings();}
 else if(!menu&&!sceneSkipRunning()&&!gameMenu.visible()&&!menuFadeLocksInput()&&upperMenu->swipe(dx,dy)){upperMenu->offerSectionSkip(sceneSkipAvailable());QLIE_LOG_LINE(<<"INPUT downward swipe upper menu\n");}
 else if(std::abs(dx)<24&&std::abs(dy)<24){int w,h;windowSize(&w,&h);QLIE_LOG_LINE(<<"INPUT touch\n");pointer(int(e.tfinger.x*w),int(e.tfinger.y*h));}
 }else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY){titleAxisY=std::abs(e.caxis.value)>6000?e.caxis.value/32767.f:0;}else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX){titleAxis=std::abs(e.caxis.value)>6000?e.caxis.value/32767.f:0;}else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT){bool held=e.caxis.value>16000;if(held&&!leftTrigger)toggleAuto();leftTrigger=held;}else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT){bool held=e.caxis.value>16000;if(held&&!rightTrigger)toggleSkip();rightTrigger=held;}else if(e.type==SDL_CONTROLLERDEVICEADDED)controllerAdded(e.cdevice.which);
 else if(e.type==SDL_CONTROLLERDEVICEREMOVED){auto i=controllers.find(e.cdevice.which);
 if(i!=controllers.end()){SDL_GameControllerClose(i->second);
 controllers.erase(i);
 }}else if(e.type==SDL_CONTROLLERBUTTONDOWN){QLIE_LOG_LINE(<<"INPUT controller button="<<unsigned(e.cbutton.button)<<"\n");
#ifndef __SWITCH__
controllerKey(e.cbutton.button);
#endif
}}if(!done)pollLoad();
 const bool resumedScene=!done&&!menuFadeLocksInput()&&resumeSceneCommand();
 if(!done)advanceSceneSkip();
 if(!menuFadeLocksInput())recordCurrentRead();
 if(normalSkip&&!menuFadeLocksInput()&&!resumedScene&&!pendingScene&&!menu&&!gameMenu.visible()&&!upperMenu->visible()&&!movie->playing()&&!story.state.awaiting){hidden=false;if(messageText->reveal())refreshMessage();auto action=sentenceWait.skip();if(action!=whj2::SentenceWait::None){transition->reset();next(action==whj2::SentenceWait::Resume);}}
 if(!menuFadeLocksInput())updateMessageCursor(double(now-lastUpdate));
 lastUpdate=now;
 const auto beforeDraw=qlie::diagnostics?SDL_GetTicks64():0;
 draw();
 finishMenuFade();
 const auto afterDraw=qlie::diagnostics?SDL_GetTicks64():0;
 if(qlie::diagnostics&&afterDraw-now>=250)
  QLIE_LOGF("FRAME stall_ms=%llu update_ms=%llu draw_ms=%llu page=%s pending=%d skip=%d skip_wait_ms=%.1f music=%d cg=%d\n",
               static_cast<unsigned long long>(afterDraw-now),static_cast<unsigned long long>(beforeDraw-now),
               static_cast<unsigned long long>(afterDraw-beforeDraw),currentPageKey.c_str(),int(bool(pendingScene)),
               int(skipAtFrameStart||normalSkip),sentenceWait.remainingForSkip(),int(musicScreen->visible()),int(cgScreen->visible()));
 if constexpr (qlie::diagnostics){
 if(cgTest||replayTest||musicTest){
  const bool matching=(cgTest&&cgScreen->visible())||(replayTest&&replayScreen->visible())||(musicTest&&musicScreen->visible());
  const bool closing=extrasExit.has_value();
  if(matching&&!extrasZoomCaptured[closing]&&extrasView()){
   snapshot(closing?"extras-zoom-out.bmp":"extras-zoom-in.bmp");
   extrasZoomCaptured[closing]=true;
  }
 }
 if(smoke&&startTransitionTest){
  if(frames==1){activate();if(!titleStarting||!menu)throw std::runtime_error("Start bypassed cloud transition");snapshot("start-cloud-begin.bmp");touchSavedStory=pixels;}
  if(titleStarting&&titleStartElapsed>=900&&!startMidCaptured){snapshot("start-cloud-middle.bmp");if(pixels==touchSavedStory)throw std::runtime_error("Cloud transition did not change pixels");startMidCaptured=true;}
  if(titleStarting&&titleStartElapsed>=2100&&!startEndCaptured){snapshot("start-cloud-black.bmp");for(size_t i=0;i<pixels.size();i++)if(i%4!=3&&pixels[i])throw std::runtime_error("Cloud transition did not reach black");startEndCaptured=true;}
  if(frames>1&&!titleStarting){if(!startMidCaptured||!startEndCaptured||menu||currentPageKey.empty())throw std::runtime_error("Start transition sequence incomplete");snapshot("start-story.bmp");showTitle();title->skipIntro();activate();if(!titleStarting)throw std::runtime_error("Second start transition absent");activate();if(titleStarting||menu)throw std::runtime_error("Confirm did not skip title exit");QLIE_LOG_LINE(<<"TITLE START PASS cloud/middle/black/2500ms/story/confirm-skip\n");done=true;}
  if(++frames>=smoke&&!done)throw std::runtime_error("Start transition timeout");
  continue;
 }
 if(smoke&&gameMenu.visible()&&panelDraws!=1)throw std::runtime_error("Panel composited more than once");
 if(movieOnly&&!movieCaptured&&movie->elapsedMs()>=1000){snapshot("movie.bmp");
 movieCaptured=true;
 QLIE_LOG_LINE(<<"MOVIE screenshot audio_clock_ms="<<movie->elapsedMs()<<"\n");
 }if(smoke){if(readSkipTest){
 if(frames==1)activate();
 // Count assertion steps only after a deferred scene command is applied.
 if(frames>0&&!pendingScene){
 ++readSkipFrame;
 if(readSkipFrame==1){if(currentPageKey.empty()||currentPageWasRead)throw std::runtime_error("Read skip needs fresh progress");settings.set("SkipMode",1);toggleSkip();if(normalSkip)throw std::runtime_error("Unread page allowed read-only skip");messageText->reveal();hidden=true;recordCurrentRead();if(progress.wasRead(currentPageKey))throw std::runtime_error("Hidden text marked read");hidden=false;recordCurrentRead();auto key=currentPageKey;save(1);whj2::Progress restored;restored.restore(qlie::file(saveDir+"/system.progress"));if(!restored.wasRead(key))throw std::runtime_error("Read page not persisted");for(unsigned version=1;version<=2;version++){whj2::StateWriter old;old.str("WHJ2-progress-"+std::to_string(version));old.n(0);old.n(0);if(version==2)old.n(0);restored.restore(old.bytes);if(restored.wasRead(key))throw std::runtime_error("Legacy progress invented read history");}skipStart=advances;SDL_Event left{};left.type=SDL_CONTROLLERAXISMOTION;left.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;left.caxis.value=32767;SDL_PushEvent(&left);}
 if(readSkipFrame==4){if(normalSkip||advances<=skipStart)throw std::runtime_error("Fresh-read ZR did not start and stop at unread page");if(!sentenceWait.waitingForMessage())throw std::runtime_error("Second story page inherited the opening fade wait");load(1);if(!currentPageWasRead)throw std::runtime_error("Read page lost on load");toggleSkip();if(!normalSkip)throw std::runtime_error("Read page refused skip");sentenceWait.elapseSkip(10000);}
 if(readSkipFrame==10){if(normalSkip||advances<=skipStart||currentPageWasRead)throw std::runtime_error("Read skip did not stop at unseen page");messageText->reveal();recordCurrentRead();pointer(1200,700);if(!normalSkip)throw std::runtime_error("Fresh-read touch skip did not enable without load");sentenceWait.elapseSkip(10000);skipStart=advances;}
 if(readSkipFrame==12){if(normalSkip||advances<=skipStart)throw std::runtime_error("Fresh-read touch skip did not stop at unread page");skipStart=advances;rightTrigger=false;SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;e.caxis.value=32767;SDL_PushEvent(&e);}
 if(readSkipFrame==14){SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;SDL_PushEvent(&e);if(advances!=skipStart||normalSkip)throw std::runtime_error("ZR skipped unread page");}
 if(readSkipFrame==17){settings.set("SkipMode",0);SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;e.caxis.value=32767;SDL_PushEvent(&e);skipStart=advances;}
 if(readSkipFrame==18){if(!normalSkip)throw std::runtime_error("ZR all-text skip refused");sentenceWait.elapseSkip(10000);}
 if(readSkipFrame==22){draw();snapshot("read-skip-scene.bmp");}
 if(readSkipFrame==23){if(advances<=skipStart)throw std::runtime_error("All-text skip did not advance");back();if(normalSkip)throw std::runtime_error("B did not cancel skip");openPanel(whj2::GameMenu::Settings);gameMenu.update(250);performUi(gameMenu.tap(410,442));if(settings.get("SkipMode")!=1)throw std::runtime_error("Read-only setting touch");whj2::Settings copy(assets);copy.restore(qlie::file(saveDir+"/settings.cfg"));if(copy.get("SkipMode")!=1)throw std::runtime_error("Skip mode not persisted");draw();snapshot("read-skip-settings.bmp");gameMenu.close();load(1);pointer(1200,700);if(!normalSkip)throw std::runtime_error("Original skip button did not activate");sentenceWait.elapseSkip(10000);}
 if(readSkipFrame==45){if(normalSkip)throw std::runtime_error("Touch skip did not stop at unread text");QLIE_LOG_LINE(<<"READ SKIP PASS opening transition wait / fresh-read ZR and touch without reload / persisted pages / progress1-2 migration / flow2 restore / unread stop / ZR unread stop / all mode / B cancel / original touch button\n");done=true;}
 }
 if(++frames>=smoke&&!done){throw std::runtime_error("Read skip test timeout");}continue;
 }if(replayReturnTest){
 if(frames==1){openReplay(0);updateExtras(PanelZoomMs);replayAction(replayScreen->confirm());updateExtras(300);if(!replayActive)throw std::runtime_error("Return test needs unlocked replay entry");}
 if(frames==20){controllerKey(SDL_CONTROLLER_BUTTON_X);gameMenu.update(250);if(!gameMenu.allowSceneReturn)throw std::runtime_error("Replay return action unavailable");for(unsigned i=0;i<13;i++)controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);if(gameMenu.focus!=16)throw std::runtime_error("Replay return focus unreachable");draw();if(panelDraws!=1)throw std::runtime_error("Replay return panel draw count");snapshot("replay-return-menu.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);if(!replayActive||!gameMenu.closing())throw std::runtime_error("Cancel unexpectedly left replay");}
 if(frames==30){if(gameMenu.closing())continue;controllerKey(SDL_CONTROLLER_BUTTON_X);gameMenu.update(250);for(unsigned i=0;i<13;i++)controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);controllerKey(SDL_CONTROLLER_BUTTON_A);if(replayActive||!replayScreen->visible()||replayScreen->pageIndex()!=0||gameMenu.visible())throw std::runtime_error("Controller scene return");}
 if(frames==50){updateExtras(PanelZoomMs);replayAction(replayScreen->confirm());updateExtras(300);if(!replayActive)throw std::runtime_error("Second replay start");controllerKey(SDL_CONTROLLER_BUTTON_X);gameMenu.update(250);performUi(gameMenu.tap(600,718));if(replayActive||!replayScreen->visible())throw std::runtime_error("Touch scene return");}
 if(frames==70){updateExtras(PanelZoomMs);replayAction(replayScreen->confirm());updateExtras(300);controllerKey(SDL_CONTROLLER_BUTTON_X);gameMenu.update(250);performUi(whj2::GameMenu::Title);if(!gameMenu.asking)throw std::runtime_error("Title confirmation missing");performUi(whj2::GameMenu::Yes);advanceMenuFade(500);draw();finishMenuFade();if(replayActive||replayScreen->visible()||!menu)throw std::runtime_error("Title incorrectly routed to replay");openPanel(whj2::GameMenu::Settings);if(gameMenu.allowSceneReturn)throw std::runtime_error("Scene return leaked outside replay");gameMenu.close();snapshot("replay-return-title.bmp");QLIE_LOG_LINE(<<"REPLAY RETURN PASS controller/touch return, cancel keeps playback, title remains title, no action outside replay\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Replay return timeout");}continue;
 }if(cgLoopTest){
 if(frames==1)openCg();
 if(frames==25){cgScreen->move(0,-1);cgScreen->move(1,0);cgAction(cgScreen->confirm());}
 if(frames==50){if(cgScreen->pageIndex()!=1)throw std::runtime_error("Loop test catalogue page");for(unsigned i=0;i<5;i++)cgScreen->move(1,0);cgAction(cgScreen->confirm());if(!cgScreen->inView())throw std::runtime_error("Animated CG fixture not unlocked");}
 if(frames==230){touchSavedStory=pixels;snapshot("cg-loop-after-first.bmp");}
 if(frames==260){if(pixels==touchSavedStory)throw std::runtime_error("CG movie froze after loop boundary");snapshot("cg-loop-moving.bmp");back();back();}
 if(frames==260&&(!cgScreen->visible()||!extrasExit))throw std::runtime_error("CG did not begin fade-out");
 if(frames>=265&&!cgScreen->visible()){if(!menu)throw std::runtime_error("Animated CG exit");QLIE_LOG_LINE(<<"CG LOOP PASS animation continues after first loop; normal exit; source audio separately verified silent\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("CG loop runtime timeout");}continue;
 }if(cgTest){
 if(frames==1){if(!progress.extras())throw std::runtime_error("CG smoke needs earned progress");title->select(2);activate();titlePhase=1;}
 else if(titlePhase==1&&title->ready()){title->select(0);activate();titlePhase=2;autoPhaseFrame=frames;}
 else if(titlePhase==2&&extrasVisible()&&extrasReady()){if(!cgScreen->visible()||cgScreen->inView())throw std::runtime_error("CG catalogue not open");snapshot("cg-page1.bmp");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=3;autoPhaseFrame=frames;}
 else if(titlePhase==3&&frames>autoPhaseFrame+12){
  if(!cgScreen->inView())throw std::runtime_error("Seen CG did not open");
  if(cgScreen->viewLoaded()){
   if(!cgVariantRequested){snapshot("cg-view.bmp");controllerKey(SDL_CONTROLLER_BUTTON_A);cgVariantRequested=true;}
   else{
    if(cgScreen->variantIndex()!=1)throw std::runtime_error("CG next variant");
    controllerKey(SDL_CONTROLLER_BUTTON_X);cgScreen->pan(0,0);titlePhase=4;autoPhaseFrame=frames;
   }
  }
 }
 else if(titlePhase==4&&frames>autoPhaseFrame+12){if(cgScreen->zoomLevel()!=2)throw std::runtime_error("CG zoom not enabled");snapshot("cg-zoom.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);if(cgScreen->zoomLevel()!=1||!cgScreen->inView())throw std::runtime_error("CG zoom cancel");controllerKey(SDL_CONTROLLER_BUTTON_B);if(cgScreen->inView())throw std::runtime_error("CG view cancel");cgScreen->move(0,-1);cgScreen->move(1,0);cgAction(cgScreen->confirm());titlePhase=5;autoPhaseFrame=frames;}
 else if(titlePhase==5&&frames>autoPhaseFrame+25){if(cgScreen->pageIndex()!=1)throw std::runtime_error("CG page2 navigation");snapshot("cg-page2.bmp");pointer(1080,695);titlePhase=6;autoPhaseFrame=frames;}
 else if(titlePhase==6&&frames>autoPhaseFrame+20&&extrasReady()){if(!musicScreen->visible())throw std::runtime_error("CG to music");pointer(950,695);titlePhase=7;autoPhaseFrame=frames;}
 else if(titlePhase==7&&frames>autoPhaseFrame+20&&extrasReady()){if(!replayScreen->visible())throw std::runtime_error("Music to replay");pointer(820,695);titlePhase=8;autoPhaseFrame=frames;}
 else if(titlePhase==8&&frames>autoPhaseFrame+20&&extrasReady()){if(!cgScreen->visible())throw std::runtime_error("Replay to CG");back();if(!cgScreen->visible()||!extrasExit)throw std::runtime_error("CG exit did not retain catalogue");titlePhase=9;}
 else if(titlePhase==9&&!extrasExit){if(cgScreen->visible()||!title->isExtraPage())throw std::runtime_error("CG back to extras");QLIE_LOG_LINE(<<"CG UI PASS original catalogue / viewed-file unlock / variants / zoom / page2 / music-replay-CG navigation / back\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("CG UI timeout");}continue;
 }if(replayTest){
 if(frames==1){if(!progress.extras())throw std::runtime_error("Replay smoke needs earned progress");title->select(2);activate();titlePhase=1;}
 else if(titlePhase==1&&title->ready()){title->select(1);activate();titlePhase=2;autoPhaseFrame=frames;}
 else if(titlePhase==2&&extrasVisible()&&extrasReady()){if(!replayScreen->visible())throw std::runtime_error("Replay page did not open");snapshot("replay-page1.bmp");pointer(1210,90);titlePhase=3;autoPhaseFrame=frames;}
 else if(titlePhase==3&&frames>autoPhaseFrame+25){if(replayScreen->pageIndex()!=1)throw std::runtime_error("Replay second page");snapshot("replay-page2.bmp");replayScreen->move(0,-1);replayAction(replayScreen->confirm());titlePhase=4;autoPhaseFrame=frames;}
 else if(titlePhase==4&&frames>autoPhaseFrame+25){if(replayScreen->pageIndex()!=0)throw std::runtime_error("Replay first page return");replayAction(replayScreen->confirm());titlePhase=5;autoPhaseFrame=frames;}
 else if(titlePhase==5&&frames>autoPhaseFrame+12&&!extrasExit){if(menu||!replayActive||!story.state.replay)throw std::runtime_error("Replay did not start");snapshot("replay-playing.bmp");openPanel(whj2::GameMenu::Save);if(gameMenu.visible())throw std::runtime_error("Replay save was not locked");openPanel(whj2::GameMenu::Load);if(gameMenu.visible())throw std::runtime_error("Replay load was not locked");
 // Test setup: seek the logical stream to its return boundary; this is not a full audiovisual playback test.
 bool found=false;for(unsigned i=0;i<100000;i++){auto before=story.save();auto e=story.next();if(e.type=="choice")throw std::runtime_error("Unexpected first replay choice");if(e.type=="finish"){story.restore(before);found=true;break;}}
 if(!found){throw std::runtime_error("Replay return boundary absent");}next();titlePhase=6;autoPhaseFrame=frames;}
 else if(titlePhase==6&&extrasReady()){if(replayActive||!menu||!replayScreen->visible()||replayScreen->pageIndex()!=0)throw std::runtime_error("Replay did not restore original page");snapshot("replay-returned.bmp");back();if(!replayScreen->visible()||!extrasExit)throw std::runtime_error("Replay exit did not retain catalogue");titlePhase=7;}
 else if(titlePhase==7&&!extrasExit){if(replayScreen->visible()||!title->isExtraPage())throw std::runtime_error("Replay back to extras");QLIE_LOG_LINE(<<"REPLAY UI PASS title entry / page1-page2 / playback sample / save-load lock / logical-boundary return / extras\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Replay UI test timeout");}continue;
 }if(viewTest){
 auto finger=[&](Uint32 type,float x){SDL_Event e{};e.type=type;e.tfinger.x=x;e.tfinger.y=.5f;e.tfinger.fingerId=7;if(SDL_PushEvent(&e)<0)throw std::runtime_error(SDL_GetError());};
 auto axis=[&](Sint16 x){SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_LEFTX;e.caxis.value=x;if(SDL_PushEvent(&e)<0)throw std::runtime_error(SDL_GetError());};
 if(frames==1)finger(SDL_FINGERDOWN,.2f);
 if(frames==3&&(std::abs(titlePointer-256)>1||!fingerActive))throw std::runtime_error("Touch-down view not immediate");
 if(frames==30){snapshot("view-touch-left.bmp");finger(SDL_FINGERMOTION,.8f);}
 if(frames==32&&std::abs(titlePointer-1024)>1)throw std::runtime_error("Touch-drag view not updated");
 if(frames==60){snapshot("view-touch-right.bmp");finger(SDL_FINGERUP,.8f);axis(-32767);mutedNonzero=uint64_t(titlePointer);}
 if(frames==75){if(titlePointer>=mutedNonzero-50)throw std::runtime_error("Left stick did not move view left");axis(0);}
 if(frames==78)mutedNonzero=uint64_t(titlePointer);
 if(frames==92){if(std::abs(titlePointer-float(mutedNonzero))>1)throw std::runtime_error("View did not hold after stick release");axis(32767);}
 if(frames==108){if(titlePointer<=mutedNonzero+50||advances||!menu||title->selection()!=0)throw std::runtime_error("Stick view navigation changed menu/story");snapshot("view-stick-right.bmp");QLIE_LOG_LINE(<<"TITLE VIEW PASS immediate touch / drag / left-right stick / neutral hold / no menu selection or story change\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Title view test timeout");}continue;
 }if(openingTest){if(opening==1&&frames==30)snapshot("opening-caution.bmp");if(opening==2&&!openingLogoCaptured&&movie->elapsedMs()>700){snapshot("opening-logo.bmp");openingLogoCaptured=true;}if(!opening){if(!openingLogoCaptured)throw std::runtime_error("Opening movie was not rendered");if(!openingTitleStarted)openingTitleStarted=SDL_GetTicks64();auto elapsed=SDL_GetTicks64()-openingTitleStarted;
 const unsigned times[]={1000,4500,7000};if(openingTitleCaptures<3&&elapsed>=times[openingTitleCaptures]){snapshot("opening-title-"+std::to_string(times[openingTitleCaptures])+".bmp");openingTitleCaptures++;}
 if(openingTitleCaptures==3){if(!title->ready())throw std::runtime_error("Title entrance did not finish");QLIE_LOG_LINE(<<"OPENING PASS caution4500 / original maker movie / continuous title animation7000\n");done=true;}}if(++frames>=smoke&&!done){throw std::runtime_error("Opening sequence timeout");}continue;}
 if(musicTest){
 if(frames==1){settings.set("MusicModeVolume",100);if(!progress.extras())throw std::runtime_error("Music smoke needs earned progress");title->select(2);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=1;}
 else if(titlePhase==1&&title->ready()){title->select(2);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=2;}
 else if(titlePhase==2&&musicScreen->visible()&&extrasReady()){if(!musicScreen->visible())throw std::runtime_error("Music page not opened");snapshot("music-page.bmp");pointer(270,200);titlePhase=3;autoPhaseFrame=frames;}
 else if(titlePhase==3&&frames>=autoPhaseFrame+8&&audio->position(0).length){if(musicScreen->playingTrack()!=0||!audio->playing(0))throw std::runtime_error("Music select/play");draw();qlie::Bytes playingIcon;for(int yy=84;yy<116;yy++)for(int xx=1024;xx<1056;xx++)for(int c=0;c<4;c++)playingIcon.push_back(pixels[(yy*1280+xx)*4+c]);pointer(1040,100);draw();qlie::Bytes pausedIcon;for(int yy=84;yy<116;yy++)for(int xx=1024;xx<1056;xx++)for(int c=0;c<4;c++)pausedIcon.push_back(pixels[(yy*1280+xx)*4+c]);if(playingIcon==pausedIcon)throw std::runtime_error("Music play/pause icon unchanged");pointer(1040,100);pointer(409,100);auto p=audio->position(0);if(p.frame<p.length/3||p.frame>p.length*2/3)throw std::runtime_error("Music touch seek");pointer(1140,100);audio->seek(0,UINT64_MAX);titlePhase=4;autoPhaseFrame=frames;}
 else if(titlePhase==4&&frames>=autoPhaseFrame+8&&audio->position(0).length){if(musicScreen->playingTrack()!=0||!audio->playing(0))throw std::runtime_error("Music loop restart");pointer(1090,100);if(musicScreen->playingTrack()!=1)throw std::runtime_error("Music next track");pointer(700,100);draw();snapshot("music-muted.bmp");if(settings.get("MusicModeVolume")!=0)throw std::runtime_error("Music volume not applied");qlie::write_file(saveDir+"/settings.cfg",settings.save());
 controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);
 if(musicScreen->selection()!=23){throw std::runtime_error("Controller seek focus unreachable");}
 pointer(409,100);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);titlePhase=5;}
 else if(titlePhase==5&&audio->position(0).length){
 const auto position=audio->position(0);const auto middle=position.length*199/399;
 if(position.frame<middle+200000||position.frame>middle+300000)throw std::runtime_error("Queued touch and controller seek adjustment");
 controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);if(musicScreen->selection()!=24)throw std::runtime_error("Controller volume focus unreachable");controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);if(settings.get("MusicModeVolume")!=5)throw std::runtime_error("Controller volume adjustment");
 controllerKey(SDL_CONTROLLER_BUTTON_B);if(musicScreen->playingTrack()!=-1)throw std::runtime_error("Music first cancel stop");controllerKey(SDL_CONTROLLER_BUTTON_B);if(!musicScreen->visible()||!extrasExit)throw std::runtime_error("Music second cancel did not start exit");titlePhase=6;}
 else if(titlePhase==6&&!extrasExit){if(musicScreen->visible())throw std::runtime_error("Music second cancel close");QLIE_LOG_LINE(<<"MUSIC PASS original page / select / seek / loop / next / volume / stop / return\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Music test timeout");}continue;
 }if(extraTest){
 if(frames==1){if(!progress.extras()||!title->select(2))throw std::runtime_error("Persisted route did not unlock title");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=1;}
 else if(titlePhase==1&&title->ready()){if(!title->isExtraPage()||title->hit(1080,330)!=0||title->hit(1080,570)!=3)throw std::runtime_error("Original extra page layout");snapshot("extra-page.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);titlePhase=2;}
 else if(titlePhase==2&&title->ready()){if(title->isExtraPage()||!title->select(2))throw std::runtime_error("Extra cancel did not return");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=3;}
 else if(titlePhase==3&&title->ready()){title->select(3);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=4;}
 else if(titlePhase==4&&title->ready()){if(title->isExtraPage())throw std::runtime_error("Extra Back button");QLIE_LOG_LINE(<<"EXTRA PASS real persisted route / original submenu / B cancel / Back button\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Extra test timeout");}
 continue;
 }if(titleTest){
 if(frames==1){if(title->ready()||!audio->playing(0))throw std::runtime_error("Original title intro/music");snapshot("title-intro.bmp");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=1;}
 else if(titlePhase==1&&title->ready()){if(!menu||advances)throw std::runtime_error("Intro skip started story");snapshot("title-ready.bmp");controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);titlePhase=2;}
 else if(titlePhase==2){if(title->selection()!=1)throw std::runtime_error("Title load selection");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=3;}
 else if(titlePhase==3&&!gameMenu.panelTransitioning()){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("Original title load");controllerKey(SDL_CONTROLLER_BUTTON_B);titlePhase=4;}
 else if(titlePhase==4&&!gameMenu.visible()){controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);if(title->selection()!=3)throw std::runtime_error("Locked extra was not skipped");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=5;}
 else if(titlePhase==5&&!gameMenu.panelTransitioning()){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("Original title settings");controllerKey(SDL_CONTROLLER_BUTTON_B);titlePhase=6;}
 else if(titlePhase==6&&!gameMenu.visible()){controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=7;}
 else if(titlePhase==7){if(!gameMenu.asking||done)throw std::runtime_error("Title quit confirmation");snapshot("title-confirm.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);titlePhase=8;}
 else if(titlePhase==8){if(title->selection()!=0)throw std::runtime_error("Title start selection");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=9;}
 else if(titlePhase==9){if(menu||!advances)throw std::runtime_error("Original title did not start story");snapshot("title-start.bmp");QLIE_LOG_LINE(<<"TITLE PASS intro / music / locked extra / load / settings / quit cancel / start\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Title test timeout");}
 continue;
 }if(fpsTest){
 if(frames==1){if(fpsVisible)throw std::runtime_error("FPS must default hidden");snapshot("fps-hidden.bmp");controllerKey(SDL_CONTROLLER_BUTTON_BACK);}
 if(frames==70){if(!fpsVisible||fpsDisplay.fps()==0||fpsDisplay.fps()>60)throw std::runtime_error("FPS sample/toggle");snapshot("fps-visible.bmp");controllerKey(SDL_CONTROLLER_BUTTON_BACK);}
 if(frames==72){if(fpsVisible)throw std::runtime_error("Minus did not hide FPS");snapshot("fps-hidden-again.bmp");}
 if(++frames>=smoke){QLIE_LOG_LINE(<<"FPS PASS default hidden / Minus show-hide / actual sample="<<fpsDisplay.fps()<<"\n");done=true;}continue;
 }if(autoTest){
 if(frames==1){activate();settings.set("AutoTextWait",0);settings.set("AutoClickWait",0);settings.set("TextSpeed",0);applySettings();autoPlay.page(0);pointer(1170,700);skipStart=advances;autoPhase=1;}
 if(autoPhase==1&&frames>1){if(!autoPlay.enabled)throw std::runtime_error("Original auto toggle");if(advances>skipStart){openPanel(whj2::GameMenu::Settings);skipStart=advances;autoPhase=2;autoPhaseFrame=frames;}else if(frames>240)throw std::runtime_error("Auto did not advance after native wait");}
 if(autoPhase==2&&frames>=autoPhaseFrame+8&&!gameMenu.panelTransitioning()){if(advances!=skipStart)throw std::runtime_error("Auto advanced beneath settings");controllerKey(SDL_CONTROLLER_BUTTON_B);autoPhase=3;}
 if(autoPhase==3&&!gameMenu.visible()){pointer(1170,700);skipStart=advances;autoPhase=4;autoPhaseFrame=frames;}
 if(autoPhase==4&&frames>=autoPhaseFrame+8){if(autoPlay.enabled||advances!=skipStart)throw std::runtime_error("Auto toggle did not stop");save();auto before=autoPlay.save();load();if(autoPlay.save()!=before)throw std::runtime_error("Auto state restore");draw();snapshot("auto.bmp");QLIE_LOG_LINE(<<"AUTO PASS original toggle / native wait / progression / panel pause / cancel / save-load\n");done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Auto test timed out");}
 continue;
 }if(settingsTest){
 const auto& opacity=gameSettingsLayout.sliders[0];
 const auto& speed=gameSettingsLayout.sliders[1];
 const auto& master=gameSettingsLayout.sliders[2];
 const auto& bgm=gameSettingsLayout.sliders[3];
 auto tap=[&](float x,float y){pointer(int(x),int(y));};
 if(frames==1){activate();controllerKey(SDL_CONTROLLER_BUTTON_X);}
 if(frames==4){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("Settings entry");gameMenu.update(250);tap(master.x,master.y);if(settings.get("MasterVolume")!=0)throw std::runtime_error("Touch master volume");}
 if(frames==12)mutedNonzero=audio->stats().nonzero;
 if(frames==18){if(audio->stats().nonzero!=mutedNonzero)throw std::runtime_error("Muted mixer emitted samples");tap(master.x+master.width,master.y);if(settings.get("MasterVolume")!=100)throw std::runtime_error("Master gain restore");tap(speed.x+speed.width,speed.y);if(settings.get("TextSpeed")!=0)throw std::runtime_error("Text speed right edge");tap(opacity.x,opacity.y);if(settings.get("WindowAlpha")!=0)throw std::runtime_error("Window alpha");draw();snapshot("settings.bmp");}
 if(frames==20){gameMenu.focus=3;controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);if(settings.get("WindowAlpha")!=12)throw std::runtime_error("Controller slider increment");whj2::Settings restored(assets);restored.restore(qlie::file(saveDir+"/settings.cfg"));if(restored.save()!=settings.save())throw std::runtime_error("Settings persistence");controllerKey(SDL_CONTROLLER_BUTTON_B);}
 if(frames==23){if(gameMenu.closing())continue;if(messageText->getSpeed()!=0)throw std::runtime_error("Text speed not applied");snapshot("window-alpha.bmp");controllerKey(SDL_CONTROLLER_BUTTON_X);gameMenu.update(250);gameMenu.focus=12;controllerKey(SDL_CONTROLLER_BUTTON_A);}
 if(frames==25){if(!gameMenu.asking)throw std::runtime_error("Reset confirmation");controllerKey(SDL_CONTROLLER_BUTTON_A);whj2::Settings defaults(assets,false,gameSettingsLayout.classic);if(settings.save()!=defaults.save())throw std::runtime_error("Reset defaults");draw();snapshot("settings-reset.bmp");}
 if(frames==26){gameMenu.update(250);SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=bgm.x/1280;ev.tfinger.y=bgm.y/720;SDL_PushEvent(&ev);}
 if(frames==27){SDL_Event ev{};ev.type=SDL_FINGERMOTION;ev.tfinger.x=(bgm.x+bgm.width)/1280;ev.tfinger.y=bgm.y/720;SDL_PushEvent(&ev);}
 if(frames==28){if(settings.get("BGMVolume")!=100||!fingerUiDrag)throw std::runtime_error("Continuous slider drag");SDL_Event ev{};ev.type=SDL_FINGERMOTION;ev.tfinger.x=0;ev.tfinger.y=.8f;SDL_PushEvent(&ev);}
 if(frames==29){if(settings.get("BGMVolume")!=0)throw std::runtime_error("Slider clamp outside track");SDL_Event ev{};ev.type=SDL_FINGERUP;ev.tfinger.x=0;ev.tfinger.y=.8f;SDL_PushEvent(&ev);}
 if(frames==30){if(fingerUiDrag||!gameMenu.visible())throw std::runtime_error("Drag release activated another action");whj2::Settings saved(assets,settings.voiceActorCount()!=0);saved.restore(qlie::file(saveDir+"/settings.cfg"));if(saved.get("BGMVolume")!=0)throw std::runtime_error("Drag release not saved");settings.reset();applySettings();}
 if(frames==31)openPanel(whj2::GameMenu::History);
 if(frames==32){if(panelDraws!=1)throw std::runtime_error("History rendered twice");snapshot("history-once.bmp");}
 if(frames==33)controllerKey(SDL_CONTROLLER_BUTTON_B);
 if(++frames>=smoke){QLIE_LOG_LINE(<<"SETTINGS PASS mute / gain restore / speed / opacity / controller / persistence / reset\n");done=true;}continue;
 }if(touchOnlyTest){
 auto touch=[](float x,float y){SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=x;ev.tfinger.y=y;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());ev.type=SDL_FINGERUP;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());};
 auto controller=[](Uint8 button){SDL_Event ev{};ev.type=SDL_CONTROLLERBUTTONDOWN;ev.cbutton.button=button;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());};
 if(frames==1)controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
 if(frames==3){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("R did not open load panel");gameMenu.update(250);controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==5){if(gameMenu.panel!=whj2::GameMenu::Load||std::filesystem::exists(slotPrefix()+".story"))throw std::runtime_error("Empty slot load changed state");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==7){if(gameMenu.closing())continue;controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==14){gestureStory=story.save();controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==16){if(!hidden||story.save()!=gestureStory||menu)throw std::runtime_error("B did not hide text in place");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==18){if(hidden||story.save()!=gestureStory)throw std::runtime_error("A did not restore text in place");controller(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);}
 if(frames==20){if(gameMenu.panel!=whj2::GameMenu::Save||std::filesystem::exists(slotPrefix()+".story"))throw std::runtime_error("L saved without slot confirmation");gameMenu.update(250);snapshot("save-menu.bmp");touch(.25f,.25f);}
 if(frames==22){touchSavedStory=qlie::file(slotPrefix()+".story");touchSavedText=text;controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==23){if(gameMenu.closing())continue;controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);}
 if(frames==24){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("R did not open load");gameMenu.update(250);snapshot("load-menu.bmp");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==25){if(!gameMenu.asking)throw std::runtime_error("Load confirmation missing");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==26){
  if(pendingLoadSlot){
   if(++touchLoadWaitFrames>360)throw std::runtime_error("Load media preparation timeout");
   continue;
  }
  if(menuFadeLocksInput())continue;
  if(gameMenu.visible()||story.save()!=touchSavedStory||text!=touchSavedText)throw std::runtime_error("Selected slot load mismatch");
  controller(SDL_CONTROLLER_BUTTON_X);
 }
 if(frames==28){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("X did not open settings");gameMenu.update(250);snapshot("settings-menu.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==30){if(gameMenu.closing())continue;controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==32){if(gameMenu.panel!=whj2::GameMenu::History||history.empty())throw std::runtime_error("Up did not open populated history");snapshot("history-menu.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==34){if(gameMenu.closing())continue;gestureStory=story.save();controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==36){if(!upperMenu->visible()||story.save()!=gestureStory)throw std::runtime_error("Y changed story");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==38){gestureStory=story.save();SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=.5f;ev.tfinger.y=.2f;SDL_PushEvent(&ev);ev.type=SDL_FINGERUP;ev.tfinger.y=.5f;SDL_PushEvent(&ev);}
 if(frames==40){if(!upperMenu->visible()||story.save()!=gestureStory)throw std::runtime_error("Swipe changed story");snapshot("upper-menu.bmp");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==42){messageText->reveal();recordCurrentRead();skipStart=advances;SDL_Event ev{};ev.type=SDL_CONTROLLERAXISMOTION;ev.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;ev.caxis.value=32767;SDL_PushEvent(&ev);}
 if(frames==48){
  if(advances<=skipStart){
   if(!normalSkip||++touchSkipWaitFrames>360)throw std::runtime_error("ZR did not advance read page after its script wait");
   continue;
  }
  SDL_Event ev{};ev.type=SDL_CONTROLLERAXISMOTION;ev.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;ev.caxis.value=0;SDL_PushEvent(&ev);}
 if(frames==50){if(normalSkip)throw std::runtime_error("ZR did not stop at unread page");controller(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);}
 if(frames==52){gameMenu.update(250);controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);}
 if(frames==54){if(gameMenu.page!=1)throw std::runtime_error("Save page navigation");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==56){if(!std::filesystem::exists(slotPrefix(9)+".story"))throw std::runtime_error("Slot 9 missing");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==57){if(gameMenu.closing())continue;controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==58)controller(SDL_CONTROLLER_BUTTON_A);
 if(frames==60){if(!gameMenu.asking||done)throw std::runtime_error("Quit bypassed confirmation");snapshot("confirmation.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==62){if(gameMenu.visible())throw std::runtime_error("B did not cancel dialog");controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==64){if(history.size()<2||gameMenu.panel!=whj2::GameMenu::History)throw std::runtime_error("History lost previous pages");snapshot("history-menu.bmp");controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==66){if(gameMenu.historyOffset!=0||gameMenu.focus!=1)throw std::runtime_error("History row navigation");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==68){if(gameMenu.closing())continue;if(gameMenu.visible())throw std::runtime_error("History cancel");}
 if(++frames>=smoke){snapshot("touch.bmp");QLIE_LOG_LINE(<<"TARGET INPUT PASS L/R panels and slots / X settings / history / B text visibility / A confirm / Y and swipe / ZR read-toggle / cancel dialog\n");done=true;}continue;
 }if(frames==1 || (frames>=12 && frames%12==0)){SDL_Event input{};input.type=SDL_CONTROLLERBUTTONDOWN;input.cbutton.button=SDL_CONTROLLER_BUTTON_A;if(SDL_PushEvent(&input)<0)throw std::runtime_error(SDL_GetError());
 }if(frames==4)save();
 if(frames==5){auto before=story.save();
 std::string expected=text;
 load();
 if(story.save()!=before||text!=expected)throw std::runtime_error("Frontend save/load mismatch");
 }if(frames==smoke-4 && smoke>20){save();
 auto expected=images->save();
 qlie::Bytes before(1280*720*4,0);
 images->draw(before);
 images->clear();
 load();
 qlie::Bytes after(1280*720*4,0);
 images->draw(after);
 if(images->save()!=expected || before!=after)throw std::runtime_error("Scene restore pixel mismatch");
 QLIE_LOG_LINE(<<"SAVE image_state_and_pixels_equal\n");
 }if(++frames>=smoke){SDL_Surface*surface=SDL_CreateRGBSurfaceFrom(pixels.data(),1280,720,32,1280*4,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
 if(!surface||SDL_SaveBMP(surface,(saveDir+"/smoke.bmp").c_str()))throw std::runtime_error("Cannot save smoke frame");
 SDL_FreeSurface(surface);
 done=true;
 }}else {frames++;}
 }
 }
#ifdef WHJ2_WITH_GPU
 if(gpu&&gpu->nativePresentation())gpu->finishPresentation();
#endif
 flushProgress();
 if constexpr (qlie::diagnostics){
  if((cgTest||replayTest||musicTest)&&(!extrasZoomCaptured[0]||!extrasZoomCaptured[1]))
   throw std::runtime_error("Extras zoom frames were not rendered");
  const auto ast=audio->stats();
  QLIE_LOG_LINE(<<"AUDIO output_frames="<<ast.frames<<" nonzero_frames="<<ast.nonzero<<"\n");
 }
 QLIE_LOG_LINE(<<"FRAME cap="<<whj2::FramePacer::Limit<<" min_interval_ms="<<framePacer.minimumMilliseconds()<<"\n");
 QLIE_LOG_LINE(<<"EXIT normal text_advances="<<advances<<" frames="<<frames<<"\n");
 return 0;
 }
};
#ifndef WHJ2_BUILD_REV
#define WHJ2_BUILD_REV "unversioned"
#endif
extern "C" const char qlie_build_revision[]=WHJ2_BUILD_REV;
int main(int argc,char**argv){
 if constexpr (qlie::diagnostics){
  const char*logPath=argc<2?std::getenv(game_profile::logEnv):nullptr;
  if(logPath){
   if(auto*file=std::fopen(logPath,"a")){
    std::fclose(file);
    if(std::freopen(logPath,"a",stderr))std::setvbuf(stderr,nullptr,_IONBF,0);
   }
  }
 }else kvideo_silence_library_logs();
try{whj2::SwitchStorage storage;
#ifdef __SWITCH__
 fsdevMountSdmc();
#endif
std::string game=argc>1?argv[1]:(std::getenv(game_profile::gameEnv)?std::getenv(game_profile::gameEnv):"");
#ifdef __SWITCH__
if(game.empty())game=game_profile::sdGame;
#endif
if(game.empty()){QLIE_LOG_LINE(<<"usage: qlie-switch-runtime GAME [SAVE_DIR] [--smoke-frames N]\n");
 return 2;
 }unsigned smoke=0;
 std::string saves=argc>2?argv[2]:(std::getenv(game_profile::saveEnv)?std::getenv(game_profile::saveEnv):game_profile::hostSaves);
 if(qlie::diagnostics&&argc>4&&std::string(argv[3])=="--smoke-frames")smoke=unsigned(std::stoul(argv[4]));
#ifdef __SWITCH__
if(argc<3)saves=game_profile::sdSaves;
#endif
storage.mount(game,saves);
#ifdef WHJ2_MAC_GAME_PICKER
const auto*keyPath=game_profile::archives.fileKey.c_str();
if(!whj2_mac_can_read_game(game.c_str(),keyPath)){
 char *chosen=whj2_mac_choose_game(game.c_str(),keyPath);
 if(!chosen)return 0;
 game=chosen;
 std::free(chosen);
}
#endif
QLIE_LOG_LINE(<<"BUILD revision="<<qlie_build_revision<<"\n");
QLIE_LOG_LINE(<<"INIT game="<<game<<" saves="<<saves<<"\n");
struct VideoHardwareScope {~VideoHardwareScope(){kvideo_release_hardware();}} videoHardware;
Frontend f(game,saves);if(argc>4&&std::string(argv[3])=="--restore-autosave"){unsigned index=unsigned(std::stoul(argv[4]));if(index<1||index>Frontend::AutoSaveCount)throw std::runtime_error("Auto save index must be1..5");auto slot=Frontend::AutoSaveBase+index-1;if(!std::filesystem::exists(f.slotPrefix(slot)+".story"))throw std::runtime_error("Auto save slot is empty");f.opening=0;if(!f.load(slot))throw std::runtime_error(f.status);}
 if constexpr (qlie::diagnostics){
 if(argc>4&&std::string(argv[3])=="--start-transition-smoke"){f.startTransitionTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--read-skip-smoke"){f.readSkipTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--replay-return-smoke"){f.replayReturnTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--cg-loop-smoke"){f.cgLoopTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--cg-smoke"){f.cgTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--replay-smoke"){f.replayTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--title-view-smoke"){f.viewTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--title-smoke"){f.titleTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--opening-smoke"){f.openingTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--music-smoke"){f.musicTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--extra-smoke"){f.extraTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--fps-smoke"){f.fpsTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--auto-smoke"){f.autoTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--settings-smoke"){f.settingsTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--touch-smoke"){f.touchOnlyTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--movie"){f.movieOnly=true;f.opening=0;
 f.audio->stopAll();
 f.menu=false;
 f.movie->open(argv[4]);
 }else if(argc>4&&std::string(argv[3])=="--movie-file"){f.movieOnly=true;f.opening=0;
 f.audio->stopAll();
 f.menu=false;
 f.movie->openBytes(qlie::file(argv[4]),"local-test");
 }
 }
 return f.run(smoke);
 }catch(const std::exception&e){QLIE_LOG_LINE(<<"FAIL "<<e.what()<<"\n");
 return 1;
 }}
