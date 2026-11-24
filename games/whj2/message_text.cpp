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
#include <iostream>
namespace whj2 {
static std::vector<uint32_t> codepoints(const std::string&value){
 std::vector<uint32_t> result;
 for(size_t p=0;p<value.size();){uint32_t cp=uint8_t(value[p++]);unsigned tail=0;if(cp>=240){cp&=7;tail=3;}else if(cp>=224){cp&=15;tail=2;}else if(cp>=192){cp&=31;tail=1;}if(p+tail>value.size())throw std::runtime_error("Invalid message UTF8");while(tail--)cp=(cp<<6)|(uint8_t(value[p++])&63);result.push_back(cp);}
 return result;
}
MessageText::MessageText(qlie::Assets&assets,bool useStoryLineBreaking):storyLineBreaking(useStoryLineBreaking){
 std::istringstream in(qlie::cp932(assets.read("script/config/default.txt")));std::string line;
 while(std::getline(in,line)){auto p=line.find('=');if(p!=line.npos&&trim(line.substr(0,p))=="TextSpeed")speed=std::stod(trim(line.substr(p+1)));}
 // OF headers and atlas coordinates follow the original bitmap-font loader.
 std::istringstream replacementFile(qlie::cp932(assets.read("avg/imoscripter_reptextlist.txt")));
 while(std::getline(replacementFile,line)){auto p=line.find('=');if(p!=line.npos)replacements.push_back({trim(line.substr(0,p)),trim(line.substr(p+1))});}
 auto bitmap=assets.read("system/font/外字.of");
 auto word=[&](size_t at){if(at+2>bitmap.size())throw std::runtime_error("Truncated OF font");return unsigned(bitmap[at])|(unsigned(bitmap[at+1])<<8);};
 if(bitmap.size()<0x1044a)throw std::runtime_error("Truncated OF header");
 unsigned cellWidth=word(0x10440),cellHeight=word(0x10442),atlasWidth=word(0x10445),atlasHeight=word(0x10447),banks=bitmap[0x10444];
 if(!cellWidth||!cellHeight||cellWidth>256||cellHeight>256||!atlasWidth||!atlasHeight||size_t(banks)*atlasWidth*atlasHeight>bitmap.size()-0x1044a)throw std::runtime_error("Invalid OF atlas");
 for(const auto&replacement:replacements){
  auto value=replacement.second;
  if(value.rfind("[f,6]",0)!=0||value.size()<8||value.substr(value.size()-3)!="[f]")throw std::runtime_error("Unsupported external glyph mapping");
  auto characters=codepoints(value.substr(5,value.size()-8));
  if(characters.size()!=1||characters[0]<0xff10||characters[0]>0xff19)throw std::runtime_error("Unsupported OF character");
  uint32_t cp=characters[0];unsigned index=0x24f+cp-0xff10,tile=word(0x40+index*2),bank=tile>>8,slot=tile&255;
  unsigned sx=(slot&15)*cellWidth,sy=(slot>>4)*cellHeight;
  if(!tile||bank>=banks||sx+cellWidth>atlasWidth||sy+cellHeight>atlasHeight)throw std::runtime_error("Invalid OF glyph tile");
  BitmapGlyph glyph{cp,cellWidth,cellHeight,qlie::Bytes(size_t(cellWidth)*cellHeight),0,0,{}};
  for(unsigned y=0;y<cellHeight;y++)for(unsigned x=0;x<cellWidth;x++){
   auto paletteIndex=bitmap[0x1044a+size_t(bank)*atlasWidth*atlasHeight+size_t(sy+y)*atlasWidth+sx+x];
   glyph.alpha[size_t(y)*cellWidth+x]=bitmap[0x10040+size_t(paletteIndex)*4+3];
  }
  bitmapGlyphs.push_back(std::move(glyph));
 }
 if(!storyLineBreaking)return;
 std::istringstream rules(qlie::cp932(assets.read("misc/value.s")));
 while(std::getline(rules,line)){
  auto p=line.find('=');if(p==line.npos)continue;
  auto key=trim(line.substr(0,p)),value=trim(line.substr(p+1));
  if(key=="\\cal,#KinsokuWordLength")lineReserve=unsigned(std::stoul(value));
  else if(key=="\\cal,#GyoutouKinsokuWord"||key=="\\cal,#GyoumatuKinsokuWord"||key=="\\cal,#IgnoreReturnSpaceWord"){
   if(value.size()<2||value.front()!='"'||value.back()!='"')throw std::runtime_error("Invalid line breaking configuration");
   auto chars=codepoints(value.substr(1,value.size()-2));
   (key=="\\cal,#GyoutouKinsokuWord"?lineStartForbidden:key=="\\cal,#GyoumatuKinsokuWord"?lineEndForbidden:noInitialIndent)=std::move(chars);
  }
 }
}
std::string messageDisplayName(const std::string&raw){if(raw.empty())return {};auto name=raw;if(name.rfind("【",0)==0){name.erase(0,3);auto end=name.find("】");if(end!=name.npos)name.resize(end);}auto alias=name.find("＠");if(alias!=name.npos)name=name.substr(alias+3);return "【"+name+"】";}
void MessageText::index(){glyphs.clear();double time=0,currentSpeed=speed;bool bitmapFont=false;
 std::string display=text;
 for(const auto&replacement:replacements){size_t p=0;while((p=display.find(replacement.first,p))!=display.npos){display.replace(p,replacement.first.size(),replacement.second);p+=replacement.second.size();}}

 auto append=[&](const std::string&value,bool center){size_t begin=glyphs.size();for(auto cp:codepoints(value)){if(cp!='\n')time+=std::max(0.,currentSpeed);glyphs.push_back({cp,time,0,bitmapFont});}if(center&&glyphs.size()>begin)glyphs[begin].centeredRun=glyphs.size()-begin;};
 for(size_t pos=0;pos<display.size();){
  if(display[pos]=='['){auto end=display.find(']',pos+1);if(end!=display.npos){auto token=display.substr(pos+1,end-pos-1);auto comma=token.find(',');auto command=token.substr(0,comma);
   if(command=="f"){if(comma==token.npos)bitmapFont=false;else if(token.substr(comma+1)=="6")bitmapFont=true;else throw std::runtime_error("Unsupported message font slot");pos=end+1;continue;}
   if(command=="n"){append("\n",false);pos=end+1;continue;}
   if(command=="spd"){currentSpeed=comma==token.npos?speed:std::stod(token.substr(comma+1));if(currentSpeed<0)currentSpeed=speed;pos=end+1;continue;}
   if(command=="pc"&&comma!=token.npos){append(token.substr(comma+1),true);pos=end+1;continue;}
   std::cerr<<"PENDING-TEXT tag="<<command<<"\n";
  }}
  size_t end=display.find('[',pos+1);if(end==display.npos)end=display.size();append(display.substr(pos,end-pos),false);pos=end;
 }
}
void MessageText::setSpeed(double value){auto count=visible();speed=std::clamp(value,0.,100.);index();elapsed=count?glyphs[count-1].deadline:0;}
void MessageText::start(const std::string&value){text=value;index();elapsed=0;}
void MessageText::update(double ms){elapsed+=std::max(0.,ms);}
size_t MessageText::visible()const{return size_t(std::upper_bound(glyphs.begin(),glyphs.end(),elapsed,[](double time,const Glyph&g){return time<g.deadline;})-glyphs.begin());}
size_t MessageText::timedCharacters(size_t first,size_t last)const{first=std::min(first,glyphs.size());last=std::min(last,glyphs.size());size_t count=0;for(size_t i=first;i<last;++i)count+=glyphs[i].codepoint!='\n';return count;}
bool MessageText::finished()const{return visible()==glyphs.size();}
bool MessageText::reveal(){if(finished())return false;elapsed=glyphs.empty()?0:glyphs.back().deadline;return true;}
std::vector<MessageText::PositionedGlyph> MessageText::layout(const MessageStyle&style)const{
 if(!style.width||!style.fontWidth||!style.fontHeight)throw std::runtime_error("Message layout dimensions");
 std::vector<PositionedGlyph> positions;unsigned x=0,y=0;size_t count=visible();bool paragraphStart=true;
 for(size_t i=0;i<count;++i){uint32_t cp=glyphs[i].codepoint;
  if(cp=='\n'){x=0;y+=style.fontHeight;paragraphStart=true;continue;}
  if(glyphs[i].centeredRun){unsigned width=0;for(size_t j=i;j<i+glyphs[i].centeredRun;++j)width+=glyphs[j].codepoint<128?std::max(1u,style.fontWidth/2):style.fontWidth;x=style.width>width?(style.width-width)/2:0;paragraphStart=false;}
  if(paragraphStart){if(storyLineBreaking&&std::find(noInitialIndent.begin(),noInitialIndent.end(),cp)==noInitialIndent.end())x=style.fontWidth;paragraphStart=false;}
  unsigned width=cp<128?std::max(1u,style.fontWidth/2):style.fontWidth;
  const bool startForbidden=std::find(lineStartForbidden.begin(),lineStartForbidden.end(),cp)!=lineStartForbidden.end();
  const bool endForbidden=std::find(lineEndForbidden.begin(),lineEndForbidden.end(),cp)!=lineEndForbidden.end();
  const unsigned remaining=(style.width-std::min(x,style.width))/std::max(1u,style.fontWidth);
  const bool earlyWrap=storyLineBreaking&&!startForbidden&&remaining<=lineReserve+unsigned(endForbidden);
  if(x+width>style.width||(x&&earlyWrap)){x=storyLineBreaking?style.fontWidth:0;y+=style.fontHeight;}
  positions.push_back({cp,x,y,width,glyphs[i].bitmap});x+=width;
 }return positions;
}
size_t MessageText::lineCount(const MessageStyle&style)const{auto positions=layout(style);return positions.empty()?0:size_t(positions.back().y/style.fontHeight)+1;}
Image MessageText::render(KFont*font,const MessageStyle&style,size_t firstLine)const{
 if(!style.width||!style.height)throw std::runtime_error("Message surface dimensions");
 Image out{style.width,style.height,qlie::Bytes(size_t(style.width)*style.height*4)};KImage dst{0,0,out.width,out.height,size_t(out.width)*4,out.pixels.data()};
 const size_t top=firstLine*style.fontHeight;
 for(auto&glyph:layout(style)){if(glyph.y<top||size_t(glyph.y)-top+style.fontHeight>style.height)continue;if(!glyph.bitmap){kfont_draw_outline(font,&dst,glyph.codepoint,int(glyph.x),int(size_t(glyph.y)-top),glyph.width,style.fontHeight,style.color);continue;}
  auto found=std::find_if(bitmapGlyphs.begin(),bitmapGlyphs.end(),[&](const BitmapGlyph&value){return value.codepoint==glyph.codepoint;});
  if(found==bitmapGlyphs.end())throw std::runtime_error("Missing OF glyph");
  if(found->scaledWidth!=glyph.width||found->scaledHeight!=style.fontHeight){
  found->scaled.resize(size_t(glyph.width)*style.fontHeight);
  // Original scaling uses 10-bit source steps and 7-bit bilinear weights.
  unsigned dx=(found->width<<10)/glyph.width,dy=(found->height<<10)/style.fontHeight;
  for(unsigned y=0;y<style.fontHeight;y++)for(unsigned x=0;x<glyph.width;x++){
   unsigned fx=x*dx,fy=y*dy,sx=fx>>10,sy=fy>>10,nx=std::min(sx+1,found->width-1),ny=std::min(sy+1,found->height-1);
   int wx=int(((~fx)&1023)>>3),wy=int(((~fy)&1023)>>3);
   auto sample=[&](unsigned xx,unsigned yy){return int(found->alpha[size_t(yy)*found->width+xx]);};
   int a=sample(nx,sy)+((sample(sx,sy)-sample(nx,sy))*wx>>7),b=sample(nx,ny)+((sample(sx,ny)-sample(nx,ny))*wx>>7);
   found->scaled[size_t(y)*glyph.width+x]=uint8_t(b+((a-b)*wy>>7));
  }
  found->scaledWidth=glyph.width;found->scaledHeight=style.fontHeight;
  }
  kfont_draw_mask(font,&dst,found->scaled.data(),glyph.width,style.fontHeight,int(glyph.x),int(size_t(glyph.y)-top),style.color);}
 return out;
}
qlie::Bytes MessageText::save()const{StateWriter w;w.str("WHJ2-message-1");w.str(text);w.f(float(elapsed));w.f(float(speed));return w.bytes;}
void MessageText::restore(const qlie::Bytes&b){StateReader r{b};if(r.str()!="WHJ2-message-1")throw std::runtime_error("Message state version");text=r.str();elapsed=r.f();speed=r.f();r.end();index();}
}
