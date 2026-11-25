#pragma once
#include "qlie.hpp"
#include "particles.hpp"
#include <array>
#include <optional>
namespace whj2 {
std::optional<double> animationProgress(unsigned mode,double power,double fraction);
struct AnimationLayer {std::string name;unsigned version;uint8_t flags,mode;std::vector<qlie::Bytes> keys;};
struct Motion {std::string name;std::vector<AnimationLayer> layers;int32_t loopStart=0,loopEnd=0;};
struct Animation {
 std::string name,comment;
 std::array<int32_t,4> bounds{};
 uint32_t flags=0;
 std::vector<Motion> motions;
 bool loopStartFirst=false;
};
struct AnimationContext {unsigned imageWidth=UINT32_MAX,imageHeight=UINT32_MAX,targetWidth=UINT32_MAX,targetHeight=UINT32_MAX;uint32_t randomSeed=0;std::array<float,16> external{};uint16_t externalMask=0;uint32_t* randomCursor=nullptr;};
struct Affine2D {float a=1,b=0,c=0,d=1,x=0,y=0;};
struct AnimationSample {
 Affine2D screen;
 ParticleParameters particles;
 struct Mask {
  float sourceX=0,sourceY=0,width=0,height=0,x=0,y=0,maskWidth=0,maskHeight=0,alignX=0,alignY=0;
  uint8_t amount=0,mode=0;
  bool crossfade=false;
 } mask;
 double mediaTime=0;int32_t childTime=0;unsigned childMotion=0;bool childTimeResolved=true;
 float z=0,scaleZ=100,rotationX=0,rotationY=0;unsigned rotationOrder=0,textureFilter=0;
 unsigned kind=0,flags=0,resource=0;float x=0,y=0,scaleX=100,scaleY=100,rotationZ=0,priority=0,anchorX=50,anchorY=50,alpha=255;
 uint32_t addColor=0,mulColor=0xffffff;std::vector<std::string> unsupported;
};
struct AnimationSoundEvent {bool stop=false,loop=false;unsigned channel=0,resource=0;float gain=1;};
std::vector<AnimationSoundEvent> animationSoundEvents(const Motion&,double previous,double current);
AnimationSample sampleAnimation(const AnimationLayer&,double milliseconds,const AnimationContext& context={});
std::vector<AnimationSample> sampleMotion(const Motion&,double milliseconds,unsigned width=1280,unsigned height=720,bool applyRenderState=true,const AnimationContext& context={});
double nestedAnimationTime(const Animation&,double milliseconds);
Animation animation(const std::string&version,const qlie::Bytes&);
}
