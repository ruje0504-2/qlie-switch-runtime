#pragma once
#include "qlie.hpp"
#include <array>
namespace whj2 {
struct AnimationLayer {std::string name;unsigned version;uint8_t flags,mode;std::vector<qlie::Bytes> keys;};
struct Motion {std::string name;std::vector<AnimationLayer> layers;};
struct Animation {std::string name,comment;std::array<int32_t,4> bounds;uint32_t flags=0;std::vector<Motion> motions;};
struct AnimationContext {unsigned imageWidth=0,imageHeight=0,targetWidth=0,targetHeight=0;};
struct Affine2D {float a=1,b=0,c=0,d=1,x=0,y=0;};
struct AnimationSample {
 Affine2D screen;
 unsigned kind=0,flags=0,resource=0;float x=0,y=0,scaleX=100,scaleY=100,rotationZ=0,priority=0,anchorX=50,anchorY=50,alpha=255;
 uint32_t addColor=0,mulColor=0xffffff;std::vector<std::string> unsupported;
};
AnimationSample sampleAnimation(const AnimationLayer&,double milliseconds,const AnimationContext& context={});
std::vector<AnimationSample> sampleMotion(const Motion&,double milliseconds,unsigned width=1280,unsigned height=720,bool applyRenderState=true,const AnimationContext& context={});
Animation animation(const std::string&version,const qlie::Bytes&);
}
