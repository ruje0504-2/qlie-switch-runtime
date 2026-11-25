// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include "settings.hpp"
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6.hpp"
#endif
#include <map>
#include <array>
#include <functional>
namespace whj2 {
class AssetReadAhead;
class GameMenu {
 qlie::Assets& assets;whj2::Settings& settings;std::map<std::string,Image> cache;
 const Image& resource(const std::string&);
 int volumeTestAt(float,float);
 int draggedSlider=-1;
 int historyDragRow=-1,historyDragOffset=0;float historyDragY=0;
 std::array<AnimationLayer,2> historyTracks{};
 AssetReadAhead* preparingPanels=nullptr;
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
 void render(qlie::Bytes*,RenderTarget*,const PanelContent&,const PanelContent&);
#ifdef QLIE_PROFILE_WHJ6
 void drawWhj6Panel(RenderQueue&);
 void drawWhj6Prompt(RenderQueue&);
#endif
public:
 enum Panel {None,Save,Load,History,Settings};
 enum Action {Nothing,Close,Slot,Quit,Title,Yes,No,SettingsChanged,ResetSettings,SceneReturn,ReplayVoice,TestVolume,DeleteSlot,PageChange,JumpHistory,SectionSkip,LoadChoiceBackup,SwitchSavePanel,TestCharacterVoice};
 Panel panel=None;int focus=0,page=0,requestedPage=0,historyOffset=0;bool asking=false;
 Action requested=Nothing;
 bool settingsDirty=false,allowSceneReturn=false;
 std::array<bool,4> historyVoices{},historyJumps{};
 std::array<int,4> historyTextOffset{},historyTextMax{};
#ifndef QLIE_PROFILE_WHJ6
 std::array<bool,8> populatedSlots{};
#endif
 int historyVoiceRow=-1,historyMaxOffset=0,historyRows=0;
#ifdef QLIE_PROFILE_WHJ6
 static constexpr int ChoiceBackupFocus=16;
 bool choiceBackupAvailable=false,allowSavePanel=false;
 unsigned characterPortraits=0,characterVoices=0,testVoiceActor=0;
 bool automaticSavePage()const{return panel==Load&&page==whj6::profile::automaticSavePage;}
 int savePageCount()const{return panel==Load?whj6::profile::automaticSavePage+1:whj6::profile::automaticSavePage;}
 Action testFocusedCharacter();
#else
 int savePageCount()const{return 15;}
#endif
 void setHistorySize(std::size_t entries);
 unsigned testVolumeCategory=0;
 explicit GameMenu(qlie::Assets&a,whj2::Settings&s):assets(a),settings(s){}
 void setWindowPreview(Image preview){preview.version.publish();windowPreview=std::move(preview);}
 static constexpr double PanelZoomMs=250;
 AnimationSample panelZoom(bool closing,double elapsed)const;
 void prepareHistory(bool jumps=false);
 void preparePanels();
 void warmPanels(AssetReadAhead&,bool jumps);
 void takeWarmPanels();
 void open(Panel);
 void close();
 void beginClose();
 void update(double milliseconds);
 bool closing()const{return panelClosing;}
 bool visible()const{return panel!=None||asking;}
 bool panelTransitioning()const{return panel!=None&&panel!=History&&panelElapsed<PanelZoomMs;}
 bool coversScene()const{return panel!=None&&panel!=History&&!panelTransitioning();}
 void ask(Action a){requested=a;promptFocus=focus;asking=true;focus=0;endDrag();}
 void dismissPrompt(){asking=false;focus=promptFocus;}
#ifndef QLIE_PROFILE_WHJ6
 int slot()const{return page*8+focus%8+1;}
#endif
 void move(int dx,int dy);
 Action confirm();
 Action jumpHistory()const;
 Action tap(float,float);
 bool beginDrag(float,float);bool drag(float,float);void endDrag(){draggedSlider=-1;historyDragRow=-1;}
 void drawHistory(RenderQueue&);
 void draw(qlie::Bytes&,const PanelContent& content={},const PanelContent& prompt={});void drawAutoButton(qlie::Bytes&,bool);void drawSkipButton(qlie::Bytes&,bool);
 void draw(RenderTarget&,const PanelContent& content={},const PanelContent& prompt={});void drawAutoButton(RenderTarget&,bool);void drawSkipButton(RenderTarget&,bool);
};
}
