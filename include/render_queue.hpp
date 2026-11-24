// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "animation.hpp"
#include "image.hpp"
#include <deque>
namespace whj2 {
Affine2D affine(const Affine2D&parent,const Affine2D&local);
Affine2D imagePlacement(float x,float y,float sx,float sy,float angle,float anchorX,float anchorY,unsigned width,unsigned height);
uint8_t blendChannel(unsigned mode,uint8_t source,uint8_t destination,uint8_t alpha);
uint32_t colorAdd(uint32_t,uint32_t);
uint32_t colorMultiply(uint32_t,uint32_t);
class RenderQueue {
 struct Command {const Image*image=nullptr;Affine2D placement;AnimationSample state;float priority=0;};
 std::vector<Command> commands;
 std::deque<Image> snapshots;
public:
 void image(const Image&,const Affine2D&,float priority,float alpha,uint32_t add,uint32_t mul,unsigned blend=0);
 void snapshot(const Image&,const Affine2D&,float priority,float alpha,uint32_t add,uint32_t mul,unsigned blend=0);
 void control(const AnimationSample&,float priority);
 void draw(qlie::Bytes&,unsigned width,unsigned height)const;
};
}
