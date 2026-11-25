// SPDX-License-Identifier: GPL-2.0-or-later
// WHJ2 PE file offsets f41f8 (abdata13), f409c (abdata12),
// f39c8 (abmotion10), f3684 (ablayer18). Key record width is 0xa2.
#include "animation.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>
namespace whj2 {
std::optional<double> animationProgress(unsigned mode,double power,double t){
 const double exponent=1+1.4*power;
 switch(mode){
  case 0:return 0;
  case 1:return t;
  case 3:return 1-std::pow(1-t,exponent);
  case 4:return std::pow(t,exponent);
  case 7:return t<.5?.5*(1-std::pow(1-t*2,exponent)):.5+.5*std::pow(t*2-1,exponent);
  case 8:return t<.5?.5*std::pow(t*2,exponent):1-.5*std::pow((1-t)*2,exponent);
  case 17:{
   // Same 1024-step cosine phase as the shared QLIE outback curve.
   const float angle=float(3.14159265L*float(t));
   const int phase=int(static_cast<long double>(angle)*512*0.3183098865475128L);
   const float cosine=float(std::sin((phase+256)*6.283185307179586/1024));
   return 1-(1-t)*cosine;
  }
  default:return {};
 }
}
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
 }std::string str(bool wide=false){
  const auto length=n(2);
  return wide?qlie::utf16le(data(length*2)):qlie::cp932(data(length));
 }std::string tag(){auto b0=data(16);
 size_t n0=0;
 while(n0<16&&b0[n0])n0++;
 return std::string(b0.begin(),b0.begin()+n0);
 }};
