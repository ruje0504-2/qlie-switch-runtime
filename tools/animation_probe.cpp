#include "animation.hpp"
#include "image.hpp"
#include <iostream>
#include <cmath>
size_t count=0,motions=0,layers=0,keys=0;
bool rects=false;
void check(const qlie::Bytes&b,const std::string&name){if(b.size()<5||!std::equal(b.begin(),b.begin()+5,"abmp1"))return;for(auto&m:whj2::abmp(b)){if(m.type.rfind("abdata",0)==0){try{auto a=whj2::animation(m.type,m.data);count++;motions+=a.motions.size();for(auto&mo:a.motions){layers+=mo.layers.size();for(auto&l:mo.layers){keys+=l.keys.size();if(rects)for(auto&k:l.keys)if(k.at(4)==1){
 double ms=uint32_t(k[0])|(uint32_t(k[1])<<8)|(uint32_t(k[2])<<16)|(uint32_t(k[3])<<24);
 auto v=whj2::sampleAnimation(l,ms);
 if(v.flags==0&&std::abs(v.scaleX)<32700&&std::abs(v.scaleY)<32700)
 std::cout<<"RECT "<<name<<" motion="<<mo.name<<" t="<<ms<<" xy="<<v.x<<","<<v.y<<" size="<<v.scaleX<<","<<v.scaleY<<" alpha="<<v.alpha<<" mul="<<v.mulColor<<"\n";
}}}}catch(const std::exception&e){throw std::runtime_error(name+": "+e.what());}}else if(m.type.rfind("abimg",0)==0)check(m.data,name+"#"+m.name);}}
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets a(argv[1]);
if(argc==3&&std::string(argv[2])=="--effect"){
 for(auto&part:whj2::abmp(a.read("image/effect/effect01.b"))){
  if(part.type.rfind("abdata",0)!=0)continue;
  auto data=whj2::animation(part.type,part.data);
  for(auto&motion:data.motions)if(motion.name=="フラッシュ"){
   unsigned rectangles=0;
   for(auto&layer:motion.layers)if(layer.flags)for(auto&key:layer.keys)if(key.at(4)==1){
    double ms=uint32_t(key[0])|(uint32_t(key[1])<<8)|(uint32_t(key[2])<<16)|(uint32_t(key[3])<<24);
    auto k=whj2::sampleAnimation(layer,ms,whj2::AnimationContext{unsigned(data.bounds[0]),unsigned(data.bounds[1]),1280,720});
    if(k.scaleX!=data.bounds[0]||k.scaleY!=data.bounds[1])throw std::runtime_error("Original flash symbolic dimensions");
    rectangles++;std::cout<<"PASS original flash symbolic size="<<k.scaleX<<"x"<<k.scaleY<<" at="<<ms<<"\n";
   }
   if(!rectangles)throw std::runtime_error("No enabled flash rectangle");
  }
  for(auto&motion:data.motions)if(motion.name=="縦衝撃"){
   if(motion.layers.empty())throw std::runtime_error("Missing effect layers");
   const auto&l=motion.layers.front();
   for(double ms:{0.,25.,75.,100.,200.}){
    auto k=whj2::sampleAnimation(l,ms);
    std::cout<<"EFFECT vertical t="<<ms<<" kind="<<k.kind<<" x="<<k.x<<" y="<<k.y<<"\n";
    if(ms==25&&std::abs(k.y-10)>0.01)throw std::runtime_error("Quarter-cycle displacement");
    if(ms==75&&std::abs(k.y+10)>0.01)throw std::runtime_error("Three-quarter displacement");
    if(ms==200&&k.kind!=0)throw std::runtime_error("Effect endpoint must be inactive");
    for(auto&w:k.unsupported)throw std::runtime_error("Unimplemented vertical effect: "+w);
   }
   std::cout<<"PASS original effect01 vertical impact samples\n";return 0;
  }
 }
 throw std::runtime_error("Original vertical impact not found");
}rects=argc==3&&std::string(argv[2])=="--rects";for(auto&n:a.names())if(n.size()>2&&n.substr(n.size()-2)==".b")check(a.read(n),n);std::cout<<"PASS metadata="<<count<<" motions="<<motions<<" layers="<<layers<<" keys="<<keys<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
