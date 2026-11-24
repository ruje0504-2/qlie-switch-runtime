// SPDX-License-Identifier: GPL-2.0-or-later
// DPNG/ABMP layouts follow GARbro (MIT, morkt); see THIRD_PARTY.md.
#include "image.hpp"
#include <png.h>
#include <cstdio>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <algorithm>
#include <array>
namespace whj2 {
namespace {
template<unsigned Radius>
Image boxBlur(const Image&source){
 constexpr unsigned diameter=2*Radius+1,area=diameter*diameter;
 const auto width=source.width,height=source.height;
 Image out{width,height,qlie::Bytes(source.pixels.size())};
 // The horizontal window includes replicated edge columns. Padding keeps
 // the per-pixel loop free of bounds checks, including for very narrow images.
 std::vector<std::array<uint32_t,4>> columns(size_t(width)+diameter);
 auto*const center=columns.data()+Radius;
 auto row=[&](int y){
  return source.pixels.data()+size_t(std::clamp(y,0,int(height)-1))*width*4;
 };
 for(int y=-int(Radius);y<=int(Radius);y++){
  auto*input=row(y);
  for(unsigned x=0;x<width;x++,input+=4)
   for(unsigned c=0;c<4;c++)center[x][c]+=input[c];
 }
 auto*output=out.pixels.data();
 for(unsigned y=0;y<height;y++){
  std::fill_n(columns.data(),Radius,center[0]);
  std::fill_n(center+width,Radius+1,center[width-1]);
  std::array<uint32_t,4> sum{};
  for(unsigned x=0;x<diameter;x++)
   for(unsigned c=0;c<4;c++)sum[c]+=columns[x][c];
  for(unsigned x=0;x<width;x++,output+=4)
   for(unsigned c=0;c<4;c++){
    output[c]=uint8_t(sum[c]/area);
    sum[c]=sum[c]+columns[x+diameter][c]-columns[x][c];
   }
  auto*leaving=row(int(y)-int(Radius));
  auto*entering=row(int(y)+int(Radius)+1);
  for(unsigned x=0;x<width;x++,leaving+=4,entering+=4)
   for(unsigned c=0;c<4;c++)center[x][c]=center[x][c]+entering[c]-leaving[c];
 }
 return out;
}
}
Image filterImage(const Image&source,const std::string&name){
 if(!source.width||!source.height||source.pixels.size()!=size_t(source.width)*source.height*4)throw std::runtime_error("Filter source dimensions");
 if(name=="blur10")return boxBlur<5>(source);
 if(name=="blur20")return boxBlur<10>(source);
 Image out{source.width,source.height,source.pixels};
 if(name=="none"||name.empty())return out;
 if(name=="mono"||name=="sepia"){
  constexpr unsigned color[3]={17,34,68};
  for(size_t i=0;i<out.pixels.size();i+=4){unsigned gray=(unsigned(source.pixels[i])+source.pixels[i+1]+source.pixels[i+2])*85>>8;
   for(unsigned c=0;c<3;c++)out.pixels[i+c]=uint8_t(name=="mono"?gray:gray+color[c]-gray*color[c]/255);
  }
  return out;
 }
 throw std::runtime_error("Unsupported image filter "+name);
}
struct Cursor {const qlie::Bytes&b;size_t p=0;uint32_t n(unsigned size){if(size>4||p+size>b.size())throw std::runtime_error("Truncated image structure");uint32_t v=0;for(unsigned i=0;i<size;i++)v|=uint32_t(b[p++])<<(8*i);return v;}qlie::Bytes data(size_t len){if(len>b.size()-p)throw std::runtime_error("Truncated image payload");qlie::Bytes v(b.begin()+p,b.begin()+p+len);p+=len;return v;}void skip(size_t n0){if(n0>b.size()-p)throw std::runtime_error("Truncated image field");p+=n0;}std::string tag(){auto v=data(16);auto p0=std::find(v.begin(),v.end(),0);return std::string(v.begin(),p0);}};
std::vector<Member> abmp(const qlie::Bytes&b){
 if(b.size()<16||std::memcmp(b.data(),"abmp1",5))throw std::runtime_error("Unsupported ABMP version");
 Cursor r{b,16};std::vector<Member>out;
 auto add=[&](std::string type,std::string name){
  const auto size=r.n(4);const auto offset=r.p;
  auto data=r.data(size);
  if(data.size()>=4&&!std::memcmp(data.data(),"1PC\xff",4))data=qlie::decompress(data);
  out.push_back({std::move(type),std::move(name),std::move(data),offset,size});
 };
 while(r.p<b.size()){
  auto section=r.tag();
  if(section.rfind("abdata",0)==0){add(std::move(section),{});continue;}
  if(section!="abimage10"&&section!="absound10")throw std::runtime_error("Unsupported ABMP section "+section);
  unsigned count=r.n(1);
  for(unsigned i=0;i<count;i++){
   auto type=r.tag();
   std::string name;
   if(type=="abimgdat15"||type=="absnddat12"){
    const auto version=r.n(4);
    if(version!=1&&!(version==2&&type=="abimgdat15"))
     throw std::runtime_error("Unsupported ABMP member revision "+std::to_string(version));
    name=qlie::utf16le(r.data(size_t(r.n(2))*2));
    const auto alternateLength=r.n(2);
    if(name.empty())name=qlie::cp932(r.data(alternateLength));
    else r.skip(alternateLength);
    r.n(1);
    if(type=="abimgdat15")r.skip(version==2?0x1d:0x11);
   }else{
    name=qlie::cp932(r.data(r.n(2)));
    if(type!="abimgdat10"&&type!="absnddat10"){
    r.skip(r.n(2));
    if(type=="abimgdat13")r.skip(12);
    else if(type=="abimgdat14")r.skip(76);
    else if(type!="abimgdat11"&&type!="abimgdat12"&&type!="absnddat11")throw std::runtime_error("Unsupported ABMP member "+type);
    }
    r.n(1);
   }
   add(std::move(type),std::move(name));
  }
 }
 return out;
}
static Image canvas(unsigned w,unsigned h){if(!w||!h||w>16384||h>16384||uint64_t(w)*h>64*1024*1024)throw std::runtime_error("Invalid image dimensions");return {w,h,qlie::Bytes(size_t(w)*h*4)};}
struct JError {jpeg_error_mgr base;jmp_buf jump;char message[JMSG_LENGTH_MAX];};
static void jfail(j_common_ptr c){auto*e=reinterpret_cast<JError*>(c->err);(*c->err->format_message)(c,e->message);longjmp(e->jump,1);}
Image image(const qlie::Bytes&b){if(b.size()<4)throw std::runtime_error("Short image");if(!std::memcmp(b.data(),"DPNG",4)){Cursor r{b,4};r.n(4);unsigned count=r.n(4),w=r.n(4),h=r.n(4);Image out=canvas(w,h);if(count>100000)throw std::runtime_error("Invalid DPNG tile count");for(unsigned i=0;i<count;i++){unsigned x=r.n(4),y=r.n(4),tw=r.n(4),th=r.n(4),size=r.n(4);r.skip(8);auto data=r.data(size);if(!size)continue;Image tile=image(data);if(tile.width!=tw||tile.height!=th||x>out.width||tw>out.width-x||y>out.height||th>out.height-y)throw std::runtime_error("DPNG tile bounds mismatch");for(unsigned yy=0;yy<th;yy++)std::memcpy(out.pixels.data()+(size_t(y+yy)*w+x)*4,tile.pixels.data()+size_t(yy)*tw*4,size_t(tw)*4);}if(r.p!=b.size())throw std::runtime_error("DPNG trailing bytes");return out;}
 if(b.size()>=8&&!png_sig_cmp(b.data(),0,8)){png_image pi{};pi.version=PNG_IMAGE_VERSION;if(!png_image_begin_read_from_memory(&pi,b.data(),b.size()))throw std::runtime_error(pi.message);Image out=canvas(pi.width,pi.height);pi.format=PNG_FORMAT_BGRA;if(!png_image_finish_read(&pi,nullptr,out.pixels.data(),0,nullptr)){std::string error=pi.message;png_image_free(&pi);throw std::runtime_error(error);}png_image_free(&pi);return out;}
 if(b[0]==255&&b[1]==216){jpeg_decompress_struct c{};JError e{};c.err=jpeg_std_error(&e.base);e.base.error_exit=jfail;if(setjmp(e.jump)){jpeg_destroy_decompress(&c);throw std::runtime_error(e.message);}jpeg_create_decompress(&c);jpeg_mem_src(&c,b.data(),b.size());jpeg_read_header(&c,TRUE);c.out_color_space=JCS_RGB;jpeg_start_decompress(&c);Image out=canvas(c.output_width,c.output_height);qlie::Bytes row(size_t(c.output_width)*3);while(c.output_scanline<c.output_height){unsigned y=c.output_scanline;auto*rp=row.data();jpeg_read_scanlines(&c,&rp,1);for(unsigned x=0;x<out.width;x++){auto*dst=out.pixels.data()+(size_t(y)*out.width+x)*4;dst[0]=row[3*x+2];dst[1]=row[3*x+1];dst[2]=row[3*x];dst[3]=255;}}jpeg_finish_decompress(&c);jpeg_destroy_decompress(&c);return out;}
 throw std::runtime_error("Unsupported image signature");
}
}
