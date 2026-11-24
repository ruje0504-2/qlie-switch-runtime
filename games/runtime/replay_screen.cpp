// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_screen.hpp"
#include <algorithm>
namespace whj2 {
void ReplayScreen::prepareLayout(){
 auto add=[&](const std::string&id,const std::string&file,int x,int y,int pri){scene.command("^"+id+",file:script/memorymode/"+file+",x:"+std::to_string(x)+",y:"+std::to_string(y)+",pri:"+std::to_string(pri));};
 add("ui_replay_bg","背景.png",0,0,100);add("ui_replay_upper","アッパーフレーム.png",0,0,100);add("ui_replay_lower","アンダーフレーム.png",0,656,100);
 for(unsigned i=0;i<15;i++)add("ui_replay_item"+std::to_string(i),"Button_Thum.b",60+int(i%5)*240,140+int(i/5)*170,110);
 for(unsigned i=0;i<(catalog.items().size()+14)/15;i++){auto id="ui_replay_page"+std::to_string(i);add(id,"Button_Page.b",1170+int(i)*40,90,110);auto name="script/memorymode/ページ_0"+std::to_string(i+1);scene.uiSlot(id,0,name+"_off.png");scene.uiSlot(id,1,name+"_on.png");}
 const char*names[]={"CG観賞","回想観賞","音楽鑑賞","戻る"};for(int i=0;i<4;i++){auto id="ui_replay_nav"+std::to_string(i);add(id,"Button_l.b",820+i*130,695,110);scene.uiSlot(id,0,"script/memorymode/"+std::string(names[i])+"_off.png");scene.uiSlot(id,1,"script/memorymode/"+std::string(names[i])+"_on.png");}
 scene.prepareUiBitmap("script/memorymode/サムネイル_no_image.png");
 prepared=true;
}
void ReplayScreen::prepare(){
 if(!prepared)prepareLayout();
 for(size_t i=0;i<catalog.items().size();++i)
  if(catalog.unlocked(i,progress))scene.prepareUiBitmap(catalog.items()[i].thumbnail);
}
void ReplayScreen::open(unsigned pageIndex){
 if(!prepared)prepare();
 opened=true;page=std::min(pageIndex,unsigned((catalog.items().size()-1)/15));selected=0;
 setup();
}
bool ReplayScreen::exists(int i)const{return i>=0&&(i<15?page*15+unsigned(i)<catalog.items().size():i<21);}
void ReplayScreen::setup(){elapsed=0;scene.resetUiAnimations();for(unsigned i=0;i<15;i++){auto id="ui_replay_item"+std::to_string(i);auto index=page*15+i;
 if(index>=catalog.items().size()){scene.command("^"+id+",show:false");continue;}scene.command("^"+id+",show:true,motion:*Enable");scene.uiSlot(id,0,catalog.unlocked(index,progress)?catalog.items()[index].thumbnail:"script/memorymode/サムネイル_no_image.png");}
 highlight();}
void ReplayScreen::highlight(){for(unsigned i=0;i<15;i++){auto index=page*15+i;if(index>=catalog.items().size())continue;auto motion=!catalog.unlocked(index,progress)?"*OnLock":selected==int(i)?"*OnMouse":"*OffMouse";scene.setUiMotion("ui_replay_item"+std::to_string(i),motion);}
 for(unsigned i=0;i<(catalog.items().size()+14)/15;i++)scene.setUiMotion("ui_replay_page"+std::to_string(i),page==i?"*OnCheck":selected==15+int(i)?"*OnMouse":"*OffMouse");
 for(int i=0;i<4;i++)scene.setUiMotion("ui_replay_nav"+std::to_string(i),i==1?"*OnLock":selected==17+i?"*OnMouse":"*OffMouse");}
void ReplayScreen::close(){opened=false;}
void ReplayScreen::update(double ms){if(!opened)return;elapsed+=std::max(0.,ms);scene.update(ms);}
void ReplayScreen::move(int x,int y){if(!opened||elapsed<250)return;
 if(selected<15){if(y<0){selected=selected<5?15:selected-5;}else if(y>0){int n=selected+5;selected=n>=15||!exists(n)?20:n;}else if(x){int n=selected+x;selected=exists(n)&&n<15?n:selected;}}
 else if(selected<17){if(y>0)selected=0;else if(x)selected=15+(selected-15+x+2)%2;}
 else {if(y<0)selected=page==0?10:0;else if(y>0)selected=15;else if(x){do{selected=17+(selected-17+x+4)%4;}while(selected==18);}}
 highlight();}
int ReplayScreen::confirm(){if(!opened||elapsed<250)return None;if(selected<15){auto index=page*15+unsigned(selected);return index<catalog.items().size()&&catalog.unlocked(index,progress)?int(index):None;}
 if(selected<17){page=unsigned(selected-15);selected=0;setup();return None;}return selected==17?Album:selected==19?Music:selected==20?Back:None;}
int ReplayScreen::tap(float x,float y){if(!opened||elapsed<250)return None;for(int i=0;i<15;i++)if(exists(i)&&scene.uiHit("ui_replay_item"+std::to_string(i),x,y)){selected=i;highlight();return confirm();}
 for(int i=0;i<2;i++)if(scene.uiHit("ui_replay_page"+std::to_string(i),x,y)){selected=15+i;return confirm();}
 for(int i=0;i<4;i++)if(i!=1&&scene.uiHit("ui_replay_nav"+std::to_string(i),x,y)){selected=17+i;highlight();return confirm();}
 return None;}
}
