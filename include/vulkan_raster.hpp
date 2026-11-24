// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include <memory>
namespace whj2 {
class VulkanRaster final:public RenderTarget {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 VulkanRaster();~VulkanRaster();
 bool nativePresentation()const;
 void begin(const qlie::Bytes&);void end(qlie::Bytes&);
 void present();void presentPixels(const qlie::Bytes&);
 void image(const Image&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)override;
 void triangle(const Image&,const std::array<TextureVertex,3>&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)override;
};
}
