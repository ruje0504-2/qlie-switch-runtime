// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_queue.hpp"
#include <iostream>
#include <algorithm>
using namespace whj2;
int main(){try{
 Image half{1,1,{200,120,40,128}};qlie::Bytes out(8*8*4);
 std::array<TextureVertex,4> v{{{0,0,1000,0,0},{8,0,1000,1,0},{8,8,1000,1,1},{0,8,1000,0,1}}};
 RenderQueue q;q.triangle(half,{v[0],v[1],v[2]},0,255,0,0xffffff);q.triangle(half,{v[0],v[2],v[3]},0,255,0,0xffffff);q.draw(out,8,8);
 for(unsigned i=0;i<64;i++)if(out[i*4]!=blendChannel(0,200,0,128))throw std::runtime_error("Triangle gap or shared-edge overdraw");
 Image texture{8,1,{}};for(unsigned i=0;i<8;i++)texture.pixels.insert(texture.pixels.end(),{uint8_t(i*30),0,0,255});
 // Projected triangle (0,0),(8,0),(0,8) with varying depths.
 // At pixel (2.5,2.5), weights are .375,.3125,.3125. The perspective
 // u is 2.5/1.3125, selecting texel 1 instead of affine texel 2.
 out.assign(out.size(),0);RenderQueue perspective;
 perspective.triangle(texture,{{{0,0,1000,0,0},{8,0,1000,8,0},{2,6,500,0,0}}},0,255,0,0xffffff);perspective.draw(out,8,8);
 if(out[(2*8+2)*4]!=30)throw std::runtime_error("Perspective UV interpolation");
 RenderQueue reversed;out.assign(out.size(),0);reversed.triangle(half,{v[2],v[1],v[0]},0,255,0,0xffffff);reversed.draw(out,8,8);if(out[(1*8+6)*4]==0)throw std::runtime_error("Reverse winding");
 RenderQueue crossing;out.assign(out.size(),0);crossing.triangle(half,{{{4,4,5,0,0},{0,0,1000,0,0},{8,0,1000,0,0}}},0,255,0,0xffffff);crossing.draw(out,8,8);
 if(std::none_of(out.begin(),out.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Near-plane crossing discarded");
 std::cout<<"PASS perspective texture coordinates, reversed winding, no common-edge gaps/overdraw\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
