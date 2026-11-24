// SPDX-License-Identifier: GPL-2.0-or-later
// WHJ2 PE file offsets f41f8 (abdata13), f409c (abdata12),
// f39c8 (abmotion10), f3684 (ablayer18). Key record width is 0xa2.
#include "animation.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>
namespace whj2 {
struct AnimReader {const qlie::Bytes&b;
 size_t p=0;
 uint32_t n(unsigned size=4){if(size>b.size()-p)throw std::runtime_error("Truncated animation");
 uint32_t v=0;
 for(unsigned i=0;i<size;i++)v|=uint32_t(b[p++])<<(8*i);
 return v;
 }qlie::Bytes data(size_t size){if(size>b.size()-p)throw std::runtime_error("Truncated animation payload");
 qlie::Bytes d(b.begin()+p,b.begin()+p+size);
 p+=size;
 return d;
 }std::string str(){return qlie::cp932(data(n(2)));
 }std::string tag(){auto b0=data(16);
 size_t n0=0;
 while(n0<16&&b0[n0])n0++;
 return std::string(b0.begin(),b0.begin()+n0);
 }};
Animation animation(const std::string&type,const qlie::Bytes&b){AnimReader r{b};
 Animation a;
 a.name=r.str();
 uint32_t version=0;
 if(type=="abdata13")version=r.n();
 else if(type!="abdata12")throw std::runtime_error("Unsupported animation metadata "+type);
 for(auto&v:a.bounds)v=int32_t(r.n());
 if(type=="abdata13"&&version>=1)a.flags=r.n();
 a.comment=r.str();
 unsigned motions=r.n(1);
 while(motions--){if(r.tag()!="abmotion10")throw std::runtime_error("Unknown animation motion header");
 Motion m;
 m.name=r.str();
 unsigned layers=r.n(1);
 while(layers--){AnimationLayer l;
 auto tag=r.tag();
 if(tag!="ablayer18"&&tag!="ablayer17"&&tag!="ablayer16"&&tag!="ablayer15")throw std::runtime_error("Unsupported animation layer "+tag);
 l.version=unsigned(std::stoi(tag.substr(7)));
 l.name=r.str();
 l.flags=uint8_t(r.n(1));
 unsigned count=r.n(1);
 l.mode=uint8_t(r.n(1));
 for(unsigned i=0;i<count;i++){l.keys.push_back(r.data(l.version==15?92:l.version==16?145:162));
 }m.layers.push_back(std::move(l));
 }a.motions.push_back(std::move(m));
 }if(r.p!=b.size())throw std::runtime_error("Trailing animation bytes");
 return a;
 }
namespace {
uint32_t keyU32(const qlie::Bytes&k,size_t p){if(p+4>k.size())throw std::runtime_error("Animation field outside key");
 return uint32_t(k[p])|(uint32_t(k[p+1])<<8)|(uint32_t(k[p+2])<<16)|(uint32_t(k[p+3])<<24);
 }
int keyI16(const qlie::Bytes&k,size_t p){if(p+2>k.size())throw std::runtime_error("Animation field outside key");
 return int16_t(uint16_t(k[p])|(uint16_t(k[p+1])<<8));
 }
// PE offsets efcb0/efcd2 select the position/scale mode at 0x30.
// f0860/f0882 use 0x33 for color/alpha. a1bb0/a1c38 use the
// exponent 1 + 1.4 * power; these are not fixed quartic easing curves.
float sampleValue(float a,float b,unsigned mode,float power,double t,std::vector<std::string>&warnings){
 if(a==b||t<=0)return a;
 if(t>=1)return b;
 double q=0;
 switch(mode){case 0:return a;
 case 1:q=t;
 break;
 case 3:q=1-std::pow(1-t,1+1.4*power);
 break;
 case 4:q=std::pow(t,1+1.4*power);
 break;
 default:warnings.push_back("easing="+std::to_string(mode));
 return a;
 }
 return float(a+(b-a)*q);
}
}
AnimationSample sampleAnimation(const AnimationLayer&layer,double ms){
 AnimationSample out;
 if(layer.keys.empty()||ms<keyU32(layer.keys.front(),0))return out;
 if(layer.version<16)throw std::runtime_error("Animation layer15 normalization pending");
 size_t i=0;
 while(i+1<layer.keys.size()&&ms>=keyU32(layer.keys[i+1],0))i++;
 const auto&a=layer.keys[i];
 const auto&b=layer.keys[std::min(i+1,layer.keys.size()-1)];
 double span=double(keyU32(b,0))-keyU32(a,0),t=span>0?std::clamp((ms-keyU32(a,0))/span,0.,1.):0;
 out.kind=a.at(4);
 out.flags=a.at(5);
 out.resource=a.at(6);
 auto val=[&](size_t offset,unsigned mode,float power){return sampleValue(float(keyI16(a,offset)),float(keyI16(b,offset)),mode,power,t,out.unsupported);
 };
 out.x=val(0x18,a.at(0x30),a.at(0x31)/100.f);
 out.y=val(0x1a,a.at(0x30),a.at(0x31)/100.f);
 out.scaleX=val(0x1c,a.at(0x30),a.at(0x31)/100.f);
 out.scaleY=val(0x1e,a.at(0x30),a.at(0x31)/100.f);
 out.priority=val(0x20,a.at(0x30),a.at(0x31)/100.f);
 out.rotationZ=val(0x22,a.at(0x30),a.at(0x31)/100.f);
 out.alpha=val(0x35,a.at(0x33),a.at(0x3b)/100.f);
 out.anchorX=float(keyI16(a,0x85));
 out.anchorY=float(keyI16(a,0x87));
 auto color=[&](size_t p){uint32_t z=0;
 auto ca=keyU32(a,p),cb=keyU32(b,p);
 for(int n=0;n<3;n++){auto v=sampleValue(float((ca>>(8*n))&255),float((cb>>(8*n))&255),a.at(0x33),a.at(0x3b)/100.f,t,out.unsupported);
 z|=uint32_t(std::clamp(std::lround(v),0l,255l))<<(8*n);
 }return z;
 };
 out.addColor=color(0x24);
 out.mulColor=color(0x28);
 for(unsigned n=0;n<4;n++)if(a.at(0x51+12*n)&&a.at(0x52+12*n))out.unsupported.push_back("procedural-track="+std::to_string(n));
 if(keyI16(a,0x48)||keyI16(a,0x4a))out.unsupported.push_back("3d-rotation");
 if(out.kind!=0&&out.kind!=4&&out.kind!=15&&out.kind!=16)out.unsupported.push_back("primitive="+std::to_string(out.kind));
 if(layer.mode>2)out.unsupported.push_back("layer-mode="+std::to_string(layer.mode));
 return out;
}

// Original PE ea267/ea29c: push before applying the top state, pop after.
// f6cfc/f6e5c add translation/rotation, multiply scale, saturate additive
// color and multiply tint/alpha. The stack is reset for each motion draw.
std::vector<AnimationSample> sampleMotion(const Motion&motion,double ms,unsigned width,unsigned height,bool applyRenderState){
 std::vector<AnimationSample> result,stack;
 auto compose=[](AnimationSample value,const AnimationSample&parent){
  value.x+=parent.x;value.y+=parent.y;
  if(value.kind!=3){value.scaleX*=parent.scaleX/100;value.scaleY*=parent.scaleY/100;}
  value.rotationZ+=parent.rotationZ;value.priority+=parent.priority;
  value.alpha=float(int(value.alpha)*int(parent.alpha)/255);
  uint32_t add=0,mul=0;
  for(unsigned n=0;n<3;n++){
   auto a=(value.addColor>>(8*n))&255,b=(parent.addColor>>(8*n))&255;
   add|=std::min(255u,a+b)<<(8*n);
   a=(value.mulColor>>(8*n))&255;b=(parent.mulColor>>(8*n))&255;
   mul|=(a*b/255)<<(8*n);
  }
  value.addColor=add;value.mulColor=mul;return value;
 };
 for(const auto&layer:motion.layers){
  auto value=sampleAnimation(layer,ms);
  if(value.kind){
   if(layer.mode==1&&stack.size()<31){
    auto parent=value;
    // The push operation always multiplies scale, including primitive 3.
    if(!stack.empty()){
     parent=compose(value,stack.back());
     parent.scaleX=value.scaleX*stack.back().scaleX/100;
     parent.scaleY=value.scaleY*stack.back().scaleY/100;
    }
    stack.push_back(parent);
   }
   if(!stack.empty())value=compose(value,stack.back());
   if(layer.mode==2&&!stack.empty())stack.pop_back();
  }
  result.push_back(std::move(value));
 }
 // PE ec148 -> da90c -> dafc4 is a separate render-state stack.
 // dad00 translates, rotates clockwise-negative about the screen center,
 // then scales about that center. These operations can produce shear.
 if(!applyRenderState)return result;
 std::vector<AnimationSample> renderStack;
 for(size_t i=0;i<result.size();i++){
  auto&value=result[i];
  if(!motion.layers[i].flags)continue;
  if(value.kind==15){if(!renderStack.empty())renderStack.pop_back();continue;}
  if(value.kind==16){
   auto state=value;
   if(!renderStack.empty())state=compose(state,renderStack.back());
   renderStack.push_back(state);continue;
  }
  if(!value.kind||renderStack.empty())continue;
  const auto&state=renderStack.back();
  constexpr float pi=3.14159265358979323846f;
  float r=state.rotationZ*pi/180,c=std::cos(r),s=std::sin(r);
  float sx=state.scaleX/100,sy=state.scaleY/100,cx=width/2.f,cy=height/2.f;
  value.screen={sx*c,sx*s,-sy*s,sy*c,
   cx+sx*(c*(state.x-cx)+s*(state.y-cy)),
   cy+sy*(-s*(state.x-cx)+c*(state.y-cy))};
  auto colored=compose(value,state);
  value.alpha=colored.alpha;value.addColor=colored.addColor;value.mulColor=colored.mulColor;
 }
 return result;
}

}
