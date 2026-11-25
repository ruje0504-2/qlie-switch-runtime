// SPDX-License-Identifier: GPL-2.0-or-later
// DPNG/ABMP layouts follow GARbro (MIT, morkt); see THIRD_PARTY.md.
#include "image.hpp"
#include "story.hpp"
#include <array>
#include "render_queue.hpp"
#include <png.h>
#include <cstdio>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
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
Image characterBlurCanvas(const Image&source,unsigned margin){
 // The character filter pads the top and the two sides.
 // WHJ3's canvas handler fills the margin with transparent 0x00888888.
 const unsigned width=source.width+margin*2,height=source.height+margin;
 Image padded{width,height,qlie::Bytes(size_t(width)*height*4,0x88)};
 for(size_t i=3;i<padded.pixels.size();i+=4)padded.pixels[i]=0;
 for(unsigned y=0;y<source.height;++y)
  std::memcpy(padded.pixels.data()+(size_t(y+margin)*width+margin)*4,
              source.pixels.data()+size_t(y)*source.width*4,size_t(source.width)*4);
 return padded;
}

using FilterFields=std::vector<std::string_view>;
FilterFields filterFields(std::string_view expression,char delimiter){
 FilterFields result;
 while(!expression.empty()){
  const auto split=expression.find(delimiter);
  result.push_back(expression.substr(0,split));
  if(split==std::string_view::npos)break;
  expression.remove_prefix(split+1);
 }
 return result;
}
int filterNumber(std::string_view field){
 const auto base=!field.empty()&&field.front()=='$'?16:10;
 if(base==16)field.remove_prefix(1);
 int number=0;
 const auto parsed=std::from_chars(field.data(),field.data()+field.size(),number,base);
 if(parsed.ec!=std::errc{}||parsed.ptr!=field.data()+field.size())
  throw std::runtime_error("Invalid image filter parameter "+std::string(field));
 return number;
}
void colorRamp(std::array<uint32_t,256>&ramp,unsigned first,unsigned last,uint32_t from,uint32_t to){
 if(first==last){ramp[first]=to;return;}
 for(unsigned level=first;level<=last;level++){
  const auto weight=(level-first)*1024/(last-first);
  for(unsigned shift:{0u,8u,16u}){
   const unsigned channel=(((from>>shift)&255)*(1024-weight)+((to>>shift)&255)*weight)>>10;
   ramp[level]=(ramp[level]&~(255u<<shift))|(channel<<shift);
  }
 }
}
Image applyLumaRamp(const Image&source,const std::array<uint32_t,256>&ramp){
 Image result=source;
 for(size_t i=0;i<result.pixels.size();i+=4){
  auto*pixel=result.pixels.data()+i;
  const auto color=ramp[(unsigned(pixel[2])*77+unsigned(pixel[1])*150+unsigned(pixel[0])*29)>>8];
  for(unsigned c=0;c<3;c++)pixel[c]=uint8_t(color>>(8*c));
 }
 return result;
}
Image gradientMap(const Image&source,const FilterFields&args){
 // WHJ2.5 4d8888 builds two ramps. 4d6fd0 indexes them with 8-bit luma.
 std::array<uint32_t,256> ramp{};
 const auto shadow=uint32_t(filterNumber(args[1]))&0xffffff;
 const auto midtone=uint32_t(filterNumber(args[2]))&0xffffff;
 const auto highlight=uint32_t(filterNumber(args[3]))&0xffffff;
 const auto pivot=unsigned(std::clamp(filterNumber(args[4]),0,255));
 colorRamp(ramp,0,pivot,shadow,midtone);
 colorRamp(ramp,pivot,255,midtone,highlight);
 return applyLumaRamp(source,ramp);
}
Image binaryMap(const Image&source,const FilterFields&args){
 // WHJ2.5 4d38a4 builds a constant/ramp/constant luminance lookup.
 const int threshold=std::clamp(filterNumber(args[1]),0,255);
 const auto low=uint32_t(filterNumber(args[2]))&0xffffff;
 const auto high=uint32_t(filterNumber(args[3]))&0xffffff;
 const int halfWidth=int(128*(1-std::clamp(filterNumber(args[4]),0,100)/100.f)+.5f);
 const auto black=unsigned(std::max(0,threshold-halfWidth));
 const auto white=unsigned(std::min(255,threshold+halfWidth));
 std::array<uint32_t,256> ramp{};
 colorRamp(ramp,0,black,low,low);
 colorRamp(ramp,black,white,low,high);
 colorRamp(ramp,white,255,high,high);
 return applyLumaRamp(source,ramp);
}
float percent(std::string_view value){
 return std::clamp(filterNumber(value),-100,100)/100.f;
}
Image chromaImage(const Image&source,const FilterFields&args){
 // 4d6958 mixes each channel with equal contributions from its two neighbors.
 const int others=int(std::round(-percent(args[1])*85));
 const int main=255-others*2;
 Image result=source;
 for(size_t i=0;i<source.pixels.size();i+=4){
  const int total=source.pixels[i]+source.pixels[i+1]+source.pixels[i+2];
  for(unsigned c=0;c<3;c++){
   const int own=source.pixels[i+c];
   result.pixels[i+c]=uint8_t(std::clamp((own*main+(total-own)*others)/256,0,255));
  }
 }
 return result;
}
int towardLimit(int value,float amount){
 if(amount<0)return int(value*(1+amount)+.5f);
 return int(value*(1-amount)+255*amount+.5f);
}
Image hueChromaImage(const Image&source,const FilterFields&args){
 // 4d46cc/4d482c convert 8-bit RGB using integer HSV and a 256-step sector.
 const int hueShift=std::clamp(filterNumber(args[1]),-180,180);
 const float saturationChange=percent(args[2]),intensity=percent(args[3]);
 const bool absolute=filterNumber(args[4])==1;
 Image result=source;
 for(size_t i=0;i<source.pixels.size();i+=4){
  const int red=source.pixels[i+2],green=source.pixels[i+1],blue=source.pixels[i];
  const int top=std::max({red,green,blue});
  const int span=top-std::min({red,green,blue});
  int hue=0,sat=0;
  if(span){
   if(top==red)hue=(green-blue)*256/span;
   if(top==green)hue=(blue-red)*256/span+512;
   if(top==blue)hue=(red-green)*256/span+1024;
   hue=hue*60/256;
   sat=span*256/top;
  }
  hue=((absolute?hueShift:hue+hueShift)+720)%360;
  sat=std::clamp(towardLimit(sat,saturationChange),0,255);
  const int sector=hue/60;
  const int step=(hue%60)*256/60;
  const int floor=(255-sat)*top/256;
  const int mix=(255-sat*(sector%2?step:256-step)/256)*top/256;
  const std::array<std::array<int,3>,6> colors{{
   {floor,mix,top},{floor,top,mix},{mix,top,floor},
   {top,mix,floor},{top,floor,mix},{mix,floor,top}
  }};
  for(unsigned c=0;c<3;c++){
   const int value=colors[sector][c];
   result.pixels[i+c]=uint8_t(intensity==0?value:towardLimit(value,intensity));
  }
 }
 return result;
}
Image brightnessImage(const Image&source,const FilterFields&args){
 // 4d65c8 converts brightness/contrast to source and destination endpoints.
 const float brightness=percent(args[1]),contrast=percent(args[2]);
 const int adjustment=int(std::round(brightness*250));
 const int margin=int(std::round(std::abs(contrast)*125));
 const int inputLow=contrast>=0?margin-adjustment:0;
 const int inputHigh=contrast>=0?int(std::round(255-contrast*125))-adjustment:255;
 const int outputLow=contrast>=0?0:margin+adjustment;
 const int outputHigh=contrast>=0?255:int(std::round(255+contrast*125))+adjustment;
 const float slope=float(outputHigh-outputLow)/(inputHigh-inputLow);
 std::array<uint8_t,256> ramp{};
 for(unsigned i=0;i<ramp.size();i++)
  ramp[i]=uint8_t(std::clamp(int(std::round(outputLow+slope*(int(i)-inputLow))),0,255));
 Image result=source;
 for(size_t i=0;i<result.pixels.size();i+=4)
  for(unsigned c=0;c<3;c++)result.pixels[i+c]=ramp[result.pixels[i+c]];
 return result;
}
Image sphereImage(const Image&source,const FilterFields&args){
 // WHJ2.5 4d4c08 and 4d4d5c project the original 16-by-16 mesh
 // before rasterizing it. A disabled axis does not contribute to its depth.
 constexpr unsigned divisions=16;
 if(source.width<divisions||source.height<divisions)return source;
 const float strength=std::clamp(filterNumber(args[1]),-100,100)/100.f;
 const bool horizontal=filterNumber(args[2])==1,vertical=filterNumber(args[3])==1;
 if(!strength||(!horizontal&&!vertical))return source;
 constexpr float halfPi=1.5707963267948966f;
 const float centerX=source.width*.5f,centerY=source.height*.5f;
 std::array<float,divisions+1> xCurve{},yCurve{};
 for(unsigned i=0;i<=divisions;i++){
  const float normalized=std::abs(2.f*i/divisions-1.f);
  xCurve[i]=horizontal?std::cos(halfPi*normalized):0.f;
  yCurve[i]=vertical?std::cos(halfPi*normalized):0.f;
 }
 std::array<std::array<TextureVertex,divisions+1>,divisions+1> vertices{};
 const float weight=horizontal&&vertical?.5f:1.f;
 for(unsigned y=0;y<=divisions;y++)for(unsigned x=0;x<=divisions;x++){
  const float u=float(source.width*x)/divisions,v=float(source.height*y)/divisions;
  const float scale=1000.f/(1000.f-(xCurve[x]+yCurve[y])*weight*strength*600.f);
  vertices[y][x]={centerX+(u-centerX)*scale,centerY+(v-centerY)*scale,1000.f,u,v};
 }
 RenderQueue renderer;
 for(unsigned y=0;y<divisions;y++)for(unsigned x=0;x<divisions;x++){
  const auto&topLeft=vertices[y][x];const auto&topRight=vertices[y][x+1];
  const auto&bottomLeft=vertices[y+1][x];const auto&bottomRight=vertices[y+1][x+1];
  renderer.triangle(source,{topLeft,topRight,bottomRight},0,255,0,0xffffff,0,2);
  renderer.triangle(source,{topLeft,bottomRight,bottomLeft},0,255,0,0xffffff,0,2);
 }
 Image result{source.width,source.height,qlie::Bytes(source.pixels.size())};
 renderer.draw(result.pixels,result.width,result.height);
 return result;
}
Image imageFilterChain(const Image&source,std::string_view expression){
 Image result;
 const Image* current=&source;
 for(const auto operation:filterFields(expression,'/')){
  const auto fields=filterFields(operation,',');
  if(fields.size()==5&&fields[0]=="gmap3")result=gradientMap(*current,fields);
  else if(fields.size()==4&&fields[0]=="sphere")result=sphereImage(*current,fields);
  else if(fields.size()==5&&fields[0]=="binalize")result=binaryMap(*current,fields);
  else if(fields.size()==2&&fields[0]=="chroma")result=chromaImage(*current,fields);
  else if(fields.size()==5&&fields[0]=="huechroma")result=hueChromaImage(*current,fields);
  else if(fields.size()==3&&fields[0]=="bright")result=brightnessImage(*current,fields);
  else if(operation=="blur,10,10")result=boxBlur<5>(*current);
  else if(operation=="blur,12,12")result=boxBlur<6>(*current);
  else if(operation=="blur,15,15")result=boxBlur<7>(*current);
  else if(operation=="blur,20,20")result=boxBlur<10>(*current);
  else throw std::runtime_error("Unsupported image filter "+std::string(operation));
  current=&result;
 }
 if(current==&source)throw std::runtime_error("Empty image filter list");
 return result;
}
}
static Image gradientOverlay(const Image&source,const std::string&name){
 const auto fields=split(name.substr(1,name.size()-2),',');
 if(fields.size()!=13||(fields[1]!="vertical"&&fields[1]!="horizontal")||fields[11]!="normal")
  throw std::runtime_error("Unsupported gradient overlay layout");
 const auto number=[](const std::string&v){return v.front()=='$'?int(std::stoul(v.substr(1),nullptr,16)):std::stoi(v);};
 struct Stop {int position;std::array<int,4> rgba;};
 std::array<Stop,3> stops;
 for(unsigned i=0;i<stops.size();++i){
  const auto rgb=unsigned(number(fields[3+i*2]));
  stops[i]={number(fields[8+i]),{int(rgb&255),int((rgb>>8)&255),
             int((rgb>>16)&255),std::clamp(number(fields[2+i*2]),0,255)}};
 }
 std::stable_sort(stops.begin(),stops.end(),[](const auto&a,const auto&b){return a.position<b.position;});
 const bool vertical=fields[1]=="vertical";
 const int length=vertical?source.height:source.width;
 for(unsigned i=0;i<stops.size();++i)stops[i].position=std::clamp(stops[i].position,i?1:0,100)*length/100;
 const int opacity=std::clamp(number(fields[12]),0,255);
 Image out{source.width,source.height,source.pixels};
 std::vector<std::array<int,4>> colors(length);
 for(int coordinate=0;coordinate<length;++coordinate){
  const unsigned segment=coordinate<stops[1].position?0:1;
  const auto&from=stops[segment];
  const auto&to=stops[segment+1];
  const int span=to.position-from.position;
  const int step=span?std::clamp((coordinate-from.position)*127/span,0,127):127;
  for(unsigned channel=0;channel<4;++channel)
   colors[coordinate][channel]=(from.rgba[channel]*(127-step)+to.rgba[channel]*step)/127;
 }
 for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){
  const auto&color=colors[vertical?y:x];
  const auto a=color[3]*opacity/255;
  auto*pixel=out.pixels.data()+(size_t(y)*out.width+x)*4;
  for(unsigned channel=0;channel<3;++channel)
   pixel[channel]=uint8_t((pixel[channel]*(255-a)+color[channel]*a)/255);
 }
 return out;
}
void applySepia(qlie::Bytes& pixels){
 constexpr unsigned color[3]={17,34,68};
 for(size_t i=0;i<pixels.size();i+=4){
  const unsigned gray=(unsigned(pixels[i])+pixels[i+1]+pixels[i+2])*85>>8;
  for(unsigned c=0;c<3;c++)pixels[i+c]=uint8_t(gray+color[c]-gray*color[c]/255);
 }
}
Image filterImage(const Image&source,const std::string&name){
 if(!source.width||!source.height||source.pixels.size()!=size_t(source.width)*source.height*4)throw std::runtime_error("Filter source dimensions");
 if(const auto separator=name.find('\\');separator!=name.npos)
  return filterImage(filterImage(source,name.substr(0,separator)),name.substr(separator+1));
 if(name.rfind("\"goverlay,",0)==0&&name.back()=='"')return gradientOverlay(source,name);
 if(name=="charablur10")return boxBlur<5>(characterBlurCanvas(source,10));
 if(name=="charablur20")return boxBlur<10>(characterBlurCanvas(source,20));
 if(name=="blur2"||name=="[blur,2,2]")return boxBlur<1>(source);
 if(name=="blur5"||name=="\"blur,4,4\""||name=="\"blur,5,5\"")return boxBlur<2>(source);
 if(name=="blur8")return boxBlur<4>(source);
 if(name=="blur10")return boxBlur<5>(source);
 if(name=="blur20")return boxBlur<10>(source);
 if(name.size()>2&&name.front()=='['&&name.back()==']')
  return imageFilterChain(source,std::string_view(name).substr(1,name.size()-2));
 Image out{source.width,source.height,source.pixels};
 if(name=="none"||name.empty())return out;
 if(name=="sepia"){applySepia(out.pixels);return out;}
 if(name=="mono"){
  for(size_t i=0;i<out.pixels.size();i+=4){
   const auto gray=uint8_t((unsigned(source.pixels[i])+source.pixels[i+1]+source.pixels[i+2])*85>>8);
   std::fill_n(out.pixels.begin()+i,3,gray);
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
