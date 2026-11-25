// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cg_catalog.hpp"
#include "scene_images.hpp"
#include "scene_transition.hpp"
namespace whj2 {
class CgScreen {
 const Progress&progress;CgCatalog catalog;SceneImages menuScene,viewScene;KFont*font;
 SceneTransition viewFade;
 std::function<qlie::Bytes()> captureView;
 AssetReadAhead*readAhead=nullptr;
 std::string pendingView;
 MessageText countText;
 bool openingWarm=false;
 std::string displayedCount;
 bool prepared=false,opened=false,viewing=false,viewReady=false;unsigned page=0,group=0,variant=0,zoom=1;int selected=0;double elapsed=0;float pointerX=640,pointerY=360;
 unsigned requestedVariant=0;
 void prepareLayout();void setup();void highlight();void requestView(unsigned);void cancelViewRequest();void closeView();void loadView();void positionView();bool exists(int)const;
 int pageCount()const;int pageButtons()const;int navigationStart()const;
public:
 void setReadAhead(AssetReadAhead*loader){readAhead=loader;menuScene.setReadAhead(loader);viewScene.setReadAhead(loader);}
 void setNativeVideoSurfaces(bool enabled){viewScene.setNativeVideoSurfaces(enabled);}
 void setViewCapture(std::function<qlie::Bytes()> capture){captureView=std::move(capture);}
 enum Action {None=0,Back=1,Replay=2,Music=3};
 CgScreen(qlie::Assets&a,const Progress&p,KFont*f,bool extendedRoots=false)
  :progress(p),catalog(a,extendedRoots),menuScene(a),viewScene(a),font(f),viewFade(a){}
 void prepare();
 void warmOpening();
 bool open();void close();bool visible()const{return opened;}bool inView()const{return viewing;}
 void update(double);void draw(qlie::Bytes&,const Affine2D* view=nullptr)const;void move(int,int);Action confirm();Action tap(float,float);Action back();void toggleZoom();void pan(float,float);void panDelta(float,float);
 void draw(RenderTarget&,const Affine2D* view=nullptr)const;
 std::set<std::string> takeShownFiles(){return viewScene.takeShownFiles();}
 bool viewLoaded()const{return viewing&&viewReady&&pendingView.empty();}
 unsigned pageIndex()const{return page;}unsigned variantIndex()const{return variant;}unsigned zoomLevel()const{return zoom;}int selection()const{return selected;}
};
}
