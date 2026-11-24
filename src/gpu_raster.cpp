// SPDX-License-Identifier: GPL-2.0-or-later
#include "raster_scene.hpp"
#include <cmath>
#include <cstdio>
#include <utility>

namespace whj2::gpu {
void Scene::freeSpan(Span span){
  auto next=freeSpans.lower_bound(span.offset);
  if(next!=freeSpans.begin()){auto previous=std::prev(next);if(previous->first+previous->second==span.offset){span.offset=previous->first;span.size+=previous->second;freeSpans.erase(previous);}}
  if(next!=freeSpans.end()&&span.offset+span.size==next->first){span.size+=next->second;freeSpans.erase(next);}
  freeSpans.emplace(span.offset,span.size);
 }
 // Only the main thread edits the arena or its free list. Recorded spans are
 // reclaimed after the previous submission's fence, including on early exit.
void Scene::waitForPrevious(){
  waitGpu();
  for(auto span:pendingReclaim)freeSpan(span);
  pendingReclaim.clear();
 }
void Scene::completeFrame(){
  ++frames;
  for(auto it=textures.begin();it!=textures.end();){
   if(it->second.lastUsed==frames){++it;continue;}
   retired.push_back(it->second.storage);
   it=textures.erase(it);
  }
  pendingReclaim.swap(retired);
  std::swap(previous,current);
  if(frames==1||frames%120==0){
   std::fprintf(stderr,"%s FRAME %llu draws=%u dispatches=%u dirty_tiles=%zu/%u cached_prefix_refs=%u textures=%zu texture_checks=%u upload_bytes=%zu cpu_build_ms=%.3f prior_wait_ms=%.3f reused_display=%d ordered_geometry=%d folded_solids=%u occluded_tile_refs=%u direct_base=%d\n",
                backendName,(unsigned long long)frames,draws,dispatches,workTiles.size(),TileCount,cachedReferences,textures.size(),textureChecks,uploadBytes,cpuBuildMs,priorWaitMs,int(reusedDisplay),int(previous->geometry),foldedSolids,occludedReferences,int(baseCopy!=UINT32_MAX));
  }
 }
Scene::Span Scene::allocateSpan(size_t size){
  for(auto it=freeSpans.begin();it!=freeSpans.end();++it)if(it->second>=size){Span span{it->first,size};size_t rest=it->second-size;freeSpans.erase(it);if(rest)freeSpans.emplace(span.offset+size,rest);return span;}
  Span span{textureEnd,size};textureEnd+=size;return span;
 }
uint32_t Scene::texture(const Image&i,uint64_t&uploadId,bool*opaque){
  const size_t bytes=i.pixels.size();
  if(bytes!=size_t(i.width)*i.height*4||!bytes)throw std::runtime_error("GPU texture dimensions");
  const uint64_t version=i.version.get();
  auto at=textures.find(&i);
  if(at!=textures.end()){
   auto&t=at->second;
   bool same=inBatch&&t.checkedBatch==batch;
   if(!same&&t.width==i.width&&t.height==i.height){
    if(version){same=t.pixelVersion==version;}
    else if(t.unversionedCopy.size()==bytes){
     ++textureChecks;
     same=std::memcmp(t.unversionedCopy.data(),i.pixels.data(),bytes)==0;
    }
   }
   if(!same){
    waitForPrevious();
    if(t.lastUsed==frames+1)retired.push_back(t.storage);else freeSpan(t.storage);
    textures.erase(at);at=textures.end();
   }
  }
  if(at==textures.end()){
   // A previous command buffer may still read the same arena or a retired span.
   waitForPrevious();
   Texture t;t.width=i.width;t.height=i.height;t.storage=allocateSpan(bytes);
   t.uploadId=nextUploadId++;t.pixelVersion=version;
   if(!version)t.unversionedCopy=i.pixels;
   uploadTexture(t.storage,i.pixels);
   uploadBytes+=bytes;
   at=textures.emplace(&i,std::move(t)).first;
  }
  auto&t=at->second;t.lastUsed=frames+1;t.checkedBatch=batch;uploadId=t.uploadId;
  if(opaque){
   if(!t.opaque.has_value()){
    bool solid=true;
    for(size_t n=3;n<bytes;n+=4)if(i.pixels[n]!=255){solid=false;break;}
    t.opaque=solid;
   }
   *opaque=*t.opaque;
  }
  return uint32_t(t.storage.offset/sizeof(uint32_t));
 }
void Scene::append(Push p,DrawSources sources){
  if(!recording)throw std::runtime_error("GPU frame not begun");
  p.rect[0]=std::max(0,p.rect[0]);p.rect[1]=std::max(0,p.rect[1]);
  p.rect[2]=std::min(int(Width),p.rect[2]);p.rect[3]=std::min(int(Height),p.rect[3]);
  if(p.rect[2]<=p.rect[0]||p.rect[3]<=p.rect[1])return;
  current->commands.push_back({p,{},{},{}});current->sources.push_back(sources);
  ++draws;
 }
 // Masks are attached after the image is queued. Bin only the final commands.
void Scene::binDraw(uint32_t index){
  const auto&p=current->commands[index].image;
  unsigned op=p.meta[2]>>20;
  bool opaque=op==0&&current->sources[index].opaque&&p.meta[3]==255&&((p.meta[2]>>8)&255)<2&&!(p.meta[2]&262144u);
  auto axis=opaque?axisBounds(p):AxisBounds{};
  unsigned x0=unsigned(p.rect[0])/Tile,x1=unsigned(p.rect[2]-1)/Tile;
  unsigned y0=unsigned(p.rect[1])/Tile,y1=unsigned(p.rect[3]-1)/Tile;
  bool manyTiles=(x1-x0+1)*(y1-y0+1)>4;
  bool cull=(p.meta[2]&65536u)&&manyTiles;
  bool rotatedSprite=p.meta[2]<(1u<<20)&&!(p.meta[2]&65536u)&&(p.p[0][1]!=0||p.p[0][2]!=0);
  bool thinSprite=manyTiles&&rotatedSprite&&float((p.rect[2]-p.rect[0])*(p.rect[3]-p.rect[1]))>2*std::abs(p.p[1][2])*p.meta[0]*p.meta[1];
  struct Edge {float x,y,dx,dy;};
  Edge edges[3]{};
  if(cull){
   for(unsigned j=0;j<3;++j){
    auto&a=p.p[j];auto&b=p.p[(j+1)%3];float dx=b[0]-a[0],dy=b[1]-a[1];
    edges[j]={a[0],a[1],dx,dy};
   }
  }
  for(unsigned y=y0;y<=y1;++y)for(unsigned x=x0;x<=x1;++x){
   bool insideTriangle=cull&&opaque;
   if(cull){
    // Maximum of each linear edge over this tile's pixel centers. A strict
    // negative maximum excludes the entire tile, including top-left ties.
    bool outside=false;
    for(auto&e:edges){
     float px=float(x*Tile)+.5f+(e.dy<0?float(Tile-1):0.f);
     float py=float(y*Tile)+.5f+(e.dx>0?float(Tile-1):0.f);
     float margin=std::abs(e.dx)+std::abs(e.dy);
     if(e.dx*(py-e.y)-e.dy*(px-e.x)<-margin){outside=true;break;}
     if(insideTriangle){
      // The opposite corner minimizes this edge. Keep a pixel of margin for
      // the fixed-point rasterizer before suppressing an earlier layer.
      px=float(x*Tile)+.5f+(e.dy>0?float(Tile-1):0.f);
      py=float(y*Tile)+.5f+(e.dx<0?float(Tile-1):0.f);
      if(e.dx*(py-e.y)-e.dy*(px-e.x)<=margin)insideTriangle=false;
     }
    }
    if(outside)continue;
   }
   if(thinSprite){
    // A rotated rain strip's bounding box may span most of the screen. The
    // extrema of either inverse affine coordinate are attained at corners.
    float umin=INFINITY,umax=-INFINITY,vmin=INFINITY,vmax=-INFINITY;
    for(unsigned n=0;n<4;++n){
     float dx=float(x*Tile+((n&1)?Tile:0))-p.p[1][0],dy=float(y*Tile+((n&2)?Tile:0))-p.p[1][1];
     float u=(p.p[0][3]*dx-p.p[0][1]*dy)/p.p[1][2],v=(-p.p[0][2]*dx+p.p[0][0]*dy)/p.p[1][2];
     umin=std::min(umin,u);umax=std::max(umax,u);vmin=std::min(vmin,v);vmax=std::max(vmax,v);
    }
    if(umax<-1||vmax<-1||umin>float(p.meta[0])+1||vmin>float(p.meta[1])+1)continue;
   }
   auto tile=y*Columns+x;auto&list=current->tiles[tile];
   if(op==4||insideTriangle||(opaque&&axis.covers(x*Tile,y*Tile,Tile,Tile))){
    occludedReferences+=unsigned(list.size());list.clear();current->covered.set(tile);
   }
   list.push_back(index);
  }
 }
void Scene::dispatch(const Image&i,Push&p,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
  if(!recording)throw std::runtime_error("GPU frame not begun");if(!i.width||!i.height||alpha<=0)return;if(blend>8)throw std::runtime_error("GPU unsupported blend");
  if(p.rect[2]<=0||p.rect[3]<=0||p.rect[0]>=int(Width)||p.rect[1]>=int(Height))return;
  p.meta[0]=i.width;p.meta[1]=i.height;p.meta[2]|=(blend<<8)|filter;p.meta[3]=unsigned(std::clamp(alpha,0.f,255.f));p.colors[0]=add;p.colors[1]=mul;DrawSources sources;
  bool mayOcclude=blend<2&&p.meta[3]==255;
  p.colors[2]=texture(i,sources.primary,mayOcclude?&sources.opaque:nullptr);
  if(i.width==1&&i.height==1)std::memcpy(&sources.singlePixel,i.pixels.data(),4);
  append(p,sources);
 }
void Scene::beginFrame(bool clear,uint32_t color){
  if(recording)throw std::runtime_error("GPU frame already begun");
  cpuFrameStart=Clock::now();priorWaitMs=0;
  current->reset(clear,color);
  draws=dispatches=textureChecks=cachedReferences=occludedReferences=foldedSolids=0;baseCopy=UINT32_MAX;uploadBytes=0;
  recording=true;inBatch=false;reusedDisplay=false;uploadBase=false;
 }
void Scene::planTiles(){
  workTiles.clear();workIndices.clear();
  uint32_t first=0;
  if(current->cleared){
   for(uint32_t id=0;id<current->commands.size();++id){
    const auto&p=current->commands[id].image;
    if((p.meta[2]>>20)!=0||(p.meta[2]&262144u)||p.meta[0]!=1||p.meta[1]!=1||!axisBounds(p).covers(0,0,Width,Height))continue;
    uint32_t texel=current->sources[id].singlePixel;
    bool replaces=(texel>>24)==255&&p.meta[3]==255&&((p.meta[2]>>8)&255)<2;
    if(id==first||replaces){current->color=overClear(p,texel,current->color);first=id+1;++foldedSolids;}
   }
  }
  for(uint32_t id=first;id<current->commands.size();++id)binDraw(id);
  bool sameBase=frames&&current->cleared&&previous->cleared&&current->color==previous->color&&current->geometry==previous->geometry;
  if(sameBase){
   unchangedDraw.resize(std::min(current->commands.size(),previous->commands.size()));
   for(size_t i=0;i<unchangedDraw.size();++i)unchangedDraw[i]=current->sameDraw(i,*previous,i);
  }
  for(unsigned tile=0;tile<TileCount;++tile){
   const auto&list=current->tiles[tile];
   const auto&old=previous->tiles[tile];
   uint32_t common=0;
   if(sameBase){
    while(common<list.size()&&common<old.size()){
     uint32_t i=list[common],j=old[common];
     bool matches=i==j?unchangedDraw[i]!=0:current->sameDraw(i,*previous,j);
     if(!matches)break;
     ++common;
    }
    if(common==list.size()&&common==old.size())continue; // final pixels still valid
   }
   uint32_t start=cachedPrefix[tile]<=common?cachedPrefix[tile]:0;
   cachedPrefix[tile]=common;
   if(!current->cleared&&list.empty())continue; // fresh CPU input already contains the complete tile
   uint32_t origin=(tile%Columns)*Tile|(((tile/Columns)*Tile)<<16);
   uint32_t control=(start?0x80000000u:0u)|(common-start);
   workTiles.push_back({origin,uint32_t(workIndices.size()),uint32_t(list.size()-start),control});
   workIndices.insert(workIndices.end(),list.begin()+start,list.end());
   cachedReferences+=start;
  }
 }

SwapDamage::Mask Scene::changedTiles(bool interlaced) const {
 SwapDamage::Mask changed;
 if(interlaced||!current->cleared||workTiles.size()==TileCount)changed.set();
 else for(const auto& h:workTiles){
  unsigned x=h[0]&65535u,y=h[0]>>16;
  changed.set((y/Tile)*Columns+x/Tile);
 }
 return changed;
}
void Scene::writeTileList(uint32_t* words) const {
 const uint32_t headerWords=uint32_t(workTiles.size()*4);
 for(auto header:workTiles){
  header[1]+=headerWords;
  words=std::copy(header.begin(),header.end(),words);
 }
 if(!workIndices.empty())std::memcpy(words,workIndices.data(),workIndices.size()*sizeof(uint32_t));
}

}

