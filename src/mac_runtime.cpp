#ifdef WHJ2_WITH_VULKAN
#include "vulkan_raster.hpp"
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
// Incremental native frontend: graphics/audio integration remains in progress.
#include "story.hpp"
#include "audio.hpp"
#include "switch_threads.hpp"
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
#include <tuple>
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
struct Frontend {
#ifdef __SWITCH__
 whj2::JoyCon joycon;
#endif
 qlie::Assets assets;
 whj2::Story story;
 whj2::Progress progress;
 std::unique_ptr<whj2::Audio>audio;
 std::unique_ptr<whj2::SceneImages>images;
 std::unique_ptr<whj2::AssetReadAhead>readAhead;
 std::unique_ptr<whj2::SceneTransition>transition;
 whj2::SentenceWait sentenceWait;
 whj2::AutoPlay autoPlay;
 std::unique_ptr<whj2::MessageText>messageText;
 size_t lastMessageVisible=SIZE_MAX;
 std::string pendingBody,pendingSpeaker,pendingVoice;
 std::unique_ptr<whj2::Movie>movie;
 std::unique_ptr<whj2::VoiceResolver>voices;
 std::unique_ptr<whj2::UpperMenu>upperMenu;
 std::unique_ptr<whj2::TitleScreen>title;
 std::unique_ptr<whj2::ChoiceScreen>choiceScreen;
 std::unique_ptr<whj2::MusicScreen>musicScreen;
 std::unique_ptr<whj2::CgScreen>cgScreen;
 std::unique_ptr<whj2::ReplayScreen>replayScreen;bool replayActive=false;unsigned replayPage=0;
 whj2::Settings settings{assets};
 whj2::GameMenu gameMenu{assets,settings};
 std::vector<whj2::HistoryEntry>history;bool historyVoicePlayed=false;int historyVoiceCacheOffset=-1;
 std::array<std::unique_ptr<whj2::MessageText>,4> historyText;
 std::array<whj2::Image,4> historyTextImages;
 std::array<int,4> historyRenderedLine{{-1,-1,-1,-1}};
 unsigned fontGeneration=0,historyFontGeneration=UINT32_MAX;size_t historyEntryCount=SIZE_MAX;
 bool normalSkip=false,skipReadOnly=true,leftTrigger=false,rightTrigger=false,currentPageWasRead=false;std::string currentPageKey;
 unsigned selectedSlot=1;unsigned skipStart=0;unsigned autoPhase=0,autoPhaseFrame=0,titlePhase=0;
 bool manualSlotsScanned=false;unsigned latestManualSlot=1;int saveThumbnailPriority=999;
 bool fingerActive=false,fingerUiDrag=false;SDL_FingerID fingerId=0;float fingerX=0,fingerY=0;qlie::Bytes gestureStory;
 bool movieOnly=false,movieCaptured=false,touchOnlyTest=false,settingsTest=false,autoTest=false,fpsTest=false,titleTest=false,extraTest=false,musicTest=false,openingTest=false,viewTest=false,replayTest=false,cgTest=false,cgLoopTest=false,replayReturnTest=false,readSkipTest=false;
 unsigned opening=0;double openingElapsed=0;bool openingLogoCaptured=false;uint64_t openingTitleStarted=0;unsigned openingTitleCaptures=0;float titleAxis=0,titleAxisY=0,titlePointer=640;
 uint64_t mutedNonzero=0;
 qlie::Bytes touchSavedStory;std::string touchSavedText;
 std::map<std::string,std::map<std::string,std::string>>audioTemplates;
 std::array<float,3>volumes{{0.4f,1.f,1.f}};
 std::array<bool,3>loops{{true,false,false}};
 std::array<unsigned,3>audioTimes{{1500,0,0}};
 std::array<unsigned,3>audioTypes{{0,2,2}};
 bool cpuInterlaced=false;unsigned cpuScanlinePhase=0;qlie::Bytes cpuScanlineHistory;
 KFont*font=nullptr;
 struct FontChoice {std::string file,label;unsigned face=0;};
 std::vector<FontChoice> fontChoices{{"","標準フォント",0}};int selectedFont=0;qlie::Bytes fontSettingsSnapshot;
 SDL_Window*window=nullptr;
 SDL_Renderer*renderer=nullptr;
 SDL_Texture*texture=nullptr;
#ifdef WHJ2_WITH_VULKAN
 std::unique_ptr<whj2::VulkanRaster>vulkan;
#endif
 qlie::Bytes pixels;
 whj2::RenderTarget*frameTarget=nullptr;
 struct CachedGlyph {KFontGlyphBounds bounds{};whj2::Image image;};
 std::map<std::tuple<uint32_t,unsigned,unsigned,uint32_t>,CachedGlyph>uiGlyphs;
 struct SlotPreview {bool exists=false,damaged=false;whj2::Image thumbnail;std::vector<uint32_t>excerpt;};
 std::map<unsigned,SlotPreview>slotPreviews;
 std::string text,name,status="",saveDir,gameDir;
 bool menu=true,done=false,hidden=false,fpsVisible=false;
 int focus=0;
 unsigned advances=0,panelDraws=0;
 whj2::FramePacer framePacer;
 whj2::FpsOverlay fpsDisplay;
 std::map<SDL_JoystickID,SDL_GameController*>controllers;
 Frontend(const std::string&game,const std::string&saves):assets(game),story(assets),pixels(1280*720*4),saveDir(saves),gameDir(game){unsigned subsystems=SDL_INIT_AUDIO|SDL_INIT_EVENTS;
 bool nativeRequested=false;
#ifdef WHJ2_WITH_VULKAN
 const char*gpuMode=std::getenv("WHJ2_RENDERER");
#ifdef __SWITCH__
 nativeRequested=!gpuMode||std::string(gpuMode)!="cpu";
#endif
#endif
 if(!nativeRequested)subsystems|=SDL_INIT_VIDEO;
#ifndef __SWITCH__
 subsystems|=SDL_INIT_GAMECONTROLLER;
#endif
 if(SDL_Init(subsystems))throw std::runtime_error(SDL_GetError());
 if(!nativeRequested){window=SDL_CreateWindow("美少女万華鏡２",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);if(!window)throw std::runtime_error(SDL_GetError());}
#ifdef WHJ2_WITH_VULKAN
#ifdef __SWITCH__
 if(nativeRequested)vulkan=std::make_unique<whj2::VulkanRaster>();
#else
 if(gpuMode&&std::string(gpuMode)=="vulkan")vulkan=std::make_unique<whj2::VulkanRaster>();
#endif
#endif

 bool nativeVulkan=false;
#ifdef WHJ2_WITH_VULKAN
 nativeVulkan=vulkan&&vulkan->nativePresentation();
#endif
 if(!nativeVulkan){
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
 if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!renderer)throw std::runtime_error(SDL_GetError());
 SDL_RenderSetLogicalSize(renderer,1280,720);
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,1280,720);
 }
 font=kfont_open(nullptr,0);
 if(!font||(!nativeVulkan&&!texture))throw std::runtime_error("Font/texture initialization failed");
 std::filesystem::create_directories(saveDir);
 {
  std::istringstream values(qlie::cp932(assets.read("avg/setting.txt")));std::string line;
  while(std::getline(values,line)){auto pos=line.find('=');if(pos!=line.npos&&whj2::trim(line.substr(0,pos))=="SaveThumPriority")saveThumbnailPriority=std::stoi(whj2::trim(line.substr(pos+1)));}
 }
 audio=std::make_unique<whj2::Audio>();
 images=std::make_unique<whj2::SceneImages>(assets);
#ifdef __SWITCH__
 readAhead=std::make_unique<whj2::AssetReadAhead>();
 images->setReadAhead(readAhead.get());audio->setReadAhead(readAhead.get());
