#pragma once
#include "image.hpp"
#include <array>
namespace whj2 {
class RenderTarget;
class UpperMenu {
 Image frame;std::array<Image,3> off,on;
 bool shown=false,sectionSkip=false;int selected=0;
 void render(qlie::Bytes*,RenderTarget*)const;
public:
 enum Action {None,Quit,Title,Screen,SectionSkip};
 explicit UpperMenu(qlie::Assets&);
 bool visible()const{return shown;}void open(){shown=true;selected=0;}void close(){shown=false;}void toggle(){shown?close():open();}
 void offerSectionSkip(bool enabled){sectionSkip=enabled;if(!enabled&&selected==3)selected=0;}
 bool sectionSkipVisible()const{return shown&&sectionSkip;}
 bool sectionSkipSelected()const{return selected==3;}
 void move(int);Action confirm();Action tap(float,float);bool swipe(float dx,float dy);
 void draw(qlie::Bytes&)const;
 void draw(RenderTarget&)const;
};
}
