// SPDX-License-Identifier: GPL-2.0-or-later
#include "choice_screen.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace whj2 {
namespace {
constexpr auto AnimationFile="avg/select/animation.b";
constexpr auto ButtonFile="avg/select/selectbutton.b";
constexpr auto AnimationLayer="ui_choice_animation";
#ifdef QLIE_PROFILE_WHJ6
constexpr MessageStyle choiceText{550,30,26,26,0xffffff};
constexpr int choiceCenter=960,choiceMiddle=540,choiceStep=80;
#else
constexpr MessageStyle choiceText{540,50,24,24,0xffffff};
constexpr int choiceCenter=640,choiceMiddle=290,choiceStep=60;
#endif
int choiceY(size_t count,size_t index){return choiceMiddle+(int(index)*2-int(count-1))*choiceStep/2;}
}
void ChoiceScreen::prepare(AssetReadAhead&loader){
 scene.setReadAhead(&loader);
 scene.warmFile(AnimationFile,true,false);
 scene.warmFile(ButtonFile,true,false);
}
void ChoiceScreen::show(const std::vector<std::string>&values,KFont*font){
 if(values.empty()||values.size()>6)throw std::runtime_error("Choice count outside original layout");
 scene.removeUiLayer(AnimationLayer);
 for(size_t i=0;i<entries.size();++i)scene.removeUiLayer("ui_choice"+std::to_string(i));
 entries=values;elapsed=0;selected=0;pending=-1;closing=false;closingElapsed=0;
 scene.command(std::string("^")+AnimationLayer+",file:"+AnimationFile+",x:100,y:600,pri:1500,motion:*Enable");
 for(size_t i=0;i<entries.size();++i){
  auto name="ui_choice"+std::to_string(i);
  scene.command("^"+name+",file:"+ButtonFile+",x:"+std::to_string(choiceCenter)+",y:"+std::to_string(choiceY(entries.size(),i))+",pri:1520,motion:*Enable");
  label.start("[spd,0][pc,"+entries[i]+"]");
  scene.uiSlot(name,2,label.render(font,choiceText));
 }
 select(0);
}
bool ChoiceScreen::select(int index){if(closing||index<0||size_t(index)>=entries.size())return false;int old=selected;selected=index;if(old!=selected)scene.command("^ui_choice"+std::to_string(old)+",motion:*OffMouse");scene.command("^ui_choice"+std::to_string(selected)+",motion:*OnMouse");return old!=selected;}
bool ChoiceScreen::confirm(){if(closing){update((closingElapsed<250?250:500)-closingElapsed);return false;}if(!ready())return false;pending=selected;closing=true;closingElapsed=0;scene.command("^ui_choice"+std::to_string(selected)+",motion:*OnClick");return true;}
void ChoiceScreen::update(double ms){ms=std::max(0.,ms);elapsed+=ms;if(!closing){scene.update(ms);return;}double before=closingElapsed;closingElapsed+=ms;if(before<250&&closingElapsed>=250){scene.update(250-before);scene.command("^ui_choice_animation,motion:*Disable");scene.update(closingElapsed-250);}else scene.update(ms);}
int ChoiceScreen::takeConfirmed(){if(!closing||closingElapsed<500)return -1;int result=pending;pending=-1;return result;}
qlie::Bytes ChoiceScreen::save()const{
 StateWriter w;
 w.str("WHJ2-choice-2");
 w.n(0); // Retain the retired automatic-backup field for save compatibility.
 w.n(entries.size());w.n(unsigned(selected));w.n(unsigned(pending+1));w.n(closing);
 w.n(uint64_t(elapsed*1000));w.n(uint64_t(closingElapsed*1000));
 const auto state=scene.save();
 w.str(std::string(state.begin(),state.end()));
 return w.bytes;
}
void ChoiceScreen::restore(const qlie::Bytes&bytes,KFont*font){
 StateReader r{bytes};
 const auto version=r.str();
 if(version!="WHJ2-choice-1"&&version!="WHJ2-choice-2")throw std::runtime_error("Choice state version");
 if(version=="WHJ2-choice-2")r.n();
 if(r.n()!=entries.size())throw std::runtime_error("Choice state count");
 const auto focus=r.n(),chosen=r.n(),exiting=r.n();
 if(focus>=entries.size()||chosen>entries.size()||exiting>1||(exiting&&chosen==0))
  throw std::runtime_error("Choice state selection");
 selected=int(focus);pending=int(chosen)-1;closing=exiting!=0;
 elapsed=r.n()/1000.;closingElapsed=r.n()/1000.;
 const auto state=r.str();r.end();
 scene.restore(qlie::Bytes(state.begin(),state.end()));
 for(size_t i=0;i<entries.size();++i){
  const auto name="ui_choice"+std::to_string(i);
#ifdef QLIE_PROFILE_WHJ6
  scene.command("^"+name+",y:"+std::to_string(choiceY(entries.size(),i)));
#endif
  label.start("[spd,0][pc,"+entries[i]+"]");
  scene.uiSlot(name,2,label.render(font,choiceText));
 }
}
int ChoiceScreen::hit(float x,float y)const{if(!ready())return -1;for(size_t i=0;i<entries.size();i++)if(scene.uiHit("ui_choice"+std::to_string(i),x,y))return int(i);return -1;}
}
