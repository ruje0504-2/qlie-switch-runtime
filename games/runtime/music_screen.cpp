// SPDX-License-Identifier: GPL-2.0-or-later
#include "music_screen.hpp"
#include "profile.hpp"
#include "render_queue.hpp"
#include "story.hpp"
#include <sstream>
#include <algorithm>
#include <cstdio>
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6/catalogs.hpp"
#include "profiles/whj6/ui.hpp"
#endif
namespace whj2 {
#ifdef QLIE_PROFILE_WHJ6
MusicScreen::MusicScreen(qlie::Assets&a,Audio&sound,Settings&config,KFont*f,const MessageText&text,int expectedTracks)
 :assets(a),audio(sound),settings(config),scene(a),labelText(text.forLabels()),font(f){
 for(const auto& row:whj6::catalogs::rows(a,"thumbnail/sound/list_music.txt")){
  if(row.size()!=3)throw std::runtime_error("WHJ6 track list fields");
  const auto path="music/"+row[2]+".ogg";
  if(!a.exists(path))throw std::runtime_error("WHJ6 music track missing: "+path);
  tracks.push_back({row[2],path,"thumbnail/sound/"+row[0],"thumbnail/sound/"+row[1]+".png"});
 }
 if(trackCount()!=expectedTracks)throw std::runtime_error("WHJ6 music track count");
}
#else
MusicScreen::MusicScreen(qlie::Assets&a,Audio&sound,Settings&config,KFont*f,const MessageText& text,int expectedTracks):assets(a),audio(sound),settings(config),scene(a),labelText(text.forLabels()),font(f){std::istringstream input(a.text("script/soundmode/list.txt"));std::string line;while(std::getline(input,line)){auto row=split(trim(line),',');if(row.size()<2)continue;auto path="music/"+row[1];if(path.find('.')==path.npos)path+=".ogg";if(!a.exists(path))throw std::runtime_error("Music mode track missing: "+path);tracks.push_back({row[0],path});}if(expectedTracks?trackCount()!=expectedTracks:(trackCount()!=18&&trackCount()!=27&&!paged()))throw std::runtime_error("Music mode track layout count");}
#endif
void MusicScreen::text(const std::string&id,unsigned slot,const std::string&value,unsigned width,unsigned height,unsigned size){
 labelText.start("[spd,0]"+value);
 auto image=labelText.render(font,MessageStyle{width,height,size,size,0xffffff});
#ifdef QLIE_PROFILE_WHJ6
 (void)slot;
 scene.setUiBitmap(id,std::move(image));
#else
 scene.uiSlot(id,slot,image);
#endif
}
#ifdef QLIE_PROFILE_WHJ6
namespace {
constexpr int trackX(int index){return 360+index/7*400;}
constexpr int trackY(int index){return 350+index%7*90;}
constexpr int controlX[]={195,315,375,455,255,515,575};
constexpr auto seekX=whj6::ui::to720(639),seekTop=whj6::ui::to720(184),seekWidth=whj6::ui::to720(292);
constexpr auto volumeX=whj6::ui::to720(169),volumeTop=whj6::ui::to720(227);
}
void MusicScreen::prepareWhj6(){
 using namespace whj6::ui;
 picture(scene,"ui_music_bg","script/soundmode/bg.png",0,0,100);
 for(int i=0;i<trackCount();++i){
  const auto id="ui_track"+std::to_string(i);
  button(scene,id,"script/soundmode/button_music.b",trackX(i),trackY(i),110);
  slots(scene,id,tracks[i].button,{"_off","_on","_hold_off","_hold_on"});
 }
 constexpr const char*controls[]={"op_prev","op_play","op_next","op_onerepeat","op_stop","op_allrepeat","op_shuffle"};
 for(int i=0;i<7;++i){
  const auto id="ui_music_control"+std::to_string(i);
  button(scene,id,"script/soundmode/button_mode.b",controlX[i],180,150);
  const auto prefix=std::string("script/soundmode/")+controls[i];
  slots(scene,id,prefix,{"_off","_on"});
  if(i==0||i==2||i==4)scene.uiSlot(id,4,prefix+"_hide.png");
  else{
   scene.uiSlot(id,2,prefix+"_off_hold.png");
   scene.uiSlot(id,3,prefix+"_on_hold.png");
  }
 }
 for(unsigned i=0;i<3;++i)
  navButton(scene,"ui_music_nav"+std::to_string(i),i);
 navButton(scene,"ui_music_back",3);
 focus(scene,"ui_music_nav2",false,true);
 button(scene,"ui_music_seek","script/soundmode/seek_scroll_bar.b",639,184,110);
 button(scene,"ui_music_seekknob","script/soundmode/seek_scroll_prog.b",639,184,120);
 button(scene,"ui_music_volume","script/soundmode/seek_scroll_bar.b",169,227,110);
 button(scene,"ui_music_volknob","script/soundmode/seek_scroll_prog.b",169,227,120);
 scene.command("^ui_music_caption,x:906.667,y:120,pri:150,show:false");
 scene.command("^ui_music_time,x:538.667,y:101,pri:150");
}
#endif
void MusicScreen::prepare(){
 if(prepared)return;
#ifdef QLIE_PROFILE_WHJ6
 prepareWhj6();
#else
 auto add=[&](const std::string&id,const std::string&file,int x,int y,int pri){scene.command("^"+id+",file:script/soundmode/"+file+",x:"+std::to_string(x)+",y:"+std::to_string(y)+",pri:"+std::to_string(pri));};
 add("ui_music_bg","背景.png",0,0,50);add("ui_music_upper","アッパーフレーム.png",0,0,100);add("ui_music_lower","アンダーフレーム.png",0,656,100);add("ui_music_operation","操作フレーム.png",0,paged()?575:60,100);
 const int rows=trackRows();
 const bool wide=rows==8;
 for(unsigned i=0;i<tracks.size();i++){
  const auto id="ui_track"+std::to_string(i);
  const int track=int(i)%perPage();
  if(game_profile::story.legacyAvg){
   constexpr int y[]={170,234,296,362,426,490,554,618};
   add(id,"SoundButton.b",400+track/rows*480,y[track%rows],110);
  }else add(id,"SoundButton.b",(paged()?230:wide?205:270)+track/rows*(paged()?405:wide?285:380),
   (paged()?150:wide?170:200)+track%rows*(paged()||wide?65:80),110);
  char number[8];std::snprintf(number,sizeof(number),"%02u.",i+1);
  text(id,3,std::string(number)+tracks[i].title,wide?270:350,50,18);
 }
 const char*controls[]={"Prev","Play","Next","Loop"};for(unsigned i=0;i<4;i++)add("ui_music_control"+std::to_string(i),std::string("ControlButton_")+controls[i]+".b",990+int(i)*50,paged()?615:100,150);
 if(!game_profile::story.legacyAvg){
  const char*navNames[]={"CG観賞","回想観賞","音楽鑑賞"};
  for(int i=0;i<3;i++){auto id="ui_music_nav"+std::to_string(i);add(id,"Button_l.b",820+i*130,695,150);scene.uiSlot(id,0,"script/soundmode/"+std::string(navNames[i])+"_off.png");scene.uiSlot(id,1,"script/soundmode/"+std::string(navNames[i])+"_on.png");}
  scene.command("^ui_music_nav2,motion:*OnLock");
 }
 add("ui_music_back","Button_l.b",1211,game_profile::story.legacyAvg?694:695,150);scene.uiSlot("ui_music_back",0,"script/soundmode/戻る_off.png");scene.uiSlot("ui_music_back",1,"script/soundmode/戻る_on.png");
 add("ui_music_seek","SeekBar_Bar.b",210,seekY(),110);add("ui_music_seekknob","SeekBar_Prog.b",210,seekY(),120);
 add("ui_music_volume","VolumeBar_Bar.b",700,seekY(),110);add("ui_music_volknob","VolumeBar_Prog.b",700,seekY(),120);
 if(!game_profile::story.legacyAvg)add("ui_music_caption","ViewText.b",400,seekY()+6,150);
 if(paged())for(int i=0;i<pageCount();++i){
  const auto id="ui_music_page"+std::to_string(i);
  const auto file="script/soundmode/ページ_0"+std::to_string(i+1);
  add(id,"Button_Page.b",1130+i*40,90,110);
  scene.uiSlot(id,0,file+"_off.png");
  scene.uiSlot(id,1,file+"_on.png");
 }
#endif
 refresh();
 prepared=true;
}
void MusicScreen::open(){opened=true;selected=0;playing=-1;audible=false;loop=false;clearSeek();volumeValue=settings.get("MusicModeVolume");audio.setMusicModeGain((volumeValue*settings.get("MasterVolume")/100)/100.f);audio.stop(0);
#ifdef QLIE_PROFILE_WHJ6
 repeat=Repeat::Off;
 scene.command("^ui_music_caption,show:false");
#endif
 prepare();
 motions.clear();
 scene.resetUiAnimations();
 for(unsigned i=0;i<tracks.size();++i){
  auto id="ui_track"+std::to_string(i);
  setMotion(id,"*Enable");
  // The entry art is idle already. OffMouse animates a genuine departure.
  motions[id]="*OffMouse";
 }
 showPage(0);highlight();refresh();
}
void MusicScreen::showPage(int value){
 page=value;
 if(!paged())return;
 for(int i=0;i<trackCount();++i){
  const auto id="ui_track"+std::to_string(i);
  scene.command("^"+id+",show:"+(i/perPage()==page?"true":"false"));
 }
}
void MusicScreen::close(){audio.stop(0);clearSeek();opened=false;}
void MusicScreen::setMotion(const std::string&id,const char*motion){
 auto&previous=motions[id];if(previous==motion)return;
 scene.setUiMotion(id,motion);
 previous=motion;
}
void MusicScreen::highlight(){
 if(!game_profile::story.legacyAvg)
  for(int i=0;i<2;i++)setMotion("ui_music_nav"+std::to_string(i),selected==control(Album)+i?"*OnMouse":"*OffMouse");
 setMotion("ui_music_seek",selected==control(Seek)?"*OnMouse":"*OffMouse");
 setMotion("ui_music_volume",selected==control(Volume)?"*OnMouse":"*OffMouse");
 setMotion("ui_music_back",selected==control(Return)?"*OnMouse":"*OffMouse");
#ifdef QLIE_PROFILE_WHJ6
 const Control controls[]={Previous,Play,Next,Loop,Stop,RepeatAll,Shuffle};
 const bool active[]={false,audible,false,repeat==Repeat::One,false,repeat==Repeat::All,repeat==Repeat::Random};
 for(int i=0;i<7;++i){
  const bool locked=(i==0||i==2||i==4)&&playing<0;
  const bool hovering=selected==control(controls[i]);
  setMotion("ui_music_control"+std::to_string(i),locked?"*OnLock":
   active[i]?(hovering?"*OnCheckOnMouse":"*OnCheckOffMouse"):(hovering?"*OnMouse":"*OffMouse"));
 }
#else
 for(int i=0;i<3;i++){const char*motion=selected==control(Previous)+i?"*OnMouse":"*OffMouse";if(i==1&&audible)motion=selected==control(Play)?"*OnCheckOnMouse":"*OnCheckOffMouse";setMotion("ui_music_control"+std::to_string(i),motion);}
 setMotion("ui_music_control3",loop?"*OnCheck":"*OffCheck");
#endif
 for(int i=page*perPage();i<=lastTrackOnPage();++i)
  setMotion("ui_track"+std::to_string(i),i==selected?"*OnMouse":i==playing?"*OnCheck":"*OffMouse");
 if(paged())for(int i=0;i<pageCount();++i)
  setMotion("ui_music_page"+std::to_string(i),i==page?"*OnCheck":selected==control(Page)+i?"*OnMouse":"*OffMouse");
}

void MusicScreen::uiSound(const char*name){auto path=std::string("misc/")+name+".wav";audio.playFile(assets,Audio::SystemChannel,path,false,255.f/256,0,3);}
void MusicScreen::play(int n){
 clearSeek();
 playing=(n+trackCount())%trackCount();
#ifdef QLIE_PROFILE_WHJ6
 scene.command("^ui_music_caption,file:"+tracks[playing].caption+",align:center:center,scalex:66.6667,scaley:66.6667,show:true");
#else
 selected=playing;
#endif
 if(paged()&&playing/perPage()!=page)showPage(playing/perPage());
 audio.playFile(assets,0,tracks[playing].path,false,255.f/256,0,5);
 audible=true;
 highlight();
 refresh();
}
#ifdef QLIE_PROFILE_WHJ6
void MusicScreen::stopWhj6(){
 audio.stop(0);clearSeek();playing=-1;audible=false;
 scene.command("^ui_music_caption,show:false");
 highlight();refresh();
}
int MusicScreen::nextWhj6(bool automatic){
 if(repeat==Repeat::Random)return std::uniform_int_distribution<int>(0,trackCount()-1)(shuffle);
 if(automatic&&repeat==Repeat::One)return playing;
 if(automatic&&repeat==Repeat::Off&&playing+1==trackCount())return -1;
 return (playing+1)%trackCount();
}
bool MusicScreen::moveWhj6(int x,int y){
 constexpr Control order[]={Previous,Stop,Play,Next,Loop,RepeatAll,Shuffle,Seek,Volume};
 constexpr int boxes=sizeof(order)/sizeof(order[0]);
 auto found=std::find_if(std::begin(order),std::end(order),[&](Control c){return selected==control(c);});
 if(found==std::end(order)||(x&&(*found==Seek||*found==Volume)))return false;
 const auto offset=int(found-std::begin(order));
 if(x)selected=control(order[(offset+x+boxes)%boxes]);
 else if(y>0)selected=0;
 else if(y<0)selected=control(*found==Seek?Volume:Seek);
 highlight();
 uiSound("Sys_OnMouse");
 return true;
}
#endif
MusicScreen::Destination MusicScreen::confirm(){
 if(selected==control(Seek)||selected==control(Volume))return NoDestination;
 uiSound(selected==control(Return)?"Sys_Cancel":"Sys_OnClick");
 if(selected==control(Album)||selected==control(Memory))return selected==control(Album)?CG:Replay;
 if(selected==control(Return))return Title;
 if(paged()&&selected>=control(Page)){
  showPage(selected-control(Page));
  selected=page*perPage();
  highlight();
  return NoDestination;
 }
 if(selected<trackCount())play(selected);
 else{
#ifdef QLIE_PROFILE_WHJ6
  if(selected==control(Previous)&&playing>=0)play(playing-1);
  else if(selected==control(Play)){
   if(playing<0)play(0);else{audible=!audible;audio.pause(0,!audible);}
  }else if(selected==control(Next)&&playing>=0)play(nextWhj6(false));
  else if(selected==control(Stop))stopWhj6();
  else if(selected==control(Loop)||selected==control(RepeatAll)||selected==control(Shuffle)){
   const auto next=selected==control(Loop)?Repeat::One:selected==control(RepeatAll)?Repeat::All:Repeat::Random;
   repeat=repeat==next?Repeat::Off:next;
  }
#else
  if(selected==control(Previous)){if(playing>=0)play(playing-1);}
  else if(selected==control(Play)){if(playing<0)play(0);else{audible=!audible;audio.volume(0,audible?255.f/256:0,250);}}
  else if(selected==control(Next)){if(playing>=0)play(playing+1);}
  else if(selected==control(Loop))loop=!loop;
#endif
  highlight();refresh();
 }
 return NoDestination;
}
MusicScreen::Destination MusicScreen::back(){
#ifdef QLIE_PROFILE_WHJ6
 if(playing>=0){stopWhj6();return NoDestination;}
#else
 if(playing>=0){audio.volume(0,0,250,true);clearSeek();playing=-1;audible=false;highlight();refresh();return NoDestination;}
#endif
 uiSound("Sys_Cancel");return Title;
}
void MusicScreen::move(int x,int y){
#ifdef QLIE_PROFILE_WHJ6
 if(moveWhj6(x,y))return;
#endif
 const int old=selected,n=trackCount(),rows=trackRows(),pageFirst=page*perPage(),
           pageLast=lastTrackOnPage(),columns=(pageLast-pageFirst+rows)/rows;
 if(selected==control(Seek)||selected==control(Volume)){
  if(y)selected=selected==control(Seek)?control(Play):control(Loop);
  else if(x){
   if(selected==control(Seek)&&playing>=0)seekOffsetMs+=int64_t(x)*5000;
   else if(selected==control(Volume)){
    volumeValue=std::clamp(volumeValue+x*5,0,100);settings.set("MusicModeVolume",volumeValue);
    dirty=true;audio.setMusicModeGain((volumeValue*settings.get("MasterVolume")/100)/100.f);
   }
   refresh();
  }
 }else if(paged()&&selected>=control(Page)){
  if(y>0)selected=pageFirst;
  else if(x)selected=control(Page)+(selected-control(Page)+x+pageCount())%pageCount();
 }else if(selected<n){
  const int local=selected-pageFirst,column=local/rows,row=local%rows;
  if(y<0&&row==0)selected=paged()?control(Page)+page:control(Previous)+column;
  else if(y>0&&(row==rows-1||selected==pageLast))
   selected=paged()?control(Previous)+column:control(
    game_profile::story.legacyAvg?Return:column==0?Album:column==1?Memory:Return);
  else if(y)selected+=y;
  else if(x)selected=std::min(pageLast,pageFirst+((column+x+columns)%columns)*rows+row);
 }else if(selected==control(Return)||selected==control(Album)||selected==control(Memory)){
  if(y)selected=y<0?std::min(pageLast,pageFirst+rows-1):pageFirst;
  else if(x&&!game_profile::story.legacyAvg){
   const int current=selected==control(Album)?0:selected==control(Memory)?1:2;
   constexpr Control tabs[]={Album,Memory,Return};
   selected=control(tabs[(current+x+3)%3]);
  }
 }else if(y>0)selected=paged()?control(Return):std::min(pageLast,pageFirst+(selected-n)*rows);
 else if(y<0)selected=paged()?std::min(pageLast,pageFirst+(selected-n+1)*rows-1):control(selected<control(Next)?Seek:Volume);
 else selected=n+(selected-n+x+4)%4;
 highlight();
 if(old!=selected&&selected!=control(Seek)&&selected!=control(Volume))uiSound("Sys_OnMouse");
}
MusicScreen::Destination MusicScreen::tap(float x,float y){
 if(paged())for(int i=0;i<pageCount();++i)if(scene.uiHit("ui_music_page"+std::to_string(i),x,y)){selected=control(Page)+i;return confirm();}
#ifdef QLIE_PROFILE_WHJ6
 using whj6::ui::contains;
 for(int i=0;i<trackCount();++i)if(contains(x,y,trackX(i)-190,trackY(i)-20,380,40)){
  selected=i;return confirm();
 }
 const Control controls[]={Previous,Play,Next,Loop,Stop,RepeatAll,Shuffle};
 for(int i=0;i<7;++i)if(contains(x,y,controlX[i]-26,154,52,52)){
  selected=control(controls[i]);return confirm();
 }
#else
 for(int i=page*perPage();i<=lastTrackOnPage();++i)if(scene.uiHit("ui_track"+std::to_string(i),x,y)){selected=i;return confirm();}
 for(int i=0;i<4;i++)if(scene.uiHit("ui_music_control"+std::to_string(i),x,y)){selected=control(Previous)+i;return confirm();}
#endif
 if(!game_profile::story.legacyAvg)
  for(int i=0;i<2;i++)if(scene.uiHit("ui_music_nav"+std::to_string(i),x,y)){selected=control(Album)+i;return confirm();}
 if(scene.uiHit("ui_music_back",x,y)){selected=control(Return);return confirm();}
 drag(x,y);return NoDestination;
}
bool MusicScreen::drag(float x,float y){
#ifdef QLIE_PROFILE_WHJ6
 const float sx=seekX,sy=seekTop,sw=seekWidth,vx=volumeX,vy=volumeTop,vw=seekWidth;
#else
 const float sx=210,sy=seekY(),sw=399,vx=700,vy=seekY(),vw=196;
#endif
 if(y>=sy-8&&y<=sy+20&&x>=sx&&x<=sx+sw&&playing>=0){
  seekFraction=std::clamp((x-sx)/sw,0.f,1.f);seekOffsetMs=0;refresh();return true;
 }
 if(y>=vy-8&&y<=vy+20&&x>=vx&&x<=vx+vw){
  volumeValue=int(std::clamp((x-vx)/vw,0.f,1.f)*100);
  settings.set("MusicModeVolume",volumeValue);dirty=true;
  audio.setMusicModeGain((volumeValue*settings.get("MasterVolume")/100)/100.f);refresh();return true;
 }
 return false;
}
void MusicScreen::refresh(){
 auto p=playing<0?AudioPosition{}:audio.position(0);
 // An asynchronously queued track has no duration until its PCM is ready.
 if(p.length&&(seekFraction||seekOffsetMs)){
  const auto fromMs=seekFraction?int64_t(double(*seekFraction)*p.length*1000/44100):int64_t(p.frame*1000/44100);
  audio.seek(0,uint64_t(std::max(int64_t(0),fromMs+seekOffsetMs)));
  clearSeek();
  p=audio.position(0);
 }
 auto current=p.frame*1000/44100,total=p.length*1000/44100;char time[64];std::snprintf(time,sizeof(time)," %02llu:%02llu (%02llu:%02llu)",(unsigned long long)(current/60000),(unsigned long long)(current/1000%60),(unsigned long long)(total/60000),(unsigned long long)(total/1000%60));
#ifdef QLIE_PROFILE_WHJ6
 if(caption!=time){caption=time;text("ui_music_time",0,caption,110,24,13);}
 constexpr float scale=2.f/3;
#else
 if(game_profile::story.legacyAvg){
  if(caption!=time){caption=time;labelText.start("[spd,0]"+caption);legacyCaption=labelText.render(font,{200,50,14,14});}
 }else{
  auto next=playing<0?std::string("no select"):tracks[playing].title+time;
  if(next!=caption){caption=next;text("ui_music_caption",0,"[pc,"+caption+"]",500,40,14);}
 }
 constexpr float scale=1;
#endif
 const float seek=p.length?100.f*p.frame/p.length:0;
 if(seek!=shownSeek){scene.command("^ui_music_seekknob,scalex:"+std::to_string(seek*scale));shownSeek=seek;}
 if(volumeValue!=shownVolume){scene.command("^ui_music_volknob,scalex:"+std::to_string(volumeValue*scale));shownVolume=volumeValue;}
}
void MusicScreen::draw(qlie::Bytes&out,const Affine2D*view)const{
 scene.draw(out,false,view);
 if(legacyCaption){
  RenderQueue queue(false,false,view);
  queue.image(*legacyCaption,{1,0,0,1,300,78},0,255,0,0xffffff);
  queue.draw(out,1280,720);
 }
}
void MusicScreen::draw(RenderTarget&out,const Affine2D*view)const{
 scene.draw(out,false,view);
 if(legacyCaption){
  RenderQueue queue(out.supportsMasks(),out.supportsVideoSurfaces(),view);
  queue.image(*legacyCaption,{1,0,0,1,300,78},0,255,0,0xffffff);
  queue.draw(out,1280,720);
 }
}
void MusicScreen::update(double ms){if(!opened)return;scene.update(ms);if(playing>=0&&audible&&!audio.playing(0)){
#ifdef QLIE_PROFILE_WHJ6
 const auto next=nextWhj6(true);
 if(next<0)stopWhj6();else play(next);
#else
 if(loop){audio.seek(0,0);audio.volume(0,255.f/256);}else play(playing+1);
#endif
 }refresh();}
}
