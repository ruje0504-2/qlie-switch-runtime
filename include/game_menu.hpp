// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include "settings.hpp"
#include <map>
namespace whj2 {
class GameMenu {
 qlie::Assets& assets;whj2::Settings& settings;std::map<std::string,Image> cache;
 const Image& resource(const std::string&);
 int draggedSlider=-1;
 void put(RenderQueue&,const std::string&,float,float,bool center=false);
public:
 enum Panel {None,Save,Load,History,Settings};
 enum Action {Nothing,Close,Slot,Quit,Title,Yes,No,SettingsChanged,ResetSettings,SceneReturn};
 Panel panel=None;int focus=0,page=0,historyOffset=0;bool asking=false;
 Action requested=Nothing;
 bool settingsDirty=false,allowSceneReturn=false;
 explicit GameMenu(qlie::Assets&a,whj2::Settings&s):assets(a),settings(s){}
 void open(Panel p){panel=p;focus=p==Settings?3:0;asking=false;historyOffset=0;}
 void close(){panel=None;asking=false;draggedSlider=-1;}
 bool visible()const{return panel!=None||asking;}
 void ask(Action a){requested=a;asking=true;focus=0;draggedSlider=-1;}
 int slot()const{return page*8+focus+1;}
 void move(int dx,int dy);
 Action confirm();
 Action tap(float,float);
 bool beginDrag(float,float);bool drag(float,float);void endDrag(){draggedSlider=-1;}
 void draw(qlie::Bytes&);void drawAutoButton(qlie::Bytes&,bool);void drawSkipButton(qlie::Bytes&,bool);
};
}