// WHJ4 PE 0x594f3e/0x594f4d reads the loop start before its end in abdata14.
// PE 0x5837b4 wraps from the start, leaving the intro before it intact.
double nestedAnimationTime(const Animation&a,double milliseconds){
 const auto time=int64_t(milliseconds);
 const int64_t start=a.bounds[a.loopStartFirst?2:3];
 const int64_t end=a.bounds[a.loopStartFirst?3:2];
 if(end<=0||(a.loopStartFirst&&time<start))return double(time);
 const auto span=end>start?end-start:end;
 if(a.loopStartFirst||time>start)return double((time-start)%span+start);
 return double(time%span);
}
// WHJ5 PE 0x5b3d54 and 0x5b3f90 convert the 0x5c legacy record to 0xa2.
static void normalizeLayer15(qlie::Bytes& key){
 key.resize(0xa2);
 for(const auto source:{0x2fu,0x32u}){
  const unsigned anchor=key[source];
  const unsigned target=source==0x2f?0x81:0x85;
  key[source]=0;
  key[target]=uint8_t(anchor%3*50);
  key[target+2]=uint8_t(anchor/3*50);
  key[target+3]=uint8_t(anchor/3*50>>8);
 }
 for(unsigned i=0;i<4;++i){
  key[0x89+i*2]=key[0x35+i];
  key[0x54+i*12]=100;
 }
 key[0x96]=100;
 std::fill(key.begin()+0x98,key.begin()+0xa0,0xff);
}
// WHJ1 PE 0x4f2224/0x4f22d4 expands a 0x91 key and converts the
// original byte alpha and mask amount. A 0xa2 layer-17 key uses the same fields.
static void normalizeLayer16Or17(qlie::Bytes& key){
 if(key.size()==0x91){
  key.resize(0xa2);
  key[0x96]=100;
  std::fill(key.begin()+0x98,key.begin()+0xa0,0xff);
 }
 key[0x35]=key[0x2c];key[0x36]=0;
 key[0x37]=key[0x39];key[0x38]=0;
 constexpr std::pair<unsigned,unsigned> fields[]{
  {0x08,2},{0x0a,2},{0x0c,2},{0x0e,2},{0x10,4},{0x14,4},
  {0x18,2},{0x1a,2},{0x1c,2},{0x1e,2},{0x22,2},{0x48,2},
  {0x4a,2},{0x4d,4},{0x96,2}
 };
 for(auto [offset,width]:fields){
  unsigned value=0;
  for(unsigned i=0;i<width;++i)value|=unsigned(key[offset+i])<<(8*i);
  if(value>=30000&&value<30100){
   value=value-30000+32700;
   for(unsigned i=0;i<width;++i)key[offset+i]=uint8_t(value>>(8*i));
  }
 }
 if(key[4]==18){key[7]=0xff;key[0x3e]=0;key[0x3f]=100;}
}
Animation animation(const std::string&type,const qlie::Bytes&b){AnimReader r{b};
 Animation a;
 const bool modern=type=="abdata15";
 const bool wide=type=="abdata14"||modern;
 a.loopStartFirst=wide;
 a.name=r.str(wide);
 uint32_t version=0;
 if(type=="abdata13"||wide)version=r.n();
 else if(type!="abdata12")throw std::runtime_error("Unsupported animation metadata "+type);
 if(wide&&version!=(modern?2u:1u))throw std::runtime_error("Unsupported "+type+" revision");
 // abdata15 revision 2 moves the playback interval to each motion.
 for(unsigned i=0;i<(modern?2u:4u);++i)a.bounds[i]=int32_t(r.n());
 if(version>=1)a.flags=r.n();
 a.comment=r.str(wide);
 unsigned motions=r.n(1);
 while(motions--){
 const auto motionTag=r.tag();
 const bool modernMotion=motionTag=="abmotion12";
 const bool wideMotion=motionTag=="abmotion11"||modernMotion;
 if(!wideMotion&&motionTag!="abmotion10")throw std::runtime_error("Unknown animation motion header");
 if(wideMotion&&r.n()!=1)throw std::runtime_error("Unsupported "+motionTag+" revision");
 Motion m;
 m.name=r.str(wideMotion);
 if(modernMotion){m.loopStart=int32_t(r.n());m.loopEnd=int32_t(r.n());r.n();}
 else if(wideMotion){r.n();r.n();}
 unsigned layers=r.n(1);
 while(layers--){AnimationLayer l;
 auto tag=r.tag();
 if(tag!="ablayer25"&&tag!="ablayer22"&&tag!="ablayer21"&&tag!="ablayer18"&&tag!="ablayer17"&&tag!="ablayer16"&&tag!="ablayer15")
  throw std::runtime_error("Unsupported animation layer "+tag);
 l.version=unsigned(std::stoi(tag.substr(7)));
 l.name=r.str(l.version>=22);
 l.flags=uint8_t(r.n(1));
 const bool modernLayer=l.version==25;
 const unsigned count=r.n(modernLayer?4:1);
 l.mode=uint8_t(r.n(1));
 const auto keyBytes=modernLayer?r.n():l.version>=21?440:l.version==15?92:l.version==16?145:162;
 if((modernLayer&&keyBytes!=0x1cc)||count>(b.size()-r.p)/keyBytes)
  throw std::runtime_error("Invalid "+tag+" key records");
 for(unsigned i=0;i<count;i++){
  auto key=r.data(keyBytes);
  if(l.version==15)normalizeLayer15(key);
  if(l.version==16||l.version==17)normalizeLayer16Or17(key);
  l.keys.push_back(std::move(key));
 }m.layers.push_back(std::move(l));
 }a.motions.push_back(std::move(m));
 }if(r.p!=b.size())throw std::runtime_error("Trailing animation bytes");
 return a;
 }
