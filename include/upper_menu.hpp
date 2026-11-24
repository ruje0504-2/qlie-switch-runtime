#pragma once
#include "image.hpp"
#include <array>
namespace whj2 {
class UpperMenu {
 Image frame;std::array<Image,3> off,on;
 bool shown=false;int selected=0;
public:
 enum Action {None,Quit,Title,Screen};
 explicit UpperMenu(qlie::Assets&);
 bool visible()const{return shown;}void open(){shown=true;selected=0;}void close(){shown=false;}void toggle(){shown?close():open();}
 void move(int);Action confirm();Action tap(float,float);bool swipe(float dx,float dy);
 void draw(qlie::Bytes&)const;
};
}
