// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bitset>
#include <vector>

namespace whj2 {
struct SwapDamage {
 static constexpr unsigned Tile=16, Width=1280, Height=720, Columns=Width/Tile, Rows=Height/Tile;
 using Mask=std::bitset<Columns*Rows>;
 struct Rect {unsigned x,y,width,height;}; // tile coordinates

 // Merge consecutive runs into one copy region when their extents match.
 // For highly fragmented updates a bounding rectangle limits driver work.
 static std::vector<Rect> regions(const Mask& tiles){
  if(tiles.none())return {};
  if(tiles.all())return {{0,0,Columns,Rows}};
  std::vector<Rect> out;
  std::array<int,Columns> previous;
  previous.fill(-1);
  unsigned left=Columns,right=0,top=Rows,bottom=0;
  for(unsigned y=0;y<Rows;++y){
   std::array<int,Columns> next;
   next.fill(-1);
   for(unsigned x=0;x<Columns;){
    if(!tiles[y*Columns+x]){++x;continue;}
    unsigned start=x++;
    while(x<Columns&&tiles[y*Columns+x])++x;
    left=std::min(left,start);right=std::max(right,x);top=std::min(top,y);bottom=y+1;
    int id=previous[start];
    if(id>=0&&out[id].width==x-start)++out[id].height;
    else{id=int(out.size());out.push_back({start,y,x-start,1});}
    next[start]=id;
   }
   previous=next;
  }
  if(out.size()>64)return {{left,top,right-left,bottom-top}};
  return out;
 }
};
}
