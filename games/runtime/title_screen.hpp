// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
#include "profiles/title_layout.hpp"
#include <array>
#include <random>
namespace whj2 {
class TitleScreen {
 SceneImages scene;double elapsed=0;float viewX=0,viewTarget=640;bool shown=false,hover=false,extras=false,extraPage=false;int selected=0;
 std::array<std::string,5> buttonMotions{};
 TitleLayout layout;
 std::mt19937 random;
 void setBackdrop(bool alternate);
 void setFrame(bool alternate,bool visible);
 const char*entryMotion(int);
 void enable();void highlight();
public:
 enum Action {None,Start,Load,Extra,Settings,Quit,CG,Memory,Sound,Back};
 explicit TitleScreen(qlie::Assets&,TitleLayout={});
 bool rollAlternate();
 bool prepareOpening(AssetReadAhead*loader,bool unlocked=false);
 void leave(){scene.clear(true);}
 void warmBackdrop(bool alternate);
 void returnToExtras(bool alternate);
 void reset(bool alternate=false,bool unlocked=false);
 void view(float x);
 void setExtraPage(bool);bool isExtraPage()const{return extraPage;}
 void update(double);void skipIntro();bool ready()const{return elapsed>=layout.introMs+layout.buttonEntranceMs;}
 int selection()const{return selected;}int move(int);bool select(int);int hit(float,float)const;
 void draw(RenderTarget&target)const{scene.draw(target);}
 Action confirm();void draw(qlie::Bytes&pixels)const{scene.draw(pixels);}
};
}
