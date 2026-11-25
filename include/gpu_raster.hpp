// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include <memory>
#include <optional>
struct KVideoSurface;
namespace whj2 {
namespace gpu { struct Scene; }
class GpuRaster:public RenderTarget {
 std::unique_ptr<gpu::Scene> impl;
 bool fieldRequested=false,nextField=false,forcedField=false;
 void drawImage(const Image&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned);
protected:
 explicit GpuRaster(std::unique_ptr<gpu::Scene>);
public:
 ~GpuRaster() override;
 bool nativePresentation()const;
 void setInterlaced(bool);
 void requestFieldPresentation()override{fieldRequested=true;}
 void begin(const qlie::Bytes&);void end(qlie::Bytes&);
 void begin(uint32_t clearColor);
 void beginVideo(const Image&);
 bool supportsVideoSurfaces()const override;
 bool beginVideoSurface(const KVideoSurface&);
 void finishPresentation();
 std::optional<uint64_t> present(bool refreshUnchanged=true);std::optional<uint64_t> presentPixels(const qlie::Bytes&);
 void beginBatch()override;void endBatch()override;
 bool supportsMasks()const override{return true;}
 void maskedImage(const Image&,const Image&,const AnimationSample::Mask&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)override;
 void image(const Image&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)override;
 void triangle(const Image&,const std::array<TextureVertex,3>&,const Affine2D&,float,uint32_t,uint32_t,unsigned,unsigned)override;
 void font(const Image&,int,int)override;
 void dim(unsigned,unsigned)override;
 void transition(const Image&,const Image*,double,unsigned)override;
};
}
