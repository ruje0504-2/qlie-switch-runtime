// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene_images.hpp"
namespace whj2 {
class ChoiceScreen {
 qlie::Assets&assets;SceneImages scene;std::vector<std::string> entries;double elapsed=0;int selected=0;
public:
 explicit ChoiceScreen(qlie::Assets&a):assets(a),scene(a){}
 void show(const std::vector<std::string>&,KFont*);
 void update(double ms){scene.update(ms);elapsed+=ms;}
 bool ready()const{return elapsed>=250;}
 void select(int);int hit(float,float)const;
 void draw(qlie::Bytes&pixels)const{scene.draw(pixels);}
};
}
