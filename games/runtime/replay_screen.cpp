// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_screen.hpp"
#include "profile.hpp"
#include <algorithm>
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6/ui.hpp"
#endif
namespace whj2 {
#ifdef QLIE_PROFILE_WHJ6
namespace {
constexpr int replayX(unsigned index){return 70+int(index%5)*360;}
constexpr int replayY(unsigned index){return 170+int(index/5)*269;}
}
#endif
void ReplayScreen::prepareLayout(){
#ifdef QLIE_PROFILE_WHJ6
 using namespace whj6::ui;
 picture(scene,"ui_replay_bg","script/memorymode/bg.png",0,0,100);
 for(unsigned i=0;i<15;++i)
  button(scene,"ui_replay_item"+std::to_string(i),"script/memorymode/button_memory.b",replayX(i),replayY(i),110);
 for(unsigned i=0;i<4;++i)
  navButton(scene,"ui_replay_nav"+std::to_string(i),i);
#else
 auto add=[&](const std::string&id,const std::string&file,int x,int y,int pri){scene.command("^"+id+",file:script/memorymode/"+file+",x:"+std::to_string(x)+",y:"+std::to_string(y)+",pri:"+std::to_string(pri));};
 add("ui_replay_bg","背景.png",0,0,100);add("ui_replay_upper","アッパーフレーム.png",0,0,100);add("ui_replay_lower","アンダーフレーム.png",0,656,100);
 const bool legacy=game_profile::story.legacyAvg!=nullptr;
 for(unsigned i=0;i<15;i++)add("ui_replay_item"+std::to_string(i),"Button_Thum.b",(legacy?57:60)+int(i%5)*240,(legacy?132+int(i==11):140)+int(i/5)*170,110);
 for(int i=0;i<pageCount();i++){auto id="ui_replay_page"+std::to_string(i);add(id,"Button_Page.b",legacy?48+i*50:1170+i*40,legacy?80:90,110);auto name="script/memorymode/ページ_0"+std::to_string(i+1);scene.uiSlot(id,0,name+"_off.png");scene.uiSlot(id,1,name+"_on.png");}
 const char*names[]={"CG観賞","回想観賞","音楽鑑賞","戻る"};
 for(int i=legacy?3:0;i<4;i++){
  auto id="ui_replay_nav"+std::to_string(i);
  add(id,"Button_l.b",820+i*130,695,110);
  scene.uiSlot(id,0,"script/memorymode/"+std::string(names[i])+"_off.png");
  scene.uiSlot(id,1,"script/memorymode/"+std::string(names[i])+"_on.png");
 }
 scene.prepareUiBitmap("script/memorymode/サムネイル_no_image.png");
#endif
 prepared=true;
}
void ReplayScreen::prepare(){
 if(!prepared)prepareLayout();
 for(size_t i=0;i<catalog.items().size();++i)
  if(catalog.unlocked(i,progress))scene.prepareUiBitmap(catalog.items()[i].thumbnail);
}
void ReplayScreen::open(unsigned pageIndex){
 if(!prepared)prepare();
 opened=true;page=pageCount()?std::min(pageIndex,unsigned(pageCount()-1)):0;
 selected=pageCount()?0:navigationStart()+3;
 setup();
}
int ReplayScreen::pageCount()const{return int((catalog.items().size()+14)/15);}
int ReplayScreen::navigationStart()const{return 15+pageButtons();}
int ReplayScreen::pageButtons()const{
#ifdef QLIE_PROFILE_WHJ6
 return 0;
#else
 return pageCount();
#endif
}
bool ReplayScreen::exists(int item)const{return item>=0&&item<15&&page*15+unsigned(item)<catalog.items().size();}
void ReplayScreen::setup(){
 elapsed=0;scene.resetUiAnimations();
 for(unsigned i=0;i<15;++i){
  const auto id="ui_replay_item"+std::to_string(i);
  const auto index=page*15+i;
  if(index>=catalog.items().size()){scene.command("^"+id+",show:false");continue;}
#ifdef QLIE_PROFILE_WHJ6
  scene.command("^"+id+",show:true,motion:*Enable");
  scene.uiSlot(id,0,catalog.unlocked(index,progress)?catalog.items()[index].thumbnail:"script/memorymode/mem_hide.png");
#else
  scene.command("^"+id+",show:true,motion:*Enable");
  scene.uiSlot(id,0,catalog.unlocked(index,progress)?catalog.items()[index].thumbnail:"script/memorymode/サムネイル_no_image.png");
#endif
 }
 highlight();
}
void ReplayScreen::highlight(){
#ifdef QLIE_PROFILE_WHJ6
 for(int i=0;i<15&&exists(i);++i)
  whj6::ui::focus(scene,"ui_replay_item"+std::to_string(i),selected==i,!catalog.unlocked(page*15+unsigned(i),progress));
 for(int i=0;i<4;++i)whj6::ui::focus(scene,"ui_replay_nav"+std::to_string(i),selected==navigationStart()+i,i==1);
#else
 for(unsigned i=0;i<15;i++){auto index=page*15+i;if(index>=catalog.items().size())continue;auto motion=!catalog.unlocked(index,progress)?"*OnLock":selected==int(i)?"*OnMouse":"*OffMouse";scene.setUiMotion("ui_replay_item"+std::to_string(i),motion);}
 for(int i=0;i<pageCount();i++)scene.setUiMotion("ui_replay_page"+std::to_string(i),page==unsigned(i)?"*OnCheck":selected==15+int(i)?"*OnMouse":"*OffMouse");
 for(int i=game_profile::story.legacyAvg?3:0;i<4;i++){
  scene.setUiMotion("ui_replay_nav"+std::to_string(i),i==1?"*OnLock":selected==navigationStart()+i?"*OnMouse":"*OffMouse");
 }
#endif
}
void ReplayScreen::close(){opened=false;}
void ReplayScreen::update(double ms){if(!opened)return;elapsed+=std::max(0.,ms);scene.update(ms);}
void ReplayScreen::move(int x,int y){if(!opened||elapsed<250)return;
 const int navigation=navigationStart();
 if(selected<15){
  if(y<0)selected=selected<5?(pageButtons()?15:selected):selected-5;
  else if(y>0)selected=exists(selected+5)?selected+5:navigation+3;
  else if(x&&exists(selected+x))selected+=x;
 }else if(selected<navigation){
  if(y>0)selected=0;
  else if(x)selected=15+(selected-15+x+pageCount())%pageCount();
 }else if(pageCount()&&y<0){
  int item=page==0?10:0;
  while(item>0&&!exists(item))item-=5;
  selected=item;
 }else if(pageButtons()&&y>0)selected=15;
 else if(x&&!game_profile::story.legacyAvg){
  do{selected=navigation+(selected-navigation+x+4)%4;}while(selected==navigation+1);
 }
 highlight();
}
int ReplayScreen::confirm(){if(!opened||elapsed<250)return None;if(selected<15){auto index=page*15+unsigned(selected);return index<catalog.items().size()&&catalog.unlocked(index,progress)?int(index):None;}
 if(selected<navigationStart()){page=unsigned(selected-15);selected=0;setup();return None;}return selected==navigationStart()+0?Album:selected==navigationStart()+2?Music:selected==navigationStart()+3?Back:None;}
int ReplayScreen::tap(float x,float y){if(!opened||elapsed<250)return None;
#ifdef QLIE_PROFILE_WHJ6
 for(int i=0;i<15;i++)if(exists(i)&&whj6::ui::contains(x,y,replayX(i),replayY(i),340,250)){
  selected=i;highlight();return confirm();
 }
#else
 for(int i=0;i<15;i++)if(exists(i)&&scene.uiHit("ui_replay_item"+std::to_string(i),x,y)){selected=i;highlight();return confirm();}
 for(int i=0;i<pageButtons();i++)if(scene.uiHit("ui_replay_page"+std::to_string(i),x,y)){selected=15+i;return confirm();}
#endif
 for(int i=game_profile::story.legacyAvg?3:0;i<4;i++)if(i!=1&&scene.uiHit("ui_replay_nav"+std::to_string(i),x,y)){selected=navigationStart()+i;highlight();return confirm();}
 return None;}
}
