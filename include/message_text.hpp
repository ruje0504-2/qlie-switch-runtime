#pragma once
#include "image.hpp"
struct KFont;
namespace whj2 {
std::string messageDisplayName(const std::string&);
struct MessageStyle {unsigned width=920,height=300,fontWidth=26,fontHeight=26;uint32_t color=0xffffff;};
class MessageText {
 struct Glyph {uint32_t codepoint;double deadline;size_t centeredRun=0;};
 std::string text;std::vector<Glyph> glyphs;
 double elapsed=0,speed=10;
 std::vector<uint32_t> lineStartForbidden,lineEndForbidden,noInitialIndent;
 unsigned lineReserve=1;
 bool storyLineBreaking=false;
 void index();
public:
 explicit MessageText(qlie::Assets&,bool storyLineBreaking=false);
 void setSpeed(double);double getSpeed()const{return speed;}
 void start(const std::string&);void update(double ms);bool reveal();bool finished()const;size_t visible()const;
 size_t timedCharacters(size_t first,size_t last)const;
 Image render(KFont*,const MessageStyle&)const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
