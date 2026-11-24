// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
namespace whj2 {
class TitleScreen {
 SceneImages scene;double elapsed=0;bool shown=false,hover=false,extras=false,extraPage=false;int selected=0;
 void enable();void highlight();
public:
 enum Action {None,Start,Load,Extra,Settings,Quit,CG,Memory,Sound,Back};
 explicit TitleScreen(qlie::Assets&a):scene(a){}
 void reset(bool alternate=false,bool unlocked=false);
 void setExtraPage(bool);bool isExtraPage()const{return extraPage;}
 void update(double);void skipIntro();bool ready()const{return elapsed>=5500;}
 int selection()const{return selected;}int move(int);bool select(int);int hit(float,float)const;
 Action confirm();void draw(qlie::Bytes&pixels)const{scene.draw(pixels);}
};
}