#endif
 messageText=std::make_unique<whj2::MessageText>(assets,true);
 refreshFontPreviews();
 transition=std::make_unique<whj2::SceneTransition>(assets);
 movie=std::make_unique<whj2::Movie>(assets,*audio);
 voices=std::make_unique<whj2::VoiceResolver>(assets);
 upperMenu=std::make_unique<whj2::UpperMenu>(assets);
 title=std::make_unique<whj2::TitleScreen>(assets);
 choiceScreen=std::make_unique<whj2::ChoiceScreen>(assets);
 musicScreen=std::make_unique<whj2::MusicScreen>(assets,*audio,settings,font);
 replayScreen=std::make_unique<whj2::ReplayScreen>(assets,progress);
 cgScreen=std::make_unique<whj2::CgScreen>(assets,progress,font);
 if(std::filesystem::exists(saveDir+"/system.progress"))progress.restore(qlie::file(saveDir+"/system.progress"));
 if(std::filesystem::exists(saveDir+"/settings.cfg"))settings.restore(qlie::file(saveDir+"/settings.cfg"));
 loadFontSelection();
 applySettings(false);
 for(auto type:{"music","sound"}){std::istringstream in(qlie::cp932(assets.read(std::string("avg/imoscripter_template_")+type+".txt")));
 std::string l;
 while(std::getline(in,l)){auto p=l.find(61);
 if(p!=l.npos)audioTemplates[type][whj2::trim(l.substr(0,p))]=whj2::trim(l.substr(p+1));
 }}
 if(readAhead){
  title->prepareOpening(readAhead.get());
  audio->warmFile(assets,"music/wbgm06.ogg");
 }
 opening=1;images->command("^ui_caution,file:script/caution/caution.b,x:0,y:0");
 std::cerr<<"OPENING caution\n";
 }
 ~Frontend(){
 readAhead.reset();
#ifdef WHJ2_WITH_VULKAN
 vulkan.reset();
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
 void syncProgress(){if(progress.capture(story))qlie::write_file(saveDir+"/system.progress",progress.save());}
 void makerLogo(){opening=2;images->clear();movie->open("script/makerlogo/omegastar.ogv");std::cerr<<"OPENING maker-logo\n";}
 void warmNextScene(bool openingHint=false){
  if(!readAhead)return;
  struct Hint {std::string path;bool audio;};std::vector<Hint> hints;std::vector<std::string> keys;
  auto add=[&](const std::string&path,bool sound){
   auto key=qlie::canonical(path);
   if(hints.size()<6&&std::find(keys.begin(),keys.end(),key)==keys.end()){keys.push_back(key);hints.push_back({path,sound});}
  };
  std::vector<std::string> upcoming;
  try{upcoming=story.upcomingAssetCommands(openingHint);}
  catch(const std::runtime_error&e){std::fprintf(stderr,"LOAD read-ahead script: %s\n",e.what());return;}
  for(auto&line:upcoming){
   if(line.rfind("％",0)==0){
    if(!normalSkip||voices->forcePlay)
     if(auto voice=voices->resolve(line))add(voice->request,true);
    if(hints.size()==6)break;
    continue;
   }
   auto comma=line.find(',');auto cmd=line.substr(0,comma);
   if(cmd=="^music01"||cmd=="^se01"||cmd=="^se02"){
    for(auto&part:whj2::split(line.substr(comma+1),','))if(part.rfind("file:",0)==0){
     auto name=part.substr(5);if(name.empty()||name=="none"||name.find('$')!=name.npos)continue;
     auto path=std::string(cmd=="^music01"?"music/":"se/")+name;if(!assets.inPack(path))path+=".ogg";
     if(assets.inPack(path))add(path,true);
    }
   }else for(auto&path:images->warmPaths(line))add(path,false);
   if(hints.size()==6)break;
  }
  readAhead->retain(keys);
  for(auto&h:hints){if(h.audio)audio->warmFile(assets,h.path);else images->warmFile(h.path);}
 }
 void showTitle(){normalSkip=false;currentPageKey.clear();cgScreen->close();replayActive=false;replayScreen->close();opening=0;images->clear();syncProgress();menu=true;hidden=false;focus=0;upperMenu->close();gameMenu.close();
  title->reset(false,progress.extras());audio->stopAll();audio->playFile(assets,0,"music/wbgm06.ogg",true,255.f/256,0,0);warmNextScene(true);
 }
 void refreshFontPreviews(){
  ++fontGeneration;
  whj2::MessageText preview(assets);whj2::MessageStyle style{400,50,20,20,0xffffff};
  preview.start("[spd,0]   美 少 女 万 華 鏡");auto sample=preview.render(font,style);
  preview.start("[spd,0]「サンプルテキスト」");gameMenu.setTextPreviews(std::move(sample),preview.render(font,style));
 }
 void saveFontChoice(int index){
  whj2::StateWriter writer;writer.str("WHJ2-font-2");writer.str(fontChoices[index].file);writer.n(fontChoices[index].face);qlie::write_file(saveDir+"/font.cfg",writer.bytes);
 }
 bool selectFont(int index,bool persist){
  if(index<0||size_t(index)>=fontChoices.size())return false;
  if(index==selectedFont){if(persist)saveFontChoice(index);return true;}
  const auto&choice=fontChoices[index];std::string path=index?gameDir+"/fonts/"+choice.file:std::string();
  std::unique_ptr<KFont,void(*)(KFont*)> nextFont(kfont_open_face(index?path.c_str():nullptr,choice.face,0),kfont_close);
  if(!nextFont){status="フォントを読み込めませんでした。";std::cerr<<"FONT FAIL file="<<choice.file<<" face="<<choice.face<<"\n";return false;}
  if(persist)saveFontChoice(index);
  auto*previous=font;font=nextFont.release();selectedFont=index;kfont_set_antialias(font,1u<<settings.get("FontAA"));kfont_set_style(font,unsigned(settings.get("FontStyle")),uint32_t(settings.get("FontStyleColor")));uiGlyphs.clear();musicScreen->setFont(font);cgScreen->setFont(font);
  refreshFontPreviews();if(!text.empty())refreshMessage();
  if(story.state.awaiting){auto savedChoice=choiceScreen->save();choiceScreen->restore(savedChoice,font);}
  kfont_close(previous);return true;
 }
 void loadFontSelection(){
  try{
   auto directory=std::filesystem::path(gameDir)/"fonts";std::vector<std::string> files;
   if(std::filesystem::is_directory(directory))for(auto&entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()){
    auto ext=entry.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext==".ttf"||ext==".otf"||ext==".ttc")files.push_back(entry.path().filename().string());
   }
   std::sort(files.begin(),files.end());
   for(const auto&file:files){auto path=(directory/file).string();char label[512];int count=kfont_face_info(path.c_str(),0,label,sizeof(label));
    if(count<1){std::cerr<<"FONT CATALOG FAIL file="<<file<<"\n";continue;}
    for(int i=0;i<count;i++){if(i&&kfont_face_info(path.c_str(),unsigned(i),label,sizeof(label))<1){std::cerr<<"FONT CATALOG FAIL file="<<file<<" face="<<i<<"\n";continue;}fontChoices.push_back({file,std::string(label)+" ["+file+" #"+std::to_string(i)+"]",unsigned(i)});}
   }
   if(std::filesystem::exists(saveDir+"/font.cfg")){
    auto bytes=qlie::file(saveDir+"/font.cfg");std::string name(bytes.begin(),bytes.end());uint64_t face=0;
    whj2::StateWriter signature;signature.str("WHJ2-font-2");
    if(bytes.size()>=signature.bytes.size()&&std::equal(signature.bytes.begin(),signature.bytes.end(),bytes.begin())){whj2::StateReader reader{bytes};reader.str();name=reader.str();face=reader.n();reader.end();}
    auto at=std::find_if(fontChoices.begin(),fontChoices.end(),[&](const FontChoice&choice){return choice.file==name&&choice.face==face;});
    if(at!=fontChoices.end())selectFont(int(at-fontChoices.begin()),false);else {status="保存されたフォントが見つかりません。";std::cerr<<"FONT selection unavailable\n";}
   }
  }catch(const std::exception&e){status="フォント設定を読み込めませんでした。";std::cerr<<"FONT CONFIG FAIL "<<e.what()<<"\n";}
 }
 void applySettings(bool persist=true){
 int fontChanged=kfont_set_antialias(font,1u<<settings.get("FontAA"));fontChanged|=kfont_set_style(font,unsigned(settings.get("FontStyle")),uint32_t(settings.get("FontStyleColor")));
 if(fontChanged>0){uiGlyphs.clear();refreshFontPreviews();if(!text.empty())refreshMessage();if(story.state.awaiting){auto state=choiceScreen->save();choiceScreen->restore(state,font);}}
 bool interlace=settings.get("Interlace")!=0;bool cpuMode=interlace;
#ifdef WHJ2_WITH_VULKAN
 if(vulkan){vulkan->setInterlaced(interlace);cpuMode=false;}
#endif
 if(cpuInterlaced!=cpuMode){cpuInterlaced=cpuMode;if(cpuMode)cpuScanlineHistory=pixels;else cpuScanlineHistory.clear();}
 audio->setUserGains(settings.gains());images->setWindowAlpha(unsigned(settings.get("WindowAlpha")));messageText->setSpeed(settings.get("TextSpeed"));if(!text.empty())refreshMessage();if(persist&&gameMenu.panel!=whj2::GameMenu::Fonts)qlie::write_file(saveDir+"/settings.cfg",settings.save());gameMenu.settingsDirty=false;}
 void drawGlyph(uint32_t cp,int x,int y,unsigned width,unsigned height,uint32_t color){
 if(!frameTarget){kfont_draw(font,newImage(),cp,x,y,width,height,color);return;}
 auto key=std::make_tuple(cp,width,height,color);auto at=uiGlyphs.find(key);
 if(at==uiGlyphs.end()){CachedGlyph glyph;if(kfont_glyph_bounds(font,cp,width,height,&glyph.bounds))return;auto&b=glyph.bounds;glyph.image={b.width,b.height,qlie::Bytes(size_t(b.width)*b.height*4)};if(b.width&&b.height){KImage dst{0,0,b.width,b.height,size_t(b.width)*4,glyph.image.pixels.data()};kfont_draw(font,&dst,cp,-b.left,-b.top,width,height,color);}glyph.image.version.publish();at=uiGlyphs.emplace(key,std::move(glyph)).first;}
 auto&glyph=at->second;frameTarget->font(glyph.image,x+glyph.bounds.left,y+glyph.bounds.top);
 }
 void line(const std::string&s,int x,int y,int size=26,uint32_t color=0xffffff){
 int start=x;
 for(auto cp:utf8(s)){int width=cp<128?size/2:size;
 if(cp==10||x+width>1230){x=start;
 y+=size+10;
 if(cp==10)continue;
 }if(y+size>710)break;
 drawGlyph(cp,x,y,width,size,color);
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
 if(cmd!="music01"&&cmd!="se01"&&cmd!="se02")return false;
 unsigned ch=cmd=="music01"?0:cmd=="se01"?1:2;
 std::string type=ch==0?"music":"sound";
 auto tokens=whj2::split(expandAudio(p==line.npos?"":line.substr(p+1),type),44);
 std::map<std::string,std::string>attrs;
 for(auto&t:tokens){if(t.empty())continue;
 auto c=t.find(58);
 if(c==t.npos)throw std::runtime_error("Unknown audio template "+t);
 attrs[t.substr(0,c)]=t.substr(c+1);
 }for(auto&v:attrs)if(v.first!="file"&&v.first!="loop"&&v.first!="vol"&&v.first!="voltype"&&v.first!="time"&&v.first!="clear")throw std::runtime_error("Unsupported audio property "+v.first);
 if(attrs.count("clear")){audio->volume(ch,0,0,true);if(ch==0)audio->volume(whj2::Audio::MusicPreviousChannel,0,0,true);
 return true;
 }if(attrs.count("loop"))loops[ch]=attrs["loop"]=="true";
 if(attrs.count("vol"))volumes[ch]=std::stof(attrs["vol"])/100.f*(ch==0?0.4f:1.f);
 if(attrs.count("time"))audioTimes[ch]=unsigned(std::stoul(attrs["time"]));
 if(attrs.count("voltype")){auto type=attrs["voltype"];if(type=="bgm")audioTypes[ch]=0;else if(type=="voice")audioTypes[ch]=1;else if(type=="se")audioTypes[ch]=2;else std::cerr<<"PENDING audio volume type "<<type<<"\n";}
 unsigned ms=audioTimes[ch];
 if(attrs.count("file")){auto f=attrs["file"];
 if(f=="none"){audio->volume(ch,0,ms,true);if(ch==0)audio->volume(whj2::Audio::MusicPreviousChannel,0,ms,true);
 return true;
 }std::string path=(ch==0?"music/":"se/")+f;
 if(!assets.exists(path))path+=".ogg";
 if(ch==0)audio->playMusicFile(assets,path,loops[ch],volumes[ch],ms,audioTypes[ch],attrs.count("time")!=0);
 else audio->playFile(assets,ch,path,loops[ch],volumes[ch],ms,audioTypes[ch]);
 }else audio->volume(ch,volumes[ch],ms);
 return true;
 }
 void playVoice(const std::string&id,bool historyReplay=false){
 if(!historyReplay&&!voices->forcePlay&&normalSkip){audio->stop(3);return;}
 if(id.empty())return;
 auto match=voices->resolve(id);
 if(!match){std::cerr<<"VOICE unresolved id="<<id;for(auto&candidate:voices->candidates(id))std::cerr<<" candidate="<<candidate;std::cerr<<"\n";if(voices->fileErrors)throw std::runtime_error("Voice file not found: "+id);return;}
 audio->playFile(assets,3,match->request,false,voices->gain,0,1);
 std::cerr<<"VOICE playing id="<<id<<" source="<<match->source.path<<" offset="<<match->source.offset<<" stored_bytes="<<match->source.size<<" loose="<<match->source.loose<<"\n";
 }
 void refreshMessage(bool show=false){images->setMessageImage(messageText->render(font,images->messageStyle()),show);lastMessageVisible=messageText->visible();}
 void recordCurrentRead(){if(!menu&&!hidden&&!gameMenu.visible()&&!movie->playing()&&images->messageVisible()&&!currentPageKey.empty()&&messageText->finished()&&progress.markRead(currentPageKey))qlie::write_file(saveDir+"/system.progress",progress.save());}
 void toggleAuto(){if(menu||movie->playing()||gameMenu.visible()||upperMenu->visible()||story.state.awaiting)return;autoPlay.enabled=!autoPlay.enabled;normalSkip=false;hidden=false;uiSound("Sys_OnClick");}
 void toggleSkip(){if(menu||movie->playing()||gameMenu.visible()||upperMenu->visible()||story.state.awaiting)return;if(normalSkip){normalSkip=false;return;}autoPlay.enabled=false;skipReadOnly=settings.get("SkipMode")==1;normalSkip=!skipReadOnly||progress.wasRead(currentPageKey);if(normalSkip){autoPlay.enabled=false;hidden=false;}}

 void next(bool resume=false){if(menu||story.state.finished)return;
  using LoadClock=std::chrono::steady_clock;
  struct PageTiming {
   whj2::AssetReadAhead* loader;
   whj2::AssetReadAhead::Stats start;
   LoadClock::time_point begin;
   double capture=0;
   ~PageTiming(){
    if(!loader)return;
    double ms=std::chrono::duration<double,std::milli>(LoadClock::now()-begin).count();
    if(ms<1000.0/60)return;
    auto end=loader->stats();
    std::fprintf(stderr,"LOAD page_ms=%.3f capture_ms=%.3f ready=%u waited=%u urgent=%u cold=%u\n",
                 ms,capture,end.ready-start.ready,end.waited-start.waited,end.urgent-start.urgent,end.cold-start.cold);
   }
  } timing{readAhead.get(),readAhead?readAhead->stats():whj2::AssetReadAhead::Stats{},LoadClock::now()};
 if(!resume){recordCurrentRead();currentPageKey.clear();images->beginPage();pendingBody.clear();pendingSpeaker.clear();pendingVoice.clear();}
 auto&body=pendingBody;auto&speaker=pendingSpeaker;auto&voiceId=pendingVoice;
 for(unsigned guard=0;guard<10000;guard++){auto e=story.next();syncProgress();
 if(e.type=="choice"){normalSkip=false;sentenceWait.cancel();transition->present();focus=0;choiceScreen->show(story.state.choices,font);
 text=body.empty()?"選択してください":body;
 messageText->start(text);messageText->reveal();refreshMessage(true);
 name=speaker;
 images->selectFace(speaker,voiceId);
 return;
 }if(e.type=="page"){if(body.empty())continue;
 transition->present();
 text=body;
  currentPageKey=qlie::canonical(e.file)+":"+std::to_string(e.line);currentPageWasRead=progress.wasRead(currentPageKey);if(skipReadOnly&&!currentPageWasRead)normalSkip=false;
 messageText->start(text);autoPlay.page(settings.get("AutoClickWait"));refreshMessage(true);
 name=speaker;
 history.push_back({name,text,voiceId});
 images->selectFace(speaker,voiceId);
 playVoice(voiceId);
 sentenceWait.page(true,audio->playing(3));
 advances++;warmNextScene();
 std::cerr<<"PAGE "<<e.file<<":"<<e.line<<"\n";
 return;
 }if(e.type=="finish"||e.type=="finished"){if(replayActive){openReplay(replayPage);return;}showTitle();
 text="";
 status="物語の終了地点に到達しました。";
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
 }if(e.type=="command"&&e.value.rfind("^movie,",0)==0){auto p=whj2::trim(e.value.substr(7));
 if(p.size()>1&&p.front()=='"'&&p.back()=='"')p=p.substr(1,p.size()-2);
 movie->open(p);
 text="";
 name="";
 return;
 }if(e.type=="command"&&e.value.rfind("^sentence",0)==0){
  transition->commandWithCapture(e.value,[this,&timing]{
   auto start=LoadClock::now();
   qlie::Bytes scene(1280*720*4);
#ifdef WHJ2_WITH_VULKAN
   if(vulkan&&vulkan->nativePresentation()){
    vulkan->begin(0xff141923u);images->drawScene(*vulkan);transition->blend(*vulkan);vulkan->end(scene);
    timing.capture+=std::chrono::duration<double,std::milli>(LoadClock::now()-start).count();return scene;
   }
#endif
   for(size_t i=0;i<scene.size();i+=4){scene[i]=35;scene[i+1]=25;scene[i+2]=20;scene[i+3]=255;}
   images->drawScene(scene);transition->blend(scene);
   timing.capture+=std::chrono::duration<double,std::milli>(LoadClock::now()-start).count();return scene;
  },&sentenceWait);continue;
 }if(e.type=="wait"||e.type=="click-wait"){sentenceWait.native(e.type=="click-wait",e.value.empty()?0:std::stod(e.value));transition->present();warmNextScene();return;
 }if(e.type=="command"&&e.value.rfind("^savefilter",0)==0){auto fields=whj2::split(e.value,',');if(fields.size()!=2||fields[1]!="0")throw std::runtime_error("Unsupported save thumbnail filter");continue;
 }if(e.type=="command"&&(audioCommand(e.value)||images->command(e.value)))continue;
 if(e.type=="create"||e.type=="free"){audio->stopAll();
 images->clear();
 transition->reset();
 sentenceWait=whj2::SentenceWait{};
 messageText->start("");
 }if(e.type=="command"||e.type=="create"||e.type=="free"||e.type=="wait"||e.type=="audio-clear"||e.type=="audio-volume"){
  // Preserve every unrendered event in the frontend trace; these are explicitly
  // pending rendering work, never counted as multimedia acceptance.
  std::cerr<<"PENDING-PRESENT "<<e.type<<" "<<e.file<<":"<<e.line<<"\n";
 continue;
 }throw std::runtime_error("Unhandled frontend event "+e.type);
 }throw std::runtime_error("Frontend event budget");
 }
 static constexpr unsigned AutoSaveBase=121,AutoSaveCount=5;
 unsigned newestManualSlot(){if(manualSlotsScanned)return latestManualSlot;latestManualSlot=1;auto latest=std::filesystem::file_time_type::min();for(unsigned i=1;i<AutoSaveBase;i++){auto path=slotPrefix(i)+".story";if(!std::filesystem::exists(path))continue;auto time=std::filesystem::last_write_time(path);if(time>latest){latest=time;latestManualSlot=i;}}manualSlotsScanned=true;return latestManualSlot;}
 std::string slotPrefix(unsigned slot=1)const{if(slot>=AutoSaveBase){if(slot>=AutoSaveBase+AutoSaveCount)throw std::runtime_error("Auto save slot range");char number[32];std::snprintf(number,sizeof(number),"/autosave/slot%02u",slot-AutoSaveBase+1);return saveDir+number;}char number[16];std::snprintf(number,sizeof(number),"/slot%02u",slot);return saveDir+number;}
 void save(unsigned slot=1){if(replayActive||menu||movie->playing())return;
 qlie::Bytes scene(1280*720*4,0);images->drawThumbnail(scene,float(saveThumbnailPriority));transition->blend(scene);qlie::Bytes thumb(160*90*4);for(unsigned y=0;y<90;y++)for(unsigned x=0;x<160;x++)for(unsigned c=0;c<4;c++)thumb[(y*160+x)*4+c]=scene[((y*8)*1280+x*8)*4+c];qlie::write_file(slotPrefix(slot)+".thumbnail",thumb);
 auto b=story.save();
 qlie::write_file(slotPrefix(slot)+".story",b);
 qlie::Bytes t(text.begin(),text.end());
 qlie::write_file(slotPrefix(slot)+".text",t);
 qlie::Bytes n(name.begin(),name.end());
 qlie::write_file(slotPrefix(slot)+".name",n);
 qlie::write_file(slotPrefix(slot)+".images",images->save());
 qlie::write_file(slotPrefix(slot)+".transition",transition->save());
 whj2::StateWriter flow;flow.str("WHJ2-flow-2");auto waitState=sentenceWait.save();flow.str(std::string(waitState.begin(),waitState.end()));flow.str(pendingBody);flow.str(pendingSpeaker);flow.str(pendingVoice);flow.str(currentPageKey);qlie::write_file(slotPrefix(slot)+".flow",flow.bytes);
 qlie::write_file(slotPrefix(slot)+".message",messageText->save());
 qlie::write_file(slotPrefix(slot)+".auto",autoPlay.save());
 if(story.state.awaiting)qlie::write_file(slotPrefix(slot)+".choice",choiceScreen->save());
 qlie::write_file(slotPrefix(slot)+".audio",audio->save());
 whj2::StateWriter settings;
 settings.str("WHJ2-mixer-2");
 for(unsigned i=0;i<3;i++){settings.f(volumes[i]);
 settings.n(loops[i]);
 settings.n(audioTimes[i]);settings.n(audioTypes[i]);
 }qlie::write_file(slotPrefix(slot)+".mixer",settings.bytes);
 qlie::write_file(slotPrefix(slot)+".history",whj2::saveHistory(history));
 slotPreviews.erase(slot);if(slot<AutoSaveBase){latestManualSlot=slot;manualSlotsScanned=true;}status="現在の状態を保存しました。";
 }
 void refreshSlotFlags(){using UI=whj2::GameMenu;if(gameMenu.panel!=UI::Save&&gameMenu.panel!=UI::Load)return;for(unsigned i=0;i<8;i++)gameMenu.populatedSlots[i]=slotPreview(unsigned(gameMenu.page)*8+i+1).exists;if(!gameMenu.asking&&gameMenu.focus>=8&&!gameMenu.populatedSlots[unsigned(gameMenu.focus)%8])gameMenu.focus%=8;}
 void deleteSlot(unsigned slot){
  if(!slot||slot>=AutoSaveBase)throw std::runtime_error("Manual delete slot range");
  bool removed=false;
  try{
   std::filesystem::remove(slotPrefix(slot)+".story");removed=true;
   for(auto ext:{".text",".name",".images",".transition",".flow",".message",".auto",".choice",".audio",".mixer",".history",".thumbnail"})std::filesystem::remove(slotPrefix(slot)+ext);
   status="データを削除しました。";
  }catch(const std::exception&e){std::cerr<<"DELETE FAIL "<<e.what()<<"\n";status=removed?"データを削除しましたが、一部のファイルが残っています。":"削除できませんでした。";}
  slotPreviews.erase(slot);if(latestManualSlot==slot)manualSlotsScanned=false;refreshSlotFlags();
 }
 void autoSaveChoice(){
  if(replayActive||menu||movie->playing()||normalSkip)return;
  std::filesystem::create_directories(saveDir+"/autosave");
  unsigned chosen=AutoSaveBase;auto oldest=std::filesystem::file_time_type::max();
  for(unsigned i=0;i<AutoSaveCount;i++){unsigned slot=AutoSaveBase+i;auto path=slotPrefix(slot)+".story";auto time=std::filesystem::exists(path)?std::filesystem::last_write_time(path):std::filesystem::file_time_type::min();if(time<=oldest){oldest=time;chosen=slot;}}
  auto priorStatus=status;save(chosen);status=priorStatus;std::cerr<<"AUTOSAVE slot="<<(chosen-AutoSaveBase+1)<<"\n";
 }
 void load(unsigned slot=1){if(movie->playing())return;
 if(!std::filesystem::exists(slotPrefix(slot)+".story")){status="保存データがありません。";
 return;
 }story.restore(qlie::file(slotPrefix(slot)+".story"));syncProgress();progress.apply(story);
 auto t=qlie::file(slotPrefix(slot)+".text"),n=qlie::file(slotPrefix(slot)+".name");
 text.assign(t.begin(),t.end());
 name.assign(n.begin(),n.end());
 images->restore(qlie::file(slotPrefix(slot)+".images"));
 if(std::filesystem::exists(slotPrefix(slot)+".transition"))transition->restore(qlie::file(slotPrefix(slot)+".transition"));else transition->reset();
 audio->restore(qlie::file(slotPrefix(slot)+".audio"),assets);
 auto b=qlie::file(slotPrefix(slot)+".mixer");
 whj2::StateReader mixerState{b};
 auto mixerVersion=mixerState.str();if(mixerVersion!="WHJ2-mixer-1"&&mixerVersion!="WHJ2-mixer-2")throw std::runtime_error("Mixer version");
 for(unsigned i=0;i<3;i++){volumes[i]=mixerState.f();
 loops[i]=mixerState.n()!=0;
 audioTimes[i]=unsigned(mixerState.n());audioTypes[i]=mixerVersion=="WHJ2-mixer-2"?unsigned(mixerState.n()):i==0?0:2;
 }mixerState.end();
 if(std::filesystem::exists(slotPrefix(slot)+".flow")){auto flowBytes=qlie::file(slotPrefix(slot)+".flow");whj2::StateReader flow{flowBytes};auto version=flow.str();if(version!="WHJ2-flow-1"&&version!="WHJ2-flow-2")throw std::runtime_error("Flow state version");auto state=flow.str();sentenceWait.restore(qlie::Bytes(state.begin(),state.end()));pendingBody=flow.str();pendingSpeaker=flow.str();pendingVoice=flow.str();currentPageKey=version=="WHJ2-flow-2"?flow.str():"";flow.end();}
 else {sentenceWait=whj2::SentenceWait{};sentenceWait.page();pendingBody=text;pendingSpeaker=name;pendingVoice.clear();currentPageKey.clear();}
 if(std::filesystem::exists(slotPrefix(slot)+".message"))messageText->restore(qlie::file(slotPrefix(slot)+".message"));else {messageText->start(text);messageText->reveal();}messageText->setSpeed(settings.get("TextSpeed"));refreshMessage();
 if(std::filesystem::exists(slotPrefix(slot)+".auto"))autoPlay.restore(qlie::file(slotPrefix(slot)+".auto"));else {autoPlay.enabled=false;autoPlay.page(settings.get("AutoClickWait"));}
 history.clear();if(std::filesystem::exists(slotPrefix(slot)+".history"))history=whj2::restoreHistory(qlie::file(slotPrefix(slot)+".history"));
 menu=false;hidden=false;
 normalSkip=false;currentPageWasRead=progress.wasRead(currentPageKey);focus=0;if(story.state.awaiting){choiceScreen->show(story.state.choices,font);if(std::filesystem::exists(slotPrefix(slot)+".choice"))choiceScreen->restore(qlie::file(slotPrefix(slot)+".choice"),font);focus=choiceScreen->selection();}
 warmNextScene();
 status="静止画・音声位置を含む状態を読み込みました。";
 }
 void activate(){if(normalSkip){normalSkip=false;return;}if(opening){if(opening==1)makerLogo();else {movie->stop();showTitle();}return;}if(!menu&&hidden){hidden=false;return;}if(movie->playing()){movie->stop();
 if(movieOnly)done=true;
 else next();
 return;
 }if(menu){auto action=title->confirm();if(action==whj2::TitleScreen::None)return;uiSound("Sys_OnClick");
 if(action==whj2::TitleScreen::Start){audio->stopAll();history.clear();autoPlay.enabled=false;story.start();progress.apply(story);menu=false;name="";focus=0;next();}
 else if(action==whj2::TitleScreen::Load)openPanel(whj2::GameMenu::Load);
 else if(action==whj2::TitleScreen::Settings)openPanel(whj2::GameMenu::Settings);
 else if(action==whj2::TitleScreen::Extra)title->setExtraPage(true);
 else if(action==whj2::TitleScreen::Sound)musicScreen->open();
 else if(action==whj2::TitleScreen::Memory)openReplay();
 else if(action==whj2::TitleScreen::CG)openCg();
 else if(action==whj2::TitleScreen::Back)title->setExtraPage(false);
 else if(action==whj2::TitleScreen::Quit)performUi(whj2::GameMenu::Quit);
 else std::cerr<<"PENDING-TITLE extra modes\n";
 }else if(story.state.awaiting){if(choiceScreen->confirm())uiSound("Sys_OnClick");
 }else {if(sentenceWait.blocksClick())return;
 if(settings.get("ClickVoiceStop"))audio->volume(3,0,250,true);
 if(messageText->reveal()){auto action=sentenceWait.prewaiting()?sentenceWait.confirm():whj2::SentenceWait::None;refreshMessage();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);return;}
 auto action=sentenceWait.confirm();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);}
 }
 void uiSound(const std::string&name){auto path="misc/"+name+".wav";audio->play(whj2::Audio::SystemChannel,assets.read(path),false,1,0,path,3);}
 void performUi(whj2::GameMenu::Action action){using UI=whj2::GameMenu;
 if(action==UI::Nothing)return;
 if(action==UI::TestVolume){audio->playFile(assets,whj2::Audio::SettingsTestChannel,"script/config/soundtest.ogg",false,1,0,gameMenu.testVolumeCategory);return;}
 if(action==UI::ReplayVoice){
  const size_t offset=size_t(gameMenu.historyOffset);
  for(int row=0;row<4;row++){
   if(gameMenu.historyVoiceRow>=0&&gameMenu.historyVoiceRow!=row)continue;
   if(offset+size_t(row)>=history.size())continue;
   auto&entry=history[history.size()-1-offset-size_t(row)];
   if(entry.voice.empty()||!voices->resolve(entry.voice))continue;
   playVoice(entry.voice,true);historyVoicePlayed=true;return;
  }
  return;
 }
 if(action==UI::SceneReturn){if(replayActive){if(movie->playing())movie->stop();uiSound("Sys_Cancel");openReplay(replayPage);}return;}
 if(action==UI::SettingsChanged){applySettings();uiSound("Sys_OnMouse");return;}
 uiSound(action==UI::No||action==UI::Close?"Sys_Cancel":"Sys_OnClick");
 if(action==UI::No){gameMenu.dismissPrompt();return;}
 if(action==UI::Yes){action=gameMenu.requested;gameMenu.dismissPrompt();
 if(action==UI::DeleteSlot){deleteSlot(selectedSlot);return;}
 if(action==UI::Slot){if(gameMenu.panel==UI::Load){load(selectedSlot);gameMenu.close();}else save(selectedSlot);return;}
 if(action==UI::ResetSettings){settings.reset();selectFont(0,true);applySettings();return;}
 if(action==UI::Quit){done=true;return;}
 if(action==UI::Title){if(movie->playing())movie->stop();showTitle();return;}}
 if(action==UI::ChooseFont){fontSettingsSnapshot=settings.save();gameMenu.open(UI::Fonts);gameMenu.fontCount=int(fontChoices.size())+8;gameMenu.focus=selectedFont;return;}
 if(action==UI::AcceptFont){gameMenu.open(UI::Settings);gameMenu.focus=24;applySettings();status.clear();return;}
 if(action==UI::ApplyFont){if(selectFont(gameMenu.focus,true)){gameMenu.open(UI::Settings);gameMenu.focus=24;applySettings();status.clear();}return;}
 if(action==UI::Close){if(gameMenu.panel==UI::Fonts){settings.restore(fontSettingsSnapshot);applySettings(false);gameMenu.open(UI::Settings);gameMenu.focus=24;status.clear();return;}if(gameMenu.panel==UI::History&&historyVoicePlayed)audio->volume(3,0,200,true);historyVoicePlayed=false;gameMenu.close();return;}
 if(action==UI::Quit||action==UI::Title||action==UI::ResetSettings){gameMenu.ask(action);return;}
 if(action==UI::DeleteSlot){selectedSlot=unsigned(gameMenu.slot());if(std::filesystem::exists(slotPrefix(selectedSlot)+".story"))gameMenu.ask(UI::DeleteSlot);else status="保存データがありません。";return;}
 if(action==UI::Slot){selectedSlot=unsigned(gameMenu.slot());
 if(gameMenu.panel==UI::Save){if(std::filesystem::exists(slotPrefix(selectedSlot)+".story")){gameMenu.ask(UI::Slot);return;}save(selectedSlot);}
 else if(gameMenu.panel==UI::Load){auto&preview=slotPreview(selectedSlot);if(!preview.exists)status="保存データがありません。";else if(preview.damaged)status="保存データが不完全です。削除または上書きしてください。";else gameMenu.ask(UI::Slot);}
 }
 }
 void openPanel(whj2::GameMenu::Panel panel){if(movie->playing()||(replayActive&&(panel==whj2::GameMenu::Save||panel==whj2::GameMenu::Load)))return;if(panel==whj2::GameMenu::Save&&menu)return;slotPreviews.clear();upperMenu->close();normalSkip=false;status.clear();gameMenu.allowSceneReturn=replayActive;gameMenu.open(panel);if(panel==whj2::GameMenu::Save||panel==whj2::GameMenu::Load){auto slot=newestManualSlot()-1;gameMenu.page=int(slot/8);gameMenu.focus=int(slot%8);}refreshSlotFlags();historyVoicePlayed=false;historyVoiceCacheOffset=-1;gameMenu.historyMaxOffset=int(history.size()>4?history.size()-4:0);gameMenu.historyRows=int(std::min<size_t>(4,history.size()));}
 void upperAction(whj2::UpperMenu::Action action){if(action==whj2::UpperMenu::None)return;upperMenu->close();
 if(action==whj2::UpperMenu::Quit){performUi(whj2::GameMenu::Quit);return;}
 if(action==whj2::UpperMenu::Title){performUi(whj2::GameMenu::Title);return;}
 if(action==whj2::UpperMenu::Screen){if(!window)return;auto flags=SDL_GetWindowFlags(window);if(SDL_SetWindowFullscreen(window,(flags&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP))std::cerr<<"PENDING-UPPERMENU screen mode: "<<SDL_GetError()<<"\n";}
 }
 void openCg(){replayScreen->close();cgScreen->open();menu=true;audio->stopAll();auto path="music/wbgm08.ogg";audio->play(0,assets.read(path),true,255.f/256,0,path,0);}
 void cgAction(whj2::CgScreen::Action action){if(action==whj2::CgScreen::None)return;uiSound(action==whj2::CgScreen::Back?"Sys_Cancel":"Sys_OnClick");if(action==whj2::CgScreen::Back){cgScreen->close();title->setExtraPage(true);leaveMusic();}else if(action==whj2::CgScreen::Replay){cgScreen->close();openReplay();}else if(action==whj2::CgScreen::Music){cgScreen->close();musicScreen->open();}}
 void openReplay(unsigned page=0){normalSkip=false;currentPageKey.clear();cgScreen->close();replayActive=false;menu=true;hidden=false;gameMenu.close();upperMenu->close();images->clear();transition->reset();sentenceWait=whj2::SentenceWait{};history.clear();text.clear();name.clear();pendingBody.clear();pendingSpeaker.clear();pendingVoice.clear();autoPlay.enabled=false;messageText->start("");audio->stopAll();replayScreen->open(page);auto path="music/wbgm16.ogg";audio->play(0,assets.read(path),true,255.f/256,0,path,0);}
 void replayAction(int action){if(action==whj2::ReplayScreen::None)return;uiSound(action==whj2::ReplayScreen::Back?"Sys_Cancel":"Sys_OnClick");
 if(action==whj2::ReplayScreen::Back){replayScreen->close();title->setExtraPage(true);leaveMusic();return;}
 if(action==whj2::ReplayScreen::Music){replayScreen->close();musicScreen->open();return;}
 if(action==whj2::ReplayScreen::Album){replayScreen->close();openCg();return;}
 if(action>=0){replayPage=replayScreen->pageIndex();auto script=replayScreen->entry(unsigned(action)).script;replayScreen->close();audio->stopAll();story.startReplay(script);progress.apply(story);replayActive=true;menu=false;hidden=false;autoPlay.enabled=false;focus=0;next();}}
 void leaveMusic(){auto destination=musicScreen->takeDestination();if(destination==whj2::MusicScreen::CG){openCg();return;}if(destination==whj2::MusicScreen::Replay){openReplay();return;}audio->play(0,assets.read("music/wbgm06.ogg"),true,255.f/256,0,"music/wbgm06.ogg",0);}
 void back(){if(normalSkip){normalSkip=false;return;}if(cgScreen->visible()){cgAction(cgScreen->back());return;}if(replayScreen->visible()){replayAction(whj2::ReplayScreen::Back);return;}if(musicScreen->visible()){if(musicScreen->back())leaveMusic();return;}if(gameMenu.asking){performUi(whj2::GameMenu::No);return;}if(gameMenu.visible()){performUi(whj2::GameMenu::Close);return;}if(upperMenu->visible()){upperMenu->close();return;}if(!menu)hidden=!hidden;}
 SlotPreview&slotPreview(unsigned slot){
 auto at=slotPreviews.find(slot);if(at!=slotPreviews.end())return at->second;
 SlotPreview preview;
 try{
  preview.exists=std::filesystem::exists(slotPrefix(slot)+".story");
  if(preview.exists){
   for(auto ext:{".text",".name",".images",".audio",".mixer"})if(!std::filesystem::exists(slotPrefix(slot)+ext))preview.damaged=true;
   if(!preview.damaged){if(std::filesystem::exists(slotPrefix(slot)+".thumbnail")){preview.thumbnail={160,90,qlie::file(slotPrefix(slot)+".thumbnail")};if(preview.thumbnail.pixels.size()!=160*90*4)throw std::runtime_error("Thumbnail size");preview.thumbnail.version.publish();}auto content=qlie::file(slotPrefix(slot)+".text");preview.excerpt=utf8(std::string(content.begin(),content.end()));}
  }
 }catch(const std::exception&e){std::cerr<<"SAVE PREVIEW FAIL slot="<<slot<<" "<<e.what()<<"\n";preview.exists=true;preview.damaged=true;}
 if(preview.damaged){preview.thumbnail={};preview.excerpt=utf8("保存データが不完全です");}
 return slotPreviews.emplace(slot,std::move(preview)).first->second;
 }
 void drawPanel(){++panelDraws;using UI=whj2::GameMenu;
 if(gameMenu.panel==UI::History){
  gameMenu.historyMaxOffset=int(history.size()>4?history.size()-4:0);
  gameMenu.historyOffset=std::clamp(gameMenu.historyOffset,0,gameMenu.historyMaxOffset);
  gameMenu.historyRows=int(std::min<size_t>(4,history.size()-size_t(gameMenu.historyOffset)));
  gameMenu.focus=std::clamp(gameMenu.focus,0,std::max(0,gameMenu.historyRows-1));
  if(historyVoiceCacheOffset!=gameMenu.historyOffset||historyEntryCount!=history.size()||historyFontGeneration!=fontGeneration){
   historyVoiceCacheOffset=gameMenu.historyOffset;historyEntryCount=history.size();historyFontGeneration=fontGeneration;gameMenu.historyVoices.fill(false);gameMenu.historyTextOffset.fill(0);gameMenu.historyTextMax.fill(0);historyRenderedLine.fill(-1);
   for(size_t row=0;row<4;row++){historyText[row].reset();historyTextImages[row]={};if(row+size_t(gameMenu.historyOffset)>=history.size())continue;
    auto&entry=history[history.size()-1-size_t(gameMenu.historyOffset)-row];
    gameMenu.historyVoices[row]=!entry.voice.empty()&&bool(voices->resolve(entry.voice));
    historyText[row]=std::make_unique<whj2::MessageText>(assets,true);historyText[row]->start(entry.text);historyText[row]->reveal();
    auto lines=historyText[row]->lineCount(whj2::MessageStyle{920,104,26,26,0xffffff});gameMenu.historyTextMax[row]=int(lines>4?lines-4:0);
   }
  }
  for(int row=0;row<gameMenu.historyRows;row++)if(historyRenderedLine[row]!=gameMenu.historyTextOffset[row]){
   historyTextImages[row]=historyText[row]->render(font,whj2::MessageStyle{920,104,26,26,0xffffff},size_t(gameMenu.historyTextOffset[row]));historyTextImages[row].version.publish();historyRenderedLine[row]=gameMenu.historyTextOffset[row];
  }
 }
 refreshSlotFlags();if(frameTarget)gameMenu.draw(*frameTarget);else gameMenu.draw(pixels);
 if(gameMenu.asking){auto prompt=gameMenu.requested==UI::Quit?"ゲームを終了しますか？":gameMenu.requested==UI::Title?"タイトルに戻りますか？":gameMenu.requested==UI::ResetSettings?"設定を初期状態に戻しますか？":gameMenu.requested==UI::DeleteSlot?"データを削除しますか？":gameMenu.panel==UI::Load?"データをロードしますか？":"データを上書き保存しますか？";line(prompt,440,275,28);return;}
 if(gameMenu.panel==UI::Fonts){
  line("フォント選択",350,150,30);int first=gameMenu.focus/5*5;
  for(int row=0;row<5&&first+row<gameMenu.fontCount;row++){int index=first+row;std::string label;if(index<int(fontChoices.size()))label=fontChoices[index].label;else {int option=index-int(fontChoices.size()),style=settings.get("FontStyle");if(option==0)label="アンチエイリアス ×"+std::to_string(1<<settings.get("FontAA"));else if(option==1)label=std::string("太字：")+((style&1)?"する":"しない");else if(option==2)label=std::string("影：")+((style&2)?"する":"しない");else if(option==3)label="縁取り："+std::to_string((style&8)?2:(style&4)?1:0);else if(option==7)label="設定";else {const char*channels[]={"R","G","B"};label=std::string("縁・影の色 ")+channels[option-4]+"："+std::to_string((settings.get("FontStyleColor")>>((6-option)*8))&255);}}if(index==selectedFont)label="* "+label;int x=340;for(auto cp:utf8(label)){int width=cp<128?12:24;if(x+width>960)break;drawGlyph(cp,x,220+row*60,width,24,index==gameMenu.focus?0xffff88:0xffffff);x+=width;}}
  if(first>0)line("前へ",840,170,24);
  if(first+5<gameMenu.fontCount)line("次へ",840,555,24);
  line("設定",420,565,24);line("キャンセル",640,565,24);if(!status.empty())line(status,320,525,18);return;
 }
 if(gameMenu.panel==UI::Settings&&gameMenu.allowSceneReturn)line("回想を終了",520,680,26,gameMenu.focus==16?0xffff88:0xffffff);
 if(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load){for(int i=0;i<8;i++){unsigned slot=unsigned(gameMenu.page*8+i+1);int x=i<4?198:834,y=136+(i%4)*135;line("No."+std::to_string(slot),x,y,16,0xff5555);auto&preview=slotPreview(slot);if(preview.exists){if(!preview.thumbnail.pixels.empty()){whj2::RenderQueue queue;queue.image(preview.thumbnail,whj2::imagePlacement(i<4?27:660,137+(i%4)*135,1,1,0,0,0,160,90),0,255,0,0xffffff);if(frameTarget)queue.draw(*frameTarget,1280,720);else queue.draw(pixels,1280,720);}auto&chars=preview.excerpt;int px=x;for(size_t n=0;n<chars.size()&&n<23;n++){drawGlyph(chars[n],px,y+22,18,18,0xffffff);px+=chars[n]<128?9:18;}}else line("NO DATA",x,y+24,18,0x999999);}
 if(!status.empty())line(status,30,677,18);}
 if(gameMenu.panel==UI::History){size_t offset=size_t(gameMenu.historyOffset);whj2::RenderQueue queue;for(int row=0;row<gameMenu.historyRows;row++){
  auto&entry=history[history.size()-1-offset-size_t(row)];int y=64+(3-row)*149;line(whj2::messageDisplayName(entry.speaker),230,y,26,0x69faff);
  auto&body=historyTextImages[row];queue.image(body,whj2::imagePlacement(230,y+30,1,1,0,0,0,body.width,body.height),0,255,0,0xffffff);
 }if(frameTarget)queue.draw(*frameTarget,1280,720);else queue.draw(pixels,1280,720);}

 }

 KImage*newImage(){static KImage dst;dst={0,0,1280,720,1280*4,pixels.data()};return &dst;}
 void recordShown(){auto shown=images->takeShownFiles();auto cgShown=cgScreen->takeShownFiles();shown.insert(cgShown.begin(),cgShown.end());if(progress.captureFiles(shown))qlie::write_file(saveDir+"/system.progress",progress.save());}
 void present(){recordShown();if(fpsVisible)fpsDisplay.draw(pixels);
#ifdef WHJ2_WITH_VULKAN
 if(vulkan&&vulkan->nativePresentation()){framePacer.wait();if(auto stamp=vulkan->presentPixels(pixels))fpsDisplay.presented(*stamp);return;}
#endif
 bool cpuScanlines=true;
#ifdef WHJ2_WITH_VULKAN
 cpuScanlines=!vulkan;
#endif
 cpuScanlinePhase^=1;
 if(cpuScanlines&&cpuInterlaced){if(cpuScanlineHistory.size()!=pixels.size())cpuScanlineHistory=pixels;else for(unsigned y=cpuScanlinePhase;y<720;y+=2)std::copy_n(pixels.data()+size_t(y)*1280*4,1280*4,cpuScanlineHistory.data()+size_t(y)*1280*4);pixels=cpuScanlineHistory;}
 SDL_UpdateTexture(texture,nullptr,pixels.data(),1280*4);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);framePacer.wait();SDL_RenderPresent(renderer);fpsDisplay.presented(SDL_GetTicks64());}