namespace whj2 {
using namespace gpu;
GpuRaster::GpuRaster(std::unique_ptr<Scene> scene):impl(std::move(scene)){impl->initialize();}
GpuRaster::~GpuRaster()=default;
void GpuRaster::finishPresentation(){impl->waitForPrevious();}
bool GpuRaster::nativePresentation()const{
#ifdef __SWITCH__
 return true;
#else
 return false;
#endif
}
std::optional<uint64_t> GpuRaster::present(bool refreshUnchanged){return impl->presentNative(refreshUnchanged);}
std::optional<uint64_t> GpuRaster::presentPixels(const qlie::Bytes& pixels){begin(pixels);return present();}
void GpuRaster::setInterlaced(bool enabled){impl->setInterlaced(enabled);}
void GpuRaster::begin(const qlie::Bytes& pixels){
 if(pixels.size()!=CanvasBytes)throw std::runtime_error("GPU input frame dimensions");
 impl->beginFrame(false,0);
 impl->waitForPrevious();
 impl->uploadCanvas(pixels);
}
void GpuRaster::end(qlie::Bytes& pixels){
 if(!impl->recording||pixels.size()!=CanvasBytes)throw std::runtime_error("GPU readback dimensions or state");
 impl->readCanvas(pixels);
}
void GpuRaster::begin(uint32_t color){impl->beginFrame(true,color);}
void GpuRaster::beginVideo(const Image&image){
 if(image.width!=Width||image.height!=Height)throw std::runtime_error("GPU video dimensions");
 begin(0u);
 Push p;p.rect[2]=Width;p.rect[3]=Height;p.meta[2]=4u<<20;
 DrawSources sources;p.colors[2]=impl->texture(image,sources.primary);
 impl->append(p,sources);
}
void GpuRaster::beginBatch(){++impl->batch;impl->inBatch=true;}
void GpuRaster::endBatch(){impl->inBatch=false;}
void GpuRaster::maskedImage(const Image&source,const Image&mask,const AnimationSample::Mask&settings,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
 auto&s=*impl;size_t before=s.current->commands.size();image(source,m,alpha,add,mul,blend,filter);if(s.current->commands.size()==before)return;
 auto&draw=s.current->commands.back();
 float width=settings.width?settings.width:source.width,height=settings.height?settings.height:source.height;
 float maskWidth=settings.maskWidth?settings.maskWidth:mask.width,maskHeight=settings.maskHeight?settings.maskHeight:mask.height;
 draw.maskSource[0]=settings.sourceX-width*settings.alignX;draw.maskSource[1]=settings.sourceY-height*settings.alignY;draw.maskSource[2]=width;draw.maskSource[3]=height;
 // An open mask over the unchanged source samples exactly the original texels.
 if(settings.amount==255&&draw.maskSource[0]==0&&draw.maskSource[1]==0&&width==source.width&&height==source.height&&source.width<=16384&&source.height<=16384){
  std::fill_n(draw.maskSource,4,0.f);
  return;
 }
 draw.image.meta[2]|=262144;
 draw.maskRect[0]=settings.x-maskWidth*settings.alignX;draw.maskRect[1]=settings.y-maskHeight*settings.alignY;draw.maskRect[2]=maskWidth;draw.maskRect[3]=maskHeight;
 draw.maskMeta[0]=mask.width;draw.maskMeta[1]=mask.height;draw.maskMeta[2]=s.texture(mask,s.current->sources.back().secondary);draw.maskMeta[3]=unsigned(settings.amount)|(unsigned(settings.mode)<<8);
}
void GpuRaster::font(const Image&i,int x,int y){
 if(!i.width||!i.height)return;
 Push p;p.p[1][0]=float(x);p.p[1][1]=float(y);p.rect[0]=x;p.rect[1]=y;p.rect[2]=x+int(i.width);p.rect[3]=y+int(i.height);p.meta[0]=i.width;p.meta[1]=i.height;p.meta[2]=1u<<20;DrawSources sources;p.colors[2]=impl->texture(i,sources.primary);impl->append(p,sources);
}
void GpuRaster::dim(unsigned numerator,unsigned denominator){
 if(!denominator||numerator>denominator)throw std::runtime_error("GPU dim ratio");
 Push p;p.rect[2]=1280;p.rect[3]=720;p.meta[0]=numerator;p.meta[1]=denominator;p.meta[2]=2u<<20;impl->append(p);
}
void GpuRaster::transition(const Image&previous,const Image*rule,double amount,unsigned mode){
 auto&s=*impl;if(previous.width!=1280||previous.height!=720)throw std::runtime_error("GPU transition dimensions");
 if(amount==1)return;
 Push p;p.rect[2]=Width;p.rect[3]=Height;DrawSources sources;p.colors[2]=s.texture(previous,sources.primary);
 if(amount==0){p.meta[2]=4u<<20;s.append(p,sources);return;}
 p.meta[2]=3u<<20;
 if(rule){p.meta[0]=rule->width;p.meta[1]=rule->height;p.meta[2]|=131072;p.meta[3]=uint8_t(255*(1-amount));p.colors[1]=mode;p.colors[3]=s.texture(*rule,sources.secondary);}
 else{
  // CPU's double expression is quantized once for the 256x256 possible byte
  // pairs. Four entries share each GPU word in the texture arena.
  if(s.lutAmount!=amount){s.transitionLut={256,64,qlie::Bytes(256*256)};for(unsigned old=0;old<256;old++)for(unsigned current=0;current<256;current++)s.transitionLut.pixels[old*256+current]=uint8_t(old*(1-amount)+current*amount);s.transitionLut.version.publish();s.lutAmount=amount;}
  p.colors[3]=s.texture(s.transitionLut,sources.secondary);
 }
 s.append(p,sources);
}
void GpuRaster::image(const Image&i,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){float det=m.a*m.d-m.b*m.c;if(std::abs(det)<1e-12f)return;Push p;p.p[0][0]=m.a;p.p[0][1]=m.b;p.p[0][2]=m.c;p.p[0][3]=m.d;p.p[1][0]=m.x;p.p[1][1]=m.y;p.p[1][2]=det;
 // Unscaled integer placements sample exactly at texel centers for both filters.
 if(m.a==1&&m.d==1&&m.b==0&&m.c==0&&std::abs(m.x)<4194304&&std::abs(m.y)<4194304&&std::trunc(m.x)==m.x&&std::trunc(m.y)==m.y)p.meta[2]|=524288u;
 float xmin=m.x,xmax=m.x,ymin=m.y,ymax=m.y;for(unsigned n=0;n<4;n++){float u=n&1?i.width:0,v=n&2?i.height:0;float x=m.a*u+m.b*v+m.x,y=m.c*u+m.d*v+m.y;xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}p.rect[0]=int(std::floor(xmin));p.rect[1]=int(std::floor(ymin));p.rect[2]=int(std::ceil(xmax));p.rect[3]=int(std::ceil(ymax));impl->dispatch(i,p,alpha,add,mul,blend,filter);}
void GpuRaster::triangle(const Image&i,const std::array<TextureVertex,3>&vertices,const Affine2D&post,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){auto v=vertices;for(auto&p:v){if(p.depth<10)return;float ratio=1000/p.depth;float x=640+(p.x-640)*ratio,y=360+(p.y-360)*ratio;p.x=post.a*x+post.b*y+post.x;p.y=post.c*x+post.d*y+post.y;}auto edge=[](const TextureVertex&a,const TextureVertex&b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};float area=edge(v[0],v[1],v[2].x,v[2].y);if(std::abs(area)<1e-6f)return;if(area<0){std::swap(v[1],v[2]);area=-area;}Push p;for(unsigned n=0;n<3;n++){p.p[n][0]=v[n].x;p.p[n][1]=v[n].y;p.p[n][2]=v[n].u;p.p[n][3]=v[n].v;p.p[3][n]=v[n].depth;}p.p[3][3]=area;p.meta[2]=65536;p.rect[0]=int(std::floor(std::min({v[0].x,v[1].x,v[2].x})));p.rect[1]=int(std::floor(std::min({v[0].y,v[1].y,v[2].y})));p.rect[2]=int(std::ceil(std::max({v[0].x,v[1].x,v[2].x})));p.rect[3]=int(std::ceil(std::max({v[0].y,v[1].y,v[2].y})));impl->dispatch(i,p,alpha,add,mul,blend,filter);}
}
