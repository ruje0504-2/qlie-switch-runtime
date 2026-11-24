// SPDX-License-Identifier: GPL-2.0-or-later
#include "title_screen.hpp"
#include <algorithm>
#include <cmath>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace whj2 {
namespace {
uint32_t titleSeed(){
#ifdef __SWITCH__
 return uint32_t(randomGet64());
#else
 return std::random_device{}();
#endif
}
}
TitleScreen::TitleScreen(qlie::Assets&assets,TitleLayout placement):scene(assets),layout(placement),random(titleSeed()){}
bool TitleScreen::rollAlternate(){
 return std::uniform_int_distribution<unsigned>(0,layout.alternateRange-1)(random)<layout.alternateHits;
}
void TitleScreen::warmBackdrop(bool alternate){scene.warmFile(layout.back[alternate]);}
bool TitleScreen::prepareOpening(AssetReadAhead*loader,bool unlocked){
 scene.setReadAhead(loader);
 const bool alternate=(!layout.alternateRequiresExtras||unlocked)&&rollAlternate();
 warmBackdrop(alternate);
 scene.warmFile(layout.frame[alternate]);
 scene.warmFile("script/title/button.b");
 return alternate;
}
void TitleScreen::setBackdrop(bool alternate){
 viewX=0;viewTarget=640;
 scene.command(std::string("^ui_back,file:")+layout.back[alternate]+",x:0,y:0,pri:0");
 scene.uiExternal("ui_back",0,0);
}
void TitleScreen::returnToExtras(bool alternate){
 setBackdrop(alternate);
 scene.seekUiLayer("ui_back",layout.returnBackdropMs);
 setExtraPage(true);
}
void TitleScreen::reset(bool alternate,bool unlocked){scene.clear();elapsed=0;shown=hover=false;selected=0;buttonMotions.fill("");extras=unlocked;extraPage=false;
 if(layout.alternateRequiresExtras&&!extras)alternate=false;
 setBackdrop(alternate);
 scene.command(std::string("^ui_frame,file:")+layout.frame[alternate]+",x:"+
  std::to_string(layout.frameX[alternate])+",y:"+std::to_string(layout.frameY[alternate])+",pri:90,show:false");
 const char*names[]={"start","load","album","option","quit"};for(unsigned i=0;i<5;i++){auto id="ui_button"+std::to_string(i);scene.command("^"+id+",file:script/title/button.b,x:"+std::to_string(layout.buttonX[0])+",y:"+std::to_string(layout.buttonY[0]+i*layout.buttonStep[0])+",pri:100,show:false");scene.uiSlot(id,0,std::string("script/title/")+names[i]+"01.png");scene.uiSlot(id,1,std::string("script/title/")+names[i]+"02.png");}
}
void TitleScreen::setExtraPage(bool extra){if(extra&&!extras)return;extraPage=extra;selected=0;buttonMotions.fill("");shown=true;hover=false;elapsed=layout.introMs;
 const char*mainNames[]={"start","load","album","option","quit"};const char*extraNames[]={"CG","Memory","Sound","Back"};
 for(int i=0;i<5;i++){auto id="ui_button"+std::to_string(i);if(extra&&i==4){scene.command("^"+id+",show:false");continue;}
  scene.command("^"+id+",x:"+std::to_string(layout.buttonX[extra])+
   ",y:"+std::to_string(layout.buttonY[extra]+i*layout.buttonStep[extra])+",show:true,motion:"+entryMotion(i));
  auto file=std::string("script/title/")+(extra?extraNames[i]:mainNames[i]);scene.uiSlot(id,0,file+"01.png");scene.uiSlot(id,1,file+"02.png");
 }
 scene.command("^ui_frame,motion:*Enable");
}
const char*TitleScreen::entryMotion(int i){
 if(!extraPage&&i==2&&!extras){buttonMotions[i]=layout.lockedEnter;return layout.lockedEnter;}
 return "*Enable";
}
void TitleScreen::enable(){buttonMotions.fill("");shown=true;scene.command("^ui_frame,show:true,motion:*Enable");for(int i=0;i<5;i++)scene.command("^ui_button"+std::to_string(i)+",show:true,motion:"+entryMotion(i));}
void TitleScreen::highlight(){hover=true;for(int i=0;i<(extraPage?4:5);i++){
 const char*motion=!extraPage&&i==2&&!extras?layout.lockedIdle:i==selected?"*OnMouse":"*OffMouse";
 if(buttonMotions[i]==motion)continue;
 scene.command("^ui_button"+std::to_string(i)+",motion:"+motion);buttonMotions[i]=motion;
}}
void TitleScreen::view(float x){viewTarget=std::clamp(x,0.f,1280.f);}
void TitleScreen::update(double ms){ms=std::max(0.,ms);viewX=(viewX*7+viewTarget)/8;scene.uiExternal("ui_back",0,viewX*1000/1280);if(!shown){double part=std::min(ms,layout.introMs-elapsed);scene.update(part);elapsed+=part;ms-=part;if(elapsed>=layout.introMs)enable();}scene.update(ms);elapsed+=ms;if(ready()&&!hover)highlight();}
void TitleScreen::skipIntro(){if(!ready())update(layout.introMs+layout.buttonEntranceMs-elapsed);}
int TitleScreen::move(int direction){if(!ready())return selected;int next=selected;do{next=(next+(direction<0?-1:1)+(extraPage?4:5))%(extraPage?4:5);}while(!extraPage&&next==2&&!extras);select(next);return selected;}
bool TitleScreen::select(int index){if(!ready()||index<0||index>=(extraPage?4:5)||(!extraPage&&index==2&&!extras))return false;if(selected!=index){selected=index;highlight();}return true;}
int TitleScreen::hit(float x,float y)const{if(!ready())return -1;for(int i=0;i<(extraPage?4:5);i++)if(scene.uiHit("ui_button"+std::to_string(i),x,y))return i;return -1;}
TitleScreen::Action TitleScreen::confirm(){if(!ready()){if(elapsed<layout.introMs)update(layout.introMs-elapsed);return None;}if(!extraPage&&selected==2&&!extras)return None;return Action(extraPage?int(CG)+selected:selected+1);}
}
