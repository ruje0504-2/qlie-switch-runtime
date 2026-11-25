// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_queue.hpp"
#include "video_surface_image.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <exception>
#include <cstdlib>
#ifdef __aarch64__
#include <arm_neon.h>
#endif
#ifdef __SWITCH__
#include "switch_threads.hpp"
#endif
namespace whj2 {
Affine2D affine(const Affine2D&p,const Affine2D&q){return {
 p.a*q.a+p.b*q.c,p.a*q.b+p.b*q.d,p.c*q.a+p.d*q.c,p.c*q.b+p.d*q.d,
 p.a*q.x+p.b*q.y+p.x,p.c*q.x+p.d*q.y+p.y};}
Affine2D imagePlacement(float x,float y,float sx,float sy,float angle,float ax,float ay,unsigned w,unsigned h){
 float r=angle*3.14159265358979323846f/180,c=std::cos(r),s=std::sin(r);
 Affine2D m{c*sx,-s*sy,s*sx,c*sy,x,y};
 m.x-=m.a*w*ax+m.b*h*ay;m.y-=m.c*w*ax+m.d*h*ay;return m;
}
uint32_t colorAdd(uint32_t a,uint32_t b){uint32_t c=0;for(unsigned n=0;n<3;n++)c|=std::min(255u,((a>>(8*n))&255)+((b>>(8*n))&255))<<(8*n);return c;}
uint32_t colorMultiply(uint32_t a,uint32_t b){uint32_t c=0;for(unsigned n=0;n<3;n++)c|=(((a>>(8*n))&255)*((b>>(8*n))&255)/255)<<(8*n);return c;}
// PE d2700 -> ca250 -> caf08/c8964 -> c86e8..c8960.
// Keep the engine's integer rounding; these are not float blend equations.
uint8_t blendChannel(unsigned mode,uint8_t source,uint8_t destination,uint8_t alpha){
 if((mode==0||mode==1)&&alpha==255)return source;
 int s=source,d=destination,a=alpha,value=0;
 auto shift7=[](int n){return n>=0?n/128:-((-n+127)/128);};
 int weighted=s*(a+(a>>7))/256;
 switch(mode){
  case 0:case 1:value=d+shift7((s-d)*((a+(a>>7))>>1));break;
  case 2:value=weighted+d-weighted*d/256;break;
  case 3:{int factor=(a>>1)+((a>>1)>>6);int v=255+shift7((s-255)*factor);value=(v+(v>>7))*d/256;break;}
  case 4:value=std::abs(weighted-d);break;
  case 5:value=std::max(weighted,d);break;
  case 6:{int factor=(a>>1)+((a>>1)>>6);int v=255+shift7((s-255)*factor);value=std::min(v,d);break;}
  case 7:value=weighted+d;break;
  case 8:value=d-weighted;break;
  case 11:case 13:case 15:case 16:{
   int target;
   if(mode==11||mode==13)target=(mode==11?d:s)<128?2*s*d/255:255-2*(255-s)*(255-d)/255;
   else if(mode==15)target=std::clamp(d+2*s-255,0,255);
   else target=s<128?std::min(d,2*s):std::max(d,2*s-255);
   value=a==255?target:d+shift7((target-d)*((a+(a>>7))>>1));
   break;
  }
  default:throw std::runtime_error("Unsupported pixel blend mode");
 }
 return uint8_t(std::clamp(value,0,255));
}
void RenderQueue::image(const Image&i,const Affine2D&m,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
 Command c;c.image=&i;c.placement=m;c.priority=pri;c.state.alpha=alpha;c.state.addColor=add;c.state.mulColor=mul;c.state.flags=blend;c.state.textureFilter=filter;commands.push_back(c);if(alpha>0&&i.width&&i.height)visibleImages++;
}
const Image& RenderQueue::snapshotImage(const Image&i){snapshots.push_back(i);return snapshots.back();}
const Image& RenderQueue::retainImage(std::shared_ptr<const Image>i){retainedImages.push_back(std::move(i));return *retainedImages.back();}
void RenderQueue::maskedImage(const Image&i,const Image&mask,const AnimationSample::Mask&settings,const Affine2D&m,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){image(i,m,pri,alpha,add,mul,blend,filter);commands.back().mask=&mask;commands.back().state.mask=settings;}
void RenderQueue::snapshot(const Image&i,const Affine2D&m,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
 image(snapshotImage(i),m,pri,alpha,add,mul,blend,filter);
}
void RenderQueue::triangle(const Image&i,const std::array<TextureVertex,3>&vertices,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter){
  std::array<TextureVertex,4> clipped;
  unsigned count=0;
 for(unsigned n=0;n<3;n++){const auto&a=vertices[n];const auto&b=vertices[(n+1)%3];bool ai=a.depth>=10,bi=b.depth>=10;
  if(ai)clipped[count++]=a;
  if(ai!=bi){float t=(10-a.depth)/(b.depth-a.depth);clipped[count++]={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,10,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};}
 }
 for(unsigned n=1;n+1<count;n++){Command c;c.image=&i;c.triangle={clipped[0],clipped[n],clipped[n+1]};c.polygon=true;c.priority=pri;c.state.alpha=alpha;c.state.addColor=add;c.state.mulColor=mul;c.state.flags=blend;c.state.textureFilter=filter;commands.push_back(c);if(alpha>0&&i.width&&i.height)visibleImages++;}
}
void RenderQueue::control(const AnimationSample&s,float pri){Command c;c.state=s;c.priority=pri;commands.push_back(c);}
namespace {
std::array<uint8_t,4> texel(const Image&im,const qlie::Bytes&pixels,float u,float v,unsigned filter){
 auto pixel=[&](int x,int y){x=std::clamp(x,0,int(im.width)-1);y=std::clamp(y,0,int(im.height)-1);return pixels.data()+(size_t(y)*im.width+unsigned(x))*4;};
 std::array<uint8_t,4> out;
 if(filter!=2){auto*p=pixel(int(std::floor(u)),int(std::floor(v)));std::copy(p,p+4,out.begin());return out;}
 float x=u-.5f,y=v-.5f;int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-ix,fy=y-iy;
 auto a=pixel(ix,iy),b=pixel(ix+1,iy),c=pixel(ix,iy+1),d=pixel(ix+1,iy+1);
 for(unsigned n=0;n<4;n++)out[n]=uint8_t(int(double((a[n]*(1-fx)+b[n]*fx)*(1-fy)+(c[n]*(1-fx)+d[n]*fx)*fy)+.5));
 return out;
}
// Vertices carry camera-space depth (1000 - worldZ). Texture coordinates are
// interpolated as u/depth, v/depth and 1/depth, then divided per pixel.
void triangleRaster(qlie::Bytes&out,unsigned w,unsigned h,const Image&im,std::array<TextureVertex,3> v,const Affine2D&post,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter,unsigned rowBegin,unsigned rowEnd){
 if(!im.width||!im.height||alpha<=0)return;
 const auto&pixels=im.videoSurface?im.videoSurface->pixels():im.pixels;
 for(auto&p:v){if(p.depth<10)return;float ratio=1000/p.depth;float x=w*.5f+(p.x-w*.5f)*ratio,y=h*.5f+(p.y-h*.5f)*ratio;p.x=post.a*x+post.b*y+post.x;p.y=post.c*x+post.d*y+post.y;}
 auto edge=[](const TextureVertex&a,const TextureVertex&b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
 float area=edge(v[0],v[1],v[2].x,v[2].y);if(std::abs(area)<1e-6f)return;
 if(area<0){std::swap(v[1],v[2]);area=-area;}
 auto topLeft=[](const TextureVertex&a,const TextureVertex&b){return b.y<a.y||(b.y==a.y&&b.x>a.x);};
 bool include[3]={topLeft(v[1],v[2]),topLeft(v[2],v[0]),topLeft(v[0],v[1])};
 int x0=std::max(0,int(std::floor(std::min({v[0].x,v[1].x,v[2].x})))),x1=std::min(int(w),int(std::ceil(std::max({v[0].x,v[1].x,v[2].x}))));
 int y0=std::max(int(rowBegin),int(std::floor(std::min({v[0].y,v[1].y,v[2].y})))),y1=std::min(int(rowEnd),int(std::ceil(std::max({v[0].y,v[1].y,v[2].y}))));
 for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
  float e[3]={edge(v[1],v[2],x+.5f,y+.5f),edge(v[2],v[0],x+.5f,y+.5f),edge(v[0],v[1],x+.5f,y+.5f)};bool inside=true;
  for(unsigned j=0;j<3;j++)if(e[j]<0||(e[j]==0&&!include[j]))inside=false;
  if(!inside)continue;
  float inverse=0,u=0,t=0;for(unsigned j=0;j<3;j++){float q=e[j]/area/v[j].depth;inverse+=q;u+=q*v[j].u;t+=q*v[j].v;}
  auto sampled=texel(im,pixels,u/inverse,t/inverse,filter);const auto*src=sampled.data();auto*dst=out.data()+(size_t(y)*w+unsigned(x))*4;
  unsigned a=unsigned(src[3])*unsigned(std::clamp(alpha,0.f,255.f))/255;if(!a)continue;
  for(unsigned n=0;n<3;n++){unsigned color=std::min(255u,unsigned(src[n])*((mul>>(8*n))&255)/255+((add>>(8*n))&255));dst[n]=blendChannel(blend,uint8_t(color),dst[n],uint8_t(a));}
  dst[3]=blendChannel(blend,uint8_t(a),dst[3],uint8_t(a));
 }
}
void raster(qlie::Bytes&out,unsigned w,unsigned h,const Image&im,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend,unsigned filter,unsigned rowBegin,unsigned rowEnd){
 if(!im.width||!im.height||alpha<=0)return;
 const auto&pixels=im.videoSurface?im.videoSurface->pixels():im.pixels;
 (void)h;
 float det=m.a*m.d-m.b*m.c;if(std::abs(det)<1e-12f)return;
 float xmin=m.x,xmax=m.x,ymin=m.y,ymax=m.y;
 for(unsigned i=0;i<4;i++){float u=i&1?im.width:0,v=i&2?im.height:0;
 float x=m.a*u+m.b*v+m.x,y=m.c*u+m.d*v+m.y;
 xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}
 for(int y=std::max(int(rowBegin),int(std::floor(ymin)));y<std::min(int(rowEnd),int(std::ceil(ymax)));y++)
 for(int x=std::max(0,int(std::floor(xmin)));x<std::min(int(w),int(std::ceil(xmax)));x++){
 float dx=x+.5f-m.x,dy=y+.5f-m.y;
 float u=(m.d*dx-m.b*dy)/det,v=(-m.c*dx+m.a*dy)/det;
 if(u<0||v<0||u>=im.width||v>=im.height)continue;
 auto sampled=texel(im,pixels,u,v,filter);const auto*src=sampled.data();auto*dst=out.data()+(size_t(y)*w+x)*4;
 unsigned sa=unsigned(src[3])*unsigned(std::clamp(alpha,0.f,255.f))/255;if(!sa)continue;
 unsigned da=dst[3];
 for(unsigned n=0;n<3;n++){unsigned col=std::min(255u,unsigned(src[n])*((mul>>(8*n))&255)/255+((add>>(8*n))&255));
 dst[n]=blendChannel(blend,uint8_t(col),dst[n],uint8_t(sa));}dst[3]=blendChannel(blend,uint8_t(sa),uint8_t(da),uint8_t(sa));
 }
}
}
void RenderQueue::drawRows(qlie::Bytes&out,unsigned w,unsigned h,unsigned rowBegin,unsigned rowEnd,RenderTarget*target)const{
 if(!target&&out.size()!=size_t(w)*h*4)throw std::runtime_error("Render canvas size");
 std::vector<const Command*>order;order.reserve(commands.size());for(auto&c:commands)order.push_back(&c);
 std::stable_sort(order.begin(),order.end(),[](auto*a,auto*b){return a->priority<b->priority;});
 std::vector<AnimationSample>stack;
 for(auto*c:order){const auto&s=c->state;
 if(!c->image){
  if(s.kind==15){
   stack.clear();
  }else if(s.kind==17){
   if(!stack.empty())stack.pop_back();
  }else if(s.kind==16){
   auto t=s;
   if(!stack.empty()){
    auto&p=stack.back();
    t.x+=p.x;t.y+=p.y;
    t.scaleX*=p.scaleX/100;t.scaleY*=p.scaleY/100;
    t.rotationZ+=p.rotationZ;
    if(!(t.flags&0x7f))t.flags=p.flags;
    t.alpha=int(t.alpha)*int(p.alpha)/255;
    t.addColor=colorAdd(t.addColor,p.addColor);
    t.mulColor=colorMultiply(t.mulColor,p.mulColor);
   }
   stack.push_back(t);
  }
  continue;
 }
 unsigned blend=s.flags&0x7f;
 auto model=c->placement;float alpha=s.alpha;uint32_t add=s.addColor,mul=s.mulColor;
 if(!stack.empty()){const auto&p=stack.back();if(p.flags&0x7f)blend=p.flags&0x7f;float r=p.rotationZ*3.14159265358979323846f/180,co=std::cos(r),si=std::sin(r),sx=p.scaleX/100,sy=p.scaleY/100;
 float cx=(logicalSize[0]?logicalSize[0]:w)/2.f,cy=(logicalSize[1]?logicalSize[1]:h)/2.f;
 Affine2D post{sx*co,sx*si,-sy*si,sy*co,cx+sx*(co*(p.x-cx)+si*(p.y-cy)),cy+sy*(-si*(p.x-cx)+co*(p.y-cy))};
 model=affine(post,model);alpha=int(alpha)*int(p.alpha)/255;add=colorAdd(add,p.addColor);mul=colorMultiply(mul,p.mulColor);}
 if(view)model=affine(*view,model);
 if(target){if(c->mask)target->maskedImage(*c->image,*c->mask,s.mask,model,alpha,add,mul,blend,s.textureFilter);else if(c->polygon)target->triangle(*c->image,c->triangle,model,alpha,add,mul,blend,s.textureFilter);else target->image(*c->image,model,alpha,add,mul,blend,s.textureFilter);continue;}
 if(c->polygon)triangleRaster(out,w,h,*c->image,c->triangle,model,alpha,add,mul,blend,s.textureFilter,rowBegin,rowEnd);
 else raster(out,w,h,*c->image,model,alpha,add,mul,blend,s.textureFilter,rowBegin,rowEnd);
 }
}
namespace {
// Each worker owns complete destination rows and executes the original ordered
// command stream. Sampling, blending and overlapping-layer order stay identical.
class RasterWorkers {
 std::mutex mutex;std::condition_variable start,finished;
 std::thread workers[2];std::function<void(unsigned,unsigned)> job;
 std::exception_ptr errors[3];unsigned generation=0,remaining=0,height=0;bool stop=false;
 unsigned coreCount=3;
 void worker(unsigned index){
#ifdef __SWITCH__
  logSwitchThread(index==1?"cpu-raster-1":"cpu-raster-2",bindSwitchThread(index));
#endif
  unsigned seen=0;std::unique_lock<std::mutex> lock(mutex);
  while(true){start.wait(lock,[&]{return stop||generation!=seen;});if(stop)return;seen=generation;
   unsigned begin=height*index/coreCount,end=height*(index+1)/coreCount;lock.unlock();
   try{job(begin,end);}catch(...){errors[index]=std::current_exception();}
   lock.lock();if(--remaining==0)finished.notify_one();
  }
 }
public:
 RasterWorkers(){
#ifdef __SWITCH__
  u64 mask=0;
  if(R_SUCCEEDED(svcGetInfo(&mask,InfoType_CoreMask,CUR_PROCESS_HANDLE,0))&&mask)
   coreCount=std::min(3u,unsigned(__builtin_popcountll(mask)));
#endif
  for(unsigned i=1;i<coreCount;++i)workers[i-1]=std::thread([this,i]{worker(i);});
 }
 ~RasterWorkers(){{std::lock_guard<std::mutex> lock(mutex);stop=true;}start.notify_all();for(auto&t:workers)if(t.joinable())t.join();}
 void run(unsigned h,const std::function<void(unsigned,unsigned)>&fn){
  if(coreCount==1){fn(0,h);return;}
  {std::lock_guard<std::mutex> lock(mutex);job=fn;height=h;remaining=coreCount-1;for(auto&e:errors)e=nullptr;++generation;}
  start.notify_all();try{fn(0,h/coreCount);}catch(...){errors[0]=std::current_exception();}
  {std::unique_lock<std::mutex> lock(mutex);finished.wait(lock,[&]{return remaining==0;});job={};}
  for(auto&e:errors)if(e)std::rethrow_exception(e);
 }
};
}
bool RenderQueue::captureAlignedBitmap(qlie::Bytes&out,unsigned w,unsigned h,uint32_t background)const{
 if(view||commands.size()!=1)return false;
 const auto&c=commands.front();
 const auto&p=c.placement;
 const auto&s=c.state;
 if(!c.image||c.image->videoSurface||c.mask||c.polygon||c.image->width!=w||c.image->height!=h||
    p.a!=1||p.d!=1||p.b!=0||p.c!=0||p.x!=0||p.y!=0||
    s.alpha!=255||s.flags>1||s.textureFilter>1||s.addColor!=0||s.mulColor!=0xffffff)return false;
 const auto&src=c.image->pixels;
 if(out.size()!=size_t(w)*h*4||src.size()!=out.size())throw std::runtime_error("Capture bitmap dimensions");
 const std::array<uint8_t,4> clear{uint8_t(background),uint8_t(background>>8),uint8_t(background>>16),uint8_t(background>>24)};
 size_t n=0;
#ifdef __aarch64__
 // Normal blend is d + floor((s-d)*((a+(a>>7))>>1)/128).
 // Signed 16-bit lanes can hold the complete product, including negative deltas.
 auto weight=[](uint8x8_t a){return vreinterpretq_s16_u16(vshrq_n_u16(vaddl_u8(a,vshr_n_u8(a,7)),1));};
 auto compose=[](uint8x8_t src,int16x8_t dst,int16x8_t w){
  const auto delta=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(src)),dst);
  return vmovn_u16(vreinterpretq_u16_s16(vaddq_s16(dst,vshrq_n_s16(vmulq_s16(delta,w),7))));
 };
 for(;n+64<=src.size();n+=64){
  auto channels=vld4q_u8(src.data()+n);
  const auto lowWeight=weight(vget_low_u8(channels.val[3])),highWeight=weight(vget_high_u8(channels.val[3]));
  for(unsigned channel=0;channel<4;++channel){
   const auto input=channels.val[channel];
   const auto dest=vdupq_n_s16(clear[channel]);
   channels.val[channel]=vcombine_u8(compose(vget_low_u8(input),dest,lowWeight),compose(vget_high_u8(input),dest,highWeight));
  }
  vst4q_u8(out.data()+n,channels);
 }
