// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj2.hpp"
#include "animation.hpp"
#include "image.hpp"
#include <cmath>
#include <iostream>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);
 for(auto&m:whj2::abmp(assets.read("script/title/title_back01.b")))if(m.type=="abdata13"){
 auto a=whj2::animation(m.type,m.data);auto&mo=a.motions.front();unsigned count=0;
 for(double t:{1000.,2750.,4625.,6500.,10500.}){auto samples=whj2::sampleMotion(mo,t);unsigned particle=0;
 for(auto&k:samples)if(k.kind==20){double expected=std::fmod(t,particle?9250.:5500.)*100/(particle?9250.:5500.);
 if(std::abs(k.particles.phase[0]-expected)>.001||k.particles.phase[1]!=0||k.particles.phase[2]!=0)throw std::runtime_error("Original parent phase missing");
 auto origin=k.particles;origin.phase[0]=0;auto points=whj2::particlePoints(k.particles,123,3),fixed=whj2::particlePoints(origin,123,3);
 if(points.empty()||std::abs(points[0].x-fixed[0].x)<.01)throw std::runtime_error("Particle drift absent");
 std::cout<<"PASS title time="<<t<<" layer="<<particle<<" phase="<<k.particles.phase[0]<<" x="<<points[0].x<<" fixed_x="<<fixed[0].x<<"\n";particle++;count++;}
 if(particle!=2)throw std::runtime_error("Original particle layers lost");}
 if(count!=10)throw std::runtime_error("Incomplete phase samples");return 0;}
 throw std::runtime_error("Missing title metadata");
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
