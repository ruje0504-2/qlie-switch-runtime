// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "render_queue.hpp"
#include <map>
namespace whj2 {
class GameMenu {
 qlie::Assets& assets;std::map<std::string,Image> cache;
 const Image& resource(const std::string&);
 void put(RenderQueue&,const std::string&,float,float,bool center=false);
public:
 enum Panel {None,Save,Load,History,Settings};
 enum Action {Nothing,Close,Slot,Quit,Title,Yes,No};
 Panel panel=None;int focus=0,page=0,historyOffset=0;bool asking=false;
 Action requested=Nothing;
 explicit GameMenu(qlie::Assets&a):assets(a){}
 void open(Panel p){panel=p;focus=0;asking=false;historyOffset=0;}
 void close(){panel=None;asking=false;}
 bool visible()const{return panel!=None||asking;}
 void ask(Action a){requested=a;asking=true;focus=0;}
 int slot()const{return page*8+focus+1;}
 void move(int dx,int dy);
 Action confirm()const;
 Action tap(float,float);
 void draw(qlie::Bytes&);
};
}
