// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "message_text.hpp"
#include "profile.hpp"
#include "state_io.hpp"
#include "story.hpp"
extern "C" {
#include "font.h"
}
#include <algorithm>
#include <cstring>
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
 std::istringstream in(assets.text("script/config/default.txt"));std::string line;
 while(std::getline(in,line)){auto p=line.find('=');if(p!=line.npos&&trim(line.substr(0,p))=="TextSpeed")speed=std::stod(trim(line.substr(p+1)));}
 std::vector<std::pair<uint32_t,unsigned>> externalGlyphs;
 if(const auto*legacy=game_profile::story.legacyAvg){
  externalGlyphs.emplace_back(legacy->externalCodepoint,legacy->externalTileIndex);
 }else{
 // OF headers and atlas coordinates follow the original bitmap-font loader.
 std::istringstream replacementFile(assets.text("avg/imoscripter_reptextlist.txt"));
 while(std::getline(replacementFile,line)){auto p=line.find('=');if(p!=line.npos)replacements.push_back({trim(line.substr(0,p)),trim(line.substr(p+1))});}
 for(const auto&replacement:replacements){
  const auto& value=replacement.second;
  if(value.rfind("[f,6]",0)!=0)continue;
  if(value.size()<8||value.substr(value.size()-3)!="[f]")throw std::runtime_error("Unsupported external glyph mapping");
  const auto characters=codepoints(value.substr(5,value.size()-8));
  if(characters.size()!=1||characters[0]<0xff10||characters[0]>0xff19)throw std::runtime_error("Unsupported OF character");
  externalGlyphs.emplace_back(characters[0],0x24f+characters[0]-0xff10);
 }
 }
 if(!externalGlyphs.empty()){
 auto bitmap=assets.read("system/font/外字.of");
 auto word=[&](size_t at){if(at+2>bitmap.size())throw std::runtime_error("Truncated OF font");return unsigned(bitmap[at])|(unsigned(bitmap[at+1])<<8);};
 if(bitmap.size()<0x1044a)throw std::runtime_error("Truncated OF header");
 unsigned cellWidth=word(0x10440),cellHeight=word(0x10442),atlasWidth=word(0x10445),atlasHeight=word(0x10447),banks=bitmap[0x10444];
 if(!cellWidth||!cellHeight||cellWidth>256||cellHeight>256||!atlasWidth||!atlasHeight||size_t(banks)*atlasWidth*atlasHeight>bitmap.size()-0x1044a)throw std::runtime_error("Invalid OF atlas");
 for(const auto&[cp,index]:externalGlyphs){
  unsigned tile=word(0x40+index*2),bank=tile>>8,slot=tile&255;
  unsigned sx=(slot&15)*cellWidth,sy=(slot>>4)*cellHeight;
  if(!tile||bank>=banks||sx+cellWidth>atlasWidth||sy+cellHeight>atlasHeight)throw std::runtime_error("Invalid OF glyph tile");
  BitmapGlyph glyph{cp,cellWidth,cellHeight,qlie::Bytes(size_t(cellWidth)*cellHeight),0,0,{}};
  for(unsigned y=0;y<cellHeight;y++)for(unsigned x=0;x<cellWidth;x++){
   auto paletteIndex=bitmap[0x1044a+size_t(bank)*atlasWidth*atlasHeight+size_t(sy+y)*atlasWidth+sx+x];
   glyph.alpha[size_t(y)*cellWidth+x]=bitmap[0x10040+size_t(paletteIndex)*4+3];
  }
  bitmapGlyphs.push_back(std::move(glyph));
 }
 }
 if(game_profile::story.legacyAvg||!storyLineBreaking)return;
 std::map<std::string,std::string> project;
 std::istringstream settings(assets.text("avg/projectsetting.txt"));
 while(std::getline(settings,line)){
  const auto p=line.find('=');
  if(p!=line.npos)project[trim(line.substr(0,p))]=trim(line.substr(p+1));
 }
 if(const auto pitch=project.find("LinePitch");pitch!=project.end())rubySize=unsigned(std::stoul(pitch->second));
 std::istringstream rules(assets.text("misc/value.s"));
 while(std::getline(rules,line)){
  auto p=line.find('=');if(p==line.npos)continue;
  auto key=trim(line.substr(0,p)),value=trim(line.substr(p+1));
  if(key=="\\cal,#KinsokuWordLength"){
   if(value=="ResultInt[0]")value=project.at("KinsokuLength");
   lineReserve=unsigned(std::stoul(value));
  }
  else if(key=="\\cal,#GyoutouKinsokuWord"||key=="\\cal,#GyoumatuKinsokuWord"||key=="\\cal,#IgnoreReturnSpaceWord"){
   if(value=="ResultStr[0]"){
    value=project.at(key=="\\cal,#GyoutouKinsokuWord"?"KinsokuGyoutouWord":
                     key=="\\cal,#GyoumatuKinsokuWord"?"KinsokuGyoumatuWord":"ReturnSpaceWord");
   }else{
    if(value.size()<2||value.front()!='"'||value.back()!='"')throw std::runtime_error("Invalid line breaking configuration");
    value=value.substr(1,value.size()-2);
   }
   auto chars=codepoints(value);
   (key=="\\cal,#GyoutouKinsokuWord"?lineStartForbidden:key=="\\cal,#GyoumatuKinsokuWord"?lineEndForbidden:noInitialIndent)=std::move(chars);
  }
 }
}
std::string messageDisplayName(const std::string&raw){if(raw.empty())return {};auto name=raw;if(name.rfind("【",0)==0){name.erase(0,3);auto end=name.find("】");if(end!=name.npos)name.resize(end);}auto alias=name.find("＠");if(alias!=name.npos)name=name.substr(alias+3);return "【"+name+"】";}
void MessageText::index(bool layoutChanged){if(layoutChanged)layoutVersion.publish();glyphs.clear();rubyRuns.clear();double time=0,currentSpeed=speed;bool bitmapFont=false;
 std::string display=text;
 for(const auto&replacement:replacements){size_t p=0;while((p=display.find(replacement.first,p))!=display.npos){display.replace(p,replacement.first.size(),replacement.second);p+=replacement.second.size();}}

 auto append=[&](const std::string&value,bool center){size_t begin=glyphs.size();for(auto cp:codepoints(value)){if(cp!='\n')time+=std::max(0.,currentSpeed);glyphs.push_back({cp,time,0,bitmapFont});}if(center&&glyphs.size()>begin)glyphs[begin].centeredRun=glyphs.size()-begin;};
 for(size_t pos=0;pos<display.size();){
  if(display[pos]=='['){auto end=display.find(']',pos+1);if(end!=display.npos){auto token=display.substr(pos+1,end-pos-1);auto comma=token.find(',');auto command=token.substr(0,comma);
   if(command=="f"){
    const auto slot=comma==token.npos?std::string():token.substr(comma+1);
    if(slot.empty()||(game_profile::story.legacyAvg&&(slot=="0"||slot=="1")))bitmapFont=false;
    else if(slot==(game_profile::story.legacyAvg?game_profile::story.legacyAvg->externalFontSlot:"6"))bitmapFont=true;
    else throw std::runtime_error("Unsupported message font slot "+slot);
    pos=end+1;continue;
   }
   if(command=="n"){append("\n",false);pos=end+1;continue;}
   if(command=="spd"){currentSpeed=comma==token.npos?speed:std::stod(token.substr(comma+1));if(currentSpeed<0)currentSpeed=speed;pos=end+1;continue;}
   if(command=="pc"&&comma!=token.npos){append(token.substr(comma+1),true);pos=end+1;continue;}
   if(command=="rb"&&comma!=token.npos){
    const auto separator=token.find(',',comma+1);
    if(separator!=token.npos&&separator>comma+1&&separator+1<token.size()&&
       token.find(',',separator+1)==token.npos&&token.find('\n',comma+1)==token.npos){
     const auto begin=glyphs.size();
     append(token.substr(comma+1,separator-comma-1),false);
     rubyRuns.push_back({begin,glyphs.size(),codepoints(token.substr(separator+1))});
     pos=end+1;continue;
    }
   }
   QLIE_LOG_LINE(<<"PENDING-TEXT tag="<<command<<"\n");
  }}
  size_t end=display.find('[',pos+1);if(end==display.npos)end=display.size();append(display.substr(pos,end-pos),false);pos=end;
 }
}
void MessageText::setSpeed(double value){
 value=std::clamp(value,0.,100.);
 if(value==speed)return;
 auto count=visible();
 speed=value;
 index(false); // Timing changes leave every glyph and its position intact.
 elapsed=count?glyphs[count-1].deadline:0;
}
void MessageText::start(const std::string&value){text=value;index();elapsed=0;}
void MessageText::update(double ms){elapsed+=std::max(0.,ms);}
size_t MessageText::visible()const{return size_t(std::upper_bound(glyphs.begin(),glyphs.end(),elapsed,[](double time,const Glyph&g){return time<g.deadline;})-glyphs.begin());}
size_t MessageText::timedCharacters(size_t first,size_t last)const{first=std::min(first,glyphs.size());last=std::min(last,glyphs.size());size_t count=0;for(size_t i=first;i<last;++i)count+=glyphs[i].codepoint!='\n';return count;}
bool MessageText::finished()const{return visible()==glyphs.size();}
bool MessageText::reveal(){if(finished())return false;elapsed=glyphs.empty()?0:glyphs.back().deadline;return true;}
std::vector<MessageText::PositionedGlyph> MessageText::layout(const MessageStyle&style,size_t count,std::vector<unsigned>* rowStarts)const{
 if(!style.width||!style.fontWidth||!style.fontHeight)throw std::runtime_error("Message layout dimensions");
 const unsigned rubyHeight=std::min(rubySize,style.fontHeight);
 const bool drawRuby=!rubyRuns.empty()&&rubyHeight&&style.height>=style.fontHeight+rubyHeight;
 const size_t layoutCount=drawRuby?glyphs.size():count;
 std::vector<PositionedGlyph> positions;positions.reserve(layoutCount);unsigned x=0,y=0;bool paragraphStart=true;
 for(size_t i=0;i<layoutCount;++i){uint32_t cp=glyphs[i].codepoint;
  if(cp=='\n'){x=0;y+=style.fontHeight;paragraphStart=true;continue;}
  if(glyphs[i].centeredRun){unsigned width=0;for(size_t j=i;j<i+glyphs[i].centeredRun;++j)width+=glyphs[j].codepoint<128?std::max(1u,style.fontWidth/2):style.fontWidth;x=style.width>width?(style.width-width)/2:0;paragraphStart=false;}
  if(paragraphStart){if(storyLineBreaking&&std::find(noInitialIndent.begin(),noInitialIndent.end(),cp)==noInitialIndent.end())x=style.fontWidth;paragraphStart=false;}
  unsigned width=cp<128?std::max(1u,style.fontWidth/2):style.fontWidth;
  const bool startForbidden=std::find(lineStartForbidden.begin(),lineStartForbidden.end(),cp)!=lineStartForbidden.end();
  const bool endForbidden=std::find(lineEndForbidden.begin(),lineEndForbidden.end(),cp)!=lineEndForbidden.end();
  const unsigned remaining=(style.width-std::min(x,style.width))/std::max(1u,style.fontWidth);
  const bool earlyWrap=storyLineBreaking&&!startForbidden&&remaining<=lineReserve+unsigned(endForbidden);
  if(x+width>style.width||(x&&earlyWrap)){x=storyLineBreaking?style.fontWidth:0;y+=style.fontHeight;}
  positions.push_back({cp,x,y,width,style.fontHeight,glyphs[i].bitmap,i});x+=width;
 }
 if(!drawRuby)return positions;
 const unsigned rubyWidth=std::min(rubySize,style.fontWidth);
 const auto rowCount=positions.back().y/style.fontHeight+1;
 std::vector<unsigned> starts(rowCount);
 std::vector<bool> annotated(rowCount);
 std::vector<PositionedGlyph> marks;
 for(const auto&run:rubyRuns){
  auto at=std::lower_bound(positions.begin(),positions.end(),run.begin,
   [](const PositionedGlyph& glyph,size_t index){return glyph.source<index;});
  while(at!=positions.end()&&at->source<run.end){
   auto first=at++;
   while(at!=positions.end()&&at->source<run.end&&at->y==first->y)++at;
   const size_t baseLength=run.end-run.begin;
   const auto start=(first->source-run.begin)*run.reading.size()/baseLength;
   const auto finish=((at-1)->source+1-run.begin)*run.reading.size()/baseLength;
   if(start==finish)continue;
   const auto right=(at-1)->x+(at-1)->width;
   const auto span=right-first->x;
   const auto length=unsigned(finish-start);
   const unsigned cell=std::min(rubyWidth,std::max(1u,span/length));
   const auto total=cell*length;
   const auto left=first->x+(span>total?(span-total)/2:0);
   annotated[first->y/style.fontHeight]=true;
   for(size_t j=start;j<finish;++j){
    const bool perCharacter=run.reading.size()==baseLength;
    const auto base=first+(perCharacter?j-start:0);
    const unsigned markX=perCharacter?base->x+(base->width-std::min(cell,base->width))/2:
                                      left+unsigned(j-start)*cell;
    const size_t revealAt=perCharacter?base->source:(at-1)->source;
    marks.push_back({run.reading[j],markX,first->y,cell,rubyHeight,false,revealAt});
   }
  }
 }
 unsigned top=0;
 for(size_t i=0;i<starts.size();++i){
  starts[i]=top;
  top+=style.fontHeight+(annotated[i]?rubyHeight:0);
 }
 for(auto&glyph:positions){
  const auto row=glyph.y/style.fontHeight;
  glyph.y=starts[row]+(annotated[row]?rubyHeight:0);
 }
 for(auto&glyph:marks)glyph.y=starts[glyph.y/style.fontHeight];
 positions.insert(positions.end(),marks.begin(),marks.end());
 std::stable_sort(positions.begin(),positions.end(),
  [](const PositionedGlyph& a,const PositionedGlyph& b){return a.source<b.source;});
 positions.erase(std::lower_bound(positions.begin(),positions.end(),count,
  [](const PositionedGlyph& glyph,size_t index){return glyph.source<index;}),positions.end());
 if(rowStarts)*rowStarts=std::move(starts);
 return positions;
}
size_t MessageText::lineCount(const MessageStyle&style)const{
 std::vector<unsigned> starts;
 auto positions=layout(style,visible(),&starts);
 if(positions.empty())return 0;
 if(starts.empty())return size_t(positions.back().y/style.fontHeight)+1;
 return size_t(std::upper_bound(starts.begin(),starts.end(),positions.back().y)-starts.begin());
}
size_t MessageText::lastPageOffset(const MessageStyle&style)const{
 std::vector<unsigned> starts;
 const auto positions=layout(style,visible(),&starts);
 if(positions.empty())return 0;
 if(starts.empty()){
  const size_t lines=positions.back().y/style.fontHeight+1;
  const size_t page=std::max(1u,style.height/style.fontHeight);
  return lines>page?lines-page:0;
 }
 unsigned bottom=0;
 for(const auto&glyph:positions)bottom=std::max(bottom,glyph.y+glyph.height);
 if(bottom<=style.height)return 0;
 return size_t(std::lower_bound(starts.begin(),starts.end(),bottom-style.height)-starts.begin());
}
bool MessageText::paintGlyph(Image&out,KFont*font,const MessageStyle&style,const PositionedGlyph&glyph,size_t top)const{
 if(glyph.y<top||size_t(glyph.y)-top+glyph.height>style.height)return false;
 KImage dst{0,0,out.width,out.height,size_t(out.width)*4,out.pixels.data()};
 const auto y=int(size_t(glyph.y)-top);
 if(!glyph.bitmap){
  kfont_draw_outline(font,&dst,glyph.codepoint,int(glyph.x),y,glyph.width,glyph.height,style.color);
  return true;
 }
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
 kfont_draw_mask(font,&dst,found->scaled.data(),glyph.width,style.fontHeight,int(glyph.x),y,style.color);
 return true;
}
Image MessageText::render(KFont*font,const MessageStyle&style,size_t firstLine)const{
 if(!style.width||!style.height)throw std::runtime_error("Message surface dimensions");
 Image out{style.width,style.height,qlie::Bytes(size_t(style.width)*style.height*4)};
 std::vector<unsigned> starts;
 const auto positions=layout(style,visible(),&starts);
 if(!starts.empty()&&firstLine>=starts.size())return out;
 const auto top=starts.empty()?firstLine*style.fontHeight:size_t(starts[firstLine]);
 for(const auto&glyph:positions)paintGlyph(out,font,style,glyph,top);
 return out;
}
namespace {
constexpr unsigned TextPatchWidth=64,TextPatchHeight=32;

void updateTextPatch(const Image& source,ImagePatch& patch){
 auto&tile=patch.image;
 const size_t stride=size_t(source.width)*4,rowBytes=size_t(tile.width)*4;
 const auto*first=source.pixels.data()+size_t(patch.y)*stride+size_t(patch.x)*4;
 bool visible=false,changed=tile.pixels.empty();
 for(unsigned y=0;y<tile.height;++y){
  const auto*row=first+y*stride;
  if(!changed&&std::memcmp(row,tile.pixels.data()+y*rowBytes,rowBytes)!=0)changed=true;
  for(size_t x=3;!visible&&x<rowBytes;x+=4)visible=row[x]!=0;
 }
 if(!visible){tile.pixels.clear();return;}
 if(!changed)return;
 tile.pixels.resize(rowBytes*tile.height);
 for(unsigned y=0;y<tile.height;++y)
  std::memcpy(tile.pixels.data()+y*rowBytes,first+y*stride,rowBytes);
 tile.version.publish();
}
}
std::shared_ptr<const Image> MessageText::renderIncremental(KFont*font,const MessageStyle&style,Surface&surface)const{
 if(!style.width||!style.height)throw std::runtime_error("Message surface dimensions");
 const auto&old=surface.style;
 const size_t count=visible();
 const bool reset=!surface.image||surface.font!=font||surface.layoutVersion!=layoutVersion.get()||
                  old.width!=style.width||old.height!=style.height||old.fontWidth!=style.fontWidth||
                  old.fontHeight!=style.fontHeight||old.color!=style.color||count<surface.visible;
 if(!reset&&count==surface.visible)return surface.image;
 const unsigned columns=(style.width+TextPatchWidth-1)/TextPatchWidth;
 if(reset){
  surface.positions=layout(style,glyphs.size());
  if(!surface.image)surface.image=std::make_shared<Image>();
  if(!surface.patches||surface.image->width!=style.width||surface.image->height!=style.height){
   surface.patches=std::make_shared<ImagePatches>();
   for(unsigned y=0;y<style.height;y+=TextPatchHeight)for(unsigned x=0;x<style.width;x+=TextPatchWidth){
    const auto width=std::min(TextPatchWidth,style.width-x),height=std::min(TextPatchHeight,style.height-y);
    surface.patches->regions.push_back({x,y,{width,height,{}}});
   }
   surface.image->patches=surface.patches;
  }
  surface.image->width=style.width;
  surface.image->height=style.height;
  surface.image->pixels.assign(size_t(style.width)*style.height*4,0);
  surface.font=font;
  surface.style=style;
  surface.layoutVersion=layoutVersion.get();
  surface.drawn=0;
 }
 auto&patches=*surface.patches;
 std::vector<bool> dirty(patches.regions.size(),reset);
 bool changed=reset;
 while(surface.drawn<surface.positions.size()&&surface.positions[surface.drawn].source<count){
  const auto&glyph=surface.positions[surface.drawn++];
  if(!paintGlyph(*surface.image,font,style,glyph,0))continue;
  changed=true;
  if(reset)continue;
  KFontGlyphBounds bounds{0,0,glyph.width,glyph.height};
  if(!glyph.bitmap&&kfont_glyph_bounds(font,glyph.codepoint,glyph.width,glyph.height,&bounds))continue;
  // The font's outline and shadow can extend two pixels past its bitmap.
  const int left=int(glyph.x)+bounds.left,top=int(glyph.y)+bounds.top;
  const unsigned x0=unsigned(std::clamp(left-2,0,int(style.width)));
  const unsigned y0=unsigned(std::clamp(top-2,0,int(style.height)));
  const unsigned x1=unsigned(std::clamp(left+int(bounds.width)+2,0,int(style.width)));
  const unsigned y1=unsigned(std::clamp(top+int(bounds.height)+2,0,int(style.height)));
  if(x0>=x1||y0>=y1)continue;
  for(unsigned y=y0/TextPatchHeight;y<=(y1-1)/TextPatchHeight;++y)
   for(unsigned x=x0/TextPatchWidth;x<=(x1-1)/TextPatchWidth;++x)dirty[y*columns+x]=true;
 }
 surface.visible=count;
 if(changed){
  for(size_t i=0;i<dirty.size();++i)if(dirty[i])updateTextPatch(*surface.image,patches.regions[i]);
  surface.image->version.publish();
  patches.sourceVersion=surface.image->version.get();
 }
 return surface.image;
}
qlie::Bytes MessageText::save()const{StateWriter w;w.str("WHJ2-message-1");w.str(text);w.f(float(elapsed));w.f(float(speed));return w.bytes;}
void MessageText::restore(const qlie::Bytes&b){StateReader r{b};if(r.str()!="WHJ2-message-1")throw std::runtime_error("Message state version");text=r.str();elapsed=r.f();speed=r.f();r.end();index();}
}
