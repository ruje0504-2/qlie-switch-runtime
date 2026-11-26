// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj6.hpp"
#include "scene_images.hpp"
#include "render_queue.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
struct Capture final : whj2::RenderTarget {
 std::vector<float> pose;
 void image(const whj2::Image& image,const whj2::Affine2D& at,float alpha,
            uint32_t,uint32_t,unsigned,unsigned) override {
  pose.insert(pose.end(),{float(image.width),float(image.height),at.a,at.b,at.c,at.d,at.x,at.y,alpha});
 }
 void triangle(const whj2::Image&,const std::array<whj2::TextureVertex,3>& points,
               const whj2::Affine2D&,float alpha,uint32_t,uint32_t,unsigned,unsigned) override {
  for(const auto& point:points)pose.insert(pose.end(),{point.x,point.y,point.depth,point.u,point.v});
  pose.push_back(alpha);
 }
};
auto pose(whj2::SceneImages& scene){Capture target;scene.draw(target);return target.pose;}
bool equal(const std::vector<float>& a,const std::vector<float>& b){
 if(a.size()!=b.size())return false;
 for(size_t i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>0.001f)return false;
 return true;
}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void enter(whj2::SceneImages& scene){
 scene.command("^chara01,$reset");
 scene.command("^chara01,file0:him_,file1:中_,file2:着物21_,file3:108ギャグ01,show:true,x:$c_left");
}
bool moving(whj2::SceneImages& scene){
 const auto first=pose(scene);
 require(!first.empty(),"portrait did not render");
 bool changed=false;
 for(unsigned i=0;i<12;++i){scene.update(23);changed|=!equal(first,pose(scene));}
 return changed;
}
void stationary(whj2::SceneImages& scene,whj2::SceneImages& reference,const char* message){
 for(unsigned i=0;i<15;++i){
  require(equal(pose(scene),pose(reference)),message);
  scene.update(37);reference.update(37);
 }
}
}

int main(int argc,char** argv){
 try{
  if(argc!=2)return 2;
  qlie::Assets assets(argv[1],whj6::profile::archives);
  whj2::SceneImages scene(assets,whj6::profile::sceneSize),reference(assets,whj6::profile::sceneSize);
  enter(scene);enter(reference);
  scene.command("^chara01,motion:横衝撃継続,motionkeep:true");
  require(moving(scene),"authored shake did not start");
  scene.command("^chara01,file3:108ギャグ01");
  require(moving(scene),"same-scene portrait change interrupted motionkeep");
  const auto active=scene.save();
  auto restored=scene.makeRestoreTarget();restored->restore(active);
  for(unsigned i=0;i<8;++i){
   require(equal(pose(scene),pose(*restored)),"active shake save/load changed its phase");
   scene.update(31);restored->update(31);
  }
  scene.command("^chara01,show:false");
  require(pose(scene).empty(),"hidden character still rendered");
  const auto hidden=scene.save();
  restored->restore(hidden);
  const char* returnToRoom="^chara01,file2:着物11_,file3:002通常02,show:true,x:$center,imgfilterbase:blur20";
  scene.command(returnToRoom);restored->command(returnToRoom);reference.command(returnToRoom);
  stationary(scene,reference,"shake leaked from y01-01:7996 into room at 8611");
  stationary(*restored,reference,"hidden save/load leaked shake into next scene");
  std::cout<<"PASS screenshot shake / same-scene keep / hide / room re-entry / save-load\n";

  for(const char* command:{"^chara01,show:true,motion:横衝撃継続,motionkeep:true",
                           "^chara01,motion:横衝撃継続,motionkeep:true,show:true"}){
   scene.command(command);
   require(moving(scene),"explicit show+motion depended on field order");
   scene.command("^chara01,show:true");
   stationary(scene,reference,"explicit show failed to restore default motion");
  }
  scene.command("^chara01,motion:横衝撃継続,motionkeep:true");
  require(moving(scene),"second shake did not start");
  scene.command("^chara01,base:motion/motion_BasicSet");
  stationary(scene,reference,"base refresh retained previous motion");
  std::cout<<"PASS show/base resets and explicit motion field order\n";
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