#ifdef WHJ2_WITH_VULKAN
 void drawNative(){
 if(uiGlyphs.size()>1024)uiGlyphs.clear();
 frameTarget=vulkan.get();
 if(cgScreen->visible()){vulkan->begin(0u);cgScreen->draw(*frameTarget);}
 else if(replayScreen->visible()){vulkan->begin(0u);replayScreen->draw(*frameTarget);}
 else if(opening==1){vulkan->begin(0u);images->draw(*frameTarget);}
 else if(musicScreen->visible()){vulkan->begin(0u);musicScreen->draw(*frameTarget);}
 else if(movie->playing()){vulkan->beginVideo(movie->image());upperMenu->draw(*frameTarget);if(gameMenu.visible())drawPanel();}
 else{
  vulkan->begin(menu?0xff000000u:0xff141923u);
  if(menu)title->draw(*frameTarget);
  else{
   images->drawScene(*frameTarget);transition->blend(*frameTarget);
   if(!hidden&&gameMenu.panel!=whj2::GameMenu::History){
    images->drawMessages(*frameTarget);if(images->messageVisible())line(whj2::messageDisplayName(name),200,570,26,0xffffff);
    if(story.state.awaiting)choiceScreen->draw(*frameTarget);
    images->drawFaces(*frameTarget);if(images->messageVisible()){gameMenu.drawAutoButton(*frameTarget,autoPlay.enabled);gameMenu.drawSkipButton(*frameTarget,normalSkip);}
   }
   upperMenu->draw(*frameTarget);
  }
  if(gameMenu.visible())drawPanel();
 }
 recordShown();if(fpsVisible)fpsDisplay.draw(*frameTarget);frameTarget=nullptr;
 // FIFO presentation supplies the native 60 Hz limit. A second software sleep
 // before acquisition can miss the next vblank and halve the frame rate.
 if(auto stamp=vulkan->present(fpsVisible))fpsDisplay.presented(*stamp);
 }
