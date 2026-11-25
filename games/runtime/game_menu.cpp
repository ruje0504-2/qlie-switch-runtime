// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_menu.hpp"
#include "asset_read_ahead.hpp"
#include "profile.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6/ui.hpp"
#endif
namespace whj2 {
struct PreparedMenuAssets {
 std::map<std::string,Image> images;
 std::array<AnimationLayer,2> historyTracks;
};
namespace {
constexpr auto PanelAssetsKey="@menu/panel-assets";
constexpr auto& sliders=gameSettingsLayout.sliders;
constexpr auto& testButtonY=gameSettingsLayout.testButtonY;
constexpr const char* sliderKeys[]={"WindowAlpha","TextSpeed","MasterVolume","BGMVolume",
 "VoiceVolume","SEVolume","SysSEVolume","MovieVolume","AutoTextWait"};
struct SettingToggle{const char*key;const char*yes;const char*no;float y;bool leftValue;};
constexpr SettingToggle toggles[]={
#ifdef QLIE_PROFILE_WHJ6
 {"ClickVoiceStop","on","off",whj6::ui::to720(500),false},
 {"AutoPlayVoiceWait","on","off",whj6::ui::to720(whj6::profile::settingsVoiceWaitY),true},
 {"SkipMode","既読のみ","全て",whj6::ui::to720(700+whj6::profile::settingsTextShift),true}
#else
 {"ClickVoiceStop","する","しない",353,!gameSettingsLayout.classic},
 {"AutoPlayVoiceWait","待つ","待たない",412,true},
 {"SkipMode","既読","未読",442,true}
#endif
};
#ifdef QLIE_PROFILE_WHJ6
constexpr float testButtonX=whj6::ui::to720(1727);
constexpr int toggleX(int i,int column){return (i==0?1335:585)+240*column;}
#else
constexpr float testButtonX=1070;
#endif
constexpr int SettingsFocusCount=22;
struct CharacterVoiceButton{float x,y;const char* label;};
constexpr CharacterVoiceButton voiceButtons[]={
 {790,540,"Voice_蓮華.png"},{930,540,"Voice_香恋.png"},{1080,540,"Voice_もよか.png"},
 {790,570,"Voice_はる.png"},{930,570,"Voice_公暁.png"},{1080,570,"Voice_舞斗.png"},
 {790,600,nullptr}
};
constexpr int HistoryVisibleRows=4, HistoryBrowserLimit=100;
constexpr int HistoryBarX=1200,HistoryBarY=95;
constexpr int HistoryTrackWidth=34,HistoryTrackHeight=528;
constexpr int HistoryThumbWidth=24,HistoryThumbHeight=47;
constexpr int HistoryThumbCenter=HistoryThumbHeight/2;
constexpr int HistoryThumbTravel=HistoryTrackHeight-2*HistoryThumbCenter;
bool inHistoryScrollbar(float x,float y){return x>=1180&&x<=1234&&y>=79&&y<=639;}
int historyOffsetAt(float y,int maximum){
 const float position=std::clamp((y-HistoryBarY-HistoryThumbCenter)/HistoryThumbTravel,0.f,1.f);
 return maximum-int(std::round(position*maximum));
}
#ifndef QLIE_PROFILE_WHJ6
const float deleteButtonX[]={615,615,615,615,1248,1248,1249,1249};
#endif
int sliderPosition(const whj2::Settings&s,int i){int value=s.get(sliderKeys[i]);return i==0?(value*20+127)/255:(i==1||i==8)?20-value/5:value/5;}
void setPosition(whj2::Settings&s,int i,int step){step=std::clamp(step,0,20);s.set(sliderKeys[i],i==0?step*255/20:(i==1||i==8)?100-step*5:step*5);if(i==8)s.set("AutoClickWait",5000-step*250);}
}
void GameMenu::loadHistoryTracks(){
 if(!historyTracks[0].keys.empty())return;
 if(!assets.inPack("script/log/animation.b"))return;
 for(const auto&member:abmp(assets.read("script/log/animation.b"))){
  if(member.type!="abdata12"&&member.type!="abdata13")continue;
  auto definition=animation(member.type,member.data);
  std::array<AnimationLayer,2> tracks{};
  for(unsigned i=0;i<2;i++){
   const std::string motionName=i==0?"*Enable":"*Disable";
   auto motion=std::find_if(definition.motions.begin(),definition.motions.end(),[&](const auto&entry){return entry.name==motionName;});
   if(motion==definition.motions.end())throw std::runtime_error("Missing history zoom motion "+motionName);
   auto layer=std::find_if(motion->layers.begin(),motion->layers.end(),[](const auto&entry){
    return entry.flags&&!entry.keys.empty()&&entry.keys.front().at(4)==16;
   });
   if(layer==motion->layers.end())throw std::runtime_error("Missing history zoom registration "+motionName);
   tracks[i]=std::move(*layer);
  }
  historyTracks=std::move(tracks);
  return;
 }
 throw std::runtime_error("Missing history zoom metadata");
}
void GameMenu::prepareHistory(bool jumps){
#ifdef QLIE_PROFILE_WHJ6
 resource("script/log/bg_mask.png");
 resource("script/log/frame.png");
 for(const char* path:{"line.b","button_voice.b","scroll_bar_log.b","scroll_button.b"})
  for(unsigned state=0;state<2;++state)resource("script/log/"+std::string(path)+"#"+std::to_string(state));
 if(jumps)for(unsigned state=0;state<2;++state)resource("script/log/button_jump.b#"+std::to_string(state));
 resource("script/log/戻る_off.png");
 return;
#endif
 loadHistoryTracks();
 for(const char* path:{"script/log/アッパーフレーム.png","script/log/アンダーフレーム.png",
     "script/log/ゲームに戻る_off.png","script/log/button_voice.b#0","script/log/button_voice.b#1",
     "script/log/scroll_bar_log.b#0",game_profile::story.legacyAvg?"script/log/barframe.png":"script/log/scroll_bar_log.b#1",
     "script/log/scroll_button.b#0","script/log/scroll_button.b#1","script/log/line.b#0"})
  resource(path);
 if(jumps){
  resource("script/log/button_jump.b#0");resource("script/log/button_jump.b#1");
 }
}
void GameMenu::preparePanels(){
 // Assemble the opening queues so their original images are decoded before
 // the first input. No canvas is needed for this warm-up.
 for(auto mode:{Save,Load,Settings}){
  open(mode);
  render(nullptr,nullptr,{},{});
 }
 close();
}
void GameMenu::warmPanels(AssetReadAhead&worker,bool jumps){
 preparingPanels=&worker;
 worker.request(PanelAssetsKey,[&source=assets,snapshot=settings,jumps]() mutable {
  GameMenu warmed(source,snapshot);
  warmed.prepareHistory(jumps);
  warmed.preparePanels();
  WarmAsset result;
  result.menuAssets=std::make_shared<PreparedMenuAssets>(
   PreparedMenuAssets{std::move(warmed.cache),std::move(warmed.historyTracks)});
  return result;
 },true);
}
void GameMenu::takeWarmPanels(){
 if(!preparingPanels||!preparingPanels->ready(PanelAssetsKey))return;
 auto*worker=preparingPanels;
 preparingPanels=nullptr;
 auto prepared=worker->takeReady(PanelAssetsKey);
 if(!prepared)return;
 cache.merge(prepared->menuAssets->images);
 if(historyTracks[0].keys.empty())historyTracks=std::move(prepared->menuAssets->historyTracks);
 worker->retire(std::move(prepared->menuAssets));
}
AnimationSample GameMenu::panelZoom(bool closing,double elapsed)const{
 const auto& track=historyTracks[closing];
 AnimationSample zoom{};
 zoom.kind=16;
 if(track.keys.empty()){
  // Match the WHJ2 history registration track when the game has no such track.
  // Its 250 ms keys move between 100% and 110% with cubic-style easing power 1.5.
  const auto progress=std::clamp(elapsed/PanelZoomMs,0.,1.);
  zoom.scaleX=zoom.scaleY=float(100+10*std::pow(closing?progress:1-progress,1+1.4*1.5));
 }else{
  const auto sample=sampleAnimation(track,elapsed);
  zoom.scaleX=sample.scaleX;
  zoom.scaleY=sample.scaleY;
 }
 return zoom;
}
void GameMenu::samplePanelView(){panelView=panelZoom(panelClosing,panelElapsed);}
void GameMenu::panelPoint(float&x,float&y)const{
 x=640+(x-640)/(panelView.scaleX/100);
 y=360+(y-360)/(panelView.scaleY/100);
}
void GameMenu::applyPanelView(RenderQueue&q)const{
 if(panelView.scaleX!=100||panelView.scaleY!=100)q.control(panelView,-1);
}
void GameMenu::open(Panel p){
 takeWarmPanels();
 loadHistoryTracks();
 close();panel=p;focus=p==Settings?3:0;
 historyOffset=0;historyVoiceRow=-1;historyVoices.fill(false);historyJumps.fill(false);historyTextOffset.fill(0);historyTextMax.fill(0);
 if(visible())samplePanelView();
}
void GameMenu::close(){
 panel=None;asking=false;endDrag();panelClosing=false;panelElapsed=0;panelView={};
}
void GameMenu::beginClose(){
 if(panel==None||panelClosing)return;
 endDrag();panelClosing=true;panelElapsed=0;samplePanelView();
}
void GameMenu::update(double ms){
 if(panel==None||panelElapsed>=PanelZoomMs)return;
 panelElapsed=std::min(panelElapsed+ms,PanelZoomMs);
 if(panelClosing&&panelElapsed>=PanelZoomMs)close();
 else samplePanelView();
}
const Image& GameMenu::resource(const std::string&path){auto it=cache.find(path);if(it==cache.end()){auto hash=path.find('#');Image decoded;
 if(hash==path.npos)decoded=image(assets.read(path));else {unsigned wanted=unsigned(std::stoul(path.substr(hash+1))),index=0;bool found=false;for(auto&member:abmp(assets.read(path.substr(0,hash))))if(member.type.rfind("abimg",0)==0){if(index++==wanted){decoded=image(member.data);found=true;break;}}if(!found)throw std::runtime_error("UI image member missing "+path);}
#ifdef QLIE_PROFILE_WHJ6
 if(path=="script/config/bg1.png")whj6::ui::layoutSettingsBackground(decoded);
#endif
 if(gameSettingsLayout.classic&&path=="script/config/設定_フレーム_0.png"&&decoded.width==1008&&decoded.height==544){
  // These printed labels describe Windows controls. Keep the original gradient.
  const unsigned left=18,width=200;
  auto row=[&](unsigned y){return decoded.pixels.begin()+(size_t(y)*decoded.width+left)*4;};
  for(const auto& range:{std::pair{46u,98u},std::pair{344u,367u},std::pair{432u,486u}})
   for(unsigned y=range.first;y<range.second;++y)std::copy_n(row(150),width*4,row(y));
 }
 if(path=="script/config/設定フレーム.png"&&decoded.width==1001&&decoded.height==540){
  // Restore the gradient behind the baked quality and font labels.
  auto row=[&](unsigned y){return decoded.pixels.begin()+(size_t(y)*decoded.width+26)*4;};
  for(unsigned y=77;y<92;++y)std::copy_n(row(72),82*4,row(y));
  for(unsigned y=372;y<387;++y)std::copy_n(row(364),97*4,row(y));
 }
 decoded.version.publish();it=cache.emplace(path,std::move(decoded)).first;}return it->second;}

void GameMenu::put(RenderQueue&q,const std::string&path,float x,float y,bool center,unsigned opacity){auto&im=resource(path);q.image(im,imagePlacement(x,y,1,1,0,center?.5f:0,center?.5f:0,im.width,im.height),0,opacity,0,0xffffff);}
void GameMenu::setHistorySize(std::size_t entries){
 const int available=int(std::min(entries,std::size_t(HistoryBrowserLimit)));
 historyRows=std::min(available,HistoryVisibleRows);
 historyMaxOffset=std::max(0,available-HistoryVisibleRows);
 historyOffset=std::clamp(historyOffset,0,historyMaxOffset);
}
void GameMenu::move(int dx,int dy){if(panelClosing||panelTransitioning())return;if(asking){if(dx||dy)focus=1-focus;return;}
 if(panel==Save||panel==Load){
#ifdef QLIE_PROFILE_WHJ6
  if(panel==Load&&choiceBackupAvailable){
   if(focus==ChoiceBackupFocus){if(dx||dy)focus=dy<0?3:0;return;}
   if((dy<0&&focus==0)||(dy>0&&focus==3)){focus=ChoiceBackupFocus;return;}
  }
#endif
  if(dx){int row=focus%4,column=(focus%8)/4*2+(focus>=8);for(int n=0;n<4;n++){column=(column+(dx<0?-1:1)+4)%4;int slot=column/2*4+row;focus=slot+(column%2)*8;if(focus<8||populatedSlots[slot])break;}}
  if(dy){int deletion=focus>=8?8:0;for(int n=0;n<8;n++){focus=(focus%8+dy+8)%8+deletion;if(!deletion||populatedSlots[focus%8])break;}}
 }
 else if(panel==History){
  if(dx&&focus<historyRows)historyTextOffset[focus]=std::clamp(historyTextOffset[focus]+dx*4,0,historyTextMax[focus]);
  if(dy<0){if(focus+1<historyRows)++focus;else historyOffset=std::min(historyOffset+1,historyMaxOffset);}
  if(dy>0){if(focus>0)--focus;else historyOffset=std::max(0,historyOffset-1);}
  historyVoiceRow=focus;
 }
 else if(panel==Settings){const int count=SettingsFocusCount+int(settings.voiceActorCount());if(dy){focus=(focus+dy+count)%count;if(focus==16&&!allowSceneReturn)focus=(focus+dy+count)%count;}else if(dx&&focus>=3&&focus<=11){int index=focus-3;setPosition(settings,index,sliderPosition(settings,index)+dx);settingsDirty=true;}else if(dx&&(focus>=13&&focus<=15)){const char*key=toggles[focus-13].key;settings.set(key,1-settings.get(key));settingsDirty=true;}else if(dx){focus=(focus+dx+count)%count;if(focus==16&&!allowSceneReturn)focus=(focus+dx+count)%count;}}
}
GameMenu::Action GameMenu::confirm(){
 if(panelClosing||panelTransitioning())return Nothing;
 if(asking)return focus==0?Yes:No;
#ifdef QLIE_PROFILE_WHJ6
 if(panel==Load&&choiceBackupAvailable&&focus==ChoiceBackupFocus)return LoadChoiceBackup;
#endif
 if(panel==Save||panel==Load)return focus>=8?DeleteSlot:Slot;
 if(panel==History){
  historyVoiceRow=focus;
  if(focus<historyRows&&historyVoices[focus])return ReplayVoice;
  return jumpHistory();
 }
 if(panel!=Settings)return Close;
 if(focus==0)return Close;
 if(focus==1)return Title;
 if(focus==2)return Quit;
 if(focus==12)return ResetSettings;
 if(focus==16&&allowSceneReturn)return SceneReturn;
 if(focus>=17&&focus<SettingsFocusCount){testVolumeCategory=unsigned(focus-17);return TestVolume;}
 if(focus>=SettingsFocusCount&&unsigned(focus-SettingsFocusCount)<settings.voiceActorCount()){
  settings.toggleCharacterVoice(unsigned(focus-SettingsFocusCount));settingsDirty=true;return SettingsChanged;
 }
 if(focus>=13&&focus<=15){const char*key=toggles[focus-13].key;settings.set(key,1-settings.get(key));settingsDirty=true;return SettingsChanged;}
 return Nothing;
}
GameMenu::Action GameMenu::jumpHistory()const{
 return panel==History&&!asking&&!panelClosing&&focus<historyRows&&historyJumps[focus]?JumpHistory:Nothing;
}
int GameMenu::volumeTestAt(float x,float y){
#ifdef QLIE_PROFILE_WHJ6
 constexpr float halfWidth=12,halfHeight=12;
#else
 const auto&button=resource("script/config/音量テスト_off.png");
 const float halfWidth=button.width*.5f,halfHeight=button.height*.5f;
#endif
 for(int i=0;i<5;i++)if(std::abs(x-testButtonX)<=halfWidth&&std::abs(y-testButtonY[i])<=halfHeight)return i;
 return -1;
}
bool GameMenu::beginDrag(float x,float y){
 if(asking||panelClosing||panelTransitioning())return false;
 const float rawX=x,rawY=y;
 panelPoint(x,y);
 if(panel==History){
  if(historyMaxOffset>0&&inHistoryScrollbar(x,y)){historyDragRow=-2;historyOffset=historyOffsetAt(y,historyMaxOffset);return true;}
#ifdef QLIE_PROFILE_WHJ6
  for(int row=0;row<historyRows;++row)if(whj6::ui::contains(x,y,454,whj6::ui::historyY(row),1170,162)){
   focus=row;historyDragRow=row;historyDragY=y;historyDragOffset=historyTextOffset[row];return true;
  }
#else
  if(x>=230&&x<=1150&&y>=64&&y<656){int visual=std::clamp(int((y-64)/149),0,3),row=3-visual;if(row<historyRows){focus=row;historyDragRow=row;historyDragY=y;historyDragOffset=historyTextOffset[row];return true;}}
#endif
  return false;
 }
 if(panel!=Settings||volumeTestAt(x,y)>=0)return false;
 for(int i=0;i<9;i++){const auto&s=sliders[i];if(std::abs(y-s.y)<=18&&x>=s.x-12&&x<=s.x+s.width+12){draggedSlider=i;focus=i+3;drag(rawX,rawY);return true;}}return false;}
bool GameMenu::drag(float x,float y){
 if(panelClosing||panelTransitioning())return false;
 panelPoint(x,y);
 if(panel==History){if(historyDragRow==-2){historyOffset=historyOffsetAt(y,historyMaxOffset);return true;}if(historyDragRow>=0){historyTextOffset[historyDragRow]=std::clamp(historyDragOffset+int(std::round((historyDragY-y)/26)),0,historyTextMax[historyDragRow]);return true;}return false;}
 if(draggedSlider<0||panel!=Settings||asking)return false;
 const auto&s=sliders[draggedSlider];const auto* key=sliderKeys[draggedSlider];int before=settings.get(key);setPosition(settings,draggedSlider,int(std::round((x-s.x)*20/s.width)));settingsDirty|=before!=settings.get(key);return true;}
GameMenu::Action GameMenu::tap(float x,float y){
 if(panelClosing||panelTransitioning())return Nothing;
 panelPoint(x,y);
#ifdef QLIE_PROFILE_WHJ6
 using namespace whj6::ui;
 if(asking){
  if(contains(x,y,650,550,300,50)){focus=0;return Yes;}
  if(contains(x,y,970,550,300,50)){focus=1;return No;}
  return Nothing;
 }
 if(panel==Load&&choiceBackupAvailable&&contains(x,y,30,1012,351,46)){focus=ChoiceBackupFocus;return LoadChoiceBackup;}
#else
 if(asking){if(std::abs(y-325)<=20){if(std::abs(x-550)<=60){focus=0;return Yes;}if(std::abs(x-730)<=60){focus=1;return No;}}return Nothing;}
#endif
 if(panel==Settings){
  int test=volumeTestAt(x,y);if(test>=0){focus=17+test;return confirm();}
  for(unsigned i=0;i<settings.voiceActorCount();++i){
   const auto& button=voiceButtons[i];
   if(x>=button.x-16&&x<=button.x+111&&std::abs(y-button.y)<=13){
    focus=SettingsFocusCount+int(i);return confirm();
   }
  }
 }
#ifdef QLIE_PROFILE_WHJ6
 if(panel==Save||panel==Load){
  for(int i=0;i<15;++i)if(contains(x,y,1139+50*i,119,42,52)){
   requestedPage=i;return PageChange;
  }
  for(int i=0;i<8;++i){
   if(populatedSlots[i]&&contains(x,y,slotX(i)+816,slotY(i)-12,66,42)){focus=8+i;return DeleteSlot;}
   if(contains(x,y,slotX(i),slotY(i),910,160)){focus=i;return Slot;}
  }
 }
#else
 if(panel==Save||panel==Load){for(int i=0;i<8;i++)if(populatedSlots[i]&&std::abs(x-deleteButtonX[i])<=15&&std::abs(y-(148+(i%4)*135))<=15){focus=8+i;return DeleteSlot;}if(y>=72&&y<=108&&x>=674){requestedPage=std::clamp(int((x-674)/40),0,14);return PageChange;}if(y>=128&&y<=651){int col=x>=645?1:0;int row=int((y-128)/135);if(row<4){focus=col*4+row;return Slot;}}}
#endif
 if(panel==History){
  if(inHistoryScrollbar(x,y)){if(historyMaxOffset>0)historyOffset=historyOffsetAt(y,historyMaxOffset);return Nothing;}
#ifdef QLIE_PROFILE_WHJ6
  for(int row=0;row<historyRows;++row){
   const int top=historyY(row);
   if(historyJumps[row]&&contains(x,y,379,top+90,42,42)){focus=row;return JumpHistory;}
   if(historyVoices[row]&&contains(x,y,379,top+30,42,42)){focus=historyVoiceRow=row;return ReplayVoice;}
  }
#else
  const auto&voice=resource("script/log/button_voice.b#0");
  for(int row=0;row<historyRows;++row){
   const float distance=std::abs(y-(585-row*150));
   if(historyJumps[row]){
    const auto&jump=resource("script/log/button_jump.b#0");
    if(std::abs(x-51)<=jump.width*.5f&&distance<=jump.height*.5f){focus=row;return JumpHistory;}
   }
   if(historyVoices[row]&&std::abs(x-210)<=voice.width*.5f&&distance<=voice.height*.5f){focus=historyVoiceRow=row;return ReplayVoice;}
  }
#endif
 }
 if(panel==Settings){
  for(int i=0;i<9;i++){
   const auto& slider=sliders[i];
   if(std::abs(y-slider.y)<=18&&x>=slider.x-12&&x<=slider.x+slider.width+12){
    focus=i+3;setPosition(settings,i,int(std::round((x-slider.x)*20/slider.width)));
    settingsDirty=true;return SettingsChanged;
   }
  }
  for(int i=0;i<3;i++){
   const auto& option=toggles[i];
#ifdef QLIE_PROFILE_WHJ6
   for(int column=0;column<2;++column)if(std::abs(y-option.y)<=11&&std::abs(x-to720(float(toggleX(i,column))))<=70){
    focus=13+i;settings.set(option.key,(column==0)==option.leftValue);
    settingsDirty=true;return SettingsChanged;
   }
#else
   if(std::abs(y-option.y)<=15&&(std::abs(x-410)<=56||std::abs(x-534)<=56)){
    focus=13+i;settings.set(option.key,(x<472)==option.leftValue);
    settingsDirty=true;return SettingsChanged;
   }
#endif
  }
 }
#ifdef QLIE_PROFILE_WHJ6
 if(y>=678&&y<=702){
  if(std::abs(x-1110)<=70)return Close;
  if(panel==Settings||panel==Save||panel==Load){
   if(std::abs(x-to720(975))<=70)return Quit;
   if(std::abs(x-to720(1205))<=70)return Title;
   if(panel==Settings&&std::abs(x-to720(1435))<=70)return ResetSettings;
   if((panel==Save||(panel==Load&&allowSavePanel))&&std::abs(x-to720(1435))<=70)return SwitchSavePanel;
   if(panel==Settings&&allowSceneReturn&&x>=490&&x<=630)return SceneReturn;
  }
 }
#else
 if(y>=670){if(x>1145)return Close;if(panel==Settings){if(x>1015)return ResetSettings;if(x>885)return Title;if(x>755)return Quit;if(allowSceneReturn&&x>=490)return SceneReturn;}else if(panel==Save||panel==Load){if(x>1015)return Title;if(x>885)return Quit;}}
#endif
 return Nothing;
}
namespace {
std::string autoButtonAsset(bool enabled){
#ifdef QLIE_PROFILE_WHJ6
 return enabled?"avg/window/fnc_オートプレイ解除_on.png":"avg/window/fnc_オートプレイ開始_off.png";
#else
 return std::string("avg/window/オートプレイ")+(enabled?"_on.png":"_off.png");
#endif
}
std::string skipButtonAsset(bool enabled){
#ifdef QLIE_PROFILE_WHJ6
 return enabled?"avg/window/fnc_スキップ停止_on.png":"avg/window/fnc_スキップ開始_off.png";
#else
 return std::string("avg/window/スキップ")+(enabled?"_on.png":"_off.png");
#endif
}
}
void GameMenu::drawAutoButton(qlie::Bytes&pixels,bool enabled){RenderQueue q;put(q,autoButtonAsset(enabled),1170,700,true);q.draw(pixels,1280,720);}
void GameMenu::drawSkipButton(qlie::Bytes&pixels,bool enabled){RenderQueue q;put(q,skipButtonAsset(enabled),1200,700,true);q.draw(pixels,1280,720);}
void GameMenu::drawAutoButton(RenderTarget&target,bool enabled){RenderQueue q;put(q,autoButtonAsset(enabled),1170,700,true);q.draw(target,1280,720);}
void GameMenu::drawSkipButton(RenderTarget&target,bool enabled){RenderQueue q;put(q,skipButtonAsset(enabled),1200,700,true);q.draw(target,1280,720);}
void GameMenu::draw(qlie::Bytes&pixels,const PanelContent&content,const PanelContent&prompt){render(&pixels,nullptr,content,prompt);}
void GameMenu::draw(RenderTarget&target,const PanelContent&content,const PanelContent&prompt){render(nullptr,&target,content,prompt);}
#ifdef QLIE_PROFILE_WHJ6
void GameMenu::drawWhj6Panel(RenderQueue& q){
 using namespace whj6::ui;
 auto art=[&](const std::string& path,float x,float y,bool centered=false,unsigned opacity=255){
  const auto& image=resource(path);
  bitmap(q,image,to720(x),to720(y),to720(float(image.width)),to720(float(image.height)),centered,opacity);
 };
 auto button=[&](const std::string& path,int x,int y,bool highlighted=false){
  art(path+(highlighted?"_on.png":"_off.png"),float(x),float(y),true);
 };
 if(panel==Save||panel==Load){
  const std::string dir="script/saveload/";
  art(dir+(panel==Save?"bg_save.png":"bg_load.png"),0,0);
  for(int i=0;i<15;++i){
   char number[3];std::snprintf(number,sizeof(number),"%02d",i+1);
   art(dir+"list_"+number+(i==page?"_hold.png":"_off.png"),1160+i*50,145,true);
  }
  for(int i=0;i<8;++i){
   art(dir+"button_data.b#"+(focus==i?"1":"0"),slotX(i),slotY(i));
   art(dir+"file_sel_thumbnail.png",slotX(i)+20,slotY(i)+10);
   const auto number=std::to_string(page*8+i+1);
   for(unsigned digit=0;digit<number.size();++digit)
    art(dir+"num_"+number[digit]+".png",slotX(i)+360+int(digit)*18,slotY(i)+24);
   if(populatedSlots[i])button(dir+"file_del",slotX(i)+849,slotY(i)+9,focus==8+i);
  }
  button(dir+"ゲームの終了",975,1035);
  button(dir+"タイトルへ戻る",1205,1035);
  art(dir+(panel==Save?"ロード_off.png":"セーブ_off.png"),1435,1035,true,
      panel==Save||allowSavePanel?255:127);
  button(dir+"戻る",1665,1035);
  if(panel==Load&&choiceBackupAvailable)
   rectangle(q,20,675,234,30,focus==ChoiceBackupFocus?0x59697f:0x374861);
  return;
 }
 if(panel==History){
  const std::string dir="script/log/";
  art(dir+"bg_mask.png",0,0);
  art(dir+"frame.png",0,0);
  for(int row=0;row<historyRows;++row){
   const bool selected=(asking?promptFocus:focus)==row;
   const auto state=selected?"1":"0";
   const auto y=historyY(row);
   art(dir+"line.b#"+state,349,y);
   if(historyVoices[row])art(dir+"button_voice.b#"+state,400,y+51,true);
   if(historyJumps[row])art(dir+"button_jump.b#"+state,400,y+111,true);
  }
  art(dir+"scroll_bar_log.b#0",1773,115);
  const float ratio=historyMaxOffset?float(historyMaxOffset-historyOffset)/historyMaxOffset:0;
  art(dir+"scroll_button.b#"+(historyDragRow==-2?"1":"0"),1799,156+ratio*780,true);
  button(dir+"戻る",1665,1035);
  return;
 }
 if(panel==Settings){
  const std::string dir="script/config/";
  art(dir+"bg1.png",0,0);
  constexpr const char* prompts[]={"クイックロード","データロード","データ上書き","データ入替え",
   "データ削除","ログジャンプ","設定初期化","タイトルへ戻る","ゲーム終了"};
  for(int i=0;i<9;++i)
   art(dir+"表示_"+prompts[i]+std::string(i==0||i==3?"_off.png":"_hold.png"),585+(i%2)*240,380+whj6::profile::settingsPromptShift+(i/2)*40,true,180);
  art(dir+"変更する_off.png",585,740+whj6::profile::settingsTextShift,true,180);
  art(dir+"priview_windowalpha.b#1",482,812+whj6::profile::settingsTextShift,false,unsigned(settings.get("WindowAlpha")));
  if(!windowPreview.pixels.empty())bitmap(q,windowPreview,to720(496),to720(826+whj6::profile::settingsTextShift),270,38);
  for(int i=0;i<9;++i){
   const auto&slider=sliders[i];
   const auto state=focus==3+i?"1":"0";
   const auto& track=resource(dir+"hscroll_bar.b#"+state);
   const auto& thumb=resource(dir+"scroll_button.b#"+state);
   bitmap(q,track,slider.x,slider.y+slider.trackY,slider.width,to720(float(track.height)));
   bitmap(q,thumb,slider.x+sliderPosition(settings,i)*slider.width/20,slider.y,16,16,true);
  }
  for(int i=0;i<3;++i){
   const auto& option=toggles[i];
   const bool left=bool(settings.get(option.key))==option.leftValue;
   for(int column=0;column<2;++column){
    const bool checked=column==!left;
    const char*label=((column==0)==option.leftValue)?option.yes:option.no;
    const bool highlighted=focus==13+i;
    const auto* suffix=checked?(highlighted?"_hold_on.png":"_hold.png"):(highlighted?"_on.png":"_off.png");
    art(dir+label+suffix,toggleX(i,column),option.y*1.5f,true);
   }
  }
  for(int i=0;i<5;++i)
   art(dir+"button_stest.b#"+(focus==17+i?"1":"0"),1727,testButtonY[i]*1.5f,true);
  for(int i=0;i<6;++i)art(dir+"button_mute.b#4",1697,225+i*40,true);
  art(dir+"hscroll_bar.b#2",1230,440,false,180);
  art(dir+"scroll_button.b#2",1410,450,true,180);
  constexpr const char* pictures[]={"hide_01_雪女","hide_03_兄弟子","hide_04_師匠","hide_05_ふぶき",
   "hide_06_荒鷹","hide_07_冬将軍","08_その他男性","09_その他女性"};
  for(int i=0;i<8;++i){
   const int x=1015+(i%4)*185,y=560+(i/4)*180;
   art(dir+"キャラアイコン_"+pictures[i]+".png",x,y);
   art(dir+"scroll_bar.b#2",x+145,y);
   art(dir+"scroll_button.b#2",x+157,y+12,true);
   art(dir+"button_mute.b#4",x+87,y+145,true);
   art(dir+"button_stest.b#4",x+117,y+145,true);
  }
  const char* names[]={"ゲームの終了","タイトルへ戻る","設定の初期化","戻る"};
  const int controls[]={2,1,12,0};
  for(int i=0;i<4;++i)button(dir+names[i],975+i*230,1035,focus==controls[i]);
 }
}
void GameMenu::drawWhj6Prompt(RenderQueue&q){
 using namespace whj6::ui;
 const std::string dir="script/dialog/";
 auto art=[&](const std::string& path,int x,int y){
  const auto& image=resource(dir+path);
  bitmap(q,image,to720(x),to720(y),to720(float(image.width)),to720(float(image.height)),true);
 };
 art("frame_yesno.b#1",960,540);
 art(focus==0?"button_yes.b#1":"button_yes.b#0",800,575);
 art(focus==1?"button_no.b#1":"button_no.b#0",1120,575);
 const char* message=nullptr;
 switch(requested){
 case Quit:message="ゲームを終了";break;
 case Title:message="タイトルへ戻る";break;
 case ResetSettings:message="設定初期化";break;
 case JumpHistory:message="ログジャンプ";break;
 case DeleteSlot:message="データ削除";break;
 case Slot:message=panel==Load?"データロード":"データ上書き";break;
 default:break;
 }
 if(message)art(std::string("mes_")+message+".png",960,495);
}
#endif
void GameMenu::drawHistory(RenderQueue&q){
 static const auto tint=[] {Image pixel{1,1,{32,19,0,255}};pixel.version.publish();return pixel;}();
 // script/log/black.b uses a tinted rectangle at alpha 191.
 q.image(tint,imagePlacement(0,0,1280,720,0,0,0,1,1),0,191,0,0xffffff);
 put(q,"script/log/アッパーフレーム.png",0,0);put(q,"script/log/アンダーフレーム.png",0,658);put(q,"script/log/ゲームに戻る_off.png",1208,694,true);for(int row=0;row<historyRows;++row){
  const char*texture=(asking?promptFocus:focus)==row?"1":"0";
  if(historyVoices[row])put(q,std::string("script/log/button_voice.b#")+texture,210,585-row*150,true);
  if(historyJumps[row])put(q,std::string("script/log/button_jump.b#")+texture,51,585-row*150,true);
 }
 // BarFrame sits behind the original scroll track artwork at priority -1.
 if(game_profile::story.legacyAvg)put(q,"script/log/barframe.png",1193,86);
 else put(q,"script/log/scroll_bar_log.b#1",HistoryBarX,HistoryBarY-10,false,191);
 put(q,"script/log/scroll_bar_log.b#0",HistoryBarX-8,HistoryBarY-16);
 // The native control centers the logical thumb, then adds the artwork offsets.
 const int thumbX=HistoryBarX+(HistoryTrackWidth-HistoryThumbWidth)/2-4;
 const int thumbY=HistoryBarY-9+(historyMaxOffset?int(std::round(double(HistoryThumbTravel)*(historyMaxOffset-historyOffset)/historyMaxOffset)):0);
 put(q,std::string("script/log/scroll_button.b#")+(historyDragRow==-2?"1":"0"),thumbX,thumbY);
 for(float y:{63.f,211.f,361.f,512.f,657.f})put(q,"script/log/line.b#0",640,y,true);
}
void GameMenu::render(qlie::Bytes*pixels,RenderTarget*target,const PanelContent&content,const PanelContent&prompt){RenderQueue q;
 applyPanelView(q);
 auto draw=[&](const RenderQueue&queue){if(target)queue.draw(*target,1280,720);else if(pixels)queue.draw(*pixels,1280,720);};
 auto dim=[&](unsigned numerator,unsigned denominator){if(target)target->dim(numerator,denominator);else for(size_t i=0;i<pixels->size();i+=4)for(unsigned c=0;c<3;c++)(*pixels)[i+c]=uint8_t(unsigned((*pixels)[i+c])*numerator/denominator);};
#ifdef QLIE_PROFILE_WHJ6
 drawWhj6Panel(q);
#else
 if(panel==Save||panel==Load){std::string dir="script/saveload/";put(q,dir+"背景.png",0,0);put(q,dir+(panel==Save?"アッパーフレーム_セーブ.png":"アッパーフレーム_ロード.png"),0,0);put(q,dir+"アンダーフレーム.png",1,656);
 for(int i=0;i<15;i++){char n[8];std::snprintf(n,sizeof(n),"%02d",i+1);put(q,dir+"ページ_"+n+(i==page?"_on.png":"_off.png"),694+i*40,90,true);}
 for(int i=0;i<8;i++){float x=i<4?323:956,y=182+(i%4)*135;put(q,dir+(focus==i?"セーブファイル_on.png":"セーブファイル_off.png"),x,y,true);put(q,dir+"サムネイル_no_data.png",i<4?27:660,137+(i%4)*135);if(populatedSlots[i])put(q,dir+(focus==8+i?"削除ボタン_on.png":"削除ボタン_off.png"),deleteButtonX[i],148+(i%4)*135,true);}
 put(q,dir+"戻る_off.png",1211,695,true);put(q,dir+"タイトルへ戻る_off.png",1080,695,true);put(q,dir+"ゲームの終了_off.png",950,695,true);
 }else if(panel==History){drawHistory(q);
 }else if(panel==Settings){put(q,"script/config/背景.png",0,0);if(gameSettingsLayout.classic)put(q,"script/config/設定_フレーム_0.png",135,86);else put(q,"script/config/設定フレーム.png",139,89);put(q,"script/config/アッパーフレーム.png",0,0);put(q,"script/config/アンダーフレーム.png",0,656);
 const bool classic=gameSettingsLayout.classic;
 if(classic){
  put(q,"script/config/priview_windowalpha.b#1",349,225);
  put(q,"script/config/priview_windowalpha.b#2",349,225,false,unsigned(settings.get("WindowAlpha")));
  put(q,"script/config/priview_windowalpha.b#4",349,225);
 }else{
  put(q,"script/config/priview_windowalpha.b#0",356,225);
  put(q,"script/config/priview_windowalpha.b#1",349,225,false,unsigned(settings.get("WindowAlpha")));
  put(q,"script/config/priview_windowalpha.b#3",349,225);
 }
 if(!windowPreview.pixels.empty()){
  const Affine2D at{1,0,0,1,classic?370.f:363.f,classic?229.f:230.f};
  q.image(windowPreview,at,0,255,0,0xffffff);
 }
 for(int i=0;i<5;i++)put(q,std::string("script/config/音量テスト")+(focus==17+i?"_on.png":"_off.png"),1070,testButtonY[i],true);
 for(int i=0;i<9;i++){
  const auto& slider=sliders[i];
  const auto part=std::to_string(focus==i+3?1:0);
  const char* track=i<2||i==8?"scroll_bar_s.b#":"scroll_bar.b#";
  put(q,std::string("script/config/")+track+part,slider.x+slider.trackX,slider.y+slider.trackY);
  put(q,"script/config/scroll_button.b#"+part,slider.x+sliderPosition(settings,i)*slider.width/20,
      slider.y+slider.thumbY,true);
 }
 for(unsigned i=0;i<settings.voiceActorCount();++i){
  const auto& button=voiceButtons[i];
  const unsigned part=(settings.characterVoiceMuted(i)?0:2)+(focus==SettingsFocusCount+int(i));
  put(q,"script/config/button_check.b#"+std::to_string(part),button.x,button.y,true);
  const std::string label=button.label?std::string("script/config/")+button.label:"script/config/button_check.b#4";
  put(q,label,button.x+49,button.y,true);
 }
 put(q,std::string("script/config/確認_設定初期化")+(focus==12?"_on.png":"_off.png"),1079,694,true);
 for(const auto& option:toggles){
  const bool left=bool(settings.get(option.key))==option.leftValue;
  const auto* leftLabel=option.leftValue?option.yes:option.no;
  const auto* rightLabel=option.leftValue?option.no:option.yes;
  put(q,std::string("script/config/")+leftLabel+(left?"_on.png":"_off.png"),410,option.y,true);
  put(q,std::string("script/config/")+rightLabel+(!left?"_on.png":"_off.png"),534,option.y,true);
 }
 const char*names[]={"戻る","タイトルへ戻る","ゲームの終了"};const int xs[]={1210,950,820};for(int i=0;i<3;i++)put(q,std::string("script/config/")+names[i]+(focus==i?"_on.png":"_off.png"),xs[i],695,true);
 }
#endif
 if((!asking||prompt)&&content)content(q);
 draw(q);
 if(asking){
  if(target||pixels)dim(3,8);
  RenderQueue dialog;applyPanelView(dialog);
#ifdef QLIE_PROFILE_WHJ6
  drawWhj6Prompt(dialog);
#else
  put(dialog,"script/config/確認_フレーム.png",640,300,true);
  put(dialog,std::string("script/config/確認_はい")+(focus==0?"_on.png":"_off.png"),550,325,true);
  put(dialog,std::string("script/config/確認_いいえ")+(focus==1?"_on.png":"_off.png"),730,325,true);
#endif
  if(prompt)prompt(dialog);
  else if(content)content(dialog);
  draw(dialog);
 }
}
}
