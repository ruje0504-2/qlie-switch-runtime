// SPDX-License-Identifier: GPL-2.0-or-later
// Incremental native frontend: graphics/audio integration remains in progress.
#include "story.hpp"
#include "audio.hpp"
#include "scene_images.hpp"
#include "message_text.hpp"
#include "scene_transition.hpp"
#include "sentence_wait.hpp"
#include "state_io.hpp"
#include "movie.hpp"
#include "joycon.hpp"
#include "voice.hpp"
#include "upper_menu.hpp"
#include "game_menu.hpp"
#include "auto_play.hpp"
#include "frame_pacer.hpp"
#include "fps_overlay.hpp"
#include "title_screen.hpp"
#include "choice_screen.hpp"
#include "progress.hpp"
#include "music_screen.hpp"
#include <sstream>
#include <algorithm>
extern "C" {
#include "font.h"
}
#include <SDL.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdio>
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
 whj2::Settings settings{assets};
 whj2::GameMenu gameMenu{assets,settings};
 std::vector<std::pair<std::string,std::string>>history;
 bool fastForward=false;unsigned selectedSlot=1;unsigned skipStart=0;unsigned autoPhase=0,autoPhaseFrame=0,titlePhase=0;
 bool fingerActive=false,fingerUiDrag=false;SDL_FingerID fingerId=0;float fingerX=0,fingerY=0;qlie::Bytes gestureStory;
 bool movieOnly=false,movieCaptured=false,touchOnlyTest=false,settingsTest=false,autoTest=false,fpsTest=false,titleTest=false,extraTest=false,musicTest=false;
 uint64_t mutedNonzero=0;
 qlie::Bytes touchSavedStory;std::string touchSavedText;
 std::map<std::string,std::map<std::string,std::string>>audioTemplates;
 std::array<float,3>volumes{{0.4f,1.f,1.f}};
 std::array<bool,3>loops{{true,false,false}};
 std::array<unsigned,3>audioTimes{{1500,0,0}};
 std::array<unsigned,3>audioTypes{{0,2,2}};
 KFont*font=nullptr;
 SDL_Window*window=nullptr;
 SDL_Renderer*renderer=nullptr;
 SDL_Texture*texture=nullptr;
 qlie::Bytes pixels;
 std::string text,name,status="",saveDir;
 bool menu=true,done=false,hidden=false,fpsVisible=false;
 int focus=0;
 unsigned advances=0,panelDraws=0;
 whj2::FramePacer framePacer;
 whj2::FpsOverlay fpsDisplay;
 std::map<SDL_JoystickID,SDL_GameController*>controllers;
 Frontend(const std::string&game,const std::string&saves):assets(game),story(assets),pixels(1280*720*4),saveDir(saves){unsigned subsystems=SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS;
#ifndef __SWITCH__
 subsystems|=SDL_INIT_GAMECONTROLLER;
#endif
 if(SDL_Init(subsystems))throw std::runtime_error(SDL_GetError());
 window=SDL_CreateWindow("美少女万華鏡２",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
 if(!window)throw std::runtime_error(SDL_GetError());
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
 if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!renderer)throw std::runtime_error(SDL_GetError());
 SDL_RenderSetLogicalSize(renderer,1280,720);
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,1280,720);
 font=kfont_open(nullptr,0);
 if(!font||!texture)throw std::runtime_error("Font/texture initialization failed");
 std::filesystem::create_directories(saveDir);
 audio=std::make_unique<whj2::Audio>();
 images=std::make_unique<whj2::SceneImages>(assets);
 messageText=std::make_unique<whj2::MessageText>(assets);
 transition=std::make_unique<whj2::SceneTransition>(assets);
 movie=std::make_unique<whj2::Movie>(assets,*audio);
 voices=std::make_unique<whj2::VoiceResolver>(assets);
 upperMenu=std::make_unique<whj2::UpperMenu>(assets);
 title=std::make_unique<whj2::TitleScreen>(assets);
 choiceScreen=std::make_unique<whj2::ChoiceScreen>(assets);
 musicScreen=std::make_unique<whj2::MusicScreen>(assets,*audio,settings,font);
 if(std::filesystem::exists(saveDir+"/system.progress"))progress.restore(qlie::file(saveDir+"/system.progress"));
 if(std::filesystem::exists(saveDir+"/settings.cfg"))settings.restore(qlie::file(saveDir+"/settings.cfg"));
 applySettings(false);
 for(auto type:{"music","sound"}){std::istringstream in(qlie::cp932(assets.read(std::string("avg/imoscripter_template_")+type+".txt")));
 std::string l;
 while(std::getline(in,l)){auto p=l.find(61);
 if(p!=l.npos)audioTemplates[type][whj2::trim(l.substr(0,p))]=whj2::trim(l.substr(p+1));
 }}
 showTitle();
 }
 ~Frontend(){musicScreen.reset();movie.reset();
 for(auto&c:controllers)SDL_GameControllerClose(c.second);
 audio.reset();
 kfont_close(font);
 if(texture)SDL_DestroyTexture(texture);
 if(renderer)SDL_DestroyRenderer(renderer);
 if(window)SDL_DestroyWindow(window);
 SDL_Quit();
 }
 void syncProgress(){if(progress.capture(story))qlie::write_file(saveDir+"/system.progress",progress.save());}
 void showTitle(){syncProgress();menu=true;hidden=false;focus=0;upperMenu->close();gameMenu.close();title->reset(false,progress.extras());audio->stopAll();audio->play(0,assets.read("music/wbgm06.ogg"),true,255.f/256,0,"music/wbgm06.ogg",0);}
 void applySettings(bool persist=true){audio->setUserGains(settings.gains());images->setWindowAlpha(unsigned(settings.get("WindowAlpha")));messageText->setSpeed(settings.get("TextSpeed"));if(!text.empty())refreshMessage();if(persist)qlie::write_file(saveDir+"/settings.cfg",settings.save());gameMenu.settingsDirty=false;}
 void line(const std::string&s,int x,int y,int size=26,uint32_t color=0xffffff){KImage dst{0,0,1280,720,1280*4,pixels.data()};
 int start=x;
 for(auto cp:utf8(s)){int width=cp<128?size/2:size;
 if(cp==10||x+width>1230){x=start;
 y+=size+10;
 if(cp==10)continue;
 }if(y+size>710)break;
 kfont_draw(font,&dst,cp,x,y,width,size,color);
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
 if(attrs.count("clear")){audio->volume(ch,0,0,true);
 return true;
 }if(attrs.count("loop"))loops[ch]=attrs["loop"]=="true";
 if(attrs.count("vol"))volumes[ch]=std::stof(attrs["vol"])/100.f*(ch==0?0.4f:1.f);
 if(attrs.count("time"))audioTimes[ch]=unsigned(std::stoul(attrs["time"]));
 if(attrs.count("voltype")){auto type=attrs["voltype"];if(type=="bgm")audioTypes[ch]=0;else if(type=="voice")audioTypes[ch]=1;else if(type=="se")audioTypes[ch]=2;else std::cerr<<"PENDING audio volume type "<<type<<"\n";}
 unsigned ms=audioTimes[ch];
 if(attrs.count("file")){auto f=attrs["file"];
 if(f=="none"){audio->volume(ch,0,ms,true);
 return true;
 }std::string path=(ch==0?"music/":"se/")+f;
 if(!assets.exists(path))path+=".ogg";
 if(!assets.exists(path))throw std::runtime_error("Missing audio "+path);
 audio->play(ch,assets.read(path),loops[ch],volumes[ch],ms,path,audioTypes[ch]);
 }else audio->volume(ch,volumes[ch],ms);
 return true;
 }
 void playVoice(const std::string&id){
 if(id.empty())return;
 auto match=voices->resolve(id);
 if(!match){std::cerr<<"VOICE unresolved id="<<id;for(auto&candidate:voices->candidates(id))std::cerr<<" candidate="<<candidate;std::cerr<<"\n";if(voices->fileErrors)throw std::runtime_error("Voice file not found: "+id);return;}
 auto bytes=assets.read(match->request);audio->play(3,bytes,false,voices->gain,0,match->request);
 std::cerr<<"VOICE playing id="<<id<<" source="<<match->source.path<<" offset="<<match->source.offset<<" bytes="<<bytes.size()<<" loose="<<match->source.loose<<"\n";
 }
 void refreshMessage(bool show=false){images->setMessageImage(messageText->render(font,images->messageStyle()),show);lastMessageVisible=messageText->visible();}
 void next(bool resume=false){if(menu||story.state.finished)return;
 if(!resume){images->beginPage();pendingBody.clear();pendingSpeaker.clear();pendingVoice.clear();}
 auto&body=pendingBody;auto&speaker=pendingSpeaker;auto&voiceId=pendingVoice;
 for(unsigned guard=0;guard<10000;guard++){auto e=story.next();syncProgress();
 if(e.type=="choice"){sentenceWait.cancel();transition->present();focus=0;choiceScreen->show(story.state.choices,font);
 text=body.empty()?"選択してください":body;
 messageText->start(text);messageText->reveal();refreshMessage(true);
 name=speaker;
 images->selectFace(speaker,voiceId);
 return;
 }if(e.type=="page"){if(body.empty())continue;
 transition->present();
 text=body;
 messageText->start(text);autoPlay.page(settings.get("AutoClickWait"));refreshMessage(true);
 name=speaker;
 history.emplace_back(name,text);
 images->selectFace(speaker,voiceId);
 playVoice(voiceId);
 sentenceWait.page(true,audio->playing(3));
 advances++;
 std::cerr<<"PAGE "<<e.file<<":"<<e.line<<"\n";
 return;
 }if(e.type=="finish"||e.type=="finished"){showTitle();
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
 }if(e.type=="command"&&e.value.rfind("^sentence",0)==0){qlie::Bytes scene(1280*720*4);for(size_t i=0;i<scene.size();i+=4){scene[i]=35;scene[i+1]=25;scene[i+2]=20;scene[i+3]=255;}images->drawScene(scene);transition->blend(scene);transition->command(e.value,scene,&sentenceWait);continue;
 }if(e.type=="wait"||e.type=="click-wait"){sentenceWait.native(e.type=="click-wait",e.value.empty()?0:std::stod(e.value));transition->present();return;
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
 std::string slotPrefix(unsigned slot=1)const{char number[16];std::snprintf(number,sizeof(number),"/slot%02u",slot);return saveDir+number;}
 void save(unsigned slot=1){if(menu||movie->playing())return;
 qlie::Bytes scene(1280*720*4,0);images->drawScene(scene);transition->blend(scene);if(!hidden){images->drawMessages(scene);images->drawFaces(scene);}qlie::Bytes thumb(160*90*4);for(unsigned y=0;y<90;y++)for(unsigned x=0;x<160;x++)for(unsigned c=0;c<4;c++)thumb[(y*160+x)*4+c]=scene[((y*8)*1280+x*8)*4+c];qlie::write_file(slotPrefix(slot)+".thumbnail",thumb);
 auto b=story.save();
 qlie::write_file(slotPrefix(slot)+".story",b);
 qlie::Bytes t(text.begin(),text.end());
 qlie::write_file(slotPrefix(slot)+".text",t);
 qlie::Bytes n(name.begin(),name.end());
 qlie::write_file(slotPrefix(slot)+".name",n);
 qlie::write_file(slotPrefix(slot)+".images",images->save());
 qlie::write_file(slotPrefix(slot)+".transition",transition->save());
 whj2::StateWriter flow;flow.str("WHJ2-flow-1");auto waitState=sentenceWait.save();flow.str(std::string(waitState.begin(),waitState.end()));flow.str(pendingBody);flow.str(pendingSpeaker);flow.str(pendingVoice);qlie::write_file(slotPrefix(slot)+".flow",flow.bytes);
 qlie::write_file(slotPrefix(slot)+".message",messageText->save());
 qlie::write_file(slotPrefix(slot)+".auto",autoPlay.save());
 qlie::write_file(slotPrefix(slot)+".audio",audio->save());
 whj2::StateWriter settings;
 settings.str("WHJ2-mixer-2");
 for(unsigned i=0;i<3;i++){settings.f(volumes[i]);
 settings.n(loops[i]);
 settings.n(audioTimes[i]);settings.n(audioTypes[i]);
 }qlie::write_file(slotPrefix(slot)+".mixer",settings.bytes);
 whj2::StateWriter log;log.str("WHJ2-history-1");log.n(history.size());for(auto&entry:history){log.str(entry.first);log.str(entry.second);}qlie::write_file(slotPrefix(slot)+".history",log.bytes);
 status="現在の状態を保存しました。";
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
 if(std::filesystem::exists(slotPrefix(slot)+".flow")){auto flowBytes=qlie::file(slotPrefix(slot)+".flow");whj2::StateReader flow{flowBytes};if(flow.str()!="WHJ2-flow-1")throw std::runtime_error("Flow state version");auto state=flow.str();sentenceWait.restore(qlie::Bytes(state.begin(),state.end()));pendingBody=flow.str();pendingSpeaker=flow.str();pendingVoice=flow.str();flow.end();}
 else {sentenceWait=whj2::SentenceWait{};sentenceWait.page();pendingBody=text;pendingSpeaker=name;pendingVoice.clear();}
 if(std::filesystem::exists(slotPrefix(slot)+".message"))messageText->restore(qlie::file(slotPrefix(slot)+".message"));else {messageText->start(text);messageText->reveal();}messageText->setSpeed(settings.get("TextSpeed"));refreshMessage();
 if(std::filesystem::exists(slotPrefix(slot)+".auto"))autoPlay.restore(qlie::file(slotPrefix(slot)+".auto"));else {autoPlay.enabled=false;autoPlay.page(settings.get("AutoClickWait"));}
 history.clear();if(std::filesystem::exists(slotPrefix(slot)+".history")){auto bytes=qlie::file(slotPrefix(slot)+".history");whj2::StateReader log{bytes};if(log.str()!="WHJ2-history-1")throw std::runtime_error("History version");auto count=log.n();for(uint64_t i=0;i<count;i++){auto who=log.str();auto words=log.str();history.emplace_back(who,words);}log.end();}
 menu=false;hidden=false;
 focus=0;if(story.state.awaiting)choiceScreen->show(story.state.choices,font);
 status="静止画・音声位置を含む状態を読み込みました。";
 }
 void activate(){if(!menu&&hidden){hidden=false;return;}if(movie->playing()){movie->stop();
 if(movieOnly)done=true;
 else next();
 return;
 }if(menu){auto action=title->confirm();if(action==whj2::TitleScreen::None)return;uiSound("Sys_OnClick");
 if(action==whj2::TitleScreen::Start){audio->stopAll();history.clear();autoPlay.enabled=false;story.start();progress.apply(story);menu=false;name="";focus=0;next();}
 else if(action==whj2::TitleScreen::Load)openPanel(whj2::GameMenu::Load);
 else if(action==whj2::TitleScreen::Settings)openPanel(whj2::GameMenu::Settings);
 else if(action==whj2::TitleScreen::Extra)title->setExtraPage(true);
 else if(action==whj2::TitleScreen::Sound)musicScreen->open();
 else if(action==whj2::TitleScreen::Back)title->setExtraPage(false);
 else if(action==whj2::TitleScreen::Quit)performUi(whj2::GameMenu::Quit);
 else std::cerr<<"PENDING-TITLE extra modes\n";
 }else if(story.state.awaiting){if(!choiceScreen->ready())return;story.choose(unsigned(focus));
 next();
 }else {if(sentenceWait.blocksClick())return;
 if(settings.get("ClickVoiceStop"))audio->volume(3,0,0,true);
 if(messageText->reveal()){auto action=sentenceWait.prewaiting()?sentenceWait.confirm():whj2::SentenceWait::None;refreshMessage();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);return;}
 auto action=sentenceWait.confirm();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);}
 }
 void uiSound(const std::string&name){auto path="misc/"+name+".wav";audio->play(whj2::Audio::SystemChannel,assets.read(path),false,1,0,path,3);}
 void performUi(whj2::GameMenu::Action action){using UI=whj2::GameMenu;
 if(action==UI::Nothing)return;
 if(action==UI::SettingsChanged){applySettings();uiSound("Sys_OnMouse");return;}
 uiSound(action==UI::No||action==UI::Close?"Sys_Cancel":"Sys_OnClick");
 if(action==UI::No){gameMenu.asking=false;return;}
 if(action==UI::Yes){action=gameMenu.requested;gameMenu.asking=false;
 if(action==UI::Slot){save(selectedSlot);gameMenu.close();return;}
 if(action==UI::ResetSettings){settings.reset();applySettings();return;}
 if(action==UI::Quit){done=true;return;}
 if(action==UI::Title){if(movie->playing())movie->stop();showTitle();return;}}
 if(action==UI::Close){gameMenu.close();return;}
 if(action==UI::Quit||action==UI::Title||action==UI::ResetSettings){gameMenu.ask(action);return;}
 if(action==UI::Slot){selectedSlot=unsigned(gameMenu.slot());
 if(gameMenu.panel==UI::Save){if(std::filesystem::exists(slotPrefix(selectedSlot)+".story")){gameMenu.ask(UI::Slot);return;}save(selectedSlot);gameMenu.close();}
 else if(gameMenu.panel==UI::Load){if(std::filesystem::exists(slotPrefix(selectedSlot)+".story")){load(selectedSlot);gameMenu.close();}else status="保存データがありません。";}
 }
 }
 void openPanel(whj2::GameMenu::Panel panel){if(movie->playing())return;if(panel==whj2::GameMenu::Save&&menu)return;upperMenu->close();status.clear();gameMenu.open(panel);}
 void upperAction(whj2::UpperMenu::Action action){if(action==whj2::UpperMenu::None)return;upperMenu->close();
 if(action==whj2::UpperMenu::Quit){performUi(whj2::GameMenu::Quit);return;}
 if(action==whj2::UpperMenu::Title){performUi(whj2::GameMenu::Title);return;}
 if(action==whj2::UpperMenu::Screen){auto flags=SDL_GetWindowFlags(window);if(SDL_SetWindowFullscreen(window,(flags&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP))std::cerr<<"PENDING-UPPERMENU screen mode: "<<SDL_GetError()<<"\n";}
 }
 void leaveMusic(){audio->play(0,assets.read("music/wbgm06.ogg"),true,255.f/256,0,"music/wbgm06.ogg",0);}
 void back(){if(musicScreen->visible()){if(musicScreen->back())leaveMusic();return;}if(gameMenu.asking){performUi(whj2::GameMenu::No);return;}if(gameMenu.visible()){performUi(whj2::GameMenu::Close);return;}if(upperMenu->visible()){upperMenu->close();return;}if(!menu)hidden=!hidden;}
 void drawPanel(){++panelDraws;using UI=whj2::GameMenu;gameMenu.draw(pixels);
 if(gameMenu.asking){auto prompt=gameMenu.requested==UI::Quit?"ゲームを終了しますか？":gameMenu.requested==UI::Title?"タイトルに戻りますか？":gameMenu.requested==UI::ResetSettings?"設定を初期状態に戻しますか？":"上書きしてよろしいですか？";line(prompt,440,275,28);return;}
 if(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load){for(int i=0;i<8;i++){unsigned slot=unsigned(gameMenu.page*8+i+1);int x=i<4?198:834,y=136+(i%4)*135;line("No."+std::to_string(slot),x,y,16,0xff5555);bool exists=std::filesystem::exists(slotPrefix(slot)+".story");if(exists){if(std::filesystem::exists(slotPrefix(slot)+".thumbnail")){whj2::Image thumbnail{160,90,qlie::file(slotPrefix(slot)+".thumbnail")};if(thumbnail.pixels.size()!=160*90*4)throw std::runtime_error("Thumbnail size");whj2::RenderQueue queue;queue.image(thumbnail,whj2::imagePlacement(i<4?27:660,137+(i%4)*135,1,1,0,0,0,160,90),0,255,0,0xffffff);queue.draw(pixels,1280,720);}auto content=qlie::file(slotPrefix(slot)+".text");std::string excerpt(content.begin(),content.end());auto chars=utf8(excerpt);int px=x;for(size_t n=0;n<chars.size()&&n<23;n++){kfont_draw(font,newImage(),chars[n],px,y+22,18,18,0xffffff);px+=chars[n]<128?9:18;}}else line("NO DATA",x,y+24,18,0x999999);}
 if(!status.empty())line(status,30,677,18);}
 if(gameMenu.panel==UI::History){size_t offset=std::min(size_t(gameMenu.historyOffset),history.size());for(size_t row=0;row<4&&row+offset<history.size();row++){auto&entry=history[history.size()-1-offset-row];int y=64+int(3-row)*149;line(whj2::messageDisplayName(entry.first),230,y,24);int x=230,cy=y+30;for(auto cp:utf8(entry.second)){int width=cp<128?12:24;if(cp==10||x+width>1140){x=230;cy+=30;if(cp==10)continue;}if(cy>=y+140)break;kfont_draw(font,newImage(),cp,x,cy,width,24,0xffffff);x+=width;}}}
 }
 KImage*newImage(){static KImage dst;dst={0,0,1280,720,1280*4,pixels.data()};return &dst;}
 void present(){if(fpsVisible)fpsDisplay.draw(pixels);SDL_UpdateTexture(texture,nullptr,pixels.data(),1280*4);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);framePacer.wait();SDL_RenderPresent(renderer);fpsDisplay.presented(SDL_GetTicks64());}
 void draw(){panelDraws=0;if(musicScreen->visible()){std::fill(pixels.begin(),pixels.end(),0);musicScreen->draw(pixels);present();return;}if(movie->playing()){pixels=movie->image().pixels;
 upperMenu->draw(pixels);
 if(gameMenu.visible())drawPanel();
 present();
 return;
 }for(size_t i=0;i<pixels.size();i+=4){pixels[i]=menu?0:35;
 pixels[i+1]=menu?0:25;
 pixels[i+2]=menu?0:20;
 pixels[i+3]=255;
 }if(!menu){images->drawScene(pixels);transition->blend(pixels);}
 if(menu){title->draw(pixels);
 }else if(!hidden&&gameMenu.panel!=whj2::GameMenu::History){images->drawMessages(pixels);
 if(images->messageVisible())line(whj2::messageDisplayName(name),200,570,26,0xffffff);
 if(story.state.awaiting)choiceScreen->draw(pixels);
 }if(!menu&&!hidden&&gameMenu.panel!=whj2::GameMenu::History){images->drawFaces(pixels);if(images->messageVisible())gameMenu.drawAutoButton(pixels,autoPlay.enabled);}
 if(!menu)upperMenu->draw(pixels);
 if(gameMenu.visible())drawPanel();
 present();
 }
 void snapshot(const std::string&name){SDL_Surface*surface=SDL_CreateRGBSurfaceFrom(pixels.data(),1280,720,32,1280*4,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
 if(!surface||SDL_SaveBMP(surface,(saveDir+"/"+name).c_str()))throw std::runtime_error("Cannot save screenshot");
 SDL_FreeSurface(surface);
 std::cerr<<"SCREENSHOT "<<saveDir<<"/"<<name<<"\n";
 }
 void touchDrag(float x,float y,bool start){int w,h;SDL_GetWindowSize(window,&w,&h);float lx,ly;SDL_RenderWindowToLogical(renderer,int(x*w),int(y*h),&lx,&ly);if(musicScreen->visible()){bool moved=musicScreen->drag(lx,ly);if(start)fingerUiDrag=moved;return;}if(start)fingerUiDrag=gameMenu.beginDrag(lx,ly);else gameMenu.drag(lx,ly);if(fingerUiDrag&&gameMenu.settingsDirty){applySettings(false);uiSound("Sys_OnMouse");}}
 void pointer(int px,int py){float x,y;
 SDL_RenderWindowToLogical(renderer,px,py,&x,&y);
 if(musicScreen->visible()){if(musicScreen->tap(x,y))leaveMusic();return;}
 if(gameMenu.visible()){performUi(gameMenu.tap(x,y));return;}
 if(upperMenu->visible()){upperAction(upperMenu->tap(x,y));return;}
 if(!menu&&!hidden&&images->messageVisible()&&std::abs(x-1170)<=14&&std::abs(y-700)<=14){autoPlay.enabled=!autoPlay.enabled;uiSound("Sys_OnClick");return;}
 if(menu){if(!title->ready()){activate();return;}int target=title->hit(x,y);if(!title->select(target))return;focus=title->selection();
 }else if(story.state.awaiting){int target=choiceScreen->hit(x,y);if(target<0)return;focus=target;choiceScreen->select(focus);
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
 if(button==SDL_CONTROLLER_BUTTON_BACK){fpsVisible=!fpsVisible;return;}
 if(musicScreen->visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A){musicScreen->confirm();if(!musicScreen->visible())leaveMusic();}else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)musicScreen->move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)musicScreen->move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)musicScreen->move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)musicScreen->move(1,0);return;}
 if(gameMenu.visible()){if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_A)performUi(gameMenu.confirm());else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)gameMenu.move(0,-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)gameMenu.move(0,1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)gameMenu.move(-1,0);else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)gameMenu.move(1,0);else if(button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER&&(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load))gameMenu.page=(gameMenu.page+14)%15;else if(button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER&&(gameMenu.panel==UI::Save||gameMenu.panel==UI::Load))gameMenu.page=(gameMenu.page+1)%15;if(gameMenu.settingsDirty){applySettings();uiSound("Sys_OnMouse");}return;}
 if(button==SDL_CONTROLLER_BUTTON_Y&&!menu){upperMenu->toggle();return;}
 if(upperMenu->visible()){if(button==SDL_CONTROLLER_BUTTON_A)upperAction(upperMenu->confirm());else if(button==SDL_CONTROLLER_BUTTON_B)back();else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP||button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)upperMenu->move(-1);else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN||button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)upperMenu->move(1);return;}
 if(menu){switch(button){case SDL_CONTROLLER_BUTTON_B:if(title->isExtraPage()){title->setExtraPage(false);uiSound("Sys_Cancel");}break;case SDL_CONTROLLER_BUTTON_A:activate();break;case SDL_CONTROLLER_BUTTON_DPAD_UP:case SDL_CONTROLLER_BUTTON_DPAD_LEFT:focus=title->move(-1);uiSound("Sys_OnMouse");break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:focus=title->move(1);uiSound("Sys_OnMouse");break;case SDL_CONTROLLER_BUTTON_X:openPanel(UI::Settings);break;case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:openPanel(UI::Load);break;default:break;}return;}
 switch(button){case SDL_CONTROLLER_BUTTON_A:activate();break;
 case SDL_CONTROLLER_BUTTON_B:back();break;
 case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:openPanel(UI::Save);break;
 case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:openPanel(UI::Load);break;
 case SDL_CONTROLLER_BUTTON_X:openPanel(UI::Settings);break;
 case SDL_CONTROLLER_BUTTON_DPAD_UP:if(!menu)openPanel(UI::History);else focus=(focus+2)%3;break;
 case SDL_CONTROLLER_BUTTON_DPAD_DOWN:{int count=menu?3:int(story.state.choices.size());if(count){focus=(focus+1)%count;if(story.state.awaiting)choiceScreen->select(focus);}break;}
 case SDL_CONTROLLER_BUTTON_DPAD_LEFT:case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:if(story.state.awaiting&&!story.state.choices.empty()){int count=int(story.state.choices.size());focus=(focus+count+(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1))%count;choiceScreen->select(focus);}break;
 default:break;
 }}
 int run(unsigned smoke){
#ifdef __SWITCH__
 std::cerr<<"INPUT libnx Joy-Con and SDL touch\n";
#else
 std::cerr<<"INPUT physical joysticks="<<SDL_NumJoysticks()<<"\n";
 for(int i=0;i<SDL_NumJoysticks();i++)controllerAdded(i);
#endif
 unsigned frames=0;
 if(smoke&&!titleTest&&!movieOnly)title->skipIntro();
 uint64_t lastUpdate=SDL_GetTicks64();
 while(!done){auto now=SDL_GetTicks64();
 if(musicScreen->visible()){musicScreen->update(double(now-lastUpdate));if(musicScreen->takeSettingsChanged())applySettings();}
 else if(menu&&!gameMenu.visible())title->update(double(now-lastUpdate));
 if(!menu&&!movie->playing()&&!gameMenu.visible()&&!upperMenu->visible()){
 images->update(double(now-lastUpdate));
 if(story.state.awaiting)choiceScreen->update(double(now-lastUpdate));
 messageText->update(double(now-lastUpdate));if(messageText->visible()!=lastMessageVisible)refreshMessage();
 transition->update(double(now-lastUpdate));
 autoPlay.tick(double(now-lastUpdate),messageText->visible(),settings.get("AutoTextWait"),settings.get("TextSpeed"));
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
 }auto pad=joycon.poll(now);
 fastForward=pad.fastForward;
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
 }next();
 }}SDL_Event e;
 while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)done=true;
