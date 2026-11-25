// SPDX-License-Identifier: GPL-2.0-or-later
#include "title_screen.hpp"
#include "render_queue.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace whj2 {
namespace {
constexpr auto titleBitmaps="@title/button-bitmaps";
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
void TitleScreen::warmBackdrop(bool alternate){
 scene.warmFile(layout.back[alternate]);
 scene.warmFile(layout.frame[alternate]);
}
bool TitleScreen::prepareOpening(AssetReadAhead*loader,bool unlocked){
 scene.setReadAhead(loader);
 const bool alternate=(!layout.alternateRequiresExtras||unlocked)&&rollAlternate();
 warmBackdrop(alternate);
 scene.warmFile(layout.buttonArt[0]);
 if(unlocked&&layout.buttonArt[0]!=std::string_view(layout.buttonArt[1]))scene.warmFile(layout.buttonArt[1]);
 if(std::string_view(layout.quitButton)!="script/title/button.b")scene.warmFile(layout.quitButton);
 std::vector<std::string> paths;
 auto add=[&](const char*item){for(auto state:{layout.buttonOff,layout.buttonOn})paths.push_back(std::string("script/title/")+item+state);};
 for(auto item:layout.mainFiles)add(item);
 if(unlocked||layout.albumAlwaysAvailable)for(auto item:layout.extraFiles)add(item);
 scene.warmUiBitmaps(titleBitmaps,std::move(paths));
 return alternate;
}
void TitleScreen::setBackdrop(bool alternate){
 viewX=0;viewTarget=640;
 scene.command(std::string("^ui_back,file:")+layout.back[alternate]+",x:0,y:0,pri:0");
 scene.uiExternal("ui_back",0,0);
}
void TitleScreen::returnToExtras(bool alternate){
 setBackdrop(alternate);
 setFrame(alternate,true);
 scene.seekUiLayer("ui_back",layout.returnBackdropMs);
 setExtraPage(true);
}
void TitleScreen::setFrame(bool alternate,bool visible){
 scene.command(std::string("^ui_frame,file:")+layout.frame[alternate]+",x:"+
  std::to_string(layout.frameX[alternate])+",y:"+std::to_string(layout.frameY[alternate])+",pri:90,show:"+(visible?"true":"false"));
}
std::string TitleScreen::buttonPosition(unsigned index,bool extra)const{
 const auto point=layout.positions?(*layout.positions)[extra][index]:
  std::array<int,2>{layout.buttonX[extra],layout.buttonY[extra]+int(index)*layout.buttonStep[extra]+
                    (extra&&index==3?layout.extraLastYOffset:0)};
 return ",x:"+std::to_string(point[0])+",y:"+std::to_string(point[1]);
}
void TitleScreen::reset(bool alternate,bool unlocked){scene.clear(true);scene.takeUiBitmaps(titleBitmaps);elapsed=0;shown=hover=false;selected=0;buttonMotions.fill("");extras=unlocked||layout.albumAlwaysAvailable;extraPage=false;
 shownButtons=0;
 backdropExitRemaining=-1;
 if(layout.alternateRequiresExtras&&!extras)alternate=false;
 setBackdrop(alternate);
 setFrame(alternate,layout.frameBackdrop);
 for(unsigned i=0;i<5;i++){
  const auto id="ui_button"+std::to_string(i);
  const auto asset=std::string("script/title/")+layout.mainFiles[i];
  scene.command("^"+id+",file:"+(i==4?layout.quitButton:layout.buttonArt[0])+buttonPosition(i,false)+",pri:100,show:false");
  scene.uiSlot(id,0,asset+layout.buttonOff);
  scene.uiSlot(id,1,asset+layout.buttonOn);
 }
}
void TitleScreen::setExtraPage(bool extra){if(extra&&!extras)return;extraPage=extra;selected=0;buttonMotions.fill("");shown=true;hover=false;elapsed=layout.introMs;shownButtons=5;
 for(int i=0;i<5;i++){auto id="ui_button"+std::to_string(i);if(extra&&i==4){scene.command("^"+id+",show:false");continue;}
  std::string command="^"+id;
  if(layout.buttonArt[0]!=std::string_view(layout.buttonArt[1]))
   command+=std::string(",file:")+layout.buttonArt[extra];
  scene.command(command+buttonPosition(unsigned(i),extra)+",show:true,motion:"+entryMotion(i));
  auto file=std::string("script/title/")+(extra?layout.extraFiles[i]:layout.mainFiles[i]);scene.uiSlot(id,0,file+layout.buttonOff);scene.uiSlot(id,1,file+layout.buttonOn);
 }
 if(!layout.frameBackdrop)scene.command("^ui_frame,motion:*Enable"+(layout.extraFrameXOffset?
  ",x:"+std::to_string(layout.frameX[0]+(extra?layout.extraFrameXOffset:0)):std::string()));
}
const char*TitleScreen::entryMotion(int i){
 if(!extraPage&&i==2&&!extras){buttonMotions[i]=layout.lockedEnter;return layout.lockedEnter;}
 return "*Enable";
}
void TitleScreen::showNextButton(){
 const auto index=shownButtons++;
 scene.command("^ui_button"+std::to_string(index)+",show:true,motion:"+entryMotion(int(index)));
}
void TitleScreen::enable(){
 buttonMotions.fill("");shown=true;
 if(!layout.frameBackdrop)scene.command("^ui_frame,show:true,motion:*Enable");
 const auto count=layout.buttonStaggerMs>0?1u:5u;
 while(shownButtons<count)showNextButton();
}
void TitleScreen::highlight(){hover=true;for(int i=0;i<(extraPage?4:5);i++){
 const char*motion=!extraPage&&i==2&&!extras?layout.lockedIdle:i==selected?"*OnMouse":"*OffMouse";
 if(buttonMotions[i]==motion)continue;
 scene.command("^ui_button"+std::to_string(i)+",motion:"+motion);buttonMotions[i]=motion;
}}
void TitleScreen::beginExit(){
 backdropExitRemaining=layout.backdropExitDelayMs;
 if(!layout.frameBackdrop)scene.command("^ui_frame,motion:*Disable");
 for(int i=0;i<5;i++)scene.command("^ui_button"+std::to_string(i)+",motion:*Disable");
}
void TitleScreen::view(float x){viewTarget=std::clamp(x,0.f,1280.f);}
void TitleScreen::update(double ms){
 ms=std::max(0.,ms);
 const float nextView=(viewX*7+viewTarget)/8;
 if(nextView!=viewX){viewX=nextView;scene.uiExternal("ui_back",0,viewX*1000/1280);}
 if(!shown){double part=std::min(ms,layout.introMs-elapsed);scene.update(part);elapsed+=part;ms-=part;if(elapsed>=layout.introMs)enable();}
 while(shown&&shownButtons<5){
  const double delay=layout.introMs+shownButtons*layout.buttonStaggerMs-elapsed;
  if(delay>ms)break;
  const double advance=std::max(0.,delay);
  scene.update(advance);
  elapsed+=advance;ms-=advance;
  showNextButton();
 }
 if(backdropExitRemaining>=0){
  const double advance=std::min(ms,backdropExitRemaining);
  scene.update(advance);elapsed+=advance;ms-=advance;
  backdropExitRemaining-=advance;
  if(backdropExitRemaining<=0){
   scene.command("^ui_back,motion:*Disable");
   backdropExitRemaining=-1;
  }
 }
 scene.update(ms);elapsed+=ms;if(ready()&&!hover)highlight();
}
unsigned TitleScreen::openingBrightness()const{
 if(layout.fadeInMs<=0||elapsed>=layout.fadeInMs)return 255;
 return unsigned(255*elapsed/layout.fadeInMs);
}
void TitleScreen::draw(RenderTarget&target)const{
 scene.draw(target);
 const auto brightness=openingBrightness();
 if(brightness<255)target.dim(brightness,255);
}
void TitleScreen::draw(qlie::Bytes&pixels)const{
 scene.draw(pixels);
 const auto brightness=openingBrightness();
 if(brightness<255)for(size_t i=0;i<pixels.size();i+=4)
  for(unsigned c=0;c<3;c++)pixels[i+c]=uint8_t(unsigned(pixels[i+c])*brightness/255);
}
void TitleScreen::skipIntro(){if(!ready())update(layout.introMs+layout.buttonEntranceMs-elapsed);}
int TitleScreen::move(int direction){if(!ready())return selected;int next=selected;do{next=(next+(direction<0?-1:1)+(extraPage?4:5))%(extraPage?4:5);}while(!extraPage&&next==2&&!extras);select(next);return selected;}
bool TitleScreen::select(int index){if(!ready()||index<0||index>=(extraPage?4:5)||(!extraPage&&index==2&&!extras))return false;if(selected!=index){selected=index;highlight();}return true;}
int TitleScreen::hit(float x,float y)const{if(!ready())return -1;for(int i=0;i<(extraPage?4:5);i++)if(scene.uiHit("ui_button"+std::to_string(i),x,y))return i;return -1;}
TitleScreen::Action TitleScreen::confirm(){if(!ready()){if(elapsed<layout.introMs)update(layout.introMs-elapsed);return None;}if(!extraPage&&selected==2&&!extras)return None;return Action(extraPage?int(CG)+selected:selected+1);}
}
