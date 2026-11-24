// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cg_catalog.hpp"
#include "scene_images.hpp"
namespace whj2 {
class CgScreen {
 const Progress&progress;CgCatalog catalog;SceneImages menuScene,viewScene;qlie::Assets&assets;KFont*font;
 bool opened=false,viewing=false;unsigned page=0,group=0,variant=0,zoom=1;int selected=0;double elapsed=0;float pointerX=640,pointerY=360;
 void setup();void highlight();void loadView();void positionView();bool exists(int)const;
public:
 void setFont(KFont*value){font=value;}
 enum Action {None=0,Back=1,Replay=2,Music=3};
 CgScreen(qlie::Assets&a,const Progress&p,KFont*f):progress(p),catalog(a),menuScene(a),viewScene(a),assets(a),font(f){}
 void open();void close();bool visible()const{return opened;}bool inView()const{return viewing;}
 void update(double);void draw(qlie::Bytes&)const;void move(int,int);Action confirm();Action tap(float,float);Action back();void toggleZoom();void pan(float,float);void panDelta(float,float);
 void draw(RenderTarget&)const;
 std::set<std::string> takeShownFiles(){return viewScene.takeShownFiles();}
 unsigned pageIndex()const{return page;}unsigned variantIndex()const{return variant;}unsigned zoomLevel()const{return zoom;}int selection()const{return selected;}
};
}
