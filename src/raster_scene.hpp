// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu_raster.hpp"
#include "swap_damage.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

namespace whj2::gpu {
struct Push {float p[4][4]{};int32_t rect[4]{};uint32_t meta[4]{},colors[4]{};};
static_assert(sizeof(Push)==112,"Shader push layout");
struct DrawData {Push image;float maskSource[4]{},maskRect[4]{};uint32_t maskMeta[4]{};};
static_assert(sizeof(DrawData)==160,"Shader draw layout");
constexpr unsigned Tile=SwapDamage::Tile, Width=SwapDamage::Width, Height=SwapDamage::Height, Columns=Width/Tile, Rows=Height/Tile;
constexpr unsigned TileCount=Columns*Rows;
constexpr size_t CanvasBytes=size_t(Width)*Height*sizeof(uint32_t);
using Clock=std::chrono::steady_clock;
constexpr unsigned FrameTraceInterval=12;
inline double milliseconds(Clock::duration elapsed){return std::chrono::duration<double,std::milli>(elapsed).count();}

struct DrawSources {
 uint64_t primary=0,secondary=0;
 bool opaque=false;
 uint32_t singlePixel=0;
 bool monochrome=false;
#ifdef QLIE_PROFILE_WHJ6
 std::shared_ptr<const ImageAlphaTiles> alphaTiles{};
#endif
};
struct AxisBounds {
 float left,top,right,bottom;
 bool covers(unsigned x,unsigned y,unsigned w,unsigned h)const{
  return left<=float(x)&&top<=float(y)&&right>=float(x+w)&&bottom>=float(y+h);
 }
};
inline AxisBounds axisBounds(const Push&p){
 // A rotated or projected draw cannot use its bounding box as opaque coverage.
 if(p.meta[2]&(1u<<16)||p.p[0][1]!=0||p.p[0][2]!=0)return {};
 float x=p.p[1][0],y=p.p[1][1],right=x+p.p[0][0]*p.meta[0],bottom=y+p.p[0][3]*p.meta[1];
 return {std::min(x,right),std::min(y,bottom),std::max(x,right),std::max(y,bottom)};
}
inline bool coversCanvasWithSolid(const Push&p){
 return (p.meta[2]>>20)==0&&!(p.meta[2]&262144u)&&p.meta[0]==1&&p.meta[1]==1&&
        axisBounds(p).covers(0,0,Width,Height);
}
inline bool replacesWithSolid(const Push&p,uint32_t texel){
 return (texel>>24)==255&&p.meta[3]==255&&((p.meta[2]>>8)&255)<2;
}
inline uint32_t overClear(const Push&p,uint32_t texel,uint32_t background){
 unsigned a=(texel>>24)*p.meta[3]/255,mode=(p.meta[2]>>8)&255;
 if(!a)return background;
 uint32_t color=0;
 for(unsigned c=0;c<4;++c){
  unsigned shift=8*c,src=a;
  if(c<3)src=std::min(255u,((texel>>shift)&255)*((p.colors[1]>>shift)&255)/255+((p.colors[0]>>shift)&255));
  color|=uint32_t(blendChannel(mode,uint8_t(src),uint8_t(background>>shift),uint8_t(a)))<<shift;
 }
 return color;
}
struct RasterFrame {
 std::vector<DrawData> commands;
 std::vector<DrawSources> sources;
 std::array<std::vector<uint32_t>,TileCount> tiles;
 SwapDamage::Mask covered;
 bool geometry=false,cleared=false;
 uint32_t color=0,alignedBitmap=UINT32_MAX;