else if(e.type==SDL_FINGERDOWN){if(!fingerActive){fingerActive=true;fingerId=e.tfinger.fingerId;fingerX=e.tfinger.x;fingerY=e.tfinger.y;touchDrag(fingerX,fingerY,true);}}
 else if(e.type==SDL_FINGERMOTION&&fingerActive&&e.tfinger.fingerId==fingerId&&fingerUiDrag){touchDrag(e.tfinger.x,e.tfinger.y,false);}
 else if(e.type==SDL_FINGERUP&&fingerActive&&e.tfinger.fingerId==fingerId){fingerActive=false;
 float dx=(e.tfinger.x-fingerX)*1280,dy=(e.tfinger.y-fingerY)*720;
 if(fingerUiDrag){touchDrag(e.tfinger.x,e.tfinger.y,false);gameMenu.endDrag();fingerUiDrag=false;applySettings();}
 else if(!menu&&!gameMenu.visible()&&upperMenu->swipe(dx,dy)){std::cerr<<"INPUT downward swipe upper menu\n";}
 else if(std::abs(dx)<24&&std::abs(dy)<24){int w,h;SDL_GetWindowSize(window,&w,&h);std::cerr<<"INPUT touch\n";pointer(int(e.tfinger.x*w),int(e.tfinger.y*h));}
 }else if(e.type==SDL_CONTROLLERAXISMOTION&&e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT){fastForward=e.caxis.value>16000;}else if(e.type==SDL_CONTROLLERDEVICEADDED)controllerAdded(e.cdevice.which);
 else if(e.type==SDL_CONTROLLERDEVICEREMOVED){auto i=controllers.find(e.cdevice.which);
 if(i!=controllers.end()){SDL_GameControllerClose(i->second);
 controllers.erase(i);
 }}else if(e.type==SDL_CONTROLLERBUTTONDOWN){std::cerr<<"INPUT controller button="<<unsigned(e.cbutton.button)<<"\n";
#ifndef __SWITCH__
controllerKey(e.cbutton.button);
#endif
}}if(fastForward&&!menu&&!gameMenu.visible()&&!upperMenu->visible()&&!movie->playing()&&!story.state.awaiting){hidden=false;messageText->reveal();refreshMessage();auto action=sentenceWait.force();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);}
 draw();
 if(smoke&&gameMenu.visible()&&panelDraws!=1)throw std::runtime_error("Panel composited more than once");
 if(movieOnly&&!movieCaptured&&movie->elapsedMs()>=1000){snapshot("movie.bmp");
 movieCaptured=true;
 std::cerr<<"MOVIE screenshot audio_clock_ms="<<movie->elapsedMs()<<"\n";
 }if(smoke){if(musicTest){
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
 if(frames==22){touchSavedStory=qlie::file(slotPrefix()+".story");touchSavedText=text;controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);}
 if(frames==24){if(gameMenu.panel!=whj2::GameMenu::Load)throw std::runtime_error("R did not open load");snapshot("load-menu.bmp");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==26){if(gameMenu.visible()||story.save()!=touchSavedStory||text!=touchSavedText)throw std::runtime_error("Selected slot load mismatch");controller(SDL_CONTROLLER_BUTTON_X);}
 if(frames==28){if(gameMenu.panel!=whj2::GameMenu::Settings)throw std::runtime_error("X did not open settings");snapshot("settings-menu.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==30)controller(SDL_CONTROLLER_BUTTON_DPAD_UP);
 if(frames==32){if(gameMenu.panel!=whj2::GameMenu::History||history.empty())throw std::runtime_error("Up did not open populated history");snapshot("history-menu.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==34){gestureStory=story.save();controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==36){if(!upperMenu->visible()||story.save()!=gestureStory)throw std::runtime_error("Y changed story");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==38){gestureStory=story.save();SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=.5f;ev.tfinger.y=.2f;SDL_PushEvent(&ev);ev.type=SDL_FINGERUP;ev.tfinger.y=.5f;SDL_PushEvent(&ev);}
 if(frames==40){if(!upperMenu->visible()||story.save()!=gestureStory)throw std::runtime_error("Swipe changed story");snapshot("upper-menu.bmp");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==42){skipStart=advances;SDL_Event ev{};ev.type=SDL_CONTROLLERAXISMOTION;ev.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;ev.caxis.value=32767;SDL_PushEvent(&ev);}
 if(frames==48){if(advances<=skipStart)throw std::runtime_error("ZR did not force advance");SDL_Event ev{};ev.type=SDL_CONTROLLERAXISMOTION;ev.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;ev.caxis.value=0;SDL_PushEvent(&ev);}
 if(frames==50){if(fastForward)throw std::runtime_error("ZR release did not stop");controller(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);}
 if(frames==52)controller(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
 if(frames==54){if(gameMenu.page!=1)throw std::runtime_error("Save page navigation");controller(SDL_CONTROLLER_BUTTON_A);}
 if(frames==56){if(!std::filesystem::exists(slotPrefix(9)+".story"))throw std::runtime_error("Slot 9 missing");controller(SDL_CONTROLLER_BUTTON_Y);}
 if(frames==58)controller(SDL_CONTROLLER_BUTTON_A);
 if(frames==60){if(!gameMenu.asking||done)throw std::runtime_error("Quit bypassed confirmation");snapshot("confirmation.bmp");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==62){if(gameMenu.visible())throw std::runtime_error("B did not cancel dialog");controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==64){if(history.size()<4||gameMenu.panel!=whj2::GameMenu::History)throw std::runtime_error("History lost previous pages");snapshot("history-menu.bmp");controller(SDL_CONTROLLER_BUTTON_DPAD_UP);}
 if(frames==66){if(gameMenu.historyOffset!=1)throw std::runtime_error("History navigation");controller(SDL_CONTROLLER_BUTTON_B);}
 if(frames==68&&gameMenu.visible())throw std::runtime_error("History cancel");
 if(++frames>=smoke){snapshot("touch.bmp");std::cerr<<"TARGET INPUT PASS L/R panels and slots / X settings / history / B text visibility / A confirm / Y and swipe / ZR hold-release / cancel dialog\n";done=true;}continue;
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

 }}auto ast=audio->stats();
 std::cerr<<"AUDIO output_frames="<<ast.frames<<" nonzero_frames="<<ast.nonzero<<"\n";
 std::cerr<<"FRAME cap="<<whj2::FramePacer::Limit<<" min_interval_ms="<<framePacer.minimumMilliseconds()<<"\n";
 std::cerr<<"EXIT normal text_advances="<<advances<<" frames="<<frames<<"\n";
 return 0;
 }
};
int main(int argc,char**argv){try{std::string game=argc>1?argv[1]:(std::getenv("WHJ2_GAME")?std::getenv("WHJ2_GAME"):"");
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
Frontend f(game,saves);if(argc>4&&std::string(argv[3])=="--title-smoke"){f.titleTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--music-smoke"){f.musicTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--extra-smoke"){f.extraTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--fps-smoke"){f.fpsTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--auto-smoke"){f.autoTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--settings-smoke"){f.settingsTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--touch-smoke"){f.touchOnlyTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--movie"){f.movieOnly=true;
 f.audio->stopAll();
 f.menu=false;
 f.movie->open(argv[4]);
 }else if(argc>4&&std::string(argv[3])=="--movie-file"){f.movieOnly=true;
 f.audio->stopAll();
 f.menu=false;
 f.movie->openBytes(qlie::file(argv[4]),"local-test");
 }return f.run(smoke);
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";
 return 1;
 }}
