// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_text.hpp"
#include "state_io.hpp"
#include "story.hpp"
extern "C" {
#include "font.h"
}
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace whj2 {
MessageText::MessageText(qlie::Assets&assets){std::istringstream in(qlie::cp932(assets.read("script/config/default.txt")));std::string line;while(std::getline(in,line)){auto p=line.find('=');if(p!=line.npos&&trim(line.substr(0,p))=="TextSpeed")speed=std::stod(trim(line.substr(p+1)));}}
void MessageText::index(){boundaries.clear();for(size_t p=0;p<text.size();){unsigned c=uint8_t(text[p]);size_t n=c<128?1:(c&224)==192?2:(c&240)==224?3:(c&248)==240?4:0;if(!n||p+n>text.size())throw std::runtime_error("Invalid message UTF8");p+=n;boundaries.push_back(p);}}
void MessageText::start(const std::string&value){text=value;index();elapsed=0;}
void MessageText::update(double ms){elapsed+=std::max(0.,ms);}
size_t MessageText::visible()const{return speed<=0?boundaries.size():std::min(boundaries.size(),size_t(elapsed/speed));}
bool MessageText::finished()const{return visible()==boundaries.size();}
bool MessageText::reveal(){if(finished())return false;elapsed=speed*boundaries.size();return true;}
Image MessageText::render(KFont*font,const MessageStyle&style)const{if(!style.width||!style.height)throw std::runtime_error("Message surface dimensions");Image out{style.width,style.height,qlie::Bytes(size_t(style.width)*style.height*4)};KImage dst{0,0,out.width,out.height,size_t(out.width)*4,out.pixels.data()};unsigned x=0,y=0;size_t count=visible();
 for(size_t i=0,p=0;i<count;++i){uint32_t cp=uint8_t(text[p++]);unsigned tail=0;if(cp>=240){cp&=7;tail=3;}else if(cp>=224){cp&=15;tail=2;}else if(cp>=192){cp&=31;tail=1;}while(tail--)cp=(cp<<6)|(uint8_t(text[p++])&63);
  unsigned width=cp<128?std::max(1u,style.fontWidth/2):style.fontWidth;
  if(cp=='\n'||x+width>style.width){x=0;y+=style.fontHeight;if(cp=='\n')continue;}
  if(y+style.fontHeight>style.height)break;
  kfont_draw_outline(font,&dst,cp,int(x),int(y),width,style.fontHeight,style.color);x+=width;
 }return out;
}
qlie::Bytes MessageText::save()const{StateWriter w;w.str("WHJ2-message-1");w.str(text);w.f(float(elapsed));w.f(float(speed));return w.bytes;}
void MessageText::restore(const qlie::Bytes&b){StateReader r{b};if(r.str()!="WHJ2-message-1")throw std::runtime_error("Message state version");text=r.str();elapsed=r.f();speed=r.f();r.end();index();}
}
