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
 std::unique_ptr<whj2::Audio>audio;
 std::unique_ptr<whj2::SceneImages>images;
 std::unique_ptr<whj2::SceneTransition>transition;
 whj2::SentenceWait sentenceWait;
 std::unique_ptr<whj2::MessageText>messageText;
 size_t lastMessageVisible=SIZE_MAX;
 std::string pendingBody,pendingSpeaker,pendingVoice;
 std::unique_ptr<whj2::Movie>movie;
 std::unique_ptr<whj2::VoiceResolver>voices;
 bool movieOnly=false,movieCaptured=false,touchOnlyTest=false;
 qlie::Bytes touchSavedStory;std::string touchSavedText;
 std::map<std::string,std::map<std::string,std::string>>audioTemplates;
 std::array<float,3>volumes{{0.4f,1.f,1.f}};
 std::array<bool,3>loops{{true,false,false}};
 std::array<unsigned,3>audioTimes{{1500,0,0}};
 KFont*font=nullptr;
 SDL_Window*window=nullptr;
 SDL_Renderer*renderer=nullptr;
 SDL_Texture*texture=nullptr;
 qlie::Bytes pixels;
 std::string text,name,status="画面・音声の接続は開発中です。",saveDir;
 bool menu=true,done=false,hidden=false;
 int focus=0;
 unsigned advances=0;
 std::map<SDL_JoystickID,SDL_GameController*>controllers;
 Frontend(const std::string&game,const std::string&saves):assets(game),story(assets),pixels(1280*720*4),saveDir(saves){unsigned subsystems=SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS;
#ifndef __SWITCH__
 subsystems|=SDL_INIT_GAMECONTROLLER;
#endif
 if(SDL_Init(subsystems))throw std::runtime_error(SDL_GetError());
 window=SDL_CreateWindow("WHJ2 native runtime - development",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
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
 for(auto type:{"music","sound"}){std::istringstream in(qlie::cp932(assets.read(std::string("avg/imoscripter_template_")+type+".txt")));
 std::string l;
 while(std::getline(in,l)){auto p=l.find(61);
 if(p!=l.npos)audioTemplates[type][whj2::trim(l.substr(0,p))]=whj2::trim(l.substr(p+1));
 }}}
 ~Frontend(){movie.reset();
 for(auto&c:controllers)SDL_GameControllerClose(c.second);
 audio.reset();
 kfont_close(font);
 if(texture)SDL_DestroyTexture(texture);
 if(renderer)SDL_DestroyRenderer(renderer);
 if(window)SDL_DestroyWindow(window);
 SDL_Quit();
 }
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
 }for(auto&v:attrs)if(v.first!="file"&&v.first!="loop"&&v.first!="vol"&&v.first!="time"&&v.first!="clear")throw std::runtime_error("Unsupported audio property "+v.first);
 if(attrs.count("clear")){audio->volume(ch,0,0,true);
 return true;
 }if(attrs.count("loop"))loops[ch]=attrs["loop"]=="true";
 if(attrs.count("vol"))volumes[ch]=std::stof(attrs["vol"])/100.f*(ch==0?0.4f:1.f);
 if(attrs.count("time"))audioTimes[ch]=unsigned(std::stoul(attrs["time"]));
 unsigned ms=audioTimes[ch];
 if(attrs.count("file")){auto f=attrs["file"];
 if(f=="none"){audio->volume(ch,0,ms,true);
 return true;
 }std::string path=(ch==0?"music/":"se/")+f;
 if(!assets.exists(path))path+=".ogg";
 if(!assets.exists(path))throw std::runtime_error("Missing audio "+path);
 audio->play(ch,assets.read(path),loops[ch],volumes[ch],ms,path);
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
 for(unsigned guard=0;guard<10000;guard++){auto e=story.next();
 if(e.type=="choice"){sentenceWait.cancel();transition->present();focus=0;
 text=body.empty()?"選択してください":body;
 messageText->start(text);messageText->reveal();refreshMessage(true);
 name=speaker;
 images->selectFace(speaker,voiceId);
 return;
 }if(e.type=="page"){if(body.empty())continue;
 transition->present();
 text=body;
 messageText->start(text);refreshMessage(true);
 name=speaker;
 images->selectFace(speaker,voiceId);
 playVoice(voiceId);
 sentenceWait.page(true,audio->playing(3));
 advances++;
 std::cerr<<"PAGE "<<e.file<<":"<<e.line<<"\n";
 return;
 }if(e.type=="finish"||e.type=="finished"){menu=true;
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
 void save(){if(menu||movie->playing())return;
 auto b=story.save();
 qlie::write_file(saveDir+"/slot01.story",b);
 qlie::Bytes t(text.begin(),text.end());
 qlie::write_file(saveDir+"/slot01.text",t);
 qlie::Bytes n(name.begin(),name.end());
 qlie::write_file(saveDir+"/slot01.name",n);
 qlie::write_file(saveDir+"/slot01.images",images->save());
 qlie::write_file(saveDir+"/slot01.transition",transition->save());
 whj2::StateWriter flow;flow.str("WHJ2-flow-1");auto waitState=sentenceWait.save();flow.str(std::string(waitState.begin(),waitState.end()));flow.str(pendingBody);flow.str(pendingSpeaker);flow.str(pendingVoice);qlie::write_file(saveDir+"/slot01.flow",flow.bytes);
 qlie::write_file(saveDir+"/slot01.message",messageText->save());
 qlie::write_file(saveDir+"/slot01.audio",audio->save());
 whj2::StateWriter settings;
 settings.str("WHJ2-mixer-1");
 for(unsigned i=0;i<3;i++){settings.f(volumes[i]);
 settings.n(loops[i]);
 settings.n(audioTimes[i]);
 }qlie::write_file(saveDir+"/slot01.mixer",settings.bytes);
 status="現在の状態を保存しました（演出状態の復元は開発中）。";
 }
 void load(){if(movie->playing())return;
 if(!std::filesystem::exists(saveDir+"/slot01.story")){status="保存データがありません。";
 return;
 }story.restore(qlie::file(saveDir+"/slot01.story"));
 auto t=qlie::file(saveDir+"/slot01.text"),n=qlie::file(saveDir+"/slot01.name");
 text.assign(t.begin(),t.end());
 name.assign(n.begin(),n.end());
 images->restore(qlie::file(saveDir+"/slot01.images"));
 if(std::filesystem::exists(saveDir+"/slot01.transition"))transition->restore(qlie::file(saveDir+"/slot01.transition"));else transition->reset();
 audio->restore(qlie::file(saveDir+"/slot01.audio"),assets);
 auto b=qlie::file(saveDir+"/slot01.mixer");
 whj2::StateReader settings{b};
 if(settings.str()!="WHJ2-mixer-1")throw std::runtime_error("Mixer version");
 for(unsigned i=0;i<3;i++){volumes[i]=settings.f();
 loops[i]=settings.n()!=0;
 audioTimes[i]=unsigned(settings.n());
 }settings.end();
 if(std::filesystem::exists(saveDir+"/slot01.flow")){auto flowBytes=qlie::file(saveDir+"/slot01.flow");whj2::StateReader flow{flowBytes};if(flow.str()!="WHJ2-flow-1")throw std::runtime_error("Flow state version");auto state=flow.str();sentenceWait.restore(qlie::Bytes(state.begin(),state.end()));pendingBody=flow.str();pendingSpeaker=flow.str();pendingVoice=flow.str();flow.end();}
 else {sentenceWait=whj2::SentenceWait{};sentenceWait.page();pendingBody=text;pendingSpeaker=name;pendingVoice.clear();}
 if(std::filesystem::exists(saveDir+"/slot01.message"))messageText->restore(qlie::file(saveDir+"/slot01.message"));else {messageText->start(text);messageText->reveal();}refreshMessage();
 menu=false;
 focus=0;
 status="静止画・音声位置を含む状態を読み込みました。";
 }
 void activate(){if(movie->playing()){movie->stop();
 if(movieOnly)done=true;
 else next();
 return;
 }if(menu){if(focus==0){story.start();
 menu=false;
 name="";
 next();
 }else if(focus==1)load();
 else done=true;
 }else if(story.state.awaiting){story.choose(unsigned(focus));
 next();
 }else {if(sentenceWait.blocksClick())return;
 if(messageText->reveal()){auto action=sentenceWait.prewaiting()?sentenceWait.confirm():whj2::SentenceWait::None;refreshMessage();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);return;}
 auto action=sentenceWait.confirm();if(action==whj2::SentenceWait::Next)next();else if(action==whj2::SentenceWait::Resume)next(true);}
 }
 void draw(){if(movie->playing()){pixels=movie->image().pixels;
 SDL_UpdateTexture(texture,nullptr,pixels.data(),1280*4);
 SDL_RenderClear(renderer);
 SDL_RenderCopy(renderer,texture,nullptr,nullptr);
 SDL_RenderPresent(renderer);
 return;
 }for(size_t i=0;i<pixels.size();i+=4){pixels[i]=35;
 pixels[i+1]=25;
 pixels[i+2]=20;
 pixels[i+3]=255;
 }if(!menu){images->drawScene(pixels);transition->blend(pixels);}
 if(menu){line("美少女万華鏡２　日本語版 開発版",45,30,32);
 line(status,45,90,20,0xffd58b);
 }else {for(int hy=0;hy<64;hy++){for(int hx=920;hx<1280;hx++){auto*pixel=pixels.data()+(size_t(hy)*1280+hx)*4;for(int c=0;c<3;c++){pixel[c]=uint8_t(unsigned(pixel[c])*60/255);}}}
 line("WHJ2 開発版・演出未完成",20,15,18,0xffd58b);
 line("保存",940,20,24);
 line("読込",1050,20,24);
 line("メニュー",1150,20,22);
 }if(menu){std::vector<std::string>items={"はじめから","ロード","終了"};
 for(int i=0;i<3;i++)line((focus==i?"▶ ":"　 ")+items[size_t(i)],100,210+i*70,30,focus==i?0xffd58b:0xffffff);
 }else if(!hidden){images->drawMessages(pixels);
 line(name,200,570,26,0xffffff);
 if(story.state.awaiting)for(size_t i=0;i<story.state.choices.size();i++)line((focus==int(i)?"▶ ":"　 ")+story.state.choices[i],80,220+int(i)*50,26,focus==int(i)?0xffd58b:0xffffff);
 }if(!menu&&!hidden)images->drawFaces(pixels);
 line("A: 次へ　B / ＋: メニュー　上下: 選択　X: 保存　Y: 読込　タッチ操作対応",45,675,19,0xb0becf);
 SDL_UpdateTexture(texture,nullptr,pixels.data(),1280*4);
 SDL_RenderClear(renderer);
 SDL_RenderCopy(renderer,texture,nullptr,nullptr);
 SDL_RenderPresent(renderer);
 }
 void snapshot(const std::string&name){SDL_Surface*surface=SDL_CreateRGBSurfaceFrom(pixels.data(),1280,720,32,1280*4,0x00ff0000,0x0000ff00,0x000000ff,0xff000000);
 if(!surface||SDL_SaveBMP(surface,(saveDir+"/"+name).c_str()))throw std::runtime_error("Cannot save screenshot");
 SDL_FreeSurface(surface);
 std::cerr<<"SCREENSHOT "<<saveDir<<"/"<<name<<"\n";
 }
 void pointer(int px,int py){float x,y;
 SDL_RenderWindowToLogical(renderer,px,py,&x,&y);
 if(!menu&&y<70&&x>=920){if(x<1030)save();
 else if(x<1140)load();
 else {if(movie->playing())movie->stop();
 menu=true;
 focus=0;
 }return;
 }if(menu){if(y<210||y>=420)return;
 focus=int((y-210)/70);
 }else if(story.state.awaiting){if(y<220||y>=220+50*story.state.choices.size())return;
 focus=int((y-220)/50);
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
 void controllerKey(Uint8 button){SDL_Keycode k=0;
 switch(button){case SDL_CONTROLLER_BUTTON_A:k=SDLK_RETURN;
 break;
 case SDL_CONTROLLER_BUTTON_B:k=SDLK_ESCAPE;
 break;
 case SDL_CONTROLLER_BUTTON_DPAD_UP:k=SDLK_UP;
 break;
 case SDL_CONTROLLER_BUTTON_DPAD_DOWN:k=SDLK_DOWN;
 break;
 case SDL_CONTROLLER_BUTTON_X:k=SDLK_F5;
 break;
 case SDL_CONTROLLER_BUTTON_Y:k=SDLK_F9;
 break;
 default:return;
 }SDL_Event e{};
 e.type=SDL_KEYDOWN;
 e.key.keysym.sym=k;
 if(SDL_PushEvent(&e)<0)throw std::runtime_error(SDL_GetError());
 }
 int run(unsigned smoke){
#ifdef __SWITCH__
 std::cerr<<"INPUT libnx Joy-Con and SDL touch\n";
#else
 std::cerr<<"INPUT physical joysticks="<<SDL_NumJoysticks()<<"\n";
 for(int i=0;i<SDL_NumJoysticks();i++)controllerAdded(i);
#endif
 unsigned frames=0;
 uint64_t lastUpdate=SDL_GetTicks64();
 while(!done){auto now=SDL_GetTicks64();
 if(!menu&&!movie->playing()){
 images->update(double(now-lastUpdate));
 messageText->update(double(now-lastUpdate));if(messageText->visible()!=lastMessageVisible)refreshMessage();
 transition->update(double(now-lastUpdate));
 auto waitAction=sentenceWait.tick(double(now-lastUpdate),audio->playing(3));
 if(waitAction==whj2::SentenceWait::Next)next();else if(waitAction==whj2::SentenceWait::Resume)next(true);
 for(auto&event:images->takeSoundEvents()){
  if(event.stop)audio->volume(event.channel,0,0,true);
  else audio->play(event.channel,event.bytes,event.loop,event.gain,0,event.source);
 }
}
 lastUpdate=now;
#ifdef __SWITCH__
 if(!appletMainLoop()){done=true;
 break;
 }auto pad=joycon.poll(now);
 int count=menu?3:int(story.state.choices.size());
 if(count&&pad.direction)focus=(focus+count+pad.direction)%count;
 if(pad.menu||pad.back){if(movie->playing())movie->stop();
 menu=true;
 focus=0;
 }else if(pad.confirm)activate();
 if(pad.save)save();
 if(pad.load)load();
 if(pad.hide)hidden=!hidden;
#endif
 if(movie->playing()){movie->update();
 if(!movie->playing()){if(movieOnly){done=true;
 break;
 }next();
 }}SDL_Event e;
 while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)done=true;
#ifndef __SWITCH__
else if(e.type==SDL_KEYDOWN&&!e.key.repeat){auto k=e.key.keysym.sym;
 std::cerr<<"INPUT key="<<k<<"\n";
 int count=menu?3:int(story.state.choices.size());
 if(k==SDLK_UP&&count)focus=(focus+count-1)%count;
 else if(k==SDLK_DOWN&&count)focus=(focus+1)%count;
 else if(k==SDLK_RETURN||k==SDLK_SPACE)activate();
 else if(k==SDLK_F5)save();
 else if(k==SDLK_F9)load();
 else if(k==SDLK_ESCAPE){if(movie->playing())movie->stop();
 menu=true;
 focus=0;
 if(movieOnly)done=true;
 }else if(k==SDLK_h)hidden=!hidden;
 }else if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT&&e.button.which!=SDL_TOUCH_MOUSEID){std::cerr<<"INPUT mouse\n";
 pointer(e.button.x,e.button.y);
 }
#endif
else if(e.type==SDL_FINGERDOWN){int w,h;
 SDL_GetWindowSize(window,&w,&h);
 std::cerr<<"INPUT touch\n";
 pointer(int(e.tfinger.x*w),int(e.tfinger.y*h));
 }else if(e.type==SDL_CONTROLLERDEVICEADDED)controllerAdded(e.cdevice.which);
 else if(e.type==SDL_CONTROLLERDEVICEREMOVED){auto i=controllers.find(e.cdevice.which);
 if(i!=controllers.end()){SDL_GameControllerClose(i->second);
 controllers.erase(i);
 }}else if(e.type==SDL_CONTROLLERBUTTONDOWN){std::cerr<<"INPUT controller button="<<unsigned(e.cbutton.button)<<"\n";
#ifndef __SWITCH__
controllerKey(e.cbutton.button);
#endif
}}draw();
 if(movieOnly&&!movieCaptured&&movie->elapsedMs()>=1000){snapshot("movie.bmp");
 movieCaptured=true;
 std::cerr<<"MOVIE screenshot audio_clock_ms="<<movie->elapsedMs()<<"\n";
 }if(smoke){if(touchOnlyTest){
 auto touch=[](float x,float y){SDL_Event ev{};ev.type=SDL_FINGERDOWN;ev.tfinger.x=x;ev.tfinger.y=y;if(SDL_PushEvent(&ev)<0)throw std::runtime_error(SDL_GetError());};
 if(frames==1)touch(0.2f,0.44f);
 if(frames==3&&(!menu||std::filesystem::exists(saveDir+"/slot01.story")))throw std::runtime_error("Empty touch load changed state");
 if(frames==5||frames==60)touch(0.2f,0.34f);
 if(frames==12||frames==36)touch(0.5f,0.5f);
 if(frames==24)touch(0.76f,0.04f);
 if(frames==26){touchSavedStory=qlie::file(saveDir+"/slot01.story");touchSavedText=text;}
 if(frames==38)touch(0.85f,0.04f);
 if(frames==40&&(story.save()!=touchSavedStory||text!=touchSavedText))throw std::runtime_error("Touch load mismatch");
 if(frames==48)touch(0.94f,0.04f);
 if(frames==50&&!menu)throw std::runtime_error("Touch menu did not open");
 if(frames==62&&menu)throw std::runtime_error("Touch new game did not activate");
 if(++frames>=smoke){snapshot("touch.bmp");std::cerr<<"TOUCH PASS next/save/load/menu/new-game\n";done=true;}continue;
 }if(frames==1 || (frames>=12 && frames%12==0)){SDL_Event input{};
 switch((frames/12)%4){case 0:input.type=SDL_KEYDOWN;
 input.key.keysym.sym=SDLK_RETURN;
 break;
 case 1:input.type=SDL_MOUSEBUTTONDOWN;
 input.button.button=SDL_BUTTON_LEFT;
 input.button.x=640;
 input.button.y=360;
 break;
 case 2:input.type=SDL_FINGERDOWN;
 input.tfinger.x=0.5f;
 input.tfinger.y=0.5f;
 break;
 case 3:input.type=SDL_CONTROLLERBUTTONDOWN;
 input.cbutton.button=SDL_CONTROLLER_BUTTON_A;
 break;
 }if(SDL_PushEvent(&input)<0)throw std::runtime_error(SDL_GetError());
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
 SDL_Delay(5);
 }}auto ast=audio->stats();
 std::cerr<<"AUDIO output_frames="<<ast.frames<<" nonzero_frames="<<ast.nonzero<<"\n";
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
Frontend f(game,saves);if(argc>4&&std::string(argv[3])=="--touch-smoke"){f.touchOnlyTest=true;smoke=unsigned(std::stoul(argv[4]));}
 if(argc>4&&std::string(argv[3])=="--movie"){f.movieOnly=true;
 f.menu=false;
 f.movie->open(argv[4]);
 }else if(argc>4&&std::string(argv[3])=="--movie-file"){f.movieOnly=true;
 f.menu=false;
 f.movie->openBytes(qlie::file(argv[4]),"local-test");
 }return f.run(smoke);
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";
 return 1;
 }}
