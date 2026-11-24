// SPDX-License-Identifier: GPL-2.0-or-later
#include "upper_menu.hpp"
#include "render_queue.hpp"
#include <cmath>
namespace whj2 {
UpperMenu::UpperMenu(qlie::Assets&assets):frame(image(assets.read("avg/uppermenu/メニューバー.png"))){
 frame.version.publish();
 const char*names[]={"ゲームの終了","タイトルへ戻る","画面の切り替え"};
 for(unsigned i=0;i<3;i++){off[i]=image(assets.read(std::string("avg/uppermenu/")+names[i]+"_off.png"));on[i]=image(assets.read(std::string("avg/uppermenu/")+names[i]+"_on.png"));off[i].version.publish();on[i].version.publish();}
}
void UpperMenu::move(int direction){if(shown)selected=(selected+direction%3+3)%3;}
UpperMenu::Action UpperMenu::confirm(){return shown?Action(selected+1):None;}
UpperMenu::Action UpperMenu::tap(float x,float y){if(!shown)return None;for(int i=0;i<3;i++){float center=935+i*135;if(std::abs(x-center)<=56&&std::abs(y-25)<=14){selected=i;return Action(i+1);}}close();return None;}
bool UpperMenu::swipe(float dx,float dy){if(dy>=64&&dy>std::abs(dx)){open();return true;}return false;}
void UpperMenu::draw(qlie::Bytes&pixels)const{render(&pixels,nullptr);}
void UpperMenu::draw(RenderTarget&target)const{render(nullptr,&target);}
void UpperMenu::render(qlie::Bytes*pixels,RenderTarget*target)const{if(!shown)return;RenderQueue queue;queue.image(frame,imagePlacement(0,0,1,1,0,0,0,frame.width,frame.height),3000,255,0,0xffffff);
 for(unsigned i=0;i<3;i++){auto&button=int(i)==selected?on[i]:off[i];queue.image(button,imagePlacement(935+i*135,25,1,1,0,.5,.5,button.width,button.height),3010,255,0,0xffffff);}if(target)queue.draw(*target,1280,720);else queue.draw(*pixels,1280,720);
}
}
