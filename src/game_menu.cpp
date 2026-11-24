// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_menu.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
namespace whj2 {
namespace {
struct SettingSlider{const char*key;float x,y,width;};
const SettingSlider sliders[]={
 {"WindowAlpha",357,195,241},{"TextSpeed",357,314,241},
 {"MasterVolume",664,163,461},{"BGMVolume",664,222,461},{"VoiceVolume",664,279,461},
 {"SEVolume",664,340,461},{"SysSEVolume",664,399,461},{"MovieVolume",664,458,461},{"AutoTextWait",357,373,241}};
const float testButtonY[]={202,260,321,381,440};
const float deleteButtonX[]={615,615,615,615,1248,1248,1249,1249};
int sliderPosition(const whj2::Settings&s,int i){int value=s.get(sliders[i].key);return i==0?(value*20+127)/255:(i==1||i==8)?20-value/5:value/5;}
void setPosition(whj2::Settings&s,int i,int step){step=std::clamp(step,0,20);s.set(sliders[i].key,i==0?step*255/20:(i==1||i==8)?100-step*5:step*5);if(i==8)s.set("AutoClickWait",5000-step*250);}
}
const Image& GameMenu::resource(const std::string&path){auto it=cache.find(path);if(it==cache.end()){auto hash=path.find('#');Image decoded;
 if(hash==path.npos)decoded=image(assets.read(path));else {unsigned wanted=unsigned(std::stoul(path.substr(hash+1))),index=0;bool found=false;for(auto&member:abmp(assets.read(path.substr(0,hash))))if(member.type.rfind("abimg",0)==0){if(index++==wanted){decoded=image(member.data);found=true;break;}}if(!found)throw std::runtime_error("UI image member missing "+path);}
 decoded.version.publish();it=cache.emplace(path,std::move(decoded)).first;}return it->second;}

void GameMenu::put(RenderQueue&q,const std::string&path,float x,float y,bool center,unsigned opacity){auto&im=resource(path);q.image(im,imagePlacement(x,y,1,1,0,center?.5f:0,center?.5f:0,im.width,im.height),0,opacity,0,0xffffff);}
void GameMenu::move(int dx,int dy){if(asking){if(dx||dy)focus=1-focus;return;}
 if(panel==Save||panel==Load){
  if(dx){int row=focus%4,column=(focus%8)/4*2+(focus>=8);for(int n=0;n<4;n++){column=(column+(dx<0?-1:1)+4)%4;int slot=column/2*4+row;focus=slot+(column%2)*8;if(focus<8||populatedSlots[slot])break;}}
  if(dy){int deletion=focus>=8?8:0;for(int n=0;n<8;n++){focus=(focus%8+dy+8)%8+deletion;if(!deletion||populatedSlots[focus%8])break;}}
 }
 else if(panel==History){
  if(dy<0){if(focus+1<historyRows)++focus;else historyOffset=std::min(historyOffset+1,historyMaxOffset);}
  if(dy>0){if(focus>0)--focus;else historyOffset=std::max(0,historyOffset-1);}
  historyVoiceRow=focus;
 }
 else if(panel==Settings){constexpr int count=24;if(dy){focus=(focus+dy+count)%count;if(focus==16&&!allowSceneReturn)focus=(focus+dy+count)%count;}else if(dx&&focus>=3&&focus<=11){int index=focus-3;setPosition(settings,index,sliderPosition(settings,index)+dx);settingsDirty=true;}else if(dx&&(focus>=13&&focus<=15)){const char*key=focus==13?"ClickVoiceStop":focus==14?"AutoPlayVoiceWait":"SkipMode";settings.set(key,1-settings.get(key));settingsDirty=true;}else if(dx){focus=(focus+dx+count)%count;if(focus==16&&!allowSceneReturn)focus=(focus+dx+count)%count;}}
}
GameMenu::Action GameMenu::confirm(){if(asking)return focus==0?Yes:No;if(panel==Save||panel==Load)return focus>=8?DeleteSlot:Slot;if(panel==History){historyVoiceRow=focus;return ReplayVoice;}if(panel==Settings){if(focus==0)return Close;if(focus==1)return Title;if(focus==2)return Quit;if(focus==12)return ResetSettings;if(focus==16&&allowSceneReturn)return SceneReturn;if(focus==22||focus==23){settings.set("Interlace",focus-22);settingsDirty=true;return SettingsChanged;}if(focus>=17&&focus<22){testVolumeCategory=unsigned(focus-17);return TestVolume;}if(focus>=13&&focus<=15){const char*key=focus==13?"ClickVoiceStop":focus==14?"AutoPlayVoiceWait":"SkipMode";settings.set(key,1-settings.get(key));settingsDirty=true;return SettingsChanged;}return Nothing;}return Close;}
int GameMenu::volumeTestAt(float x,float y){const auto&button=resource("script/config/音量テスト_off.png");for(int i=0;i<5;i++)if(std::abs(x-1070)<=button.width*.5f&&std::abs(y-testButtonY[i])<=button.height*.5f)return i;return -1;}
int GameMenu::qualityAt(float x,float y){const auto&button=resource("script/config/高画質_off.png");for(int i=0;i<2;i++)if(std::abs(x-(410+i*124))<=button.width*.5f&&std::abs(y-176)<=button.height*.5f)return i;return -1;}
bool GameMenu::beginDrag(float x,float y){if(panel!=Settings||asking||volumeTestAt(x,y)>=0||qualityAt(x,y)>=0)return false;for(int i=0;i<9;i++){const auto&s=sliders[i];if(std::abs(y-s.y)<=18&&x>=s.x-12&&x<=s.x+s.width+12){draggedSlider=i;focus=i+3;drag(x,y);return true;}}return false;}
bool GameMenu::drag(float x,float){if(draggedSlider<0||panel!=Settings||asking)return false;const auto&s=sliders[draggedSlider];int before=settings.get(s.key);setPosition(settings,draggedSlider,int(std::round((x-s.x)*20/s.width)));settingsDirty|=before!=settings.get(s.key);return true;}
GameMenu::Action GameMenu::tap(float x,float y){if(asking){if(std::abs(y-325)<=20){if(std::abs(x-550)<=60){focus=0;return Yes;}if(std::abs(x-730)<=60){focus=1;return No;}}return Nothing;}
 if(panel==Settings){int quality=qualityAt(x,y);if(quality>=0){focus=22+quality;return confirm();}int test=volumeTestAt(x,y);if(test>=0){focus=17+test;return confirm();}}
 if(panel==Save||panel==Load){for(int i=0;i<8;i++)if(populatedSlots[i]&&std::abs(x-deleteButtonX[i])<=15&&std::abs(y-(148+(i%4)*135))<=15){focus=8+i;return DeleteSlot;}if(y>=72&&y<=108&&x>=674){page=std::clamp(int((x-674)/40),0,14);focus=0;return Nothing;}if(y>=128&&y<=651){int col=x>=645?1:0;int row=int((y-128)/135);if(row<4){focus=col*4+row;return Slot;}}}
 if(panel==History){const auto&button=resource("script/log/button_voice.b#0");for(int row=0;row<4;row++){if(historyVoices[row]&&std::abs(x-210)<=button.width*.5f&&std::abs(y-(585-row*150))<=button.height*.5f){focus=historyVoiceRow=row;return ReplayVoice;}}}
 if(panel==Settings){for(int i=0;i<9;i++){const auto&slider=sliders[i];if(std::abs(y-slider.y)<=18&&x>=slider.x-12&&x<=slider.x+slider.width+12){focus=i+3;setPosition(settings,i,int(std::round((x-slider.x)*20/slider.width)));settingsDirty=true;return SettingsChanged;}}for(int i=0;i<3;i++){if(std::abs(y-(i==0?353:i==1?412:442))<=15&&(std::abs(x-410)<=56||std::abs(x-534)<=56)){focus=13+i;settings.set(i==0?"ClickVoiceStop":i==1?"AutoPlayVoiceWait":"SkipMode",x<472?1:0);settingsDirty=true;return SettingsChanged;}}}
 if(y>=670){if(x>1145)return Close;if(panel==Settings){if(x>1015)return ResetSettings;if(x>885)return Title;if(x>755)return Quit;if(allowSceneReturn&&x>=490)return SceneReturn;}else if(panel==Save||panel==Load){if(x>1015)return Title;if(x>885)return Quit;}}
 return Nothing;
}
void GameMenu::drawAutoButton(qlie::Bytes&pixels,bool enabled){RenderQueue q;put(q,std::string("avg/window/オートプレイ")+(enabled?"_on.png":"_off.png"),1170,700,true);q.draw(pixels,1280,720);}
void GameMenu::drawSkipButton(qlie::Bytes&pixels,bool enabled){RenderQueue q;put(q,std::string("avg/window/スキップ")+(enabled?"_on.png":"_off.png"),1200,700,true);q.draw(pixels,1280,720);}
void GameMenu::drawAutoButton(RenderTarget&target,bool enabled){RenderQueue q;put(q,std::string("avg/window/オートプレイ")+(enabled?"_on.png":"_off.png"),1170,700,true);q.draw(target,1280,720);}
void GameMenu::drawSkipButton(RenderTarget&target,bool enabled){RenderQueue q;put(q,std::string("avg/window/スキップ")+(enabled?"_on.png":"_off.png"),1200,700,true);q.draw(target,1280,720);}
void GameMenu::draw(qlie::Bytes&pixels){render(&pixels,nullptr);}
void GameMenu::draw(RenderTarget&target){render(nullptr,&target);}
void GameMenu::render(qlie::Bytes*pixels,RenderTarget*target){RenderQueue q;
 auto draw=[&](const RenderQueue&queue){if(target)queue.draw(*target,1280,720);else queue.draw(*pixels,1280,720);};
 auto dim=[&](unsigned numerator,unsigned denominator){if(target)target->dim(numerator,denominator);else for(size_t i=0;i<pixels->size();i+=4)for(unsigned c=0;c<3;c++)(*pixels)[i+c]=uint8_t(unsigned((*pixels)[i+c])*numerator/denominator);};
 if(panel==Save||panel==Load){std::string dir="script/saveload/";put(q,dir+"背景.png",0,0);put(q,dir+(panel==Save?"アッパーフレーム_セーブ.png":"アッパーフレーム_ロード.png"),0,0);put(q,dir+"アンダーフレーム.png",1,656);
 for(int i=0;i<15;i++){char n[8];std::snprintf(n,sizeof(n),"%02d",i+1);put(q,dir+"ページ_"+n+(i==page?"_on.png":"_off.png"),694+i*40,90,true);}
 for(int i=0;i<8;i++){float x=i<4?323:956,y=182+(i%4)*135;put(q,dir+(focus==i?"セーブファイル_on.png":"セーブファイル_off.png"),x,y,true);put(q,dir+"サムネイル_no_data.png",i<4?27:660,137+(i%4)*135);if(populatedSlots[i])put(q,dir+(focus==8+i?"削除ボタン_on.png":"削除ボタン_off.png"),deleteButtonX[i],148+(i%4)*135,true);}
 put(q,dir+"戻る_off.png",1211,695,true);put(q,dir+"タイトルへ戻る_off.png",1080,695,true);put(q,dir+"ゲームの終了_off.png",950,695,true);
 }else if(panel==History){dim(3,10);put(q,"script/log/アッパーフレーム.png",0,0);put(q,"script/log/アンダーフレーム.png",0,658);put(q,"script/log/ゲームに戻る_off.png",1208,694,true);for(int row=0;row<4;row++)if(historyVoices[row])put(q,std::string("script/log/button_voice.b#")+(focus==row?"1":"0"),210,585-row*150,true);
 }else if(panel==Settings){put(q,"script/config/背景.png",0,0);put(q,"script/config/設定フレーム.png",139,89);put(q,"script/config/アッパーフレーム.png",0,0);put(q,"script/config/アンダーフレーム.png",0,656);
 put(q,"script/config/preview_font.b#0",356,490);
 put(q,"script/config/preview_font.b#1",349,490,false,unsigned(settings.get("WindowAlpha")));
 put(q,"script/config/preview_font.b#2",349,490);
 put(q,"script/config/priview_windowalpha.b#0",356,225);
 put(q,"script/config/priview_windowalpha.b#1",349,225,false,unsigned(settings.get("WindowAlpha")));
 put(q,"script/config/priview_windowalpha.b#3",349,225);
 for(unsigned i=0;i<textPreviews.size();++i){auto&text=textPreviews[i];if(!text.pixels.empty())q.image(text,imagePlacement(i?363:361,i?230:495,1,1,0,0,0,text.width,text.height),0,255,0,0xffffff);}
 for(int i=0;i<2;i++)put(q,std::string("script/config/")+(i?"低画質":"高画質")+((settings.get("Interlace")==i||focus==22+i)?"_on.png":"_off.png"),410+i*124,176,true);
 for(int i=0;i<5;i++)put(q,std::string("script/config/音量テスト")+(focus==17+i?"_on.png":"_off.png"),1070,testButtonY[i],true);
 for(int i=0;i<9;i++){const auto&slider=sliders[i];bool active=focus==i+3;auto part=std::to_string(active?1:0);put(q,std::string("script/config/")+((i<2||i==8)?"scroll_bar_s.b#":"scroll_bar.b#")+part,slider.x-((i<2||i==8)?8:10),slider.y-((i<2||i==8)?10:11));put(q,"script/config/scroll_button.b#"+part,slider.x+sliderPosition(settings,i)*slider.width/20,slider.y,true);}
 put(q,std::string("script/config/確認_設定初期化")+(focus==12?"_on.png":"_off.png"),1079,694,true);
 for(int i=0;i<3;i++){const char*yes=i==0?"する":i==1?"待つ":"既読";const char*no=i==0?"しない":i==1?"待たない":"未読";bool value=settings.get(i==0?"ClickVoiceStop":i==1?"AutoPlayVoiceWait":"SkipMode");put(q,std::string("script/config/")+yes+(value?"_on.png":"_off.png"),410,i==0?353:i==1?412:442,true);put(q,std::string("script/config/")+no+(!value?"_on.png":"_off.png"),534,i==0?353:i==1?412:442,true);}
 const char*names[]={"戻る","タイトルへ戻る","ゲームの終了"};const int xs[]={1210,950,820};for(int i=0;i<3;i++)put(q,std::string("script/config/")+names[i]+(focus==i?"_on.png":"_off.png"),xs[i],695,true);
 }
 draw(q);
 if(asking){dim(3,8);RenderQueue dialog;put(dialog,"script/config/確認_フレーム.png",640,300,true);put(dialog,std::string("script/config/確認_はい")+(focus==0?"_on.png":"_off.png"),550,325,true);put(dialog,std::string("script/config/確認_いいえ")+(focus==1?"_on.png":"_off.png"),730,325,true);draw(dialog);}
}
}
