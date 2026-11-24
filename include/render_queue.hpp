// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "animation.hpp"
#include "image.hpp"
#include <deque>
#include <optional>
#include <stdexcept>
namespace whj2 {
Affine2D affine(const Affine2D&parent,const Affine2D&local);
Affine2D imagePlacement(float x,float y,float sx,float sy,float angle,float anchorX,float anchorY,unsigned width,unsigned height);
uint8_t blendChannel(unsigned mode,uint8_t source,uint8_t destination,uint8_t alpha);
uint32_t colorAdd(uint32_t,uint32_t);
uint32_t colorMultiply(uint32_t,uint32_t);
struct TextureVertex {float x=0,y=0,depth=1000,u=0,v=0;};
class RenderTarget {
public:
 virtual ~RenderTarget()=default;
 // A queue keeps its source images immutable for the duration of this batch.
 virtual void beginBatch(){}
 virtual void endBatch(){}
 virtual bool supportsMasks()const{return false;}
 virtual bool supportsVideoSurfaces()const{return false;}
 virtual void maskedImage(const Image&,const Image&,const AnimationSample::Mask&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned){throw std::runtime_error("Render target mask unsupported");}
 virtual void image(const Image&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)=0;
 virtual void triangle(const Image&,const std::array<TextureVertex,3>&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)=0;
 virtual void font(const Image&,int,int){throw std::runtime_error("Render target font unsupported");}
 virtual void dim(unsigned,unsigned){throw std::runtime_error("Render target dim unsupported");}
 virtual void transition(const Image&,const Image*,double,unsigned){throw std::runtime_error("Render target transition unsupported");}
};
class RenderQueue {
 struct Command {const Image*image=nullptr;const Image*mask=nullptr;Affine2D placement;AnimationSample state;float priority=0;std::array<TextureVertex,3> triangle{};bool polygon=false;};
 std::vector<Command> commands;
 std::deque<Image> snapshots;
 std::vector<std::shared_ptr<const Image>> retainedImages;
 size_t visibleImages=0;
 bool shaderMasks=false,nativeVideo=false;
 std::optional<Affine2D> view;
 void drawRows(qlie::Bytes&,unsigned,unsigned,unsigned,unsigned,RenderTarget* =nullptr)const;
public:
 explicit RenderQueue(bool masks=false,bool video=false,const Affine2D* outerView=nullptr)
  :shaderMasks(masks),nativeVideo(video){if(outerView)view=*outerView;}
 bool supportsMasks()const{return shaderMasks;}
 bool supportsVideoSurfaces()const{return nativeVideo;}
 size_t imageCount()const{return visibleImages;}
 void image(const Image&,const Affine2D&,float priority,float alpha,uint32_t add,uint32_t mul,unsigned blend=0,unsigned filter=0);
 void maskedImage(const Image&,const Image&,const AnimationSample::Mask&,const Affine2D&,float,float,uint32_t,uint32_t,unsigned,unsigned);
 const Image& snapshotImage(const Image&);
 const Image& retainImage(std::shared_ptr<const Image>);
 void snapshot(const Image&,const Affine2D&,float priority,float alpha,uint32_t add,uint32_t mul,unsigned blend=0,unsigned filter=0);
 void triangle(const Image&,const std::array<TextureVertex,3>&,float priority,float alpha,uint32_t add,uint32_t mul,unsigned blend=0,unsigned filter=0);
 void control(const AnimationSample&,float priority);
 bool captureAlignedBitmap(qlie::Bytes&,unsigned width,unsigned height,uint32_t background)const;
 void draw(qlie::Bytes&,unsigned width,unsigned height)const;
 void draw(RenderTarget&,unsigned width,unsigned height)const;
};
}