#endif
 for(;n<src.size();n+=4){
  const auto alpha=src[n+3];
  for(unsigned channel=0;channel<4;++channel)out[n+channel]=blendChannel(0,src[n+channel],clear[channel],alpha);
 }
 return true;
}
void RenderQueue::draw(RenderTarget&target,unsigned w,unsigned h)const{qlie::Bytes unused;target.beginBatch();drawRows(unused,w,h,0,h,&target);target.endBatch();if(fieldPresentation)target.requestFieldPresentation();}
void RenderQueue::draw(qlie::Bytes&out,unsigned w,unsigned h)const{
 if(out.size()!=size_t(w)*h*4)throw std::runtime_error("Render canvas size");
 // Materialize before workers read rows in parallel.
 for(const auto&c:commands)if(c.image&&c.image->videoSurface&&c.state.alpha>0)c.image->videoSurface->pixels();
 static const bool serial=[] {auto*mode=std::getenv("WHJ2_RENDER_THREADS");return mode&&std::string(mode)=="1";}();
 bool large=commands.size()>8;
 if(!large)for(auto&c:commands)if(c.image&&size_t(c.image->width)*c.image->height>=131072){large=true;break;}
 if(serial||!large||size_t(w)*h<262144){drawRows(out,w,h,0,h);return;}
 static RasterWorkers pool;
 pool.run(h,[&](unsigned begin,unsigned end){drawRows(out,w,h,begin,end);});
}

}
