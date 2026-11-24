// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "replay_catalog.hpp"
#include "scene_images.hpp"
namespace whj2 {
class ReplayScreen {
 const Progress&progress;ReplayCatalog catalog;SceneImages scene;
 bool opened=false;unsigned page=0;int selected=0;double elapsed=0;
 void setup();void highlight();bool exists(int)const;
public:
 enum Action {None=-1,Back=-2,Music=-3,Album=-4};
 ReplayScreen(qlie::Assets&a,const Progress&p):progress(p),catalog(a),scene(a){}
 void open(unsigned pageIndex=0);void close();bool visible()const{return opened;}
 void update(double);void draw(qlie::Bytes&out)const{scene.draw(out);}
 void move(int,int);int confirm();int tap(float,float);
 unsigned pageIndex()const{return page;}int selection()const{return selected;}
 const ReplayEntry&entry(unsigned i)const{return catalog.items().at(i);}
};
}
