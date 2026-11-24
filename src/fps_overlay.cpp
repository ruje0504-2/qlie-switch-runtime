// SPDX-License-Identifier: GPL-2.0-or-later
// Glyph rows and four-pass styling adapted from biko3-switch-runtime (0BSD).
#include "fps_overlay.hpp"
#include "render_queue.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace whj2 {
namespace {
const unsigned char glyphs[13][7]={
 {31,16,16,30,16,16,16},{30,17,17,30,16,16,16},{15,16,16,14,1,1,30},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
bool pixel(char c,unsigned x,unsigned y){if(c=='[')return x==0||((y==0||y==6)&&x<4);if(c==']')return x==4||((y==0||y==6)&&x>0);const char*map="FPS0123456789";const char*found=std::strchr(map,c);return found&&(glyphs[found-map][y]&(16>>x));}
}
void FpsOverlay::presented(uint64_t now){if(!started){started=true;sampleStart=now;return;}++frames;auto elapsed=now-sampleStart;if(elapsed>=1000){value=unsigned((frames*1000+elapsed/2)/elapsed);sampleStart=now;frames=0;}}
void FpsOverlay::rebuild(){char text[32];std::snprintf(text,sizeof(text),"FPS [%u]",value);label={unsigned(std::strlen(text)*18+2),24,{}};label.pixels.resize(size_t(label.width)*label.height*4);
 for(unsigned pass=0;pass<4;pass++)for(unsigned i=0;text[i];i++)for(unsigned y=0;y<7;y++)for(unsigned x=0;x<5;x++)if(pixel(text[i],x,y))for(unsigned yy=0;yy<3;yy++)for(unsigned xx=0;xx<3;xx++){unsigned px=i*18+x*3+xx+(pass<2?1+pass:pass-2),py=y*3+yy;auto*out=&label.pixels[(py*label.width+px)*4];out[0]=pass>=2?255:0;out[1]=pass>=2?216:0;out[2]=pass>=2?128:0;out[3]=255;}
 label.version.publish();
 renderedValue=value;
}
void FpsOverlay::draw(qlie::Bytes&pixels){if(pixels.size()!=1280*720*4)throw std::runtime_error("FPS canvas size");if(renderedValue!=value)rebuild();for(unsigned y=0;y<label.height;y++)for(unsigned x=0;x<label.width;x++){const auto*src=&label.pixels[(y*label.width+x)*4];if(src[3])std::memcpy(&pixels[((696+y)*1280+x)*4],src,4);}}
void FpsOverlay::draw(RenderTarget&target){if(renderedValue!=value)rebuild();RenderQueue q;q.image(label,imagePlacement(0,696,1,1,0,0,0,label.width,label.height),0,255,0,0xffffff);q.draw(target,1280,720);}

}
