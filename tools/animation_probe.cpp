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
if(argc==4&&std::string(argv[2])=="--inspect"){
 for(auto&part:whj2::abmp(a.read(argv[3]))){std::cout<<"MEMBER "<<part.type<<" name="<<part.name<<" bytes="<<part.data.size()<<"\n";
 if(part.type.rfind("abdata",0)!=0)continue;auto data=whj2::animation(part.type,part.data);
 std::cout<<"BOUNDS "<<data.bounds[0]<<","<<data.bounds[1]<<","<<data.bounds[2]<<","<<data.bounds[3]<<"\n";
 for(auto&motion:data.motions){std::cout<<"MOTION "<<motion.name<<"\n";for(auto&layer:motion.layers){std::cout<<"LAYER "<<layer.name<<" flags="<<unsigned(layer.flags)<<" mode="<<unsigned(layer.mode)<<" keys="<<layer.keys.size()<<"\n";
 for(auto&key:layer.keys){unsigned t=uint32_t(key[0])|(uint32_t(key[1])<<8)|(uint32_t(key[2])<<16)|(uint32_t(key[3])<<24);auto v=whj2::sampleAnimation(layer,t,{unsigned(data.bounds[0]),unsigned(data.bounds[1]),1280,720});
 std::cout<<"KEY "<<t<<" kind="<<v.kind<<" resource="<<v.resource<<" xy="<<v.x<<","<<v.y<<" scale="<<v.scaleX<<","<<v.scaleY<<" anchor="<<v.anchorX<<","<<v.anchorY<<" alpha="<<v.alpha<<" event="<<unsigned(key.at(0x34))<<" control="<<(int16_t(uint16_t(key.at(0x89))|(uint16_t(key.at(0x8a))<<8)))<<"\n";
 }}}
 }return 0;
}
if(argc==3&&std::string(argv[2])=="--external"){
 unsigned tested=0;
 for(auto&part:whj2::abmp(a.read("script/title/title_back01.b")))if(part.type.rfind("abdata",0)==0){
  auto data=whj2::animation(part.type,part.data);
  for(auto&motion:data.motions)for(auto&layer:motion.layers){
   auto unresolved=whj2::sampleAnimation(layer,5500);
   if(unresolved.kind!=4||unresolved.childTimeResolved)continue;
   for(float value:{0.f,250.f,1000.f}){whj2::AnimationContext context;context.externalMask=1;context.external[0]=value;
    auto resolved=whj2::sampleAnimation(layer,5500,context);
    if(!resolved.childTimeResolved||resolved.childTime!=int(value))throw std::runtime_error("Title external clock mapping");
    std::cout<<"PASS original title external[0]="<<value<<" child="<<resolved.childTime<<"\n";tested++;
   }
  }
 }
 if(tested!=3)throw std::runtime_error("Missing original external title clock");return 0;
}
if(argc==3&&std::string(argv[2])=="--nested"){
 for(auto&part:whj2::abmp(a.read("image/cut/eyecatch01.b")))if(part.type.rfind("abdata",0)==0){
  auto data=whj2::animation(part.type,part.data);unsigned tested=0;
  for(auto&motion:data.motions)for(auto&layer:motion.layers)for(double time:{500.,1500.,2500.,3250.}){
   auto k=whj2::sampleAnimation(layer,time);
   if(k.kind!=4||k.resource!=1)continue;
   int expected=time<=2500?int((time-500)*.75):1500;
   if(k.childTime!=expected||k.childMotion!=0||!k.childTimeResolved)throw std::runtime_error("Original eyecatch child clock");
   std::cout<<"PASS original eyecatch parent="<<time<<" child="<<k.childTime<<"\n";tested++;
  }
  if(tested!=4)throw std::runtime_error("Expected original child-clock samples");
 }
 return 0;
}
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
