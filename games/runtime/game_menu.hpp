// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include "settings.hpp"
#include <map>
#include <array>
#include <functional>
namespace whj2 {
class GameMenu {
 qlie::Assets& assets;whj2::Settings& settings;std::map<std::string,Image> cache;
 const Image& resource(const std::string&);
 int volumeTestAt(float,float);
 int draggedSlider=-1;
 int historyDragRow=-1,historyDragOffset=0;float historyDragY=0;
 std::array<AnimationLayer,2> historyTracks{};
 AnimationSample panelView;
 double panelElapsed=0;
 bool panelClosing=false;
 void loadHistoryTracks();
 void samplePanelView();
 void panelPoint(float&,float&)const;
 void applyPanelView(RenderQueue&)const;
 int promptFocus=0;
 Image windowPreview;
 void put(RenderQueue&,const std::string&,float,float,bool center=false,unsigned opacity=255);
 using PanelContent=std::function<void(RenderQueue&)>;
 void render(qlie::Bytes*,RenderTarget*,const PanelContent&);
public:
 enum Panel {None,Save,Load,History,Settings};
 enum Action {Nothing,Close,Slot,Quit,Title,Yes,No,SettingsChanged,ResetSettings,SceneReturn,ReplayVoice,TestVolume,DeleteSlot,PageChange};
 Panel panel=None;int focus=0,page=0,requestedPage=0,historyOffset=0;bool asking=false;
 Action requested=Nothing;
 bool settingsDirty=false,allowSceneReturn=false;
 std::array<bool,4> historyVoices{};
 std::array<int,4> historyTextOffset{},historyTextMax{};
 std::array<bool,8> populatedSlots{};
 int historyVoiceRow=-1,historyMaxOffset=0,historyRows=0;
 void setHistorySize(std::size_t entries);
 unsigned testVolumeCategory=0;
 explicit GameMenu(qlie::Assets&a,whj2::Settings&s):assets(a),settings(s){}
 void setWindowPreview(Image preview){preview.version.publish();windowPreview=std::move(preview);}
 static constexpr double PanelZoomMs=250;
 AnimationSample panelZoom(bool closing,double elapsed)const;
 void prepareHistory();
 void preparePanels();
 void open(Panel);
 void close();
 void beginClose();
 void update(double milliseconds);
 bool closing()const{return panelClosing;}
 bool visible()const{return panel!=None||asking;}
 bool panelTransitioning()const{return panel!=None&&panel!=History&&panelElapsed<PanelZoomMs;}
 bool coversScene()const{return panel!=None&&panel!=History&&!panelTransitioning();}
 void ask(Action a){requested=a;promptFocus=focus;asking=true;focus=0;draggedSlider=-1;}
 void dismissPrompt(){asking=false;focus=promptFocus;}
 int slot()const{return page*8+focus%8+1;}
 void move(int dx,int dy);
 Action confirm();
 Action tap(float,float);
 bool beginDrag(float,float);bool drag(float,float);void endDrag(){draggedSlider=-1;historyDragRow=-1;}
 void drawHistory(RenderQueue&);
 void draw(qlie::Bytes&,const PanelContent& content={});void drawAutoButton(qlie::Bytes&,bool);void drawSkipButton(qlie::Bytes&,bool);
 void draw(RenderTarget&,const PanelContent& content={});void drawAutoButton(RenderTarget&,bool);void drawSkipButton(RenderTarget&,bool);
};
}
