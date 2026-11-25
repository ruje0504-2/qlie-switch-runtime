#pragma once
#include "image.hpp"
#include <array>
namespace whj2 {
class RenderTarget;
class UpperMenu {
#ifdef QLIE_PROFILE_WHJ6
 Image frame;std::array<Image,5> off,on;
 std::array<Image,2> saveLoadLocked;
 bool saveLoadEnabled=true,screenMode=false;
 bool enabled(int index)const{return index==2?screenMode:index<3||saveLoadEnabled;}
#else
 Image frame;std::array<Image,3> off,on;
#endif
 const int buttonCount;
 bool shown=false,sectionSkip=false;int selected=0;
 void render(qlie::Bytes*,RenderTarget*)const;
public:
 enum Action {None,Quit,Title,Screen,SectionSkip,Save,Load};
 explicit UpperMenu(qlie::Assets&,bool showScreen=true);
 bool visible()const{return shown;}void open(){shown=true;selected=0;}void close(){shown=false;}void toggle(){shown?close():open();}
 void offerSectionSkip(bool enabled){sectionSkip=enabled;if(!enabled&&selected==buttonCount)selected=0;}
 bool sectionSkipVisible()const{return shown&&sectionSkip;}
 bool sectionSkipSelected()const{return selected==buttonCount;}
#ifdef QLIE_PROFILE_WHJ6
 void saveLoadAvailable(bool allowed){saveLoadEnabled=allowed;}
#endif
 void move(int);Action confirm();Action tap(float,float);bool swipe(float dx,float dy);
 void draw(qlie::Bytes&)const;
 void draw(RenderTarget&)const;
};
}
