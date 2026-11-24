// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
#include <array>
#include <random>
namespace whj2 {
class TitleScreen {
 SceneImages scene;double elapsed=0;float viewX=0,viewTarget=640;bool shown=false,hover=false,extras=false,extraPage=false;int selected=0;
 std::array<std::string,5> buttonMotions{};
 std::mt19937 random;
 void setBackdrop(bool alternate);
 const char*entryMotion(int);
 void enable();void highlight();
public:
 enum Action {None,Start,Load,Extra,Settings,Quit,CG,Memory,Sound,Back};
 explicit TitleScreen(qlie::Assets&);
 bool rollAlternate();
 bool prepareOpening(AssetReadAhead*loader);
 void warmBackdrop(bool alternate);
 void returnToExtras(bool alternate);
 void reset(bool alternate=false,bool unlocked=false);
 void view(float x);
 void setExtraPage(bool);bool isExtraPage()const{return extraPage;}
 void update(double);void skipIntro();bool ready()const{return elapsed>=5500;}
 int selection()const{return selected;}int move(int);bool select(int);int hit(float,float)const;
 void draw(RenderTarget&target)const{scene.draw(target);}
 Action confirm();void draw(qlie::Bytes&pixels)const{scene.draw(pixels);}
};
}
