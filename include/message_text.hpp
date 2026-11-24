#pragma once
#include "image.hpp"
struct KFont;
namespace whj2 {
struct MessageStyle {unsigned width=920,height=300,fontWidth=26,fontHeight=26;uint32_t color=0xffffff;};
class MessageText {
 std::string text;std::vector<size_t> boundaries;
 double elapsed=0,speed=10;
 void index();
public:
 explicit MessageText(qlie::Assets&);
 void start(const std::string&);void update(double ms);bool reveal();bool finished()const;size_t visible()const;
 Image render(KFont*,const MessageStyle&)const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
