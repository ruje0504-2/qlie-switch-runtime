// SPDX-License-Identifier: GPL-2.0-or-later
#include "upper_menu.hpp"
#include "render_queue.hpp"
#include <cmath>
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6/ui.hpp"
#endif
namespace whj2 {
#ifdef QLIE_PROFILE_WHJ6
namespace {
constexpr int buttonX[]={770,990,1450,1670};
constexpr UpperMenu::Action actions[]={UpperMenu::Quit,UpperMenu::Title,UpperMenu::Save,UpperMenu::Load};
}
UpperMenu::UpperMenu(qlie::Assets&assets,bool)
 :frame(image(assets.read("avg/uppermenu/メニューバー.png"))),buttonCount(4){
 frame.version.publish();
 constexpr const char*names[]={"ゲームの終了","タイトルへ戻る","データのセーブ","データのロード"};
 for(int i=0;i<buttonCount;++i){
  const auto prefix=std::string("avg/uppermenu/")+names[i];
  off[i]=image(assets.read(prefix+"_off.png"));off[i].version.publish();
  on[i]=image(assets.read(prefix+"_on.png"));on[i].version.publish();
  if(i>=2){saveLoadLocked[i-2]=image(assets.read(prefix+"_lock.png"));saveLoadLocked[i-2].version.publish();}
 }
}
void UpperMenu::move(int direction){
 if(!shown||!direction)return;
 const int count=buttonCount+int(sectionSkip);
 const int step=direction>0?1:-1;
 do selected=(selected+step+count)%count;
 while(selected<buttonCount&&!enabled(selected));
}
UpperMenu::Action UpperMenu::confirm(){
 if(!shown)return None;
 if(sectionSkipSelected())return SectionSkip;
 return enabled(selected)?actions[selected]:None;
}
UpperMenu::Action UpperMenu::tap(float x,float y){
 if(!shown)return None;
 if(sectionSkip&&whj6::ui::contains(x,y,460,0,192,42)){selected=buttonCount;return SectionSkip;}
 for(int i=0;i<buttonCount;++i)if(whj6::ui::contains(x,y,buttonX[i]-105,0,210,42)){
  if(!enabled(i))return None;
  selected=i;return actions[i];
 }
 close();return None;
}
#else
UpperMenu::UpperMenu(qlie::Assets&assets,bool showScreen):frame(image(assets.read("avg/uppermenu/メニューバー.png"))),buttonCount(showScreen?3:2){
 frame.version.publish();
 const char*names[]={"ゲームの終了","タイトルへ戻る","画面の切り替え"};
 for(int i=0;i<buttonCount;i++){off[i]=image(assets.read(std::string("avg/uppermenu/")+names[i]+"_off.png"));on[i]=image(assets.read(std::string("avg/uppermenu/")+names[i]+"_on.png"));off[i].version.publish();on[i].version.publish();}
}
void UpperMenu::move(int direction){const int count=buttonCount+int(sectionSkip);if(shown)selected=(selected+direction%count+count)%count;}
UpperMenu::Action UpperMenu::confirm(){return shown?(sectionSkipSelected()?SectionSkip:Action(selected+1)):None;}
UpperMenu::Action UpperMenu::tap(float x,float y){if(!shown)return None;if(sectionSkip&&std::abs(x-800)<=56&&std::abs(y-25)<=14){selected=buttonCount;return SectionSkip;}for(int i=0;i<buttonCount;i++){float center=935+i*135;if(std::abs(x-center)<=56&&std::abs(y-25)<=14){selected=i;return Action(i+1);}}close();return None;}
#endif
bool UpperMenu::swipe(float dx,float dy){if(dy>=64&&dy>std::abs(dx)){open();return true;}return false;}
void UpperMenu::draw(qlie::Bytes&pixels)const{render(&pixels,nullptr);}
void UpperMenu::draw(RenderTarget&target)const{render(nullptr,&target);}
void UpperMenu::render(qlie::Bytes*pixels,RenderTarget*target)const{if(!shown)return;RenderQueue queue;
#ifdef QLIE_PROFILE_WHJ6
 using whj6::ui::to720;
 queue.image(frame,imagePlacement(0,0,2.f/3,2.f/3,0,0,0,frame.width,frame.height),3000,255,0,0xffffff);
 for(int i=0;i<buttonCount;++i){
  const auto& button=!enabled(i)?saveLoadLocked[i-2]:i==selected?on[i]:off[i];
  queue.image(button,imagePlacement(to720(buttonX[i]),to720(21),2.f/3,2.f/3,0,.5,.5,button.width,button.height),
              3010,255,0,0xffffff);
 }
 if(target)queue.draw(*target,1280,720);else queue.draw(*pixels,1280,720);
#else
 queue.image(frame,imagePlacement(0,0,1,1,0,0,0,frame.width,frame.height),3000,255,0,0xffffff);
 for(int i=0;i<buttonCount;i++){auto&button=i==selected?on[i]:off[i];queue.image(button,imagePlacement(935+i*135,25,1,1,0,.5,.5,button.width,button.height),3010,255,0,0xffffff);}if(target)queue.draw(*target,1280,720);else queue.draw(*pixels,1280,720);
#endif
}
}
