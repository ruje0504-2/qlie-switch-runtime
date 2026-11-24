// SPDX-License-Identifier: GPL-2.0-or-later
#include "music_screen.hpp"
#include "story.hpp"
#include <sstream>
#include <algorithm>
#include <cstdio>
namespace whj2 {
MusicScreen::MusicScreen(qlie::Assets&a,Audio&sound,Settings&config,KFont*f):assets(a),audio(sound),settings(config),scene(a),labelText(a),font(f){std::istringstream input(qlie::cp932(a.read("script/soundmode/list.txt")));std::string line;while(std::getline(input,line)){auto row=split(trim(line),',');if(row.size()<2)continue;auto path="music/"+row[1];if(path.find('.')==path.npos)path+=".ogg";if(!a.exists(path))throw std::runtime_error("Music mode track missing: "+path);tracks.push_back({row[0],path});}if(tracks.size()!=18)throw std::runtime_error("Music mode track layout count");}
void MusicScreen::text(const std::string&id,unsigned slot,const std::string&value,unsigned width,unsigned height,unsigned size){labelText.start("[spd,0]"+value);scene.uiSlot(id,slot,labelText.render(font,MessageStyle{width,height,size,size,0xffffff}));}
void MusicScreen::prepare(){
 if(prepared)return;
 auto add=[&](const std::string&id,const std::string&file,int x,int y,int pri){scene.command("^"+id+",file:script/soundmode/"+file+",x:"+std::to_string(x)+",y:"+std::to_string(y)+",pri:"+std::to_string(pri));};
 add("ui_music_bg","背景.png",0,0,50);add("ui_music_upper","アッパーフレーム.png",0,0,100);add("ui_music_lower","アンダーフレーム.png",0,656,100);add("ui_music_operation","操作フレーム.png",0,60,100);
 for(unsigned i=0;i<tracks.size();i++){auto id="ui_track"+std::to_string(i);add(id,"SoundButton.b",270+int(i/6)*380,200+int(i%6)*80,110);char number[8];std::snprintf(number,sizeof(number),"%02u.",i+1);text(id,3,std::string(number)+tracks[i].title,350,50,18);}
 const char*controls[]={"Prev","Play","Next","Loop"};for(unsigned i=0;i<4;i++)add("ui_music_control"+std::to_string(i),std::string("ControlButton_")+controls[i]+".b",990+int(i)*50,100,150);
 const char*navNames[]={"CG観賞","回想観賞","音楽鑑賞"};for(int i=0;i<3;i++){auto id="ui_music_nav"+std::to_string(i);add(id,"Button_l.b",820+i*130,695,150);scene.uiSlot(id,0,"script/soundmode/"+std::string(navNames[i])+"_off.png");scene.uiSlot(id,1,"script/soundmode/"+std::string(navNames[i])+"_on.png");}
 scene.command("^ui_music_nav2,motion:*OnLock");
 add("ui_music_back","Button_l.b",1211,695,150);scene.uiSlot("ui_music_back",0,"script/soundmode/戻る_off.png");scene.uiSlot("ui_music_back",1,"script/soundmode/戻る_on.png");
 add("ui_music_seek","SeekBar_Bar.b",210,92,110);add("ui_music_seekknob","SeekBar_Prog.b",210,92,120);
 add("ui_music_volume","VolumeBar_Bar.b",700,92,110);add("ui_music_volknob","VolumeBar_Prog.b",700,92,120);
 add("ui_music_caption","ViewText.b",400,98,150);
 refresh();
 prepared=true;
}
void MusicScreen::open(){opened=true;selected=0;playing=-1;audible=false;loop=false;clearSeek();volumeValue=settings.get("MusicModeVolume");audio.setMusicModeGain((volumeValue*settings.get("MasterVolume")/100)/100.f);audio.stop(0);
 prepare();
 motions.clear();
 scene.resetUiAnimations();
 for(unsigned i=0;i<tracks.size();++i){
  auto id="ui_track"+std::to_string(i);
  setMotion(id,"*Enable");
  // The entry art is idle already. OffMouse animates a genuine departure.
  motions[id]="*OffMouse";
 }
 highlight();refresh();
}
void MusicScreen::close(){audio.stop(0);clearSeek();opened=false;}
void MusicScreen::setMotion(const std::string&id,const char*motion){
 auto&previous=motions[id];if(previous==motion)return;
 scene.setUiMotion(id,motion);previous=motion;
}
void MusicScreen::highlight(){
 for(int i=0;i<2;i++)setMotion("ui_music_nav"+std::to_string(i),selected==25+i?"*OnMouse":"*OffMouse");
 setMotion("ui_music_seek",selected==23?"*OnMouse":"*OffMouse");
 setMotion("ui_music_volume",selected==24?"*OnMouse":"*OffMouse");
 setMotion("ui_music_back",selected==22?"*OnMouse":"*OffMouse");
 for(int i=0;i<3;i++){const char*motion=selected==18+i?"*OnMouse":"*OffMouse";if(i==1&&audible)motion=selected==19?"*OnCheckOnMouse":"*OnCheckOffMouse";setMotion("ui_music_control"+std::to_string(i),motion);}
 setMotion("ui_music_control3",loop?"*OnCheck":"*OffCheck");
 for(int i=0;i<18;i++)setMotion("ui_track"+std::to_string(i),i==selected?"*OnMouse":i==playing?"*OnCheck":"*OffMouse");
}

void MusicScreen::uiSound(const char*name){auto path=std::string("misc/")+name+".wav";audio.playFile(assets,Audio::SystemChannel,path,false,255.f/256,0,3);}
void MusicScreen::play(int n){clearSeek();playing=(n+18)%18;selected=playing;audio.playFile(assets,0,tracks[playing].path,false,255.f/256,0,5);audible=true;highlight();refresh();}
MusicScreen::Destination MusicScreen::confirm(){
 if(selected==23||selected==24)return NoDestination;
 uiSound(selected==22?"Sys_Cancel":"Sys_OnClick");
 if(selected==25||selected==26)return selected==25?CG:Replay;
 if(selected==22)return Title;
 if(selected<18)play(selected);
 else{
  if(selected==18){if(playing>=0)play(playing-1);}
  else if(selected==19){if(playing<0)play(0);else{audible=!audible;audio.volume(0,audible?255.f/256:0,250);}}
  else if(selected==20){if(playing>=0)play(playing+1);}
  else if(selected==21)loop=!loop;
  highlight();refresh();
 }
 return NoDestination;
}
MusicScreen::Destination MusicScreen::back(){
 if(playing>=0){audio.volume(0,0,250,true);clearSeek();playing=-1;audible=false;highlight();refresh();return NoDestination;}
 uiSound("Sys_Cancel");return Title;
}
void MusicScreen::move(int x,int y){const int old=selected;if(selected==23||selected==24){if(y){selected=selected==23?19:21;}else if(x){if(selected==23&&playing>=0){seekOffsetMs+=int64_t(x)*5000;}else if(selected==24){volumeValue=std::clamp(volumeValue+x*5,0,100);settings.set("MusicModeVolume",volumeValue);dirty=true;audio.setMusicModeGain((volumeValue*settings.get("MasterVolume")/100)/100.f);}refresh();}highlight();if(old!=selected&&selected!=23&&selected!=24)uiSound("Sys_OnMouse");return;}if(selected<18){if(y<0&&selected%6==0)selected=18+selected/6;else if(y>0&&selected%6==5)selected=selected/6==0?25:selected/6==1?26:22;else if(y)selected+=y;else if(x)selected=(selected+6*x+18)%18;}else if(selected==22||selected==25||selected==26){if(y)selected=y<0?5:0;else if(x){int row=selected==25?0:selected==26?1:2;row=(row+x+3)%3;selected=row==0?25:row==1?26:22;}}else if(y>0)selected=std::min(17,(selected-18)*6);else if(y<0)selected=selected<20?23:24;else selected=18+(selected-18+x+4)%4;highlight();if(old!=selected&&selected!=23&&selected!=24)uiSound("Sys_OnMouse");}
MusicScreen::Destination MusicScreen::tap(float x,float y){
 for(int i=0;i<18;i++)if(scene.uiHit("ui_track"+std::to_string(i),x,y)){selected=i;return confirm();}
 for(int i=0;i<4;i++)if(scene.uiHit("ui_music_control"+std::to_string(i),x,y)){selected=18+i;return confirm();}
 for(int i=0;i<2;i++)if(scene.uiHit("ui_music_nav"+std::to_string(i),x,y)){selected=25+i;return confirm();}
 if(scene.uiHit("ui_music_back",x,y)){selected=22;return confirm();}
 drag(x,y);return NoDestination;
}
bool MusicScreen::drag(float x,float y){if(y>=84&&y<=112&&x>=210&&x<=609&&playing>=0){seekFraction=std::clamp((x-210)/399,0.f,1.f);seekOffsetMs=0;refresh();return true;}if(y>=84&&y<=112&&x>=700&&x<=896){volumeValue=int(std::clamp((x-700)/196,0.f,1.f)*100);settings.set("MusicModeVolume",volumeValue);dirty=true;audio.setMusicModeGain((volumeValue*settings.get("MasterVolume")/100)/100.f);refresh();return true;}return false;}
void MusicScreen::refresh(){
 auto p=playing<0?AudioPosition{}:audio.position(0);
 // An asynchronously queued track has no duration until its PCM is ready.
 if(p.length&&(seekFraction||seekOffsetMs)){
  const auto fromMs=seekFraction?int64_t(double(*seekFraction)*p.length*1000/44100):int64_t(p.frame*1000/44100);
  audio.seek(0,uint64_t(std::max(int64_t(0),fromMs+seekOffsetMs)));
  clearSeek();
  p=audio.position(0);
 }
 auto current=p.frame*1000/44100,total=p.length*1000/44100;char time[64];std::snprintf(time,sizeof(time)," %02llu:%02llu (%02llu:%02llu)",(unsigned long long)(current/60000),(unsigned long long)(current/1000%60),(unsigned long long)(total/60000),(unsigned long long)(total/1000%60));auto next=playing<0?std::string("no select"):tracks[playing].title+time;if(next!=caption){caption=next;text("ui_music_caption",0,"[pc,"+caption+"]",500,40,14);}
 const float seek=p.length?100.f*p.frame/p.length:0;
 if(seek!=shownSeek){scene.command("^ui_music_seekknob,scalex:"+std::to_string(seek));shownSeek=seek;}
 if(volumeValue!=shownVolume){scene.command("^ui_music_volknob,scalex:"+std::to_string(volumeValue));shownVolume=volumeValue;}
}
void MusicScreen::update(double ms){if(!opened)return;scene.update(ms);if(playing>=0&&audible&&!audio.playing(0)){if(loop){audio.seek(0,0);audio.volume(0,255.f/256);}else play(playing+1);}refresh();}
}
