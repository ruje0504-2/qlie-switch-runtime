// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_queue.hpp"
#include <iostream>
int main(){try{whj2::Image image{2,1,{0,0,0,255,255,255,255,255}};qlie::Bytes point(16),linear(16),triangles(16);whj2::RenderQueue a,b,c;whj2::Affine2D m;m.a=2;
 a.image(image,m,0,255,0,0xffffff,0,1);a.draw(point,4,1);b.image(image,m,0,255,0,0xffffff,0,2);b.draw(linear,4,1);
 const unsigned expected[]={0,64,191,255},nearest[]={0,0,255,255};for(unsigned x=0;x<4;x++)if(linear[4*x]!=expected[x]||point[4*x]!=nearest[x])throw std::runtime_error("Texel-center linear interpolation mismatch");
 c.triangle(image,{{{0,0,1000,0,0},{4,0,1000,2,0},{4,1,1000,2,1}}},0,255,0,0xffffff,0,2);c.triangle(image,{{{0,0,1000,0,0},{4,1,1000,2,1},{0,1,1000,0,1}}},0,255,0,0xffffff,0,2);c.draw(triangles,4,1);
 if(triangles!=linear)throw std::runtime_error("Affine and projected filter differ");
 std::cout<<"PASS point 0,0,255,255; linear 0,64,191,255; affine/projected parity\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
