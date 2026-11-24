// SPDX-License-Identifier: GPL-2.0-or-later
#include "title_screen.hpp"
#include <algorithm>
#include <cmath>
namespace whj2 {
void TitleScreen::reset(bool alternate,bool unlocked){scene.clear();elapsed=0;shown=hover=false;selected=0;buttonMotions.fill("");extras=unlocked;extraPage=false;viewX=0;viewTarget=640;
 scene.command(std::string("^ui_back,file:script/title/title_back0")+(alternate?"2":"1")+".b,x:0,y:0,pri:0");
 scene.uiExternal("ui_back",0,0);
 scene.command("^ui_frame,file:script/title/frame.b,x:1070,y:450,pri:90,show:false");
 const char*names[]={"start","load","album","option","quit"};for(unsigned i=0;i<5;i++){auto id="ui_button"+std::to_string(i);scene.command("^"+id+",file:script/title/button.b,x:1080,y:"+std::to_string(310+i*70)+",pri:100,show:false");scene.uiSlot(id,0,std::string("script/title/")+names[i]+"01.png");scene.uiSlot(id,1,std::string("script/title/")+names[i]+"02.png");}
}
void TitleScreen::setExtraPage(bool extra){if(extra&&!extras)return;extraPage=extra;selected=0;buttonMotions.fill("");shown=true;hover=false;elapsed=5000;
 const char*mainNames[]={"start","load","album","option","quit"};const char*extraNames[]={"CG","Memory","Sound","Back"};
 for(int i=0;i<5;i++){auto id="ui_button"+std::to_string(i);if(extra&&i==4){scene.command("^"+id+",show:false");continue;}
  scene.command("^"+id+",x:1080,y:"+std::to_string(extra?330+i*80:310+i*70)+",show:true,motion:*Enable");
  auto file=std::string("script/title/")+(extra?extraNames[i]:mainNames[i]);scene.uiSlot(id,0,file+"01.png");scene.uiSlot(id,1,file+"02.png");
 }
 scene.command("^ui_frame,motion:*Enable");
}
void TitleScreen::enable(){buttonMotions.fill("");shown=true;scene.command("^ui_frame,show:true,motion:*Enable");for(int i=0;i<5;i++)scene.command("^ui_button"+std::to_string(i)+",show:true,motion:*Enable");}
void TitleScreen::highlight(){hover=true;for(int i=0;i<(extraPage?4:5);i++){
 const char*motion=!extraPage&&i==2&&!extras?"*OnLock":i==selected?"*OnMouse":"*OffMouse";
 if(buttonMotions[i]==motion)continue;
 scene.command("^ui_button"+std::to_string(i)+",motion:"+motion);buttonMotions[i]=motion;
}}
void TitleScreen::view(float x){viewTarget=std::clamp(x,0.f,1280.f);}
void TitleScreen::update(double ms){ms=std::max(0.,ms);viewX=(viewX*7+viewTarget)/8;scene.uiExternal("ui_back",0,viewX*1000/1280);if(!shown){double part=std::min(ms,5000-elapsed);scene.update(part);elapsed+=part;ms-=part;if(elapsed>=5000)enable();}scene.update(ms);elapsed+=ms;if(ready()&&!hover)highlight();}
void TitleScreen::skipIntro(){if(!ready())update(5500-elapsed);}
int TitleScreen::move(int direction){if(!ready())return selected;int next=selected;do{next=(next+(direction<0?-1:1)+(extraPage?4:5))%(extraPage?4:5);}while(!extraPage&&next==2&&!extras);select(next);return selected;}
bool TitleScreen::select(int index){if(!ready()||index<0||index>=(extraPage?4:5)||(!extraPage&&index==2&&!extras))return false;if(selected!=index){selected=index;highlight();}return true;}
int TitleScreen::hit(float x,float y)const{if(!ready())return -1;for(int i=0;i<(extraPage?4:5);i++)if(scene.uiHit("ui_button"+std::to_string(i),x,y))return i;return -1;}
TitleScreen::Action TitleScreen::confirm(){if(!ready()){if(elapsed<5000)update(5000-elapsed);return None;}if(!extraPage&&selected==2&&!extras)return None;return Action(extraPage?int(CG)+selected:selected+1);}
}
