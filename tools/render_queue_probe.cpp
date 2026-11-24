// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_queue.hpp"
#include <stdexcept>
#include <iostream>
using namespace whj2;
void check(bool v,const char*s){if(!v)throw std::runtime_error(s);}
int main(){try{
 Image red{1,1,{255,0,0,255}},blue{1,1,{0,0,255,255}};
 qlie::Bytes out(8*8*4);
 auto pixel=[&](int x,int y,int c){return out[(y*8+x)*4+c];};
 AnimationSample push,pop;push.kind=16;push.x=2;pop.kind=15;
 RenderQueue q;
 // Append out of order: sort must put the control before the first image
 // and restore identity for a later scene layer, not just local ABMP layers.
 q.image(blue,imagePlacement(1,2,1,1,0,0,0,1,1),200,255,0,0xffffff);
 q.control(pop,100);q.image(red,imagePlacement(1,2,1,1,0,0,0,1,1),0,255,0,0xffffff);q.control(push,-10);
 q.draw(out,8,8);check(pixel(3,2,0)==255,"cross-layer translation");check(pixel(1,2,2)==255,"depth-ordered pop restores scene");
 check(pixel(2,2,3)==0,"no duplicate untransformed image");
 out.assign(out.size(),0);RenderQueue scaled;push.scaleX=push.scaleY=200;
 scaled.control(push,-10);scaled.image(red,imagePlacement(1,2,1,1,0,0,0,1,1),0,255,0,0xffffff);scaled.draw(out,8,8);
 check(pixel(2,0,0)==255&&pixel(3,1,0)==255,"translation before centered scale and inverse raster");
 check(pixel(1,0,3)==0&&pixel(4,1,3)==0,"scaled footprint exact");
 out.assign(out.size(),0);q.draw(out,8,8);check(pixel(3,2,0)==255,"frame resets render stack");
 out.assign(out.size(),0);RenderQueue rotated;push.x=0;push.scaleX=200;push.scaleY=100;push.rotationZ=90;
 rotated.control(push,-10);rotated.image(red,imagePlacement(4,4,1,1,0,0,0,1,1),0,255,0,0xffffff);rotated.draw(out,8,8);
 check(pixel(4,3,0)==255&&pixel(5,3,0)==255,"nonuniform scale after centered rotation");
 out.assign(out.size(),0);RenderQueue alpha;alpha.image(blue,imagePlacement(1,2,1,1,0,0,0,1,1),0,255,0,0xffffff);alpha.image(red,imagePlacement(1,2,1,1,0,0,0,1,1),1,128,0,0xffffff);alpha.draw(out,8,8);
 check(pixel(1,2,0)==127&&pixel(1,2,2)==127&&pixel(1,2,3)==191,"native fixed-point alpha layering");
 out.assign(out.size(),0);RenderQueue nested;AnimationSample first,second,one,all;
 first.kind=second.kind=16;first.x=1;second.x=2;one.kind=17;all.kind=15;
 nested.control(first,0);nested.control(second,1);nested.image(red,imagePlacement(0,0,1,1,0,0,0,1,1),2,255,0,0xffffff);
 nested.control(one,3);nested.image(red,imagePlacement(0,1,1,1,0,0,0,1,1),4,255,0,0xffffff);
 nested.control(second,5);nested.control(all,6);nested.image(red,imagePlacement(0,2,1,1,0,0,0,1,1),7,255,0,0xffffff);nested.draw(out,8,8);
 check(pixel(3,0,0)==255&&pixel(1,1,0)==255&&pixel(0,2,0)==255,"primitive16 push / primitive17 pop / primitive15 reset");
 std::cout<<"PASS cross-scene translation, depth reset, nested pop, centered scaling/rotation, frame reset and alpha pixels\n";
 return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
