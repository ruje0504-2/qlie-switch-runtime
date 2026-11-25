// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include "scene_images.hpp"
#include <initializer_list>

namespace whj6::ui {
inline void rectangle(whj2::RenderQueue& queue,float x,float y,float width,float height,
                      uint32_t color,unsigned alpha=255){
 static const auto pixel=[] {
  whj2::Image image{1,1,{255,255,255,255}};
  image.version.publish();
  return image;
 }();
 queue.image(pixel,whj2::imagePlacement(x,y,width,height,0,0,0,1,1),0,alpha,0,color);
}
inline void bitmap(whj2::RenderQueue& queue,const whj2::Image& image,float x,float y,
                   float width,float height,bool center=false,unsigned opacity=255){
 const auto anchor=center?.5f:0.f;
 queue.image(image,whj2::imagePlacement(x,y,width/image.width,height/image.height,0,
             anchor,anchor,image.width,image.height),0,opacity,0,0xffffff);
}
inline constexpr float to720(float value){return value*2.f/3;}
inline constexpr int slotX(int index){return index<4?40:970;}
inline constexpr int slotY(int index){return 195+(index%4)*200;}
inline constexpr int historyY(int row){return 819-row*180;}
inline void picture(whj2::SceneImages& scene,const std::string& id,const std::string& asset,
                    int x,int y,int priority){
 scene.command("^"+id+",file:"+asset+",x:"+std::to_string(to720(float(x)))+
               ",y:"+std::to_string(to720(float(y)))+",pri:"+std::to_string(priority)+
               ",scalex:66.6667,scaley:66.6667,align:left:top,texfilter:linear");
}
inline void button(whj2::SceneImages& scene,const std::string& id,const std::string& asset,
                   int x,int y,int priority){
 picture(scene,id,asset,x,y,priority);
 scene.setUiMotion(id,"*Enable");
}
inline void slots(whj2::SceneImages& scene,const std::string& id,const std::string& prefix,
                  std::initializer_list<const char*> states){
 unsigned index=0;
 for(const auto* state:states)scene.uiSlot(id,index++,prefix+state+".png");
}
inline void navButton(whj2::SceneImages& scene,const std::string& id,unsigned index){
 constexpr int positions[]={955,1185,1415,1665};
 constexpr const char* names[]={"cg","memory","sound","戻る"};
 button(scene,id,"script/modecommon/button_top.b",positions[index],1035,120);
 const auto prefix=std::string("script/modecommon/")+names[index];
 slots(scene,id,prefix,{"_off","_on"});
 if(index<3)scene.uiSlot(id,2,prefix+"_lock.png");
}
inline void focus(whj2::SceneImages& scene,const std::string& id,bool selected,
                  bool inactive=false,bool checked=false){
 scene.setUiMotion(id,inactive?"*OnLock":checked?"*OnCheck":selected?"*OnMouse":"*OffMouse");
}
inline bool contains(float x,float y,float left,float top,float width,float height){
 return x>=to720(left)&&x<to720(left+width)&&y>=to720(top)&&y<to720(top+height);
}
}
