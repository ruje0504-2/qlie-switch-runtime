// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include "settings.hpp"
#include <map>
#include <array>
namespace whj2 {
class GameMenu {
 qlie::Assets& assets;whj2::Settings& settings;std::map<std::string,Image> cache;
 const Image& resource(const std::string&);
 int volumeTestAt(float,float);
 int qualityAt(float,float);
 int draggedSlider=-1;
 int promptFocus=0;
 std::array<Image,2> textPreviews;
 void put(RenderQueue&,const std::string&,float,float,bool center=false,unsigned opacity=255);
 void render(qlie::Bytes*,RenderTarget*);
public:
 enum Panel {None,Save,Load,History,Settings,Fonts};
 enum Action {Nothing,Close,Slot,Quit,Title,Yes,No,SettingsChanged,ResetSettings,SceneReturn,ReplayVoice,TestVolume,DeleteSlot,ChooseFont,ApplyFont};
 Panel panel=None;int focus=0,page=0,historyOffset=0;bool asking=false;
 Action requested=Nothing;
 bool settingsDirty=false,allowSceneReturn=false;
 std::array<bool,4> historyVoices{};
 std::array<bool,8> populatedSlots{};
 int historyVoiceRow=-1,historyMaxOffset=0,historyRows=0;
 unsigned testVolumeCategory=0;
 int fontCount=1;
 explicit GameMenu(qlie::Assets&a,whj2::Settings&s):assets(a),settings(s){}
 void setTextPreviews(Image font,Image window){font.version.publish();window.version.publish();textPreviews[0]=std::move(font);textPreviews[1]=std::move(window);}
 void open(Panel p){panel=p;focus=p==Settings?3:0;asking=false;historyOffset=0;historyVoiceRow=-1;historyVoices.fill(false);}
 void close(){panel=None;asking=false;draggedSlider=-1;}
 bool visible()const{return panel!=None||asking;}
 void ask(Action a){requested=a;promptFocus=focus;asking=true;focus=0;draggedSlider=-1;}
 void dismissPrompt(){asking=false;focus=promptFocus;}
 int slot()const{return page*8+focus%8+1;}
 void move(int dx,int dy);
 Action confirm();
 Action tap(float,float);
 bool beginDrag(float,float);bool drag(float,float);void endDrag(){draggedSlider=-1;}
 void draw(qlie::Bytes&);void drawAutoButton(qlie::Bytes&,bool);void drawSkipButton(qlie::Bytes&,bool);
 void draw(RenderTarget&);void drawAutoButton(RenderTarget&,bool);void drawSkipButton(RenderTarget&,bool);
};
}
