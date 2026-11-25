// SPDX-License-Identifier: GPL-2.0-or-later
#include "upper_menu.hpp"
#include "render_queue.hpp"
#include <cmath>
namespace whj2 {
UpperMenu::UpperMenu(qlie::Assets&assets,bool showScreen):frame(image(assets.read("avg/uppermenu/メニューバー.png"))),buttonCount(showScreen?3:2){
 frame.version.publish();
 const char*names[]={"ゲームの終了","タイトルへ戻る","画面の切り替え"};
 for(int i=0;i<buttonCount;i++){off[i]=image(assets.read(std::string("avg/uppermenu/")+names[i]+"_off.png"));on[i]=image(assets.read(std::string("avg/uppermenu/")+names[i]+"_on.png"));off[i].version.publish();on[i].version.publish();}
}
void UpperMenu::move(int direction){const int count=buttonCount+int(sectionSkip);if(shown)selected=(selected+direction%count+count)%count;}
UpperMenu::Action UpperMenu::confirm(){return shown?(sectionSkipSelected()?SectionSkip:Action(selected+1)):None;}
UpperMenu::Action UpperMenu::tap(float x,float y){if(!shown)return None;if(sectionSkip&&std::abs(x-800)<=56&&std::abs(y-25)<=14){selected=buttonCount;return SectionSkip;}for(int i=0;i<buttonCount;i++){float center=935+i*135;if(std::abs(x-center)<=56&&std::abs(y-25)<=14){selected=i;return Action(i+1);}}close();return None;}
bool UpperMenu::swipe(float dx,float dy){if(dy>=64&&dy>std::abs(dx)){open();return true;}return false;}
void UpperMenu::draw(qlie::Bytes&pixels)const{render(&pixels,nullptr);}
void UpperMenu::draw(RenderTarget&target)const{render(nullptr,&target);}
void UpperMenu::render(qlie::Bytes*pixels,RenderTarget*target)const{if(!shown)return;RenderQueue queue;queue.image(frame,imagePlacement(0,0,1,1,0,0,0,frame.width,frame.height),3000,255,0,0xffffff);
 for(int i=0;i<buttonCount;i++){auto&button=i==selected?on[i]:off[i];queue.image(button,imagePlacement(935+i*135,25,1,1,0,.5,.5,button.width,button.height),3010,255,0,0xffffff);}if(target)queue.draw(*target,1280,720);else queue.draw(*pixels,1280,720);
}
}
