// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
#include <array>
namespace whj2 {
class TitleScreen {
 SceneImages scene;double elapsed=0;float viewX=0,viewTarget=640;bool shown=false,hover=false,extras=false,extraPage=false;int selected=0;
 std::array<std::string,5> buttonMotions{};
 void enable();void highlight();
public:
 enum Action {None,Start,Load,Extra,Settings,Quit,CG,Memory,Sound,Back};
 explicit TitleScreen(qlie::Assets&a):scene(a){}
 void prepareOpening(AssetReadAhead*loader){
  scene.setReadAhead(loader);
  for(const char*path:{"script/title/title_back01.b","script/title/frame.b","script/title/button.b"})scene.warmFile(path);
 }
 void reset(bool alternate=false,bool unlocked=false);
 void view(float x);
 void setExtraPage(bool);bool isExtraPage()const{return extraPage;}
 void update(double);void skipIntro();bool ready()const{return elapsed>=5500;}
 int selection()const{return selected;}int move(int);bool select(int);int hit(float,float)const;
 void draw(RenderTarget&target)const{scene.draw(target);}
 Action confirm();void draw(qlie::Bytes&pixels)const{scene.draw(pixels);}
};
}
