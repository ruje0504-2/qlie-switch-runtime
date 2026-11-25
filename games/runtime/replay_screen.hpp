// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "replay_catalog.hpp"
#include "scene_images.hpp"
namespace whj2 {
class ReplayScreen {
 const Progress&progress;ReplayCatalog catalog;SceneImages scene;
 bool prepared=false,opened=false;unsigned page=0;int selected=0;double elapsed=0;
 void prepareLayout();void setup();void highlight();bool exists(int)const;
 int pageCount()const;int navigationStart()const;
public:
 enum Action {None=-1,Back=-2,Music=-3,Album=-4};
 ReplayScreen(qlie::Assets&a,const Progress&p):progress(p),catalog(a),scene(a){}
 void prepare();
 void open(unsigned pageIndex=0);void close();bool visible()const{return opened;}
 void update(double);void draw(qlie::Bytes&out,const Affine2D*view=nullptr)const{scene.draw(out,false,view);}
 void draw(RenderTarget&out,const Affine2D*view=nullptr)const{scene.draw(out,false,view);}
 void move(int,int);int confirm();int tap(float,float);
 unsigned pageIndex()const{return page;}int selection()const{return selected;}
 const ReplayEntry&entry(unsigned i)const{return catalog.items().at(i);}
};
}
