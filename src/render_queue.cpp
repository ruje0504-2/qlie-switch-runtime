// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_queue.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
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
  default:throw std::runtime_error("Unsupported pixel blend mode");
 }
 return uint8_t(std::clamp(value,0,255));
}
void RenderQueue::image(const Image&i,const Affine2D&m,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend){
 Command c;c.image=&i;c.placement=m;c.priority=pri;c.state.alpha=alpha;c.state.addColor=add;c.state.mulColor=mul;c.state.flags=blend;commands.push_back(c);
}
const Image& RenderQueue::snapshotImage(const Image&i){snapshots.push_back(i);return snapshots.back();}
void RenderQueue::snapshot(const Image&i,const Affine2D&m,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend){
 image(snapshotImage(i),m,pri,alpha,add,mul,blend);
}
void RenderQueue::triangle(const Image&i,const std::array<TextureVertex,3>&vertices,float pri,float alpha,uint32_t add,uint32_t mul,unsigned blend){
 std::vector<TextureVertex> clipped;
 for(unsigned n=0;n<3;n++){const auto&a=vertices[n];const auto&b=vertices[(n+1)%3];bool ai=a.depth>=10,bi=b.depth>=10;
  if(ai)clipped.push_back(a);
  if(ai!=bi){float t=(10-a.depth)/(b.depth-a.depth);clipped.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,10,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t});}
 }
 for(size_t n=1;n+1<clipped.size();n++){Command c;c.image=&i;c.triangle={clipped[0],clipped[n],clipped[n+1]};c.polygon=true;c.priority=pri;c.state.alpha=alpha;c.state.addColor=add;c.state.mulColor=mul;c.state.flags=blend;commands.push_back(c);}
}
void RenderQueue::control(const AnimationSample&s,float pri){Command c;c.state=s;c.priority=pri;commands.push_back(c);}
namespace {
// Vertices carry camera-space depth (1000 - worldZ). Texture coordinates are
// interpolated as u/depth, v/depth and 1/depth, then divided per pixel.
void triangleRaster(qlie::Bytes&out,unsigned w,unsigned h,const Image&im,std::array<TextureVertex,3> v,const Affine2D&post,float alpha,uint32_t add,uint32_t mul,unsigned blend){
 if(!im.width||!im.height||alpha<=0)return;
 for(auto&p:v){if(p.depth<10)return;float ratio=1000/p.depth;float x=w*.5f+(p.x-w*.5f)*ratio,y=h*.5f+(p.y-h*.5f)*ratio;p.x=post.a*x+post.b*y+post.x;p.y=post.c*x+post.d*y+post.y;}
 auto edge=[](const TextureVertex&a,const TextureVertex&b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
 float area=edge(v[0],v[1],v[2].x,v[2].y);if(std::abs(area)<1e-6f)return;
 if(area<0){std::swap(v[1],v[2]);area=-area;}
 auto topLeft=[](const TextureVertex&a,const TextureVertex&b){return b.y<a.y||(b.y==a.y&&b.x>a.x);};
 bool include[3]={topLeft(v[1],v[2]),topLeft(v[2],v[0]),topLeft(v[0],v[1])};
 int x0=std::max(0,int(std::floor(std::min({v[0].x,v[1].x,v[2].x})))),x1=std::min(int(w),int(std::ceil(std::max({v[0].x,v[1].x,v[2].x}))));
 int y0=std::max(0,int(std::floor(std::min({v[0].y,v[1].y,v[2].y})))),y1=std::min(int(h),int(std::ceil(std::max({v[0].y,v[1].y,v[2].y}))));
 for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
  float e[3]={edge(v[1],v[2],x+.5f,y+.5f),edge(v[2],v[0],x+.5f,y+.5f),edge(v[0],v[1],x+.5f,y+.5f)};bool inside=true;
  for(unsigned j=0;j<3;j++)if(e[j]<0||(e[j]==0&&!include[j]))inside=false;
  if(!inside)continue;
  float inverse=0,u=0,t=0;for(unsigned j=0;j<3;j++){float q=e[j]/area/v[j].depth;inverse+=q;u+=q*v[j].u;t+=q*v[j].v;}
  int tx=std::clamp(int(std::floor(u/inverse)),0,int(im.width)-1),ty=std::clamp(int(std::floor(t/inverse)),0,int(im.height)-1);
  const auto*src=im.pixels.data()+(size_t(ty)*im.width+unsigned(tx))*4;auto*dst=out.data()+(size_t(y)*w+unsigned(x))*4;
  unsigned a=unsigned(src[3])*unsigned(std::clamp(alpha,0.f,255.f))/255;if(!a)continue;
  for(unsigned n=0;n<3;n++){unsigned color=std::min(255u,unsigned(src[n])*((mul>>(8*n))&255)/255+((add>>(8*n))&255));dst[n]=blendChannel(blend,uint8_t(color),dst[n],uint8_t(a));}
  dst[3]=blendChannel(blend,uint8_t(a),dst[3],uint8_t(a));
 }
}
void raster(qlie::Bytes&out,unsigned w,unsigned h,const Image&im,const Affine2D&m,float alpha,uint32_t add,uint32_t mul,unsigned blend){
 if(!im.width||!im.height||alpha<=0)return;
 float det=m.a*m.d-m.b*m.c;if(std::abs(det)<1e-12f)return;
 float xmin=m.x,xmax=m.x,ymin=m.y,ymax=m.y;
 for(unsigned i=0;i<4;i++){float u=i&1?im.width:0,v=i&2?im.height:0;
 float x=m.a*u+m.b*v+m.x,y=m.c*u+m.d*v+m.y;
 xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}
 for(int y=std::max(0,int(std::floor(ymin)));y<std::min(int(h),int(std::ceil(ymax)));y++)
 for(int x=std::max(0,int(std::floor(xmin)));x<std::min(int(w),int(std::ceil(xmax)));x++){
 float dx=x+.5f-m.x,dy=y+.5f-m.y;
 int u=int(std::floor((m.d*dx-m.b*dy)/det)),v=int(std::floor((-m.c*dx+m.a*dy)/det));
 if(u<0||v<0||u>=int(im.width)||v>=int(im.height))continue;
 const auto*src=im.pixels.data()+(size_t(v)*im.width+u)*4;auto*dst=out.data()+(size_t(y)*w+x)*4;
 unsigned sa=unsigned(src[3])*unsigned(std::clamp(alpha,0.f,255.f))/255;if(!sa)continue;
 unsigned da=dst[3];
 for(unsigned n=0;n<3;n++){unsigned col=std::min(255u,unsigned(src[n])*((mul>>(8*n))&255)/255+((add>>(8*n))&255));
 dst[n]=blendChannel(blend,uint8_t(col),dst[n],uint8_t(sa));}dst[3]=blendChannel(blend,uint8_t(sa),uint8_t(da),uint8_t(sa));
 }
}
}
void RenderQueue::draw(qlie::Bytes&out,unsigned w,unsigned h)const{
 if(out.size()!=size_t(w)*h*4)throw std::runtime_error("Render canvas size");
 std::vector<const Command*>order;for(auto&c:commands)order.push_back(&c);
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
 if(!stack.empty()){const auto&p=stack.back();if(p.flags&0x7f)blend=p.flags&0x7f;float r=p.rotationZ*3.14159265358979323846f/180,co=std::cos(r),si=std::sin(r),sx=p.scaleX/100,sy=p.scaleY/100,cx=w/2.f,cy=h/2.f;
 Affine2D post{sx*co,sx*si,-sy*si,sy*co,cx+sx*(co*(p.x-cx)+si*(p.y-cy)),cy+sy*(-si*(p.x-cx)+co*(p.y-cy))};
 model=affine(post,model);alpha=int(alpha)*int(p.alpha)/255;add=colorAdd(add,p.addColor);mul=colorMultiply(mul,p.mulColor);}
 if(c->polygon)triangleRaster(out,w,h,*c->image,c->triangle,model,alpha,add,mul,blend);
 else raster(out,w,h,*c->image,model,alpha,add,mul,blend);
 }
}
}
