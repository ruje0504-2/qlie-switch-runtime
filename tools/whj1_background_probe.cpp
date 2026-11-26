// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj1.hpp"
#include "scene_images.hpp"
#include "animation.hpp"
#include "image.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
auto frame(whj2::SceneImages& scene){qlie::Bytes pixels(1280*720*4);scene.draw(pixels);return pixels;}
void background(whj2::SceneImages& scene,const char* name){
 scene.command(std::string("^bg02,file:")+name+",x:640,y:360,pri:100,show:true");
}
}

int main(int argc,char** argv){
 try{
  if(argc!=2)return 2;
  qlie::Assets assets(argv[1],whj1::profile::archives);
  whj2::SceneImages scene(assets),reference(assets);
  background(scene,"n14_1_048");background(reference,"n14_1_048");
  scene.command("^bg03,file:ズームアウト_エクステ,x:640,y:360,pri:600,show:true");
  scene.command("^bg03,move:square,movepow:100,rep0:image/n14_1_103.png");
  for(unsigned i=0;i<6;++i){
   const int values[]={640,360,200,200,0,0};
   scene.command("^bg03,value"+std::to_string(i)+":"+std::to_string(values[i]));
  }
  require(frame(scene)!=frame(reference),"zoom overlay did not start");
  scene.update(500);
  auto saved=scene.save();auto restored=scene.makeRestoreTarget();restored->restore(saved);
  require(frame(scene)==frame(*restored),"mid-zoom save/load changed picture");
  scene.update(500);restored->update(500);
  require(frame(scene)==frame(reference),"zoom overlay remained opaque at its external-alpha endpoint");
  require(frame(*restored)==frame(reference),"restored zoom overlay remained opaque");
  for(const auto* image:{"n14_1_046","n14_1_049","n14_1_063","n14_1_067"}){
   background(scene,image);background(reference,image);scene.update(800);reference.update(800);
   require(frame(scene)==frame(reference),"zoom overlay obscured later scripted CG");
  }
  std::cout<<"PASS original zoom / midpoint save-load / fade endpoint / four later CGs\n";
  for(const auto& member:whj2::abmp(assets.read("image/ズームアウト_エクステ.b"))){
   if(member.type.rfind("abdata",0)!=0)continue;
   const auto animation=whj2::animation(member.type,member.data);
   for(float alpha:{0.f,128.f,255.f}){
    whj2::AnimationContext context;context.externalMask=63;
    context.external={640,360,200,200,0,alpha};
    const auto sample=whj2::sampleMotion(animation.motions.front(),1000,1280,720,false,context);
    require(sample.size()==1&&std::abs(sample.front().alpha-alpha)<0.001f,"legacy alpha did not resolve value[5]");
   }
  }
  std::cout<<"PASS external opacity values 0/128/255\n";
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
