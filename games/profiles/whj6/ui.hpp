// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profiles/whj6.hpp"
#include "render_queue.hpp"
#include "scene_images.hpp"
#include <algorithm>
#include <initializer_list>

namespace whj6::ui {
inline void layoutSettingsBackground(whj2::Image& image){
 if(image.width!=1920||image.height!=1080)return;
 // The labels are painted into BG1. Borrow the repeating pattern from a
 // nearby column and match the local shading along the four edges.
 struct Label {int left,top,right,bottom,patternOffset;};
 constexpr Label labels[]={
  {473,346,599,369,170},{895,346,936,369,-170},
  {199,205,305,237,340},{199,245,256,277,340},
  {199,284,407,317,340},{199,324,402,356,340}
 };
 auto pixel=[&](int x,int y,int channel)->uint8_t&{
  return image.pixels[(size_t(y)*image.width+x)*4+channel];
 };
 for(const auto& label:labels)for(int channel=0;channel<3;++channel){
  auto difference=[&](int x,int y){
   return int(pixel(x,y,channel))-pixel(x+label.patternOffset,y,channel);
  };
  auto vertical=[&](int x,float t){
   return difference(x,label.top-1)*(1-t)+difference(x,label.bottom)*t;
  };
  for(int y=label.top;y<label.bottom;++y){
   const float t=float(y-label.top+1)/(label.bottom-label.top+1);
   const float left=difference(label.left-1,y)-vertical(label.left-1,t);
   const float right=difference(label.right,y)-vertical(label.right,t);
   for(int x=label.left;x<label.right;++x){
    const float u=float(x-label.left+1)/(label.right-label.left+1);
    const float shade=vertical(x,t)+left*(1-u)+right*u;
    const float value=pixel(x+label.patternOffset,y,channel)+shade;
    pixel(x,y,channel)=uint8_t(std::clamp(value+.5f,0.f,255.f));
   }
  }
 }
 // Keep the typography and preview at 1:1 scale. Only stretch the empty
 // strips between the two sections and blend into the original side borders.
 struct Row {int from,to;};
 constexpr int left=151,right=949,top=156,bottom=924;
 constexpr Row rows[]={
  {top,top},{161,210},{195,244},
  {365,365+profile::settingsPromptShift},{532,532+profile::settingsPromptShift},
  {565,565+profile::settingsTextShift},{914,914+profile::settingsTextShift},{bottom,bottom}
 };
 constexpr int width=right-left;
 qlie::Bytes source(size_t(width)*(bottom-top)*4);
 for(int y=top;y<bottom;++y)
  std::copy_n(&pixel(left,y,0),width*4,source.data()+size_t(y-top)*width*4);
 auto original=[&](int x,int y,int channel){
  return source[(size_t(y-top)*width+x-left)*4+channel];
 };
 for(size_t i=1;i<std::size(rows);++i){
  const auto [fromTop,toTop]=rows[i-1];
  const auto [fromBottom,toBottom]=rows[i];
  for(int y=toTop;y<toBottom;++y){
   const float from=fromTop+float(y-toTop)*(fromBottom-fromTop)/(toBottom-toTop);
   const int srcY=int(from);
   const float fraction=from-srcY;
   for(int x=left;x<right;++x){
    const float weight=std::min({1.f,float(x-left)/27,float(right-1-x)/15});
    for(int channel=0;channel<4;++channel){
     const float value=original(x,srcY,channel)*(1-fraction)+
                       original(x,std::min(srcY+1,bottom-1),channel)*fraction;
     pixel(x,y,channel)=uint8_t(value*weight+original(x,y,channel)*(1-weight)+.5f);
    }
   }
  }
 }
}

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
