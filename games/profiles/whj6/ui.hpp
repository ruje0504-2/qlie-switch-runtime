// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include "scene_images.hpp"

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
                   float width,float height,bool center=false){
 const auto anchor=center?.5f:0.f;
 queue.image(image,whj2::imagePlacement(x,y,width/image.width,height/image.height,0,
             anchor,anchor,image.width,image.height),0,255,0,0xffffff);
}
inline void sprite(whj2::SceneImages& scene,const std::string& id,const std::string& asset,
                   int x,int y,int priority,float scale=66.6667f,bool centered=true){
 const auto size=std::to_string(scale);
 scene.command("^"+id+",file:"+asset+",x:"+std::to_string(x)+",y:"+std::to_string(y)+
               ",pri:"+std::to_string(priority)+",scalex:"+size+",scaley:"+size+
               (centered?",align:center:center":",align:left:top")+",texfilter:linear");
}
inline void focus(whj2::SceneImages& scene,const std::string& id,bool selected,bool inactive=false){
 scene.command("^"+id+(selected?",addcolor:$303030":",addcolor:$000000")+
               (inactive?",mulcolor:$909090":",mulcolor:$FFFFFF"));
}
inline constexpr const char* extraNav[]={"cg_off.png","memory_off.png","sound_off.png","戻る_off.png"};
inline constexpr float navX(unsigned i){return 820.f+i*130;}
}
