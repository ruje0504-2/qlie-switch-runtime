// SPDX-License-Identifier: GPL-2.0-or-later
#include "cg_screen.hpp"
#include "asset_read_ahead.hpp"
#include "profile.hpp"
#include <algorithm>
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6/ui.hpp"
#endif
namespace whj2 {
namespace {
constexpr auto openingBitmaps="@cg/opening-bitmaps";
#ifdef QLIE_PROFILE_WHJ6
constexpr auto lockedThumbnail="script/albummode/cg_hide.png";
constexpr const char* whj6ViewAssets[]={"前へ","拡大","閉じる","次へ","戻る"};
constexpr int whj6ViewY[]={965,825,755,895,1035};
constexpr int albumX(unsigned index){return 55+int(index%6)*305;}
constexpr int albumY(unsigned index){return 170+int(index/6)*200;}
#else
constexpr auto lockedThumbnail="script/albummode/サムネイル_no_image.png";
#endif
}
void CgScreen::prepareLayout(){
#ifdef QLIE_PROFILE_WHJ6
 using namespace whj6::ui;
 picture(menuScene,"ui_cg_bg","script/albummode/bg.png",0,0,100);
 for(unsigned i=0;i<24;++i)
  button(menuScene,"ui_cg_item"+std::to_string(i),"script/albummode/button_album.b",albumX(i),albumY(i),110);
 for(int i=0;i<pageButtons();++i){
  const auto id="ui_cg_page"+std::to_string(i);
  button(menuScene,id,"script/albummode/button_page.b",1285+i*230,130,110);
  slots(menuScene,id,"script/albummode/cs_0"+std::to_string(i+1),{"_off","_on","_hold_off","_hold_on"});
 }
 for(unsigned i=0;i<4;++i)
  navButton(menuScene,"ui_cg_nav"+std::to_string(i),i);
 for(int i=0;i<5;++i){
  const auto id="ui_cg_viewbutton"+std::to_string(i);
  button(viewScene,id,"script/albummode/button_view.b",1875,whj6ViewY[i],120);
  slots(viewScene,id,std::string("script/albummode/")+whj6ViewAssets[i],{"_off","_on"});
 }
 picture(viewScene,"ui_cg_diff","script/albummode/diff.png",0,0,119);
 viewScene.command("^ui_cg_diff,x:1250,y:456.6667,align:center:center");
 viewScene.command("^ui_cg_count,x:1233,y:447,pri:120");
#else
 auto add=[&](const std::string&id,const std::string&file,int x,int y,int pri){menuScene.command("^"+id+",file:script/albummode/"+file+",x:"+std::to_string(x)+",y:"+std::to_string(y)+",pri:"+std::to_string(pri));};
 add("ui_cg_bg","背景.png",0,0,100);add("ui_cg_upper","アッパーフレーム.png",0,0,100);add("ui_cg_lower","アンダーフレーム.png",0,656,100);
 for(unsigned i=0;i<24;i++)add("ui_cg_item"+std::to_string(i),"Button_Thum.b",85+int(i%6)*190,135+int(i/6)*130,110);
 for(int i=0;i<pageButtons();i++){auto id="ui_cg_page"+std::to_string(i);add(id,"Button_Page.b",1148+int(i)*40,90,110);auto name="script/albummode/ページ_0"+std::to_string(i+1);menuScene.uiSlot(id,0,name+"_off.png");menuScene.uiSlot(id,1,name+"_on.png");}
 const char*names[]={"CG観賞","回想観賞","音楽鑑賞","戻る"};
 for(int i=game_profile::story.legacyAvg?3:0;i<4;i++){
  auto id="ui_cg_nav"+std::to_string(i);
  add(id,"Button_l.b",820+i*130,695,110);
  menuScene.uiSlot(id,0,"script/albummode/"+std::string(names[i])+"_off.png");
  menuScene.uiSlot(id,1,"script/albummode/"+std::string(names[i])+"_on.png");
 }
 menuScene.prepareUiBitmap(lockedThumbnail);
 const char*buttons[]={"ViewPrev_n.png","ViewZoom_n.png","ViewQuit_n.png","ViewNext_n.png"};
 for(int i=0;i<4;i++){
  auto id="ui_cg_viewbutton"+std::to_string(i);
  viewScene.command("^"+id+",file:script/albummode/Button_View.b,x:"+std::to_string(1080+i*40)+",y:700,pri:100");
  viewScene.uiSlot(id,0,"script/albummode/"+std::string(buttons[i]));
 }
 viewScene.command(game_profile::story.legacyAvg?
  "^ui_cg_count,file:script/albummode/ViewText.b,x:60,y:695,pri:100":
  "^ui_cg_count,file:script/albummode/ViewText.b,x:20,y:690,pri:100");
#endif
 prepared=true;
}
void CgScreen::prepare(){
 if(!prepared)prepareLayout();
 for(size_t i=0;i<catalog.items().size();++i)
  if(catalog.unlocked(i,progress))menuScene.prepareUiBitmap(catalog.items()[i].thumbnail);
}
void CgScreen::warmOpening(){
 if(!readAhead||openingWarm||prepared)return;
 std::vector<std::string> paths{
#ifdef QLIE_PROFILE_WHJ6
  "script/albummode/bg.png",
#else
  "script/albummode/背景.png",
  "script/albummode/アッパーフレーム.png",
  "script/albummode/アンダーフレーム.png",
#endif
  lockedThumbnail
 };
 for(size_t i=0;i<std::min<size_t>(24,catalog.items().size());++i)
  if(catalog.unlocked(i,progress))paths.push_back(catalog.items()[i].thumbnail);
 // Keep the small menu batch even when title or story hints change.
 openingWarm=menuScene.warmUiBitmaps(openingBitmaps,std::move(paths),true);
}
bool CgScreen::open(){
 if(openingWarm){
  if(!readAhead->ready(openingBitmaps))return false;
  menuScene.takeUiBitmaps(openingBitmaps);
  openingWarm=false;
 }
 close();
 if(!prepared)prepareLayout();
 opened=true;page=0;selected=pageCount()?0:navigationStart()+3;
 setup();
 return true;
}
void CgScreen::setup(){
 elapsed=0;menuScene.resetUiAnimations();
 for(unsigned i=0;i<24;i++){
  const auto index=page*24+i;const auto id="ui_cg_item"+std::to_string(i);
  if(index>=catalog.items().size()){menuScene.command("^"+id+",show:false");continue;}
  const auto file=catalog.unlocked(index,progress)?catalog.items()[index].thumbnail:lockedThumbnail;
  menuScene.command("^"+id+",show:true,motion:*Enable");menuScene.uiSlot(id,0,file);
 }
 highlight();
}
int CgScreen::pageCount()const{return int((catalog.items().size()+23)/24);}
int CgScreen::pageButtons()const{return game_profile::story.legacyAvg&&pageCount()==1?0:pageCount();}
int CgScreen::navigationStart()const{return 24+pageButtons();}
bool CgScreen::exists(int item)const{return item>=0&&item<24&&page*24+unsigned(item)<catalog.items().size();}
void CgScreen::highlight(){
#ifdef QLIE_PROFILE_WHJ6
 for(int i=0;i<24&&exists(i);++i)
  whj6::ui::focus(menuScene,"ui_cg_item"+std::to_string(i),selected==i,!catalog.unlocked(page*24+unsigned(i),progress));
 for(int i=0;i<pageButtons();++i)
  whj6::ui::focus(menuScene,"ui_cg_page"+std::to_string(i),selected==24+i,false,page==unsigned(i));
 for(int i=0;i<4;++i)whj6::ui::focus(menuScene,"ui_cg_nav"+std::to_string(i),selected==navigationStart()+i,i==0);
#else
 for(unsigned i=0;i<24;i++){auto index=page*24+i;if(index>=catalog.items().size())continue;auto mode=!catalog.unlocked(index,progress)?"*OnLock":selected==int(i)?"*OnMouse":"*OffMouse";menuScene.setUiMotion("ui_cg_item"+std::to_string(i),mode);}
 for(int i=0;i<pageButtons();i++)menuScene.setUiMotion("ui_cg_page"+std::to_string(i),page==unsigned(i)?"*OnCheck":selected==24+i?"*OnMouse":"*OffMouse");
 for(int i=game_profile::story.legacyAvg?3:0;i<4;i++)menuScene.setUiMotion("ui_cg_nav"+std::to_string(i),i==0?"*OnLock":selected==navigationStart()+i?"*OnMouse":"*OffMouse");
#endif
}
void CgScreen::cancelViewRequest(){
 if(readAhead&&!pendingView.empty())readAhead->discard(pendingView);
 pendingView.clear();
}
void CgScreen::requestView(unsigned selection){
 cancelViewRequest();
 requestedVariant=selection;
 const auto&file=catalog.items()[group].files[selection];
 if(readAhead&&viewScene.warmFile(file,true,true)){
  pendingView=qlie::canonical(file);
  return;
 }
 loadView();
}
void CgScreen::loadView(){
 const auto&files=catalog.items()[group].files;
 const auto&file=files[requestedVariant];
 if(viewReady){
  viewFade.commandWithCapture("^sentence,fade:overlap:250",[this]{
   if(captureView)return captureView();
   qlie::Bytes previous(1280*720*4);
   draw(previous);
   return previous;
  });
  viewScene.removeUiLayer("ui_cg_view",file!=files[variant]);
 }else viewScene.resetUiAnimations();
 variant=requestedVariant;
 viewScene.command("^ui_cg_view,file:"+file+",x:640,y:360,align:center:center,texfilter:2");
 auto count=std::to_string(variant+1)+"/"+std::to_string(files.size());
 if(count!=displayedCount){
  countText.start("[spd,0]"+count);
#ifdef QLIE_PROFILE_WHJ6
  viewScene.setUiBitmap("ui_cg_count",countText.render(font,MessageStyle{43,24,14,14,0xffffff}));
#else
  viewScene.uiSlot("ui_cg_count",0,countText.render(font,MessageStyle{100,50,16,16,0xffffff}));
#endif
  displayedCount=std::move(count);
 }
 viewReady=true;positionView();viewFade.present();
}
void CgScreen::positionView(){
 const auto size=viewScene.dimensions("ui_cg_view");
 float scale=float(zoom);
#ifdef QLIE_PROFILE_WHJ6
 if(size.first&&size.second)scale*=std::min(1280.f/size.first,720.f/size.second);
#endif
 const float dx=std::max(0.f,size.first*scale*.5f-640),dy=std::max(0.f,size.second*scale*.5f-360);
 const float x=640-(pointerX-640)*dx/640,y=360-(pointerY-360)*dy/360;
 const auto percent=std::to_string(scale*100);
 viewScene.command("^ui_cg_view,x:"+std::to_string(x)+",y:"+std::to_string(y)+",scalex:"+percent+",scaley:"+percent);
}
void CgScreen::closeView(){cancelViewRequest();viewFade.reset();viewing=viewReady=false;viewScene.removeUiLayer("ui_cg_view",true);
#ifdef QLIE_PROFILE_WHJ6
 setViewControls(true);
 if(prepared)whj6::ui::focus(viewScene,"ui_cg_viewbutton1",false);
#endif
}
#ifdef QLIE_PROFILE_WHJ6
void CgScreen::setViewControls(bool visible){
 viewControls=visible;
 if(!prepared)return;
 const std::string show=visible?",show:true":",show:false";
 for(int i=0;i<5;++i)viewScene.command("^ui_cg_viewbutton"+std::to_string(i)+show);
 viewScene.command("^ui_cg_count"+show);
 viewScene.command("^ui_cg_diff"+show);
}
#endif
void CgScreen::close(){closeView();opened=false;}
void CgScreen::update(double ms){
 if(!opened)return;
 elapsed+=std::max(0.,ms);
 if(!pendingView.empty()&&readAhead->ready(pendingView)){
  loadView();pendingView.clear();return;
 }
 if(viewing&&viewReady){viewScene.update(ms);viewFade.update(ms);}else menuScene.update(ms);
}
void CgScreen::draw(qlie::Bytes&out,const Affine2D*view)const{if(viewing&&viewReady){viewScene.draw(out,readAhead!=nullptr);viewFade.blend(out);}else menuScene.draw(out,false,view);}
void CgScreen::draw(RenderTarget&out,const Affine2D*view)const{if(viewing&&viewReady){viewScene.draw(out,readAhead!=nullptr);viewFade.blend(out);}else menuScene.draw(out,false,view);}
void CgScreen::pan(float x,float y){pointerX=std::clamp(x,0.f,1280.f);pointerY=std::clamp(y,0.f,720.f);if(viewing&&viewReady)positionView();}
void CgScreen::panDelta(float x,float y){pan(pointerX+x,pointerY+y);}
void CgScreen::toggleZoom(){if(viewing&&viewReady){zoom=zoom==1?2:1;positionView();
#ifdef QLIE_PROFILE_WHJ6
 whj6::ui::focus(viewScene,"ui_cg_viewbutton1",false,false,zoom==2);
#endif
}}
CgScreen::Action CgScreen::back(){if(viewing){if(zoom==2)toggleZoom();else closeView();}else return Back;return None;}
void CgScreen::move(int x,int y){if(!opened||elapsed<250)return;if(viewing){if(x){auto count=catalog.items()[group].files.size();requestView(unsigned((int(requestedVariant)+x+int(count))%int(count)));}if(y)panDelta(0,y*40);return;}
 const int navigation=navigationStart();
 if(selected<24){
  if(y<0)selected=selected<6?(pageButtons()?24:selected):selected-6;
  else if(y>0)selected=exists(selected+6)?selected+6:navigation+3;
  else if(x&&exists(selected+x))selected+=x;
 }else if(selected<navigation){
  if(y>0)selected=0;
  else if(x)selected=24+(selected-24+x+pageButtons())%pageButtons();
 }else if(pageCount()&&y<0){
  int item=page==0?18:6;
  while(item>0&&!exists(item))item-=6;
  selected=item;
 }else if(pageButtons()&&y>0)selected=24;
 else if(x&&!game_profile::story.legacyAvg){
  do{selected=navigation+(selected-navigation+x+4)%4;}while(selected==navigation+0);
 }
 highlight();
}
CgScreen::Action CgScreen::confirm(){if(!opened||elapsed<250)return None;if(viewing){move(1,0);return None;}if(selected<24){auto index=page*24+unsigned(selected);if(index<catalog.items().size()&&catalog.unlocked(index,progress)){group=index;variant=0;zoom=1;pointerX=640;pointerY=360;viewing=true;requestView(0);}return None;}
 if(selected<navigationStart()){page=unsigned(selected-24);selected=0;setup();return None;}return selected==navigationStart()+1?Replay:selected==navigationStart()+2?Music:selected==navigationStart()+3?Back:None;}
CgScreen::Action CgScreen::tap(float x,float y){
 if(!opened||elapsed<250)return None;
 if(viewing){
#ifdef QLIE_PROFILE_WHJ6
  if(!viewControls){setViewControls(true);return None;}
  for(int i=0;i<5;++i)if(viewScene.uiHit("ui_cg_viewbutton"+std::to_string(i),x,y)){
   if(i==0)move(-1,0);else if(i==1)toggleZoom();else if(i==2)setViewControls(false);
   else if(i==3)move(1,0);else return back();
   return None;
  }
#else
  for(int i=0;i<4;i++)if(viewScene.uiHit("ui_cg_viewbutton"+std::to_string(i),x,y)){
   if(i==0)move(-1,0);else if(i==1)toggleZoom();else if(i==2)return back();else move(1,0);
   return None;
  }
#endif
  return confirm();
 }
#ifdef QLIE_PROFILE_WHJ6
 using whj6::ui::contains;
 for(int i=0;i<24;++i)if(exists(i)&&contains(x,y,albumX(i),albumY(i),285,190)){
  selected=i;highlight();return confirm();
 }
 for(int i=0;i<pageButtons();++i)if(contains(x,y,1179+i*230,109,212,42)){
  selected=24+i;return confirm();
 }
#else
 for(int i=0;i<24;i++)if(exists(i)&&menuScene.uiHit("ui_cg_item"+std::to_string(i),x,y)){selected=i;highlight();return confirm();}
 for(int i=0;i<pageButtons();i++)if(menuScene.uiHit("ui_cg_page"+std::to_string(i),x,y)){selected=24+i;return confirm();}
#endif
 for(int i=game_profile::story.legacyAvg?3:1;i<4;i++)if(menuScene.uiHit("ui_cg_nav"+std::to_string(i),x,y)){selected=navigationStart()+i;return confirm();}
 return None;}
}
