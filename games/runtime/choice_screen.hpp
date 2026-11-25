// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
namespace whj2 {
class ChoiceScreen {
 SceneImages scene;MessageText label;std::vector<std::string> entries;double elapsed=0;int selected=0;
 int pending=-1;double closingElapsed=0;bool closing=false;
public:
 ChoiceScreen(qlie::Assets&a,const MessageText&text,std::array<unsigned,2> sceneSize={1280,720})
  :scene(a,sceneSize),label(text.forLabels()){}
 void prepare(AssetReadAhead&);
 void show(const std::vector<std::string>&,KFont*);
 void update(double ms);
 bool ready()const{return !closing&&elapsed>=250;}
 bool exiting()const{return closing;}
 bool confirm();int takeConfirmed();
 void skipConfirmation(){if(closing)update(500-closingElapsed);}
 int selection()const{return selected;}
 qlie::Bytes save()const;void restore(const qlie::Bytes&,KFont*);
 bool select(int);int hit(float,float)const;
 void draw(qlie::Bytes&pixels)const{scene.draw(pixels);}
 void draw(RenderTarget&target)const{scene.draw(target);}
};
}
