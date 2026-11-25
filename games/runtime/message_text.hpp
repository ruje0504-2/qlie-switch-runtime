#pragma once
#include "image.hpp"
#include <memory>
struct KFont;
namespace whj2 {
std::string messageDisplayName(const std::string&);
struct MessageStyle {unsigned width=920,height=300,fontWidth=26,fontHeight=26;uint32_t color=0xffffff;};
class MessageText {
 struct PositionedGlyph {uint32_t codepoint;unsigned x,y,width,height;bool bitmap;size_t source;};
 std::vector<PositionedGlyph> layout(const MessageStyle&,size_t count,std::vector<unsigned>* rowStarts=nullptr)const;
 bool paintGlyph(Image&,KFont*,const MessageStyle&,const PositionedGlyph&,size_t top)const;
 struct Glyph {uint32_t codepoint;double deadline;size_t centeredRun=0;bool bitmap=false;};
 struct BitmapGlyph {uint32_t codepoint;unsigned width,height;qlie::Bytes alpha;mutable unsigned scaledWidth=0,scaledHeight=0;mutable qlie::Bytes scaled;};
 std::vector<BitmapGlyph> bitmapGlyphs;
 std::vector<std::pair<std::string,std::string>> replacements;
 struct RubyRun {size_t begin,end;std::vector<uint32_t> reading;};
 std::string text;std::vector<Glyph> glyphs;
 std::vector<RubyRun> rubyRuns;
 double elapsed=0,speed=10;
 std::vector<uint32_t> lineStartForbidden,lineEndForbidden,noInitialIndent;
 unsigned lineReserve=1,rubySize=8;
 bool storyLineBreaking=false;
 PixelVersion layoutVersion;
 void index(bool layoutChanged=true);
public:
 class Surface {
  friend class MessageText;
  std::shared_ptr<Image> image;
  std::shared_ptr<ImagePatches> patches;
  std::vector<PositionedGlyph> positions;
  KFont*font=nullptr;
  MessageStyle style;
  uint64_t layoutVersion=0;
  size_t drawn=0,visible=0;
 public:
  Surface()=default;
  Surface(const Surface&)=delete;
  Surface&operator=(const Surface&)=delete;
 };
 MessageText()=default;
 explicit MessageText(qlie::Assets&,bool storyLineBreaking=false);
 void setSpeed(double);double getSpeed()const{return speed;}
 void start(const std::string&);void update(double ms);bool reveal();bool finished()const;size_t visible()const;
 size_t timedCharacters(size_t first,size_t last)const;
 size_t lineCount(const MessageStyle&)const;
 size_t lastPageOffset(const MessageStyle&)const;
 Image render(KFont*,const MessageStyle&,size_t firstLine=0)const;
 // The frontend updates this surface between synchronous CPU draws or GPU uploads.
 std::shared_ptr<const Image> renderIncremental(KFont*,const MessageStyle&,Surface&)const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
