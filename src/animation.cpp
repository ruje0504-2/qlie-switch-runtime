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
// PE ec1cc wraps only when the child animation has a positive loop end.
double nestedAnimationTime(const Animation&a,double milliseconds){
 auto time=int64_t(milliseconds);int64_t end=a.bounds[2],start=a.bounds[3];
 if(end>0){auto span=end-start;if(span<1)span=end;
  time=time>start?(time-start)%span+start:time%span;
 }
 return double(time);
}
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
AnimationSample sampleAnimation(const AnimationLayer&layer,double ms,const AnimationContext&context){
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
 auto number=[&](const qlie::Bytes&key,size_t offset){
  int v=keyI16(key,offset);
  switch(v){case 32727:if(context.imageWidth!=UINT32_MAX)return float(context.imageWidth);break;
   case 32728:if(context.imageHeight!=UINT32_MAX)return float(context.imageHeight);break;
   case 32729:if(context.targetWidth!=UINT32_MAX)return float(context.targetWidth);break;
   case 32730:if(context.targetHeight!=UINT32_MAX)return float(context.targetHeight);break;default:break;}
  if((v>=32700&&v<32716)||(v>=32720&&v<32736))out.unsupported.push_back("numeric-context="+std::to_string(v));
  return float(v);
 };
 auto val=[&](size_t offset,unsigned mode,float power){return sampleValue(number(a,offset),number(b,offset),mode,power,t,out.unsupported);
 };
 out.x=val(0x18,a.at(0x30),a.at(0x31)/100.f);
 out.y=val(0x1a,a.at(0x30),a.at(0x31)/100.f);
 out.scaleX=val(0x1c,a.at(0x30),a.at(0x31)/100.f);
 out.scaleY=val(0x1e,a.at(0x30),a.at(0x31)/100.f);
 out.priority=val(0x20,a.at(0x30),a.at(0x31)/100.f);
 out.rotationZ=val(0x22,a.at(0x30),a.at(0x31)/100.f);
 out.alpha=val(0x35,a.at(0x33),a.at(0x3b)/100.f);
 if(out.kind==10){
  auto input=[&](size_t offset){return val(offset,a.at(0x2d),a.at(0x2e)/100.f);};
  out.mask.sourceX=input(8);out.mask.sourceY=input(0xa);out.mask.width=input(0xc);out.mask.height=input(0xe);
  out.mask.x=input(0x40);out.mask.y=input(0x42);out.mask.maskWidth=input(0x44);out.mask.maskHeight=input(0x46);
  out.mask.alignX=number(a,0x81)/100;out.mask.alignY=number(a,0x83)/100;
  out.mask.amount=uint8_t(std::clamp(int(std::round(val(0x37,a.at(0x33),a.at(0x3b)/100.f))),0,255));out.mask.mode=a.at(0x3a);
  out.childMotion=a.at(7);
 }
 if(out.kind==4||out.kind==20||out.kind==21){
  auto timeValue=[&](const qlie::Bytes&key,size_t offset){
   auto value=int32_t(keyU32(key,offset));
   if((value>=32700&&value<32716)||(value>=32720&&value<32736)){
    out.childTimeResolved=false;out.unsupported.push_back("child-time-context="+std::to_string(value));
   }
   return float(value);
  };
  out.childTime=int32_t(sampleValue(timeValue(a,0x10),timeValue(b,0x10),a.at(0x3c),a.at(0x3d)/100.f,t,out.unsupported));
  out.childMotion=a.at(7);
 }
 if((out.kind==20||out.kind==21)&&a.size()<152)out.unsupported.push_back("particle-layout="+std::to_string(layer.version));
 if((out.kind==20||out.kind==21)&&a.size()>=152){
  auto particleValue=[&](size_t offset){return val(offset,a.at(0x2d),a.at(0x2e)/100.f);};
  out.particles.count=particleValue(8);out.particles.edge=particleValue(0xa);
  out.particles.divisions=particleValue(0xc);out.particles.scaleOffset=particleValue(0x46);
  for(unsigned axis=0;axis<3;axis++)out.particles.phase[axis]=particleValue(0x40+axis*2);
  out.particles.extent={out.scaleX,out.scaleY,val(0x96,a.at(0x30),a.at(0x31)/100.f)};
  auto childTime=[&](size_t offset){return int32_t(sampleValue(float(int32_t(keyU32(a,offset))),float(int32_t(keyU32(b,offset))),a.at(0x3c),a.at(0x3d)/100.f,t,out.unsupported));};
  out.particles.timeBegin=childTime(0x10);out.particles.timeEnd=childTime(0x14);
 }
 out.anchorX=number(a,0x85);
 out.anchorY=number(a,0x87);
 auto color=[&](size_t p){uint32_t z=0;
 auto ca=keyU32(a,p),cb=keyU32(b,p);
 for(int n=0;n<3;n++){auto v=sampleValue(float((ca>>(8*n))&255),float((cb>>(8*n))&255),a.at(0x33),a.at(0x3b)/100.f,t,out.unsupported);
 z|=uint32_t(std::clamp(std::lround(v),0l,255l))<<(8*n);
 }return z;
 };
 out.addColor=color(0x24);
 out.mulColor=color(0x28);
 // PE f1468 interpolates four signed parameters before invoking the
 // waveform and property tables. Time is relative to this key (f15ed).
 for(unsigned n=0;n<4;n++){
  size_t q=0x51+12*n;unsigned op=a.at(q),target=a.at(q+1);
  if(!op||!target)continue;
  float p[4];
  for(unsigned j=0;j<4;j++)p[j]=val(q+4+j*2,a.at(q+2),a.at(q+3)/100.f);
  float value=0;bool known=true;
  auto trig=[](float radians,bool cosine){
   // a18c4 quantizes the phase to the engine's 1024-entry sine table.
   int phase=int(radians*512.f/3.14159265f)+(cosine?256:0);
   return float(std::sin((phase&1023)*2.0*3.14159265358979323846/1024));
  };
  if(op==5){
   // PE f0f90: table index uses the time quotient, not the remainder.
   static const auto randomTable=particleRandomTable();
   int period=int(std::round(p[2]));int elapsed=int(ms-keyU32(a,0));
   uint32_t index=context.randomSeed+uint32_t(period>1?elapsed/period:1);
   float fraction=float(double(randomTable[index&1023]&0xffffff)/16777215.);
   float spread=float(static_cast<long double>(p[1]-p[0])*fraction);
   value=p[0]+spread;
  }
  else if(op==2||op==3)value=trig(p[0]*3.14159265f/180,op==3)*p[1];
  else if(op==4)value=p[0];
  else if(op>=7&&op<=10){
   int period=int(std::round(p[2]));
   if(period==0){out.unsupported.push_back("procedural-period=0");continue;}
   int elapsed=int(ms-keyU32(a,0));int remainder=period>1?elapsed%period:1;
   float phase=float(remainder)/period;
   if(op==7||op==8)value=(p[0]+p[1])*.5f+(p[1]-p[0])*.5f*trig(phase*2*3.14159265f,op==8);
   else if(op==9){phase*=2;if(phase>1)phase=2-phase;value=p[0]+(p[1]-p[0])*phase;}
   else value=phase<.5f?p[0]:p[1];
  }else known=false;
  if(!known){out.unsupported.push_back("procedural-function="+std::to_string(op));continue;}
  switch(target){
   case 1:out.resource=unsigned(std::clamp(int(out.resource)+int(std::round(value)),0,255));break;
   case 13:out.x+=value;break;case 14:out.y+=value;break;
   case 15:out.scaleX+=value;break;case 16:out.scaleY+=value;break;
   case 17:out.rotationZ+=value;break;
   case 21:out.priority+=value;break;
   case 24:out.alpha=std::clamp(out.alpha+std::round(value),0.f,255.f);break;
   case 28:out.scaleX*=value/100;break;case 29:out.scaleY*=value/100;break;
   default:out.unsupported.push_back("procedural-target="+std::to_string(target));break;
  }
 }
 if(keyI16(a,0x48)||keyI16(a,0x4a))out.unsupported.push_back("3d-rotation");
 if(out.kind==4&&(out.flags&0x7f)>8)out.unsupported.push_back("bitmap-blend="+std::to_string(out.flags&0x7f));
 if(out.kind!=0&&out.kind!=1&&out.kind!=4&&out.kind!=10&&out.kind!=15&&out.kind!=16)out.unsupported.push_back("primitive="+std::to_string(out.kind));
 if(layer.mode>2)out.unsupported.push_back("layer-mode="+std::to_string(layer.mode));
 return out;
}

// Original PE ea267/ea29c: push before applying the top state, pop after.
// f6cfc/f6e5c add translation/rotation, multiply scale, saturate additive
// color and multiply tint/alpha. The stack is reset for each motion draw.
std::vector<AnimationSample> sampleMotion(const Motion&motion,double ms,unsigned width,unsigned height,bool applyRenderState,const AnimationContext&context){
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
  auto value=sampleAnimation(layer,ms,context);
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
