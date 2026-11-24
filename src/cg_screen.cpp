// SPDX-License-Identifier: GPL-2.0-or-later
#include "cg_screen.hpp"
#include <algorithm>
namespace whj2 {
void CgScreen::open(){opened=true;viewing=false;page=0;selected=0;menuScene.clear();viewScene.clear();
 auto add=[&](const std::string&id,const std::string&file,int x,int y,int pri){menuScene.command("^"+id+",file:script/albummode/"+file+",x:"+std::to_string(x)+",y:"+std::to_string(y)+",pri:"+std::to_string(pri));};
 add("ui_cg_bg","背景.png",0,0,100);add("ui_cg_upper","アッパーフレーム.png",0,0,100);add("ui_cg_lower","アンダーフレーム.png",0,656,100);
 for(unsigned i=0;i<24;i++)add("ui_cg_item"+std::to_string(i),"Button_Thum.b",85+int(i%6)*190,135+int(i/6)*130,110);
 for(unsigned i=0;i<(catalog.items().size()+23)/24;i++){auto id="ui_cg_page"+std::to_string(i);add(id,"Button_Page.b",1148+int(i)*40,90,110);auto name="script/albummode/ページ_0"+std::to_string(i+1);menuScene.uiSlot(id,0,name+"_off.png");menuScene.uiSlot(id,1,name+"_on.png");}
 const char*names[]={"CG観賞","回想観賞","音楽鑑賞","戻る"};for(int i=0;i<4;i++){auto id="ui_cg_nav"+std::to_string(i);add(id,"Button_l.b",820+i*130,695,110);menuScene.uiSlot(id,0,"script/albummode/"+std::string(names[i])+"_off.png");menuScene.uiSlot(id,1,"script/albummode/"+std::string(names[i])+"_on.png");}setup();}
void CgScreen::setup(){elapsed=0;for(unsigned i=0;i<24;i++){auto index=page*24+i;auto id="ui_cg_item"+std::to_string(i);if(index>=catalog.items().size()){menuScene.command("^"+id+",show:false");continue;}menuScene.command("^"+id+",show:true,motion:*Enable");menuScene.uiSlot(id,0,catalog.unlocked(index,progress)?catalog.items()[index].thumbnail:"script/albummode/サムネイル_no_image.png");}highlight();}
bool CgScreen::exists(int n)const{return n>=0&&(n<24?page*24+unsigned(n)<catalog.items().size():n<30);}
void CgScreen::highlight(){for(unsigned i=0;i<24;i++){auto index=page*24+i;if(index>=catalog.items().size())continue;auto mode=!catalog.unlocked(index,progress)?"*OnLock":selected==int(i)?"*OnMouse":"*OffMouse";menuScene.command("^ui_cg_item"+std::to_string(i)+",motion:"+mode);}
 for(int i=0;i<2;i++)menuScene.command("^ui_cg_page"+std::to_string(i)+",motion:"+(page==unsigned(i)?"*OnCheck":selected==24+i?"*OnMouse":"*OffMouse"));
 for(int i=0;i<4;i++)menuScene.command("^ui_cg_nav"+std::to_string(i)+",motion:"+(i==0?"*OnLock":selected==26+i?"*OnMouse":"*OffMouse"));}
void CgScreen::loadView(){viewScene.clear();auto file=catalog.items()[group].files[variant];viewScene.command("^ui_cg_view,file:"+file+",x:640,y:360,align:center:center,texfilter:2");
 const char*buttons[]={"ViewPrev_n.png","ViewZoom_n.png","ViewQuit_n.png","ViewNext_n.png"};for(int i=0;i<4;i++){auto id="ui_cg_viewbutton"+std::to_string(i);viewScene.command("^"+id+",file:script/albummode/Button_View.b,x:"+std::to_string(1080+i*40)+",y:700,pri:100");viewScene.uiSlot(id,0,"script/albummode/"+std::string(buttons[i]));}
 MessageText count(assets);count.start("[spd,0]"+std::to_string(variant+1)+"/"+std::to_string(catalog.items()[group].files.size()));viewScene.command("^ui_cg_count,file:script/albummode/ViewText.b,x:20,y:690,pri:100");viewScene.uiSlot("ui_cg_count",0,count.render(font,MessageStyle{100,50,16,16,0xffffff}));positionView();}
void CgScreen::positionView(){auto size=viewScene.dimensions("ui_cg_view");float dx=std::max(0.f,size.first*zoom*.5f-640),dy=std::max(0.f,size.second*zoom*.5f-360);float x=640-(pointerX-640)*dx/640,y=360-(pointerY-360)*dy/360;
 viewScene.command("^ui_cg_view,x:"+std::to_string(x)+",y:"+std::to_string(y)+",scalex:"+std::to_string(zoom*100)+",scaley:"+std::to_string(zoom*100));}
void CgScreen::close(){opened=viewing=false;menuScene.clear();viewScene.clear();}
void CgScreen::update(double ms){if(!opened)return;elapsed+=std::max(0.,ms);if(viewing)viewScene.update(ms);else menuScene.update(ms);}
void CgScreen::draw(qlie::Bytes&out)const{if(viewing)viewScene.draw(out);else menuScene.draw(out);}
void CgScreen::pan(float x,float y){pointerX=std::clamp(x,0.f,1280.f);pointerY=std::clamp(y,0.f,720.f);if(viewing)positionView();}
void CgScreen::panDelta(float x,float y){pan(pointerX+x,pointerY+y);}
void CgScreen::toggleZoom(){if(viewing){zoom=zoom==1?2:1;positionView();}}
CgScreen::Action CgScreen::back(){if(viewing){if(zoom==2){zoom=1;positionView();}else{viewing=false;viewScene.clear();}}else{close();return Back;}return None;}
void CgScreen::move(int x,int y){if(!opened||elapsed<250)return;if(viewing){if(x){auto count=catalog.items()[group].files.size();variant=unsigned((int(variant)+x+int(count))%int(count));loadView();}if(y)panDelta(0,y*40);return;}
 if(selected<24){if(y<0)selected=selected<6?24:selected-6;else if(y>0){int n=selected+6;selected=n>=24||!exists(n)?29:n;}else if(x){int n=selected+x;if(n<24&&exists(n))selected=n;}}
 else if(selected<26){if(y>0)selected=0;else if(x)selected=24+(selected-24+x+2)%2;}
 else{if(y<0)selected=page==0?18:6;else if(y>0)selected=24;else if(x){do{selected=26+(selected-26+x+4)%4;}while(selected==26);}}highlight();}
CgScreen::Action CgScreen::confirm(){if(!opened||elapsed<250)return None;if(viewing){move(1,0);return None;}if(selected<24){auto index=page*24+unsigned(selected);if(index<catalog.items().size()&&catalog.unlocked(index,progress)){group=index;variant=0;zoom=1;pointerX=640;pointerY=360;viewing=true;loadView();}return None;}
 if(selected<26){page=unsigned(selected-24);selected=0;setup();return None;}return selected==27?Replay:selected==28?Music:selected==29?Back:None;}
CgScreen::Action CgScreen::tap(float x,float y){if(!opened||elapsed<250)return None;if(viewing){for(int i=0;i<4;i++)if(viewScene.uiHit("ui_cg_viewbutton"+std::to_string(i),x,y)){if(i==0)move(-1,0);else if(i==1)toggleZoom();else if(i==2)return back();else move(1,0);return None;}return confirm();}
 for(int i=0;i<24;i++)if(exists(i)&&menuScene.uiHit("ui_cg_item"+std::to_string(i),x,y)){selected=i;highlight();return confirm();}
 for(int i=0;i<2;i++)if(menuScene.uiHit("ui_cg_page"+std::to_string(i),x,y)){selected=24+i;return confirm();}
 for(int i=1;i<4;i++)if(menuScene.uiHit("ui_cg_nav"+std::to_string(i),x,y)){selected=26+i;return confirm();}
 return None;}
}
