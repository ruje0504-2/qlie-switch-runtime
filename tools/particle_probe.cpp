// SPDX-License-Identifier: GPL-2.0-or-later
#include "animation.hpp"
#include "image.hpp"
#include <iostream>
#include <cmath>
#include <cstring>
#include <stdexcept>
int main(int argc,char**argv){try{
 if(argc==2&&std::string(argv[1])=="--table"){
  auto t=whj2::particleRandomTable();std::cout.write(reinterpret_cast<const char*>(t.data()),sizeof(t));return 0;
 }
 whj2::ParticleParameters p;p.count=3.5f;p.extent={100,200,300};p.timeBegin=100;p.timeEnd=200;
 auto points=whj2::particlePoints(p,9,3);if(points.size()!=4||points.back().alpha!=128)throw std::runtime_error("Fractional count fade");
 for(auto&v:points)if(v.x<0||v.x>100||v.y<0||v.y>200||v.z<0||v.z>300||v.motion>2||v.milliseconds<100||v.milliseconds>200)throw std::runtime_error("Particle bounds");
 auto again=whj2::particlePoints(p,9,3);
 for(size_t i=0;i<points.size();i++)if(points[i].x!=again[i].x||points[i].y!=again[i].y||points[i].alpha!=again[i].alpha)throw std::runtime_error("Non-deterministic particle restart");
 p.count=4;if(whj2::particlePoints(p,9).size()!=4)throw std::runtime_error("Integer particle count");
 p.count=0;if(!whj2::particlePoints(p,9).empty())throw std::runtime_error("Disabled particles");
 std::cout<<"PASS fractional/integer/disabled counts, coordinates, child time and deterministic resampling\n";
 if(argc<2)return 0;
 qlie::Assets assets(argv[1]);
 for(const auto&name:assets.names()){
  if(name.size()<2||name.substr(name.size()-2)!=".b")continue;
  for(auto&part:whj2::abmp(assets.read(name))){
   if(part.type.rfind("abdata",0)!=0)continue;
   auto a=whj2::animation(part.type,part.data);
   for(auto&motion:a.motions)if(motion.name=="c01")for(auto&layer:motion.layers){
    auto k=whj2::sampleAnimation(layer,0,whj2::AnimationContext{unsigned(a.bounds[0]),unsigned(a.bounds[1]),1280,720});
    if(k.kind!=20||k.particles.count!=40)continue;
    auto&v=k.particles;
    if(v.edge!=20||v.divisions!=0||v.extent[0]!=1280||v.extent[1]!=800||v.extent[2]!=400)throw std::runtime_error("Opening particle field mapping");
    auto distribution=whj2::particlePoints(v,0);
    if(distribution.size()!=40)throw std::runtime_error("Opening particle count");
    std::cout<<"PASS original "<<name<<" motion=c01 count="<<v.count<<" edge="<<v.edge<<" divisions="<<v.divisions<<" extent="<<v.extent[0]<<","<<v.extent[1]<<","<<v.extent[2]<<" points="<<distribution.size()<<"\n";
    std::cout<<"Particle transform/projection not yet connected to scene renderer\n";return 0;
   }
  }
 }
 throw std::runtime_error("Opening particle sample not found");
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
