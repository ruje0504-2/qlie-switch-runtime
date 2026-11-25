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
 unsigned shownButtons=0;
 double backdropExitRemaining=-1;
 int pressed=-1;
 TitleLayout layout;
 std::mt19937 random;
 void setBackdrop(bool alternate);
 void setFrame(bool alternate,bool visible);
 const char*entryMotion(int);
 std::string buttonPosition(unsigned,bool)const;
 void enable();void showNextButton();void highlight();
 unsigned openingBrightness()const;
public:
 enum Action {None,Start,Load,Extra,Settings,Quit,CG,Memory,Sound,Back};
 explicit TitleScreen(qlie::Assets&,TitleLayout={},std::array<unsigned,2> sceneSize={1280,720});
 bool rollAlternate();
 bool prepareOpening(AssetReadAhead*loader,bool unlocked=false);
#ifdef QLIE_PROFILE_WHJ6
 bool openingAssetsReady(AssetReadAhead&,bool alternate)const;
 const Image* openingBackdrop()const{return scene.uiMemberBitmap("ui_back","bg");}
#endif
 void setNativeVideoSurfaces(bool enabled){scene.setNativeVideoSurfaces(layout.asyncBackdropVideo&&enabled);}
 void beginExit();
 void leave(){scene.clear(true);}
 void warmBackdrop(bool alternate);
 void returnToExtras(bool alternate);
 void reset(bool alternate=false,bool unlocked=false);
 void view(float x);
 void setExtraPage(bool);bool isExtraPage()const{return extraPage;}
 void update(double);void skipIntro();bool ready()const{return elapsed>=layout.introMs+layout.buttonEntranceMs;}
 int selection()const{return selected;}int move(int);bool select(int);int hit(float,float)const;
 void draw(RenderTarget&,bool live=false)const;
 Action confirm();void draw(qlie::Bytes&,bool live=false)const;
 void press(float,float);
 void release();
};
}