 void reset(bool clear,uint32_t background){
  commands.clear();
  sources.clear();
  for(auto&tile:tiles)tile.clear();
  covered.reset();
  geometry=false;cleared=clear;
  color=background;alignedBitmap=UINT32_MAX;
 }
 bool sameDraw(size_t i,const RasterFrame&old,size_t j)const{
  return sources[i].primary==old.sources[j].primary&&sources[i].secondary==old.sources[j].secondary&&
         std::memcmp(&commands[i],&old.commands[j],sizeof(DrawData))==0;
 }
};
// Command encoding, texture lifetimes and tile history are independent of the device API.
class FrameTrace;
struct Scene {
 explicit Scene(const char* name,bool backgroundTrace=false);
 virtual ~Scene();
 const char* const backendName;
 struct Span {size_t offset=0,size=0;};
 struct Texture {
  Span storage;unsigned width=0,height=0;
  uint64_t lastUsed=0,checkedBatch=0,pixelVersion=0,uploadId=0;
  qlie::Bytes unversionedCopy;
  std::optional<bool> opaque,monochrome;
 };

 // Compare a replacement full-screen bitmap once when its uploaded pixels
 // change. Moving, filtering and blending still run entirely on the GPU.
 bool compareAlignedBitmaps=false;
 qlie::Bytes alignedPixels;
 uint64_t alignedUpload=0,priorAlignedUpload=0;
 SwapDamage::Mask equalBitmapTiles;
 void rememberAlignedBitmap(const Image&,uint64_t);
 bool canReuseBitmapTiles()const;
 bool uploadBase=false;
 std::map<const Image*,Texture>textures;std::vector<Span>retired;std::map<size_t,size_t>freeSpans;size_t textureEnd=0;
 RasterFrame frameA,frameB;
 RasterFrame*current=&frameA,*previous=&frameB;
 std::array<uint32_t,TileCount> cachedPrefix{};
 std::vector<uint8_t> unchangedDraw;
 std::vector<std::array<uint32_t,4>> workTiles;
 std::vector<uint32_t> workIndices;
 std::vector<Span> pendingReclaim;
 uint64_t nextUploadId=1;
 Clock::time_point cpuFrameStart;
 double priorWaitMs=0,cpuBuildMs=0;
 uint32_t baseCopy=UINT32_MAX;
 unsigned cachedReferences=0,occludedReferences=0,foldedSolids=0;bool reusedDisplay=false,nativeDisplayCurrent=false;
 Image transitionLut,solidTransitionLut;
 double lutAmount=-1,solidLutAmount=-1;
 uint32_t solidLutColor=0;
 uint64_t frames=0,batch=0;unsigned draws=0,dispatches=0,textureChecks=0;size_t uploadBytes=0;bool recording=false,inBatch=false;

 virtual void initialize()=0;
 virtual void waitGpu()=0;
 virtual void uploadTexture(Span,const qlie::Bytes&)=0;
 virtual void uploadCanvas(const qlie::Bytes&)=0;
 virtual void readCanvas(qlie::Bytes&)=0;
 virtual std::optional<uint64_t> presentNative(bool)=0;
 virtual void setInterlaced(bool)=0;
 virtual bool supportsVideoSurfaces()const{return false;}
 virtual bool beginVideoSurface(const KVideoSurface&){return false;}
 virtual std::optional<uint32_t> videoTexture(const KVideoSurface&,uint64_t&,bool,uint32_t* =nullptr){return std::nullopt;}

 void freeSpan(Span);
 Span allocateSpan(size_t);
 uint32_t texture(const Image&,uint64_t& uploadId,bool* opaque=nullptr,bool* monochrome=nullptr,
                  uint32_t* screenCoverage=nullptr);
 void waitForPrevious();
 void completeFrame();
 void traceFrame(const char* format,...) __attribute__((format(printf,2,3)));
 void append(Push,DrawSources={});
 void appendVideo(uint32_t address,uint64_t identity);
 void binDraw(uint32_t);
 void dispatch(const Image&,Push&,float,uint32_t,uint32_t,unsigned,unsigned);
 void beginFrame(bool,uint32_t);
 bool readCoveredSolid(qlie::Bytes&);
 void planTiles();
 SwapDamage::Mask changedTiles(bool interlaced) const;
 void writeTileList(uint32_t*) const;
private:
 std::unique_ptr<FrameTrace> frameTrace;
};
}
