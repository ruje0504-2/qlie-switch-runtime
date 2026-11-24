// SPDX-License-Identifier: GPL-2.0-or-later
#include "title_screen.hpp"
#include <algorithm>
#include <cmath>
namespace whj2 {
void TitleScreen::reset(bool alternate,bool unlocked){scene.clear();elapsed=0;shown=hover=false;selected=0;extras=unlocked;
 scene.command(std::string("^ui_back,file:script/title/title_back0")+(alternate?"2":"1")+".b,x:0,y:0,pri:0");
 scene.uiExternal("ui_back",0,0);
 scene.command("^ui_frame,file:script/title/frame.b,x:1070,y:450,pri:90,show:false");
 const char*names[]={"start","load","album","option","quit"};for(unsigned i=0;i<5;i++){auto id="ui_button"+std::to_string(i);scene.command("^"+id+",file:script/title/button.b,x:1080,y:"+std::to_string(310+i*70)+",pri:100,show:false");scene.uiSlot(id,0,std::string("script/title/")+names[i]+"01.png");scene.uiSlot(id,1,std::string("script/title/")+names[i]+"02.png");}
}
void TitleScreen::enable(){shown=true;scene.command("^ui_frame,show:true,motion:*Enable");for(int i=0;i<5;i++)scene.command("^ui_button"+std::to_string(i)+",show:true,motion:*Enable");}
void TitleScreen::highlight(){hover=true;for(int i=0;i<5;i++)scene.command("^ui_button"+std::to_string(i)+",motion:"+(i==2&&!extras?"*OnLock":i==selected?"*OnMouse":"*OffMouse"));}
void TitleScreen::update(double ms){ms=std::max(0.,ms);if(!shown){double part=std::min(ms,5000-elapsed);scene.update(part);elapsed+=part;ms-=part;if(elapsed>=5000)enable();}scene.update(ms);elapsed+=ms;if(ready()&&!hover)highlight();}
void TitleScreen::skipIntro(){if(!ready())update(5500-elapsed);}
int TitleScreen::move(int direction){if(!ready())return selected;int next=selected;do{next=(next+(direction<0?-1:1)+5)%5;}while(next==2&&!extras);select(next);return selected;}
bool TitleScreen::select(int index){if(!ready()||index<0||index>=5||(index==2&&!extras))return false;if(selected!=index){selected=index;highlight();}return true;}
int TitleScreen::hit(float x,float y)const{if(!ready())return -1;for(int i=0;i<5;i++)if(std::abs(x-1080)<=80&&std::abs(y-(310+i*70))<=20)return i;return -1;}
TitleScreen::Action TitleScreen::confirm(){if(!ready()){if(elapsed<5000)update(5000-elapsed);return None;}if(selected==2&&!extras)return None;return Action(selected+1);}
}