#endif
 void draw(){panelDraws=0;
#ifdef WHJ2_WITH_VULKAN
 if(vulkan&&vulkan->nativePresentation()){drawNative();return;}
#endif
 if(cgScreen->visible()){std::fill(pixels.begin(),pixels.end(),0);cgScreen->draw(pixels);present();return;}if(replayScreen->visible()){std::fill(pixels.begin(),pixels.end(),0);replayScreen->draw(pixels);present();return;}if(opening==1){std::fill(pixels.begin(),pixels.end(),0);images->draw(pixels);present();return;}if(musicScreen->visible()){std::fill(pixels.begin(),pixels.end(),0);musicScreen->draw(pixels);present();return;}if(movie->playing()){pixels=movie->image().pixels;
 upperMenu->draw(pixels);
 if(gameMenu.visible())drawPanel();
 present();
 return;
 }for(size_t i=0;i<pixels.size();i+=4){pixels[i]=menu?0:35;
 pixels[i+1]=menu?0:25;
 pixels[i+2]=menu?0:20;
 pixels[i+3]=255;
 }if(!menu){images->drawScene(pixels);transition->blend(pixels);}
 if(menu){
#ifdef WHJ2_WITH_VULKAN
 if(vulkan){vulkan->begin(pixels);title->draw(*vulkan);vulkan->end(pixels);}else
#endif
 title->draw(pixels);
 }else if(!hidden&&gameMenu.panel!=whj2::GameMenu::History){images->drawMessages(pixels);
 if(images->messageVisible())line(whj2::messageDisplayName(name),200,570,26,0xffffff);
 if(story.state.awaiting)choiceScreen->draw(pixels);
 }if(!menu&&!hidden&&gameMenu.panel!=whj2::GameMenu::History){images->drawFaces(pixels);if(images->messageVisible()){gameMenu.drawAutoButton(pixels,autoPlay.enabled);gameMenu.drawSkipButton(pixels,normalSkip);}}
 if(!menu)upperMenu->draw(pixels);
 if(gameMenu.visible())drawPanel();
 present();
 }
 void snapshot(const std::string&name){SDL_Surface*surface=SDL_CreateRGBSurfaceFrom(pixels.data(),1280,720,32,1280*4,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
 if(!surface||SDL_SaveBMP(surface,(saveDir+"/"+name).c_str()))throw std::runtime_error("Cannot save screenshot");
 SDL_FreeSurface(surface);
 std::cerr<<"SCREENSHOT "<<saveDir<<"/"<<name<<"\n";
 }
 bool titleViewActive()const{return menu&&!opening&&!gameMenu.visible()&&!musicScreen->visible()&&!replayScreen->visible()&&!cgScreen->visible();}
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
 void touchTitleView(float x,float y){if(!titleViewActive())return;int w,h;windowSize(&w,&h);float lx,ly;windowToLogical(int(x*w),int(y*h),&lx,&ly);titlePointer=std::clamp(lx,0.f,1280.f);title->view(titlePointer);}
 void touchDrag(float x,float y,bool start){int w,h;windowSize(&w,&h);float lx,ly;windowToLogical(int(x*w),int(y*h),&lx,&ly);if(cgScreen->visible()){if(start)fingerUiDrag=false;if(cgScreen->inView())cgScreen->pan(lx,ly);return;}if(replayScreen->visible()){if(start)fingerUiDrag=false;return;}if(musicScreen->visible()){bool moved=musicScreen->drag(lx,ly);if(start)fingerUiDrag=moved;return;}if(start)fingerUiDrag=gameMenu.beginDrag(lx,ly);else gameMenu.drag(lx,ly);if(fingerUiDrag&&gameMenu.settingsDirty){applySettings(false);uiSound("Sys_OnMouse");}}
 void pointer(int px,int py){float x,y;
 windowToLogical(px,py,&x,&y);
 if(opening){activate();return;}if(menu){titlePointer=x;title->view(x);}
 if(cgScreen->visible()){cgAction(cgScreen->tap(x,y));return;}
 if(replayScreen->visible()){replayAction(replayScreen->tap(x,y));return;}
 if(musicScreen->visible()){if(musicScreen->tap(x,y))leaveMusic();return;}
 if(gameMenu.visible()){refreshSlotFlags();auto action=gameMenu.tap(x,y);refreshSlotFlags();performUi(action);return;}
 if(upperMenu->visible()){upperAction(upperMenu->tap(x,y));return;}
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
 std::cerr<<"INPUT controller connected id="<<id<<"\n";
 }
 void controllerKey(Uint8 button){using UI=whj2::GameMenu;
 if(button==SDL_CONTROLLER_BUTTON_BACK){fpsVisible=!fpsVisible;if(fpsVisible)fpsDisplay.restartSample();return;}
 if(opening){if(button==SDL_CONTROLLER_BUTTON_A)activate();return;}
 if(cgScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)cgAction(cgScreen->confirm());else if(button==SDL_CONTROLLER_BUTTON_X)cgScreen->toggleZoom();else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)cgScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)cgScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)cgScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)cgScreen->move(1,0);return;}
 if(replayScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)replayAction(replayScreen->confirm());else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)replayScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)replayScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)replayScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)replayScreen->move(1,0);return;}
 if(musicScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A){musicScreen->confirm();if(!musicScreen->visible())leaveMusic();}else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)musicScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)musicScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)musicScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)musicScreen->move(1,0);return;}
 if(gameMenu.visible()){refreshSlotFlags();if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)performUi(gameMenu.confirm());else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)gameMenu.move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)gameMenu.move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)gameMenu.move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)gameMenu.move(1,0);else if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER&&!gameMenu.asking&&(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load)){gameMenu.page=(gameMenu.page+14)%15;gameMenu.focus%=8;}else if(button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER&&!gameMenu.asking&&(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load)){gameMenu.page=(gameMenu.page+1)%15;gameMenu.focus%=8;}refreshSlotFlags();if(gameMenu.settingsDirty){applySettings();uiSound("Sys_OnMouse");}return;}
 if(button==SDL_CONTROLLER_BUTTON_Y&&!menu){upperMenu->toggle();return;}
 if(upperMenu->visible()){if(button==SDL_CONTROLLER_BUTTON_A)upperAction(upperMenu->confirm());else if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)upperMenu->move(-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)upperMenu->move(1);return;}
 if(menu){switch(button){case SDL_CONTROLLER_BUTTON_B:if(title->isExtraPage()){title->setExtraPage(false);uiSound("Sys_Cancel");}break;case SDL_CONTROLLER_BUTTON_A:activate();break;case SDL_CONTROLLER_BUTTON_DPAD_UP:case SDL_CONTROLLER_BUTTON_DPAD_LEFT:focus=title->move(-1);uiSound("Sys_OnMouse");break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:focus=title->move(1);uiSound("Sys_OnMouse");break;case SDL_CONTROLLER_BUTTON_X:openPanel(UI::Settings);break;case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:openPanel(UI::Load);break;default:break;}return;}
 switch(button){case SDL_CONTROLLER_BUTTON_A:activate();break;
 case SDL_CONTROLLER_BUTTON_B:back();break;
 case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:openPanel(UI::Save);break;
 case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:openPanel(UI::Load);break;
 case SDL_CONTROLLER_BUTTON_X:openPanel(UI::Settings);break;
 case SDL_CONTROLLER_BUTTON_DPAD_UP:if(!menu)openPanel(UI::History);else focus=(focus+2)%3;break;
 case SDL_CONTROLLER_BUTTON_DPAD_DOWN:{int count=menu?3:int(story.state.choices.size());if(count){focus=(focus+1)%count;if(story.state.awaiting)if(choiceScreen->select(focus))uiSound("Sys_OnMouse");}break;}
 case SDL_CONTROLLER_BUTTON_DPAD_LEFT:case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:if(story.state.awaiting&&!story.state.choices.empty()){int count=int(story.state.choices.size());focus=(focus+count+(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1))%count;if(choiceScreen->select(focus))uiSound("Sys_OnMouse");}break;
 default:break;
 }}
 int run(unsigned smoke){
#ifdef __SWITCH__
 whj2::logSwitchThread("main",whj2::bindSwitchThread(0));
 std::cerr<<"INPUT libnx Joy-Con and "<<(window?"SDL":"libnx")<<" touch\n";
#else
 std::cerr<<"INPUT physical joysticks="<<SDL_NumJoysticks()<<"\n";
 for(int i=0;i<SDL_NumJoysticks();i++)controllerAdded(i);
#endif
 unsigned frames=0;
 if(smoke&&!openingTest&&!movieOnly){if(opening)showTitle();if(!titleTest)title->skipIntro();}
 uint64_t lastUpdate=SDL_GetTicks64();
 while(!done){auto now=SDL_GetTicks64();
#ifdef __SWITCH__
 audio->reportMixerBinding();
#endif
 if(opening==1){openingElapsed+=double(now-lastUpdate);images->update(double(now-lastUpdate));if(openingElapsed>=4500)makerLogo();}
 if(titleViewActive()&&!fingerActive){titlePointer=std::clamp(titlePointer+titleAxis*double(now-lastUpdate)*.6,0.,1280.);title->view(titlePointer);}
 if(cgScreen->visible()){cgScreen->update(double(now-lastUpdate));if(cgScreen->inView()&&!fingerActive)cgScreen->panDelta(titleAxis*double(now-lastUpdate)*.6,titleAxisY*double(now-lastUpdate)*.6);}
 else if(replayScreen->visible())replayScreen->update(double(now-lastUpdate));
 else if(musicScreen->visible()){musicScreen->update(double(now-lastUpdate));if(musicScreen->takeSettingsChanged())applySettings();}
 else if(!opening&&menu&&!gameMenu.visible())title->update(double(now-lastUpdate));
 if(!menu&&!movie->playing()&&!gameMenu.visible()&&!upperMenu->visible()){
 images->update(double(now-lastUpdate));
 if(story.state.awaiting){choiceScreen->update(double(now-lastUpdate));if(choiceScreen->takeAutoSaveRequest())autoSaveChoice();int chosen=choiceScreen->takeConfirmed();if(chosen>=0){story.choose(unsigned(chosen));next();}}
 messageText->update(double(now-lastUpdate));if(messageText->visible()!=lastMessageVisible)refreshMessage();
 transition->update(double(now-lastUpdate));
 autoPlay.tick(double(now-lastUpdate),messageText->visible(),settings.get("AutoTextWait"),settings.get("TextSpeed"),messageText->timedCharacters(autoPlay.countedGlyphs(),messageText->visible()));
 auto waitAction=sentenceWait.tick(double(now-lastUpdate),audio->playing(3));
 if(waitAction==whj2::SentenceWait::None&&sentenceWait.waitingForMessage()&&autoPlay.ready(messageText->finished(),audio->playing(3),settings.get("AutoPlayVoiceWait")))waitAction=sentenceWait.force();
 if(waitAction==whj2::SentenceWait::Next)next();else if(waitAction==whj2::SentenceWait::Resume)next(true);
 for(auto&event:images->takeSoundEvents()){
  if(event.stop)audio->volume(event.channel,0,0,true);
  else audio->play(event.channel,event.bytes,event.loop,event.gain,0,event.source,event.category);
 }
}
 lastUpdate=now;
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
 if(movie->playing()){movie->update();
 if(!movie->playing()){if(movieOnly){done=true;
 break;
 }if(opening==2){showTitle();}else next();
 }}SDL_Event e;
 while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)done=true;
else if(e.type==SDL_FINGERDOWN){if(!fingerActive){fingerActive=true;fingerId=e.tfinger.fingerId;fingerX=e.tfinger.x;fingerY=e.tfinger.y;touchTitleView(fingerX,fingerY);touchDrag(fingerX,fingerY,true);}}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&cgScreen->visible()&&cgScreen->inView()){touchDrag(e.tfinger.x,e.tfinger.y,false);}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&titleViewActive()&&!fingerUiDrag){touchTitleView(e.tfinger.x,e.tfinger.y);}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&fingerUiDrag){touchDrag(e.tfinger.x,e.tfinger.y,false);}
 else if(e.type==SDL_FINGERUP&&fingerActive&&e.tfinger.fingerId==fingerId){fingerActive=false;
 float dx=(e.tfinger.x-fingerX)*1280,dy=(e.tfinger.y-fingerY)*720;
 if(fingerUiDrag){touchDrag(e.tfinger.x,e.tfinger.y,false);gameMenu.endDrag();fingerUiDrag=false;if(gameMenu.panel==whj2::GameMenu::Settings)applySettings();}
 else if(!menu&&!gameMenu.visible()&&upperMenu->swipe(dx,dy)){std::cerr<<"INPUT downward swipe upper menu\n";}
 else if(std::abs(dx)<24&&std::abs(dy)<24){int w,h;windowSize(&w,&h);std::cerr<<"INPUT touch\n";pointer(int(e.tfinger.x*w),int(e.tfinger.y*h));}
 }else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY){titleAxisY=std::abs(e.caxis.value)>6000?e.caxis.value/32767.f:0;}else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX){titleAxis=std::abs(e.caxis.value)>6000?e.caxis.value/32767.f:0;}else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT){bool held=e.caxis.value>16000;if(held&&!leftTrigger)toggleAuto();leftTrigger=held;}else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT){bool held=e.caxis.value>16000;if(held&&!rightTrigger)toggleSkip();rightTrigger=held;}else if(e.type==SDL_CONTROLLERDEVICEADDED)controllerAdded(e.cdevice.which);
 else if(e.type==SDL_CONTROLLERDEVICEREMOVED){auto i=controllers.find(e.cdevice.which);
 if(i!=controllers.end()){SDL_GameControllerClose(i->second);
 controllers.erase(i);
 }}else if(e.type==SDL_CONTROLLERBUTTONDOWN){std::cerr<<"INPUT controller button="<<unsigned(e.cbutton.button)<<"\n";
#ifndef __SWITCH__
controllerKey(e.cbutton.button);
#endif
}}recordCurrentRead();if(normalSkip&&!menu&&!gameMenu.visible()&&!upperMenu->visible()&&!movie->playing()&&!story.state.awaiting){hidden=false;messageText->reveal();refreshMessage();auto action=sentenceWait.force();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);}
 draw();
 if(smoke&&gameMenu.visible()&&panelDraws!=1)throw std::runtime_error("Panel composited more than once");
 if(movieOnly&&!movieCaptured&&movie->elapsedMs()>=1000){snapshot("movie.bmp");
 movieCaptured=true;
 std::cerr<<"MOVIE screenshot audio_clock_ms="<<movie->elapsedMs()<<"\n";
 }if(smoke){if(readSkipTest){
 if(frames==1){activate();if(currentPageKey.empty()||currentPageWasRead)throw std::runtime_error("Read skip needs fresh progress");settings.set("SkipMode",1);toggleSkip();if(normalSkip)throw std::runtime_error("Unread page allowed read-only skip");messageText->reveal();hidden=true;recordCurrentRead();if(progress.wasRead(currentPageKey))throw std::runtime_error("Hidden text marked read");hidden=false;recordCurrentRead();auto key=currentPageKey;whj2::Progress restored;restored.restore(qlie::file(saveDir+"/system.progress"));if(!restored.wasRead(key))throw std::runtime_error("Read page not persisted");for(unsigned version=1;version<=2;version++){whj2::StateWriter old;old.str("WHJ2-progress-"+std::to_string(version));old.n(0);old.n(0);if(version==2)old.n(0);restored.restore(old.bytes);if(restored.wasRead(key))throw std::runtime_error("Legacy progress invented read history");}save(1);skipStart=advances;SDL_Event left{};left.type=SDL_CONTROLLERAXISMOTION;left.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;left.caxis.value=32767;SDL_PushEvent(&left);}
 if(frames==3){if(normalSkip||advances<=skipStart)throw std::runtime_error("Fresh-read ZR did not start and stop at unread page");load(1);if(!currentPageWasRead)throw std::runtime_error("Read page lost on load");toggleSkip();if(!normalSkip)throw std::runtime_error("Read page refused skip");}
 if(frames==10){if(normalSkip||advances<=skipStart||currentPageWasRead)throw std::runtime_error("Read skip did not stop at unseen page");messageText->reveal();recordCurrentRead();pointer(1200,700);if(!normalSkip)throw std::runtime_error("Fresh-read touch skip did not enable without load");skipStart=advances;}
 if(frames==12){if(normalSkip||advances<=skipStart)throw std::runtime_error("Fresh-read touch skip did not stop at unread page");skipStart=advances;rightTrigger=false;SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;e.caxis.value=32767;SDL_PushEvent(&e);}
 if(frames==14){SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;SDL_PushEvent(&e);if(advances!=skipStart||normalSkip)throw std::runtime_error("ZR skipped unread page");}
 if(frames==17){settings.set("SkipMode",0);toggleSkip();if(!normalSkip)throw std::runtime_error("All-text skip refused");skipStart=advances;}
 if(frames==22){draw();snapshot("read-skip-scene.bmp");}
 if(frames==23){if(advances<=skipStart)throw std::runtime_error("All-text skip did not advance");back();if(normalSkip)throw std::runtime_error("B did not cancel skip");openPanel(whj2::GameMenu::Settings);performUi(gameMenu.tap(410,442));if(settings.get("SkipMode")!=1)throw std::runtime_error("Read-only setting touch");whj2::Settings copy(assets);copy.restore(qlie::file(saveDir+"/settings.cfg"));if(copy.get("SkipMode")!=1)throw std::runtime_error("Skip mode not persisted");draw();snapshot("read-skip-settings.bmp");gameMenu.close();load(1);pointer(1200,700);if(!normalSkip)throw std::runtime_error("Original skip button did not activate");}
 if(frames==45){if(normalSkip)throw std::runtime_error("Touch skip did not stop at unread text");std::cerr<<"READ SKIP PASS fresh-read ZR and touch without reload / persisted pages / progress1-2 migration / flow2 restore / unread stop / ZR unread stop / all mode / B cancel / original touch button\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Read skip test timeout");}continue;
 }if(replayReturnTest){
 if(frames==1){openReplay(0);replayScreen->update(300);replayAction(replayScreen->confirm());if(!replayActive)throw std::runtime_error("Return test needs unlocked replay entry");}
 if(frames==20){controllerKey(SDL_CONTROLLER_BUTTON_X);if(!gameMenu.allowSceneReturn)throw std::runtime_error("Replay return action unavailable");for(unsigned i=0;i<13;i++)controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);if(gameMenu.focus!=16)throw std::runtime_error("Replay return focus unreachable");draw();if(panelDraws!=1)throw std::runtime_error("Replay return panel draw count");snapshot("replay-return-menu.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);if(!replayActive||gameMenu.visible())throw std::runtime_error("Cancel unexpectedly left replay");}
 if(frames==30){controllerKey(SDL_CONTROLLER_BUTTON_X);for(unsigned i=0;i<13;i++)controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);controllerKey(SDL_CONTROLLER_BUTTON_A);if(replayActive||!replayScreen->visible()||replayScreen->pageIndex()!=0||gameMenu.visible())throw std::runtime_error("Controller scene return");}
 if(frames==50){replayScreen->update(300);replayAction(replayScreen->confirm());if(!replayActive)throw std::runtime_error("Second replay start");controllerKey(SDL_CONTROLLER_BUTTON_X);performUi(gameMenu.tap(600,695));if(replayActive||!replayScreen->visible())throw std::runtime_error("Touch scene return");}
 if(frames==70){replayScreen->update(300);replayAction(replayScreen->confirm());controllerKey(SDL_CONTROLLER_BUTTON_X);performUi(whj2::GameMenu::Title);if(!gameMenu.asking)throw std::runtime_error("Title confirmation missing");performUi(whj2::GameMenu::Yes);if(replayActive||replayScreen->visible()||!menu)throw std::runtime_error("Title incorrectly routed to replay");openPanel(whj2::GameMenu::Settings);if(gameMenu.allowSceneReturn)throw std::runtime_error("Scene return leaked outside replay");gameMenu.close();snapshot("replay-return-title.bmp");std::cerr<<"REPLAY RETURN PASS controller/touch return, cancel keeps playback, title remains title, no action outside replay\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Replay return timeout");}continue;
 }if(cgLoopTest){
 if(frames==1)openCg();
 if(frames==25){cgScreen->move(0,-1);cgScreen->move(1,0);cgAction(cgScreen->confirm());}
 if(frames==50){if(cgScreen->pageIndex()!=1)throw std::runtime_error("Loop test catalogue page");for(unsigned i=0;i<5;i++)cgScreen->move(1,0);cgAction(cgScreen->confirm());if(!cgScreen->inView())throw std::runtime_error("Animated CG fixture not unlocked");}
 if(frames==230){touchSavedStory=pixels;snapshot("cg-loop-after-first.bmp");}
 if(frames==260){if(pixels==touchSavedStory)throw std::runtime_error("CG movie froze after loop boundary");snapshot("cg-loop-moving.bmp");back();back();}
 if(frames==265){if(cgScreen->visible()||!menu)throw std::runtime_error("Animated CG exit");std::cerr<<"CG LOOP PASS animation continues after first loop; normal exit; source audio separately verified silent\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("CG loop runtime timeout");}continue;
 }if(cgTest){
 if(frames==1){if(!progress.extras())throw std::runtime_error("CG smoke needs earned progress");title->select(2);activate();titlePhase=1;}
 else if(titlePhase==1&&title->ready()){title->select(0);activate();titlePhase=2;autoPhaseFrame=frames;}
 else if(titlePhase==2&&frames>autoPhaseFrame+25){if(!cgScreen->visible()||cgScreen->inView())throw std::runtime_error("CG catalogue not open");snapshot("cg-page1.bmp");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=3;autoPhaseFrame=frames;}
 else if(titlePhase==3&&frames>autoPhaseFrame+12){if(!cgScreen->inView())throw std::runtime_error("Seen CG did not open");snapshot("cg-view.bmp");controllerKey(SDL_CONTROLLER_BUTTON_A);if(cgScreen->variantIndex()!=1)throw std::runtime_error("CG next variant");controllerKey(SDL_CONTROLLER_BUTTON_X);cgScreen->pan(0,0);titlePhase=4;autoPhaseFrame=frames;}
 else if(titlePhase==4&&frames>autoPhaseFrame+12){if(cgScreen->zoomLevel()!=2)throw std::runtime_error("CG zoom not enabled");snapshot("cg-zoom.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);if(cgScreen->zoomLevel()!=1||!cgScreen->inView())throw std::runtime_error("CG zoom cancel");controllerKey(SDL_CONTROLLER_BUTTON_B);if(cgScreen->inView())throw std::runtime_error("CG view cancel");cgScreen->move(0,-1);cgScreen->move(1,0);cgAction(cgScreen->confirm());titlePhase=5;autoPhaseFrame=frames;}
 else if(titlePhase==5&&frames>autoPhaseFrame+25){if(cgScreen->pageIndex()!=1)throw std::runtime_error("CG page2 navigation");snapshot("cg-page2.bmp");pointer(1080,695);titlePhase=6;autoPhaseFrame=frames;}
 else if(titlePhase==6&&frames>autoPhaseFrame+20){if(!musicScreen->visible())throw std::runtime_error("CG to music");pointer(950,695);titlePhase=7;autoPhaseFrame=frames;}
 else if(titlePhase==7&&frames>autoPhaseFrame+20){if(!replayScreen->visible())throw std::runtime_error("Music to replay");pointer(820,695);titlePhase=8;autoPhaseFrame=frames;}
 else if(titlePhase==8&&frames>autoPhaseFrame+20){if(!cgScreen->visible())throw std::runtime_error("Replay to CG");back();if(cgScreen->visible()||!title->isExtraPage())throw std::runtime_error("CG back to extras");std::cerr<<"CG UI PASS original catalogue / viewed-file unlock / variants / zoom / page2 / music-replay-CG navigation / back\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("CG UI timeout");}continue;
 }if(replayTest){
 if(frames==1){if(!progress.extras())throw std::runtime_error("Replay smoke needs earned progress");title->select(2);activate();titlePhase=1;}
 else if(titlePhase==1&&title->ready()){title->select(1);activate();titlePhase=2;autoPhaseFrame=frames;}
 else if(titlePhase==2&&frames>autoPhaseFrame+25){if(!replayScreen->visible())throw std::runtime_error("Replay page did not open");snapshot("replay-page1.bmp");pointer(1210,90);titlePhase=3;autoPhaseFrame=frames;}
 else if(titlePhase==3&&frames>autoPhaseFrame+25){if(replayScreen->pageIndex()!=1)throw std::runtime_error("Replay second page");snapshot("replay-page2.bmp");replayScreen->move(0,-1);replayAction(replayScreen->confirm());titlePhase=4;autoPhaseFrame=frames;}
 else if(titlePhase==4&&frames>autoPhaseFrame+25){if(replayScreen->pageIndex()!=0)throw std::runtime_error("Replay first page return");replayAction(replayScreen->confirm());titlePhase=5;autoPhaseFrame=frames;}
 else if(titlePhase==5&&frames>autoPhaseFrame+12){if(menu||!replayActive||!story.state.replay)throw std::runtime_error("Replay did not start");snapshot("replay-playing.bmp");openPanel(whj2::GameMenu::Save);if(gameMenu.visible())throw std::runtime_error("Replay save was not locked");openPanel(whj2::GameMenu::Load);if(gameMenu.visible())throw std::runtime_error("Replay load was not locked");
 // Test setup: seek the logical stream to its return boundary; this is not a full audiovisual playback test.
 bool found=false;for(unsigned i=0;i<100000;i++){auto before=story.save();auto e=story.next();if(e.type=="choice")throw std::runtime_error("Unexpected first replay choice");if(e.type=="finish"){story.restore(before);found=true;break;}}
 if(!found){throw std::runtime_error("Replay return boundary absent");}next();titlePhase=6;autoPhaseFrame=frames;}
 else if(titlePhase==6&&frames>autoPhaseFrame+25){if(replayActive||!menu||!replayScreen->visible()||replayScreen->pageIndex()!=0)throw std::runtime_error("Replay did not restore original page");snapshot("replay-returned.bmp");back();if(replayScreen->visible()||!title->isExtraPage())throw std::runtime_error("Replay back to extras");std::cerr<<"REPLAY UI PASS title entry / page1-page2 / playback sample / save-load lock / logical-boundary return / extras\n";done=true;}
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
 if(frames==108){if(titlePointer<=mutedNonzero+50||advances||!menu||title->selection()!=0)throw std::runtime_error("Stick view navigation changed menu/story");snapshot("view-stick-right.bmp");std::cerr<<"TITLE VIEW PASS immediate touch / drag / left-right stick / neutral hold / no menu selection or story change\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Title view test timeout");}continue;
 }if(openingTest){if(opening==1&&frames==30)snapshot("opening-caution.bmp");if(opening==2&&!openingLogoCaptured&&movie->elapsedMs()>700){snapshot("opening-logo.bmp");openingLogoCaptured=true;}if(!opening){if(!openingLogoCaptured)throw std::runtime_error("Opening movie was not rendered");if(!openingTitleStarted)openingTitleStarted=SDL_GetTicks64();auto elapsed=SDL_GetTicks64()-openingTitleStarted;
 const unsigned times[]={1000,4500,7000};if(openingTitleCaptures<3&&elapsed>=times[openingTitleCaptures]){snapshot("opening-title-"+std::to_string(times[openingTitleCaptures])+".bmp");openingTitleCaptures++;}
 if(openingTitleCaptures==3){if(!title->ready())throw std::runtime_error("Title entrance did not finish");std::cerr<<"OPENING PASS caution4500 / original maker movie / continuous title animation7000\n";done=true;}}if(++frames>=smoke&&!done){throw std::runtime_error("Opening sequence timeout");}continue;}
 if(musicTest){
 if(frames==1){settings.set("MusicModeVolume",100);if(!progress.extras())throw std::runtime_error("Music smoke needs earned progress");title->select(2);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=1;}
 else if(titlePhase==1&&title->ready()){title->select(2);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=2;}
 else if(titlePhase==2){if(!musicScreen->visible())throw std::runtime_error("Music page not opened");snapshot("music-page.bmp");pointer(270,200);titlePhase=3;autoPhaseFrame=frames;}
 else if(titlePhase==3&&frames>=autoPhaseFrame+8){if(musicScreen->playingTrack()!=0||!audio->playing(0))throw std::runtime_error("Music select/play");draw();qlie::Bytes playingIcon;for(int yy=84;yy<116;yy++)for(int xx=1024;xx<1056;xx++)for(int c=0;c<4;c++)playingIcon.push_back(pixels[(yy*1280+xx)*4+c]);pointer(1040,100);draw();qlie::Bytes pausedIcon;for(int yy=84;yy<116;yy++)for(int xx=1024;xx<1056;xx++)for(int c=0;c<4;c++)pausedIcon.push_back(pixels[(yy*1280+xx)*4+c]);if(playingIcon==pausedIcon)throw std::runtime_error("Music play/pause icon unchanged");pointer(1040,100);pointer(409,100);auto p=audio->position(0);if(p.frame<p.length/3||p.frame>p.length*2/3)throw std::runtime_error("Music touch seek");pointer(1140,100);audio->seek(0,UINT64_MAX);titlePhase=4;autoPhaseFrame=frames;}
 else if(titlePhase==4&&frames>=autoPhaseFrame+8){if(musicScreen->playingTrack()!=0||!audio->playing(0))throw std::runtime_error("Music loop restart");pointer(1090,100);if(musicScreen->playingTrack()!=1)throw std::runtime_error("Music next track");pointer(700,100);draw();snapshot("music-muted.bmp");if(settings.get("MusicModeVolume")!=0)throw std::runtime_error("Music volume not applied");qlie::write_file(saveDir+"/settings.cfg",settings.save());
 controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);
 if(musicScreen->selection()!=23){throw std::runtime_error("Controller seek focus unreachable");}
 auto oldPosition=audio->position(0).frame;controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);if(audio->position(0).frame<oldPosition+200000)throw std::runtime_error("Controller seek adjustment");
 controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_UP);if(musicScreen->selection()!=24)throw std::runtime_error("Controller volume focus unreachable");controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);if(settings.get("MusicModeVolume")!=5)throw std::runtime_error("Controller volume adjustment");
 controllerKey(SDL_CONTROLLER_BUTTON_B);if(musicScreen->playingTrack()!=-1)throw std::runtime_error("Music first cancel stop");controllerKey(SDL_CONTROLLER_BUTTON_B);if(musicScreen->visible())throw std::runtime_error("Music second cancel close");std::cerr<<"MUSIC PASS original page / select / seek / loop / next / volume / stop / return\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Music test timeout");}continue;
 }if(extraTest){
 if(frames==1){if(!progress.extras()||!title->select(2))throw std::runtime_error("Persisted route did not unlock title");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=1;}
 else if(titlePhase==1&&title->ready()){if(!title->isExtraPage()||title->hit(1080,330)!=0||title->hit(1080,570)!=3)throw std::runtime_error("Original extra page layout");snapshot("extra-page.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);titlePhase=2;}
 else if(titlePhase==2&&title->ready()){if(title->isExtraPage()||!title->select(2))throw std::runtime_error("Extra cancel did not return");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=3;}
 else if(titlePhase==3&&title->ready()){title->select(3);controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=4;}
 else if(titlePhase==4&&title->ready()){if(title->isExtraPage())throw std::runtime_error("Extra Back button");std::cerr<<"EXTRA PASS real persisted route / original submenu / B cancel / Back button\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Extra test timeout");}
 continue;
 }if(titleTest){
 if(frames==1){if(title->ready()||!audio->playing(0))throw std::runtime_error("Original title intro/music");snapshot("title-intro.bmp");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=1;}
 else if(titlePhase==1&&title->ready()){if(!menu||advances)throw std::runtime_error("Intro skip started story");snapshot("title-ready.bmp");controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);titlePhase=2;}
 else if(titlePhase==2){if(title->selection()!=1)throw std::runtime_error("Title load selection");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=3;}
 else if(titlePhase==3){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("Original title load");controllerKey(SDL_CONTROLLER_BUTTON_B);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);titlePhase=4;}
 else if(titlePhase==4){if(title->selection()!=3)throw std::runtime_error("Locked extra was not skipped");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=5;}
 else if(titlePhase==5){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("Original title settings");controllerKey(SDL_CONTROLLER_BUTTON_B);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);titlePhase=6;}
 else if(titlePhase==6){controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=7;}
 else if(titlePhase==7){if(!gameMenu.asking||done)throw std::runtime_error("Title quit confirmation");snapshot("title-confirm.bmp");controllerKey(SDL_CONTROLLER_BUTTON_B);controllerKey(SDL_CONTROLLER_BUTTON_DPAD_DOWN);titlePhase=8;}
 else if(titlePhase==8){if(title->selection()!=0)throw std::runtime_error("Title start selection");controllerKey(SDL_CONTROLLER_BUTTON_A);titlePhase=9;}
 else if(titlePhase==9){if(menu||!advances)throw std::runtime_error("Original title did not start story");snapshot("title-start.bmp");std::cerr<<"TITLE PASS intro / music / locked extra / load / settings / quit cancel / start\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Title test timeout");}
 continue;
 }if(fpsTest){
 if(frames==1){if(fpsVisible)throw std::runtime_error("FPS must default hidden");snapshot("fps-hidden.bmp");controllerKey(SDL_CONTROLLER_BUTTON_BACK);}
 if(frames==70){if(!fpsVisible||fpsDisplay.fps()==0||fpsDisplay.fps()>60)throw std::runtime_error("FPS sample/toggle");snapshot("fps-visible.bmp");controllerKey(SDL_CONTROLLER_BUTTON_BACK);}
 if(frames==72){if(fpsVisible)throw std::runtime_error("Minus did not hide FPS");snapshot("fps-hidden-again.bmp");}
 if(++frames>=smoke){std::cerr<<"FPS PASS default hidden / Minus show-hide / actual sample="<<fpsDisplay.fps()<<"\n";done=true;}continue;
 }if(autoTest){
 if(frames==1){activate();settings.set("AutoTextWait",0);settings.set("AutoClickWait",0);settings.set("TextSpeed",0);applySettings();autoPlay.page(0);pointer(1170,700);skipStart=advances;autoPhase=1;}
 if(autoPhase==1&&frames>1){if(!autoPlay.enabled)throw std::runtime_error("Original auto toggle");if(advances>skipStart){openPanel(whj2::GameMenu::Settings);skipStart=advances;autoPhase=2;autoPhaseFrame=frames;}else if(frames>240)throw std::runtime_error("Auto did not advance after native wait");}
 if(autoPhase==2&&frames>=autoPhaseFrame+8){if(advances!=skipStart)throw std::runtime_error("Auto advanced beneath settings");controllerKey(SDL_CONTROLLER_BUTTON_B);pointer(1170,700);skipStart=advances;autoPhase=3;autoPhaseFrame=frames;}
 if(autoPhase==3&&frames>=autoPhaseFrame+8){if(autoPlay.enabled||advances!=skipStart)throw std::runtime_error("Auto toggle did not stop");save();auto before=autoPlay.save();load();if(autoPlay.save()!=before)throw std::runtime_error("Auto state restore");draw();snapshot("auto.bmp");std::cerr<<"AUTO PASS original toggle / native wait / progression / panel pause / cancel / save-load\n";done=true;}
 if(++frames>=smoke&&!done){throw std::runtime_error("Auto test timed out");}
 continue;
 }if(settingsTest){
 auto tap=[&](float x,float y){pointer(int(x),int(y));};
 if(frames==1){activate();controllerKey(SDL_CONTROLLER_BUTTON_X);}
 if(frames==4){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("Settings entry");tap(664,163);if(settings.get("MasterVolume")!=0)throw std::runtime_error("Touch master volume");}
 if(frames==12)mutedNonzero=audio->stats().nonzero;
 if(frames==18){if(audio->stats().nonzero!=mutedNonzero)throw std::runtime_error("Muted mixer emitted samples");tap(1125,163);if(settings.get("MasterVolume")!=100)throw std::runtime_error("Master gain restore");tap(598,314);if(settings.get("TextSpeed")!=0)throw std::runtime_error("Text speed right edge");tap(357,195);if(settings.get("WindowAlpha")!=0)throw std::runtime_error("Window alpha");draw();snapshot("settings.bmp");}
 if(frames==20){gameMenu.focus=3;controllerKey(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);if(settings.get("WindowAlpha")!=12)throw std::runtime_error("Controller slider increment");whj2::Settings restored(assets);restored.restore(qlie::file(saveDir+"/settings.cfg"));if(restored.save()!=settings.save())throw std::runtime_error("Settings persistence");controllerKey(SDL_CONTROLLER_BUTTON_B);}
 if(frames==23){if(messageText->getSpeed()!=0)throw std::runtime_error("Text speed not applied");snapshot("window-alpha.bmp");controllerKey(SDL_CONTROLLER_BUTTON_X);gameMenu.focus=12;controllerKey(SDL_CONTROLLER_BUTTON_A);}
 if(frames==25){if(!gameMenu.asking)throw std::runtime_error("Reset confirmation");controllerKey(SDL_CONTROLLER_BUTTON_A);whj2::Settings defaults(assets);if(settings.save()!=defaults.save())throw std::runtime_error("Reset defaults");draw();snapshot("settings-reset.bmp");}
 if(frames==26){SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=664.f/1280;ev.tfinger.y=222.f/720;SDL_PushEvent(&ev);}
 if(frames==27){SDL_Event ev{};ev.type=SDL_FINGERMOTION;ev.tfinger.x=1125.f/1280;ev.tfinger.y=222.f/720;SDL_PushEvent(&ev);}
 if(frames==28){if(settings.get("BGMVolume")!=100||!fingerUiDrag)throw std::runtime_error("Continuous slider drag");SDL_Event ev{};ev.type=SDL_FINGERMOTION;ev.tfinger.x=0;ev.tfinger.y=.8f;SDL_PushEvent(&ev);}
 if(frames==29){if(settings.get("BGMVolume")!=0)throw std::runtime_error("Slider clamp outside track");SDL_Event ev{};ev.type=SDL_FINGERUP;ev.tfinger.x=0;ev.tfinger.y=.8f;SDL_PushEvent(&ev);}
 if(frames==30){if(fingerUiDrag||!gameMenu.visible())throw std::runtime_error("Drag release activated another action");whj2::Settings saved(assets);saved.restore(qlie::file(saveDir+"/settings.cfg"));if(saved.get("BGMVolume")!=0)throw std::runtime_error("Drag release not saved");settings.reset();applySettings();}
 if(frames==31)openPanel(whj2::GameMenu::History);
 if(frames==32){if(panelDraws!=1)throw std::runtime_error("History rendered twice");snapshot("history-once.bmp");}
 if(frames==33)controllerKey(SDL_CONTROLLER_BUTTON_B);
 if(++frames>=smoke){std::cerr<<"SETTINGS PASS mute / gain restore / speed / opacity / controller / persistence / reset\n";done=true;}continue;
 }if(touchOnlyTest){
 auto touch=[](float x,float y){SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=x;ev.tfinger.y=y;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());ev.type=SDL_FINGERUP;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());};
 auto controller=[](Uint8 button){SDL_Event ev{};ev.type=SDL_CONTROLLERBUTTONDOWN;ev.cbutton.button=button;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());};
 if(frames==1)controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
 if(frames==3){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("R did not open load panel");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==5){if(gameMenu.panel!=whj2::GameMenu::Load||std::filesystem::exists(slotPrefix()+".story"))throw std::runtime_error("Empty slot load changed state");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==7)controller(SDL_CONTROLLER_BUTTON_A);
 if(frames==14){gestureStory=story.save();controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==16){if(!hidden||story.save()!=gestureStory||menu)throw std::runtime_error("B did not hide text in place");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==18){if(hidden||story.save()!=gestureStory)throw std::runtime_error("A did not restore text in place");controller(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);}
 if(frames==20){if(gameMenu.panel!=whj2::GameMenu::Save||std::filesystem::exists(slotPrefix()+".story"))throw std::runtime_error("L saved without slot confirmation");snapshot("save-menu.bmp");touch(.25f,.25f);}
 if(frames==22){touchSavedStory=qlie::file(slotPrefix()+".story");touchSavedText=text;controller(SDL_CONTROLLER_BUTTON_B);controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);}
 if(frames==24){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("R did not open load");snapshot("load-menu.bmp");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==25){if(!gameMenu.asking)throw std::runtime_error("Load confirmation missing");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==26){if(gameMenu.visible()||story.save()!=touchSavedStory||text!=touchSavedText)throw std::runtime_error("Selected slot load mismatch");controller(SDL_CONTROLLER_BUTTON_X);}
 if(frames==28){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("X did not open settings");snapshot("settings-menu.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==30)controller(SDL_CONTROLLER_BUTTON_DPAD_UP);
 if(frames==32){if(gameMenu.panel!=whj2::GameMenu::History||history.empty())throw std::runtime_error("Up did not open populated history");snapshot("history-menu.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==34){gestureStory=story.save();controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==36){if(!upperMenu->visible()||story.save()!=gestureStory)throw std::runtime_error("Y changed story");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==38){gestureStory=story.save();SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=.5f;ev.tfinger.y=.2f;SDL_PushEvent(&ev);ev.type=SDL_FINGERUP;ev.tfinger.y=.5f;SDL_PushEvent(&ev);}
 if(frames==40){if(!upperMenu->visible()||story.save()!=gestureStory)throw std::runtime_error("Swipe changed story");snapshot("upper-menu.bmp");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==42){messageText->reveal();recordCurrentRead();skipStart=advances;SDL_Event ev{};ev.type=SDL_CONTROLLERAXISMOTION;ev.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;ev.caxis.value=32767;SDL_PushEvent(&ev);}
 if(frames==48){if(advances<=skipStart)throw std::runtime_error("ZR did not advance read page");SDL_Event ev{};ev.type=SDL_CONTROLLERAXISMOTION;ev.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;ev.caxis.value=0;SDL_PushEvent(&ev);}
 if(frames==50){if(normalSkip)throw std::runtime_error("ZR did not stop at unread page");controller(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);}
 if(frames==52)controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
 if(frames==54){if(gameMenu.page!=1)throw std::runtime_error("Save page navigation");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==56){if(!std::filesystem::exists(slotPrefix(9)+".story"))throw std::runtime_error("Slot 9 missing");controller(SDL_CONTROLLER_BUTTON_B);controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==58)controller(SDL_CONTROLLER_BUTTON_A);
 if(frames==60){if(!gameMenu.asking||done)throw std::runtime_error("Quit bypassed confirmation");snapshot("confirmation.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==62){if(gameMenu.visible())throw std::runtime_error("B did not cancel dialog");controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==64){if(history.size()<4||gameMenu.panel!=whj2::GameMenu::History)throw std::runtime_error("History lost previous pages");snapshot("history-menu.bmp");controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==66){if(gameMenu.historyOffset!=0||gameMenu.focus!=1)throw std::runtime_error("History row navigation");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==68&&gameMenu.visible())throw std::runtime_error("History cancel");
 if(++frames>=smoke){snapshot("touch.bmp");std::cerr<<"TARGET INPUT PASS L/R panels and slots / X settings / history / B text visibility / A confirm / Y and swipe / ZR read-toggle / cancel dialog\n";done=true;}continue;
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
 std::cerr<<"SAVE image_state_and_pixels_equal\n";
 }if(++frames>=smoke){SDL_Surface*surface=SDL_CreateRGBSurfaceFrom(pixels.data(),1280,720,32,1280*4,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
 if(!surface||SDL_SaveBMP(surface,(saveDir+"/smoke.bmp").c_str()))throw std::runtime_error("Cannot save smoke frame");
 SDL_FreeSurface(surface);
 done=true;
 }}else {frames++;

 }}
#ifdef WHJ2_WITH_VULKAN
 if(vulkan&&vulkan->nativePresentation())vulkan->finishPresentation();
#endif
 auto ast=audio->stats();
 std::cerr<<"AUDIO output_frames="<<ast.frames<<" nonzero_frames="<<ast.nonzero<<"\n";
 std::cerr<<"FRAME cap="<<whj2::FramePacer::Limit<<" min_interval_ms="<<framePacer.minimumMilliseconds()<<"\n";
 std::cerr<<"EXIT normal text_advances="<<advances<<" frames="<<frames<<"\n";
 return 0;
 }
};
#ifndef WHJ2_BUILD_REV
#define WHJ2_BUILD_REV "unversioned"
#endif
int main(int argc,char**argv){
#ifdef __SWITCH__
 const char*logPath=std::getenv("WHJ2_LOG");if(!logPath)logPath="sdmc:/switch/whj2/runtime.log";
 if(!std::freopen(logPath,"a",stderr))return 2;
 std::setvbuf(stderr,nullptr,_IONBF,0);
 std::cerr<<"START WHJ2 runtime argc="<<argc<<"\n";
#endif
try{std::string game=argc>1?argv[1]:(std::getenv("WHJ2_GAME")?std::getenv("WHJ2_GAME"):"");
#ifdef __SWITCH__
if(game.empty())game="sdmc:/switch/whj2/game";
#endif
if(game.empty()){std::cerr<<"usage: whj2-runtime GAME [SAVE_DIR] [--smoke-frames N]\n";
 return 2;
 }if(argc<2&&std::getenv("WHJ2_LOG")){auto*log=std::freopen(std::getenv("WHJ2_LOG"),"a",stderr);
 if(!log)return 2;
 }unsigned smoke=0;
 std::string saves=argc>2?argv[2]:(std::getenv("WHJ2_SAVES")?std::getenv("WHJ2_SAVES"):"local/saves");
 if(argc>4&&std::string(argv[3])=="--smoke-frames")smoke=unsigned(std::stoul(argv[4]));
#ifdef __SWITCH__
if(argc<3)saves="sdmc:/switch/whj2/saves";
#endif
std::cerr<<"BUILD revision="<<WHJ2_BUILD_REV<<"\n";
std::cerr<<"INIT game="<<game<<" saves="<<saves<<"\n";
Frontend f(game,saves);if(argc>4&&std::string(argv[3])=="--restore-autosave"){unsigned index=unsigned(std::stoul(argv[4]));if(index<1||index>Frontend::AutoSaveCount)throw std::runtime_error("Auto save index must be1..5");auto slot=Frontend::AutoSaveBase+index-1;if(!std::filesystem::exists(f.slotPrefix(slot)+".story"))throw std::runtime_error("Auto save slot is empty");f.opening=0;f.load(slot);}
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
 }return f.run(smoke);
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";
 return 1;
 }}
