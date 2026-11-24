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
namespace whj2 {
struct Cursor {const qlie::Bytes&b;size_t p=0;uint32_t n(unsigned size){if(size>4||p+size>b.size())throw std::runtime_error("Truncated image structure");uint32_t v=0;for(unsigned i=0;i<size;i++)v|=uint32_t(b[p++])<<(8*i);return v;}qlie::Bytes data(size_t len){if(len>b.size()-p)throw std::runtime_error("Truncated image payload");qlie::Bytes v(b.begin()+p,b.begin()+p+len);p+=len;return v;}void skip(size_t n0){if(n0>b.size()-p)throw std::runtime_error("Truncated image field");p+=n0;}std::string tag(){auto v=data(16);auto p0=std::find(v.begin(),v.end(),0);return std::string(v.begin(),p0);}};
std::vector<Member> abmp(const qlie::Bytes&b){if(b.size()<16||std::memcmp(b.data(),"abmp1",5))throw std::runtime_error("Unsupported ABMP version");Cursor r{b,16};std::vector<Member>out;while(r.p<b.size()){std::string t=r.tag();if(t.rfind("abdata",0)==0){auto d=r.data(r.n(4));if(d.size()>=4&&!std::memcmp(d.data(),"1PC\xff",4))d=qlie::decompress(d);out.push_back({t,"",std::move(d)});continue;}if(t!="abimage10"&&t!="absound10")throw std::runtime_error("Unsupported ABMP section "+t);unsigned count=r.n(1);for(unsigned i=0;i<count;i++){auto type=r.tag();std::string name=qlie::cp932(r.data(r.n(2)));if(type!="abimgdat10"&&type!="absnddat10"){r.skip(r.n(2));if(type=="abimgdat13")r.skip(12);else if(type=="abimgdat14")r.skip(76);else if(type!="abimgdat11"&&type!="abimgdat12"&&type!="absnddat11")throw std::runtime_error("Unsupported ABMP member "+type);}r.n(1);auto d=r.data(r.n(4));if(d.size()>=4&&!std::memcmp(d.data(),"1PC\xff",4))d=qlie::decompress(d);out.push_back({type,name,std::move(d)});}}return out;}
static Image canvas(unsigned w,unsigned h){if(!w||!h||w>16384||h>16384||uint64_t(w)*h>64*1024*1024)throw std::runtime_error("Invalid image dimensions");return {w,h,qlie::Bytes(size_t(w)*h*4)};}
struct JError {jpeg_error_mgr base;jmp_buf jump;char message[JMSG_LENGTH_MAX];};
static void jfail(j_common_ptr c){auto*e=reinterpret_cast<JError*>(c->err);(*c->err->format_message)(c,e->message);longjmp(e->jump,1);}
Image image(const qlie::Bytes&b){if(b.size()<4)throw std::runtime_error("Short image");if(!std::memcmp(b.data(),"DPNG",4)){Cursor r{b,4};r.n(4);unsigned count=r.n(4),w=r.n(4),h=r.n(4);Image out=canvas(w,h);if(count>100000)throw std::runtime_error("Invalid DPNG tile count");for(unsigned i=0;i<count;i++){unsigned x=r.n(4),y=r.n(4),tw=r.n(4),th=r.n(4),size=r.n(4);r.skip(8);auto data=r.data(size);if(!size)continue;Image tile=image(data);if(tile.width!=tw||tile.height!=th||x>out.width||tw>out.width-x||y>out.height||th>out.height-y)throw std::runtime_error("DPNG tile bounds mismatch");for(unsigned yy=0;yy<th;yy++)std::memcpy(out.pixels.data()+(size_t(y+yy)*w+x)*4,tile.pixels.data()+size_t(yy)*tw*4,size_t(tw)*4);}if(r.p!=b.size())throw std::runtime_error("DPNG trailing bytes");return out;}
 if(b.size()>=8&&!png_sig_cmp(b.data(),0,8)){png_image pi{};pi.version=PNG_IMAGE_VERSION;if(!png_image_begin_read_from_memory(&pi,b.data(),b.size()))throw std::runtime_error(pi.message);Image out=canvas(pi.width,pi.height);pi.format=PNG_FORMAT_BGRA;if(!png_image_finish_read(&pi,nullptr,out.pixels.data(),0,nullptr)){std::string error=pi.message;png_image_free(&pi);throw std::runtime_error(error);}png_image_free(&pi);return out;}
 if(b[0]==255&&b[1]==216){jpeg_decompress_struct c{};JError e{};c.err=jpeg_std_error(&e.base);e.base.error_exit=jfail;if(setjmp(e.jump)){jpeg_destroy_decompress(&c);throw std::runtime_error(e.message);}jpeg_create_decompress(&c);jpeg_mem_src(&c,b.data(),b.size());jpeg_read_header(&c,TRUE);c.out_color_space=JCS_RGB;jpeg_start_decompress(&c);Image out=canvas(c.output_width,c.output_height);qlie::Bytes row(size_t(c.output_width)*3);while(c.output_scanline<c.output_height){unsigned y=c.output_scanline;auto*rp=row.data();jpeg_read_scanlines(&c,&rp,1);for(unsigned x=0;x<out.width;x++){auto*dst=out.pixels.data()+(size_t(y)*out.width+x)*4;dst[0]=row[3*x+2];dst[1]=row[3*x+1];dst[2]=row[3*x];dst[3]=255;}}jpeg_finish_decompress(&c);jpeg_destroy_decompress(&c);return out;}
 throw std::runtime_error("Unsupported image signature");
}
}