namespace {
// WHJ4 PE 0x591df0 expands the earlier 0xa2 key to 0x1b8.
constexpr auto wideKeyOffsets=[]{
 std::array<uint16_t,0xa2> offsets{};
 constexpr std::pair<unsigned,unsigned> fields[]={
  {4,4},{5,8},{6,0x24},{7,0x28},{0x8,0x50},{0xa,0x54},{0xc,0x58},{0xe,0x5c},
  {0x10,0x34},{0x14,0x38},{0x18,0x84},{0x1a,0x88},{0x1c,0x90},{0x1e,0x94},
  {0x20,0xa8},{0x22,0xa4},{0x24,0xbc},{0x28,0xc0},{0x2d,0x48},{0x2e,0x4c},
  {0x30,0x7c},{0x31,0x80},{0x33,0xb4},{0x34,0xd0},{0x35,0xc4},{0x37,0xc8},
  {0x3a,0xcc},{0x3b,0xb8},{0x3c,0x2c},{0x3d,0x30},
  {0x40,0x60},{0x42,0x64},{0x44,0x68},{0x46,0x6c},{0x48,0x9c},{0x4a,0xa0},{0x4d,0x8c},
  {0x81,0x3c},{0x83,0x40},{0x85,0x70},{0x87,0x74},{0x89,0xd4},{0x8b,0xd8},
  {0x8d,0xdc},{0x8f,0xe0},{0x91,0x0c},{0x93,0xb0},{0x96,0x98}
 };
 for(const auto&field:fields)offsets[field.first]=uint16_t(field.second);
 for(unsigned wave=0;wave<4;++wave){
  const auto from=0x51+12*wave,to=0xe4+32*wave;
  for(unsigned i=0;i<4;++i){
   offsets[from+i]=uint16_t(to+4*i);
   offsets[from+4+2*i]=uint16_t(to+16+4*i);
  }
 }
 return offsets;
}();

bool wideKey(const qlie::Bytes&key){return key.size()==0x1b8||key.size()==0x1cc;}
uint32_t keyU32(const qlie::Bytes&k,size_t p){
 if(wideKey(k)&&p){
  p=wideKeyOffsets.at(p);
  if(!p)throw std::runtime_error("Unmapped wide animation key field");
 }
 if(p+4>k.size())throw std::runtime_error("Animation field outside key");
 return uint32_t(k[p])|(uint32_t(k[p+1])<<8)|(uint32_t(k[p+2])<<16)|(uint32_t(k[p+3])<<24);
}
unsigned keyCode(const qlie::Bytes&key,size_t offset){
 return wideKey(key)?keyU32(key,offset):key.at(offset);
}
int numericReference(int value){
 if(value>=0x10000000&&value<0x10000010)return 32700+value-0x10000000;
 if(value>=0x10000100&&value<0x10000110)return 32720+value-0x10000100;
 return value;
}
int keyI16(const qlie::Bytes&k,size_t p){
 if(wideKey(k))return numericReference(int32_t(keyU32(k,p)));
 if(p+2>k.size())throw std::runtime_error("Animation field outside key");
 return int16_t(uint16_t(k[p])|(uint16_t(k[p+1])<<8));
}
float animationSine(int phase){
 static const auto sine=[]{
  std::array<float,1024> table{};
  for(size_t i=0;i<table.size();++i)table[i]=float(std::sin(i*2.0*3.14159265358979323846/1024));
  return table;
 }();
 return sine[phase&1023];
}
// WHJ1 0x4a29f8 blends a four-key cubic B-spline with linear or Hermite interpolation.
float sampleSpline(float previous,float a,float b,float following,float power,double t){
 const double u=1-t,t2=t*t,t3=t2*t;
 const double spline=(u*u*u*previous+(3*t3-6*t2+4)*a+
                      (1+3*(t+t2-t3))*b+t3*following)/6;
 if(power<=1)return float(spline*power+(a+(b-a)*t)*(1-power));
 const double tangentScale=a==b?.25:1.;
 const double hermite=a==b&&previous==following?a:
  (1+2*t)*u*u*a+(3-2*t)*t2*b+t*u*u*(b-previous)*tangentScale-
  t2*u*(following-a)*tangentScale;
 return float((2-power)*spline+(power-1)*hermite);
}
// PE offsets efcb0/efcd2 select the position/scale mode at 0x30.
// f0860/f0882 use 0x33 for color/alpha. a1bb0/a1c38 use the
// exponent 1 + 1.4 * power; these are not fixed quartic easing curves.
float sampleValue(float a,float b,unsigned mode,float power,double t,std::vector<std::string>&warnings,uint32_t&randomCursor){
 if(a==b||t<=0)return a;
 if(t>=1)return b;
 double q=0,offset=0;
 switch(mode){case 0:return a;
 case 1:q=t;
 break;
 case 3:q=1-std::pow(1-t,1+1.4*power);
 break;
 case 4:q=std::pow(t,1+1.4*power);
 break;
 case 6:{
  // WHJ5 0x5aa9ea adds a decaying random offset to the ease-out value.
  static const auto randomTable=particleRandomTable();
  randomCursor=(randomCursor+1)&1023;
  const float spread=float(static_cast<long double>(randomTable[randomCursor]%0xffffffu)
                           *float(40*power)/16777215.L);
  offset=(spread-20*power)*float(1-t);
  q=1-std::pow(1-t,1+1.4*power);
  break;
 }
 case 7:{
  // WHJ5 0x4fcff4 joins ease-out and ease-in at the midpoint.
  const double exponent=1+1.4*power;
  q=t<.5?.5*(1-std::pow(1-t*2,exponent)):.5+.5*std::pow(t*2-1,exponent);
  break;
 }
 case 8:{
  const double exponent=1+1.4*power;
  q=t<.5?.5*std::pow(t*2,exponent):1-.5*std::pow((1-t)*2,exponent);
  break;
 }
 case 17:case 18:{
  // WHJ5 0x4fd538 reverses the damped cosine used by 0x4fd4c8.
  const double progress=mode==18?float(1.f-float(t)):t;
  const float angle=float(3.14159265L*float(progress));
  const int phase=int(static_cast<long double>(angle)*512*0.3183098865475128L);
  const double cosinePart=(1-progress)*animationSine(phase+256);
  q=mode==18?cosinePart:1-cosinePart;
  break;
 }
 default:warnings.push_back("easing="+std::to_string(mode));
 return a;
 }
 return float(a+(b-a)*q+offset);
}
}
// PE e6e38/e6f74: control event8 starts a sound, event11 stops a channel.
std::vector<AnimationSoundEvent> animationSoundEvents(const Motion&motion,double previous,double current){
 std::vector<AnimationSoundEvent> out;
 for(const auto&layer:motion.layers){if(!layer.flags)continue;
  for(const auto&key:layer.keys){double time=keyU32(key,0);if(time<=previous||time>current||key.size()<0x91)continue;
   unsigned event=keyCode(key,0x34);if(event!=8&&event!=11)continue;
   int channel=keyI16(key,event==11?0x89:0x8f);if(channel<0)continue;
   AnimationSoundEvent sound;sound.stop=event==11;sound.channel=unsigned(channel);
   if(!sound.stop){int index=keyI16(key,0x89);if(index<0)continue;sound.resource=unsigned(index);
    int volume=keyI16(key,0x8b);sound.gain=std::clamp((volume?volume:256)/256.f,0.f,1.f);sound.loop=keyI16(key,0x8d)!=0;
   }out.push_back(sound);
  }
 }return out;
}
AnimationSample sampleAnimation(const AnimationLayer&layer,double ms,const AnimationContext&context){
 AnimationSample out;
 if(layer.keys.empty()||ms<keyU32(layer.keys.front(),0))return out;
 // PE e6d19: direct event-1 jumps to key[0x89], preserving overshoot.
 for(const auto&key:layer.keys)if(keyCode(key,0x34)==1&&ms>=keyU32(key,0)){
  if(keyI16(key,0x8d)!=0){out.unsupported.push_back("conditional-key-jump");break;}
  int target=keyI16(key,0x89);if(target<0||size_t(target)>=layer.keys.size()){out.unsupported.push_back("invalid-key-jump");break;}
  const auto& destination=layer.keys[size_t(target)];
  double start=keyU32(destination,0),end=keyU32(key,0);
  // A jump to the current key holds it instead of reaching later padding keys.
  if(&destination==&key){ms=end;break;}
  if(start>=end){out.unsupported.push_back("nonbackward-key-jump");break;}
  ms=start+std::fmod(ms-start,end-start);break;
 }
 size_t i=0;
 while(i+1<layer.keys.size()&&ms>=keyU32(layer.keys[i+1],0))i++;
 const auto&a=layer.keys[i];
 const auto&b=layer.keys[std::min(i+1,layer.keys.size()-1)];
 double span=double(keyU32(b,0))-keyU32(a,0),t=span>0?std::clamp((ms-keyU32(a,0))/span,0.,1.):0;
 size_t mediaStart=i;while(mediaStart>0&&keyCode(layer.keys[mediaStart-1],4)==keyCode(a,4)&&keyCode(layer.keys[mediaStart-1],6)==keyCode(a,6))mediaStart--;
 out.mediaTime=ms-keyU32(layer.keys[mediaStart],0);
 out.kind=keyCode(a,4);
 out.flags=keyCode(a,5);
 out.resource=keyCode(a,6);
 auto number=[&](const qlie::Bytes&key,size_t offset){
  int v=keyI16(key,offset);
  if(v>=32700&&v<32716&&(context.externalMask&(1u<<(v-32700))))return context.external[size_t(v-32700)];
  switch(v){case 32727:if(context.imageWidth!=UINT32_MAX)return float(context.imageWidth);break;
   case 32728:if(context.imageHeight!=UINT32_MAX)return float(context.imageHeight);break;
   case 32729:if(context.targetWidth!=UINT32_MAX)return float(context.targetWidth);break;
   case 32730:if(context.targetHeight!=UINT32_MAX)return float(context.targetHeight);break;default:break;}
  if((v>=32700&&v<32716)||(v>=32720&&v<32736))out.unsupported.push_back("numeric-context="+std::to_string(v));
  return float(v);
 };
 uint32_t localRandomCursor=context.randomSeed&1023;
 auto&randomCursor=context.randomCursor?*context.randomCursor:localRandomCursor;
 const auto&previous=layer.keys[i?i-1:i];
 const auto&following=layer.keys[std::min(i+2,layer.keys.size()-1)];
 auto interpolate=[&](const auto& read,unsigned mode,float power){
  if(mode==2)return sampleSpline(read(previous),read(a),read(b),read(following),power,t);
  return sampleValue(read(a),read(b),mode,power,t,out.unsupported,randomCursor);
 };
 auto val=[&](size_t offset,unsigned mode,float power){
  return interpolate([&](const qlie::Bytes& key){return number(key,offset);},mode,power);
 };
 out.x=val(0x18,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 out.y=val(0x1a,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 out.scaleX=val(0x1c,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 out.scaleY=val(0x1e,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 out.priority=val(0x20,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 out.rotationZ=val(0x22,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 // PE f1cb8/f1cc0 and e6324 preserve filter1(point)/2(linear).
 if(a.size()>0x91)out.textureFilter=keyCode(a,0x91);
 // PE f1ce9/f1cf1: packed 0x93 becomes runtime rotation order +0xa9.
 if(a.size()>0x93)out.rotationOrder=keyCode(a,0x93);
 out.rotationX=val(0x48,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 out.rotationY=val(0x4a,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 if(a.size()>=0x98&&b.size()>=0x98)out.scaleZ=val(0x96,keyCode(a,0x30),keyCode(a,0x31)/100.f);
 auto signedField=[&](size_t offset,unsigned mode,float power){
  return interpolate([&](const qlie::Bytes& key){return float(int32_t(keyU32(key,offset)));},mode,power);
 };
 out.z=signedField(0x4d,keyCode(a,0x30),keyCode(a,0x31)/100.f);

 const auto colorMode=keyCode(a,0x33);
 const float colorPower=keyCode(a,0x3b)/100.f;
 out.alpha=val(0x35,colorMode,colorPower);
 if(colorMode==2)out.alpha=std::clamp(std::round(out.alpha),0.f,255.f);
 if(out.kind==10){
  auto input=[&](size_t offset){return val(offset,keyCode(a,0x2d),keyCode(a,0x2e)/100.f);};
  out.mask.sourceX=input(8);out.mask.sourceY=input(0xa);out.mask.width=input(0xc);out.mask.height=input(0xe);
  out.mask.x=input(0x40);out.mask.y=input(0x42);out.mask.maskWidth=input(0x44);out.mask.maskHeight=input(0x46);
  out.mask.alignX=number(a,0x81)/100;out.mask.alignY=number(a,0x83)/100;
  out.mask.mode=keyCode(a,0x3a);
 }
 if(out.kind==9||out.kind==10){
  out.mask.amount=uint8_t(std::clamp(int(std::round(val(0x37,colorMode,colorPower))),0,255));
  out.mask.crossfade=out.kind==9;
  out.childMotion=keyCode(a,7);
 }
 if(out.kind==0||out.kind==4||out.kind==20||out.kind==21){
  auto timeValue=[&](const qlie::Bytes&key,size_t offset){
   auto value=numericReference(int32_t(keyU32(key,offset)));
   if(value>=32700&&value<32716&&(context.externalMask&(1u<<(value-32700))))return context.external[size_t(value-32700)];
   if((value>=32700&&value<32716)||(value>=32720&&value<32736)){
    out.childTimeResolved=false;out.unsupported.push_back("child-time-context="+std::to_string(value));
   }
   return float(value);
  };
  out.childTime=int32_t(interpolate([&](const qlie::Bytes& key){return timeValue(key,0x10);},keyCode(a,0x3c),keyCode(a,0x3d)/100.f));
  out.childMotion=keyCode(a,7);
 }
 if((out.kind==20||out.kind==21)&&a.size()<152)out.unsupported.push_back("particle-layout="+std::to_string(layer.version));
 // Generic group records carry these fields too (PE f6de0..f6e18).
 if(a.size()>=152){
  auto particleValue=[&](size_t offset){return val(offset,keyCode(a,0x2d),keyCode(a,0x2e)/100.f);};
  out.particles.count=particleValue(8);out.particles.edge=particleValue(0xa);
  out.particles.divisions=particleValue(0xc);out.particles.scaleOffset=particleValue(0x46);
  for(unsigned axis=0;axis<3;axis++)out.particles.phase[axis]=particleValue(0x40+axis*2);
  out.particles.extent={out.scaleX,out.scaleY,val(0x96,keyCode(a,0x30),keyCode(a,0x31)/100.f)};
  auto childTime=[&](size_t offset){return int32_t(signedField(offset,keyCode(a,0x3c),keyCode(a,0x3d)/100.f));};
  out.particles.timeBegin=childTime(0x10);out.particles.timeEnd=childTime(0x14);
 }
 out.anchorX=number(a,0x85);
 out.anchorY=number(a,0x87);
 auto color=[&](size_t p){uint32_t z=0;
 for(unsigned shift=0;shift<24;shift+=8){
 auto v=interpolate([&](const qlie::Bytes& key){return float((keyU32(key,p)>>shift)&255);},colorMode,colorPower);
 z|=uint32_t(std::clamp(std::lround(v),0l,255l))<<shift;
 }return z;
 };
 out.addColor=color(0x24);
 out.mulColor=color(0x28);
 // PE f1468 interpolates four signed parameters before invoking the
 // waveform and property tables. Time is relative to this key (f15ed).
 for(unsigned n=0;n<4;n++){
  size_t q=0x51+12*n;unsigned op=keyCode(a,q),target=keyCode(a,q+1);
  if(!op||!target)continue;
  float p[4];
  for(unsigned j=0;j<4;j++)p[j]=val(q+4+j*2,keyCode(a,q+2),keyCode(a,q+3)/100.f);
  float value=0;bool known=true;
  auto trig=[](float radians,bool cosine){
   // a18c4 quantizes the phase to the engine's 1024-entry sine table.
   int phase=int(radians*512.f/3.14159265f)+(cosine?256:0);
   return animationSine(phase);
  };
  if(op==1){
   if(!context.randomCursor){out.unsupported.push_back("sequential-random-context");continue;}
   static const auto randomTable=particleRandomTable();
   auto&cursor=*context.randomCursor;cursor=(cursor+1)&1023;
   const unsigned number=randomTable[cursor]%0xffffffu;
   const float scaled=float(static_cast<long double>(number)*float(p[1]-p[0])/16777215.L);
   value=p[0]+scaled;
  }
  else if(op==5){
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
   // WHJ1 PE 4f0949/4f096c/4f098f add the waveform to the Z/X/Y angles.
   // The native fields store turns. AnimationSample stores degrees.
   case 17:out.rotationZ+=value;break;
   case 18:out.rotationX+=value;break;
   case 19:out.rotationY+=value;break;
   case 21:out.priority+=value;break;
   case 24:out.alpha=std::clamp(out.alpha+std::round(value),0.f,255.f);break;
   case 28:out.scaleX*=value/100;break;case 29:out.scaleY*=value/100;break;
   default:out.unsupported.push_back("procedural-target="+std::to_string(target));break;
  }
 }
 if(keyI16(a,0x48)||keyI16(a,0x4a))out.unsupported.push_back("3d-rotation");
 if(out.kind==4&&(out.flags&0x7f)>8)out.unsupported.push_back("bitmap-blend="+std::to_string(out.flags&0x7f));
 if(out.kind==2)out.scaleX=out.scaleY=out.scaleZ=100;
 if(out.kind!=0&&out.kind!=1&&out.kind!=2&&out.kind!=3&&out.kind!=4&&out.kind!=5&&out.kind!=6&&out.kind!=9&&out.kind!=10&&out.kind!=15&&out.kind!=16&&out.kind!=17&&out.kind!=18)out.unsupported.push_back("primitive="+std::to_string(out.kind));
 if(layer.mode>2)out.unsupported.push_back("layer-mode="+std::to_string(layer.mode));
 return out;
}

// Original PE ea267/ea29c: push before applying the top state, pop after.
// f6cfc/f6e5c add translation/rotation, multiply scale, saturate additive
// color and multiply tint/alpha. The stack is reset for each motion draw.
std::vector<AnimationSample> sampleMotion(const Motion&motion,double ms,unsigned width,unsigned height,bool applyRenderState,const AnimationContext&context){
 if(motion.loopEnd>motion.loopStart&&ms>=motion.loopEnd)
  ms=motion.loopStart+std::fmod(ms-motion.loopStart,motion.loopEnd-motion.loopStart);
 std::vector<AnimationSample> result,stack;
 result.reserve(motion.layers.size());
 auto compose=[](AnimationSample value,const AnimationSample&parent){
  value.x+=parent.x;value.y+=parent.y;value.z+=parent.z;
  if(value.kind!=3){value.scaleX*=parent.scaleX/100;value.scaleY*=parent.scaleY/100;value.scaleZ*=parent.scaleZ/100;}
  value.rotationX+=parent.rotationX;value.rotationY+=parent.rotationY;value.rotationZ+=parent.rotationZ;value.priority+=parent.priority;
  value.childTime+=parent.childTime;
  value.particles.count+=parent.particles.count;value.particles.edge+=parent.particles.edge;
  value.particles.divisions+=parent.particles.divisions;value.particles.scaleOffset+=parent.particles.scaleOffset;
  for(unsigned axis=0;axis<3;axis++)value.particles.phase[axis]+=parent.particles.phase[axis];
  value.particles.timeBegin+=parent.particles.timeBegin;value.particles.timeEnd+=parent.particles.timeEnd;
  value.particles.extent={value.scaleX,value.scaleY,value.scaleZ};
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
  if(layer.flags&&!layer.keys.empty()&&ms>=keyU32(layer.keys.front(),0)){
   if(layer.mode==1&&stack.size()<31){
    auto parent=value;
    // The push operation always multiplies scale, including primitive 3.
    if(!stack.empty()){
     parent=compose(value,stack.back());
     parent.scaleX=value.scaleX*stack.back().scaleX/100;
     parent.scaleY=value.scaleY*stack.back().scaleY/100;
     parent.scaleZ=value.scaleZ*stack.back().scaleZ/100;
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
  if(value.kind==15){renderStack.clear();continue;}
  if(value.kind==17){if(!renderStack.empty())renderStack.pop_back();continue;}
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
