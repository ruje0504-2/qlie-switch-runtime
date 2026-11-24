// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "raster_scene.hpp"

namespace whj2::gpu {
// The motion shader implements unmasked normal/subtractive sprites, projected
// triangles, and the original two transition operations. The general shader
// covers other blend modes, glyphs, dimming and transformed masks.
inline bool motionDraw(const DrawData& draw){
 const uint32_t flags=draw.image.meta[2];
 const unsigned op=flags>>20,blend=(flags>>8)&255u;
 return op==3||op==4||(op==0&&!(flags&262144u)&&(blend<2||blend==8));
}

// A lone opaque draw proven to cover the whole tile can start with its packed
// texture color. Other tiles retain their full ordered compositing program.
inline bool opaqueBaseDraw(const DrawData& draw,const DrawSources& sources){
 const auto& p=draw.image;
 if(!sources.opaque||p.meta[3]!=255||p.colors[0]!=0||p.colors[1]!=0xffffff||
    (p.meta[2]>>20)!=0||(p.meta[2]&262144u)||((p.meta[2]>>8)&255u)>1)return false;
 if(p.meta[2]&65536u)return (p.meta[2]&255u)!=2u;
 return p.p[0][1]==0.f&&p.p[0][2]==0.f&&p.p[0][0]>=1.f&&p.p[0][3]>=1.f;
}

class DekoTileBatches {
 enum Kind : uint8_t { Motion,Base,General };
 std::vector<Kind> drawKinds;
 std::array<Kind,TileCount> tileKinds{};
public:
 static constexpr uint32_t AlignmentWords=64;
 unsigned motionTiles=0,baseTiles=0;
 uint32_t baseOffsetWords=0,generalOffsetWords=0;

 void write(const Scene& scene,uint32_t* words){
  const auto& frame=*scene.current;
  const auto& tiles=scene.workTiles;
  drawKinds.resize(frame.commands.size());
  bool hasBase=false,hasGeneral=false;
  for(size_t i=0;i<frame.commands.size();++i){
   const auto& draw=frame.commands[i];
   drawKinds[i]=opaqueBaseDraw(draw,frame.sources[i])?Base:motionDraw(draw)?Motion:General;
   hasBase|=drawKinds[i]==Base;hasGeneral|=drawKinds[i]==General;
  }
  baseOffsetWords=generalOffsetWords=0;
  motionTiles=baseTiles=0;
  if(!hasBase&&!hasGeneral){
   motionTiles=unsigned(tiles.size());scene.writeTileList(words);
   return;
  }
  for(size_t i=0;i<tiles.size();++i){
   const auto& h=tiles[i];
   const unsigned tile=(h[0]>>16)/Tile*Columns+(h[0]&65535u)/Tile;
   auto first=scene.workIndices.begin()+h[1];
   Kind kind=Motion;
   if(h[2]==1&&drawKinds[*first]==Base&&frame.covered[tile]&&frame.tiles[tile].size()==1)kind=Base;
   else if(hasGeneral&&std::any_of(first,first+h[2],[this](uint32_t id){return drawKinds[id]==General;}))kind=General;
   tileKinds[i]=kind;
   motionTiles+=kind==Motion;baseTiles+=kind==Base;
  }
  unsigned generalTiles=unsigned(tiles.size())-motionTiles-baseTiles;
  if(motionTiles==tiles.size()||baseTiles==tiles.size()||generalTiles==tiles.size()){
   scene.writeTileList(words);
   return;
  }
  auto aligned=[](uint32_t n){return (n+AlignmentWords-1)&~(AlignmentWords-1);};
  uint32_t end=4*motionTiles;
  if(baseTiles){baseOffsetWords=aligned(end);end=baseOffsetWords+4*baseTiles;}
  if(generalTiles){generalOffsetWords=aligned(end);end=generalOffsetWords+4*generalTiles;}
  uint32_t offsets[3]={0,baseOffsetWords,generalOffsetWords};
  uint32_t* heads[3]={words,words+baseOffsetWords,words+generalOffsetWords};
  for(size_t i=0;i<tiles.size();++i){
   auto h=tiles[i];Kind kind=tileKinds[i];
   h[1]+=end-offsets[kind];
   heads[kind]=std::copy(h.begin(),h.end(),heads[kind]);
  }
  std::copy(scene.workIndices.begin(),scene.workIndices.end(),words+end);
 }
};
}
