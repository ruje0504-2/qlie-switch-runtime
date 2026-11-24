// SPDX-License-Identifier: GPL-2.0-or-later
#include "particles.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace whj2 {
uint32_t particleRandomNext(uint32_t& seed){seed=seed*0x08088405u+1u;return uint32_t(uint64_t(seed)*0x7fffffffu>>32);}
uint32_t particleIdentityInitialState(){uint32_t seed=0x3f1754e7;for(unsigned i=0;i<1024;i++)particleRandomNext(seed);return seed;}
std::array<uint32_t,1024> particleRandomTable(uint32_t seed){
 std::array<uint32_t,1024> table{};
 for(auto&v:table)v=particleRandomNext(seed);
 return table;
}
// PE e8974 and daa9c: camera distance 1000, centered viewport.
ParticleProjection projectParticle(const ParticlePoint&p,const ParticleParameters&parameters,float x,float y,float ax,float ay,float angle,unsigned width,unsigned height,unsigned textureWidth,unsigned textureHeight){
 float radians=angle*3.14159265358979323846f/180;
 float co=std::cos(radians),si=std::sin(radians);
 float u=p.x-parameters.extent[0]*ax+(p.scale-1)*textureWidth*.5f,v=p.y-parameters.extent[1]*ay+(p.scale-1)*textureHeight*.5f;
 float worldX=x+co*u-si*v,worldY=y+si*u+co*v;
 float depth=1000-(p.z-parameters.extent[2]*.5f);
 float ratio=1000/std::max(10.f,depth),cx=width*.5f,cy=height*.5f;
 return {cx+(worldX-cx)*ratio,cy+(worldY-cy)*ratio,p.scale*ratio,depth,depth>=10};
}
namespace {
int rounded(float x){return int(std::round(x));}
int edgeAlpha(float position,float edge){
 int a=255;
 if(position<edge)a=rounded(position*(edge>.001f?1.f/edge:1.f)*255);
 if(position>1.f-edge)a=rounded((1.f-position)*(edge>.001f?1.f/edge:1.f)*255);
 return a;
}
}
// PE ec718..ed09f: stable random lookup, cell wrapping, edge fade and
// per-particle child timeline selection. Projection is handled separately.
std::vector<ParticlePoint> particlePoints(const ParticleParameters&p,uint32_t layerSeed,unsigned motionCount){
 std::vector<ParticlePoint> result;
 if(p.count<.001f)return result;
 if(!std::isfinite(p.count)||p.count>32767)throw std::runtime_error("Invalid particle count");
 static const auto table=particleRandomTable();
 auto random=[&](uint32_t index){return float(double(table[index&1023]&0xffffff)/16777215.);};
 int divisions=std::max(1,rounded(p.divisions));
 int whole=int(p.count);float fraction=p.count-whole;
 if(fraction<.001f){fraction=1;whole--;}
 auto seed=(layerSeed&0xffff)*777u;
 int phaseBase=(100-100%divisions)*100;
 float phase[3];
 for(unsigned i=0;i<3;i++)phase[i]=float((phaseBase+static_cast<long double>(p.phase[i]))*.01L);
 float edge=p.edge*.005f;
 for(int i=0;i<=whole;i++){
  float v[3]{random(seed+i+234)+phase[0],
             random(seed+i+555)+phase[1],
             random(seed+i+777)+phase[2]};
  int cell[3]{};
  if(divisions>1){
   for(unsigned j=0;j<3;j++)cell[j]=int(v[j])%divisions;
   if(cell[0]>0){v[1]+=random(cell[0]+i+123);v[2]+=random(cell[0]+i+678);}
   if(cell[1]>0){v[0]+=random(cell[1]+i+789);v[2]+=random(cell[1]+i+234);}
   if(cell[2]>0){v[0]+=random(cell[2]+i+345);v[1]+=random(cell[2]+i+899);}
  }
  for(auto&f:v)f-=std::trunc(f);
  uint32_t key=(seed+i)^uint32_t(cell[0]|(cell[1]<<2)|(cell[2]<<4));
  float timeMix=random(key);
  int32_t time=int32_t((1.L-timeMix)*p.timeBegin+static_cast<long double>(timeMix)*p.timeEnd);
  unsigned motion=0;
  // The second lookup adds 999 before XOR, not after it.
  if(motionCount)motion=std::min(motionCount-1,unsigned(rounded(random((seed+i+999)^uint32_t(cell[0]|(cell[1]<<2)|(cell[2]<<4)))*motionCount)));
  int a=std::min({edgeAlpha(v[0],edge),edgeAlpha(v[1],edge),edgeAlpha(v[2],edge)});
  if(i==whole)a=std::min(a,rounded(fraction*255));
  result.push_back({v[0]*p.extent[0],v[1]*p.extent[1],v[2]*p.extent[2],1+p.scaleOffset*.01f,unsigned(std::clamp(a,0,255)),motion,time});
 }
 return result;
}
}
