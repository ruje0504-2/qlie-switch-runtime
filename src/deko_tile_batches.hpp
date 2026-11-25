// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "raster_scene.hpp"

namespace whj2::gpu {
// The motion shader implements unmasked normal/subtractive sprites, projected
// triangles, and the original two transition operations. WHJ6 also uses it for
// the title snow's screen blend and the additive entrance fade.
inline bool motionDraw(const DrawData& draw){
 const uint32_t flags=draw.image.meta[2];
 const unsigned op=flags>>20,blend=(flags>>8)&255u;
 bool supportedBlend=blend<2||blend==8;
#ifdef QLIE_PROFILE_WHJ6
 supportedBlend|=blend==2||blend==7;
#endif
 return op==3||op==4||(op==0&&!(flags&262144u)&&supportedBlend);
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

#ifdef QLIE_PROFILE_WHJ6
struct Whj6SnowDraw {
 enum Part : uint8_t { None,Panorama,Video,Fog,Flash };
 Part part=None;
 AxisBounds bounds{};

 static Whj6SnowDraw classify(const DrawData& draw,const DrawSources& sources){
  const auto& p=draw.image;
  constexpr uint32_t allowed=0xff00u|524288u;
  if(p.meta[2]&~allowed||p.p[0][0]<=0.f||p.p[0][3]<=0.f||
     p.p[0][1]!=0.f||p.p[0][2]!=0.f)return {};
  const unsigned blend=(p.meta[2]>>8)&255u;
  if(blend==7&&coversCanvasWithSolid(p))return {Flash,{}};
  if(!sources.opaque||p.colors[0]!=0||p.colors[1]!=0xffffff)return {};
  if(blend==2&&p.meta[0]==1920&&p.meta[1]==1080)return {Fog,axisBounds(p)};
  if(p.meta[3]!=255)return {};
  if(blend==0)return {Panorama,axisBounds(p)};
  if(blend==2&&p.meta[0]==1280&&p.meta[1]==720)return {Video,axisBounds(p)};
  return {};
 }
};
#endif

class DekoTileBatches {
public:
 enum Kind : uint8_t { Motion,Base,General,
#ifdef QLIE_PROFILE_WHJ6
  Snow,
#endif
  KindCount };
 static constexpr uint32_t AlignmentWords=64;
 static constexpr uint32_t PaddingWords=(KindCount-1)*(AlignmentWords-1);
 std::array<unsigned,KindCount> count{};
 std::array<uint32_t,KindCount> offsetWords{};
private:
 std::vector<Kind> drawKinds;
 std::array<Kind,TileCount> tileKinds{};
#ifdef QLIE_PROFILE_WHJ6
 std::vector<Whj6SnowDraw> snowDraws;
 bool snowTile(const uint32_t* ids,unsigned layers,uint32_t control,unsigned x,unsigned y)const{
  if(layers==1){
   // The static backdrop is already in the prefix buffer. A tile touched
   // by the moving eagle or a transition still uses the ordered renderer.
   const auto& fog=snowDraws[ids[0]];
   return control==0x80000000u&&fog.part==Whj6SnowDraw::Fog&&fog.bounds.covers(x,y,Tile,Tile);
  }
  if(control||layers>3)return false;
  const auto& base=snowDraws[ids[0]];
  const auto& snow=snowDraws[ids[1]];
  return base.part==Whj6SnowDraw::Panorama&&snow.part==Whj6SnowDraw::Video&&
         base.bounds.covers(x,y,Tile,Tile)&&snow.bounds.covers(x,y,Tile,Tile)&&
         (layers==2||snowDraws[ids[2]].part==Whj6SnowDraw::Flash);
 }
#endif
public:
 void write(const Scene& scene,uint32_t* words){
  const auto& frame=*scene.current;
  const auto& tiles=scene.workTiles;
  drawKinds.resize(frame.commands.size());
  bool hasBase=false,hasGeneral=false,hasSnow=false;
#ifdef QLIE_PROFILE_WHJ6
  snowDraws.resize(frame.commands.size());
#endif
  for(size_t i=0;i<frame.commands.size();++i){
   const auto& draw=frame.commands[i];
   drawKinds[i]=opaqueBaseDraw(draw,frame.sources[i])?Base:motionDraw(draw)?Motion:General;
   hasBase|=drawKinds[i]==Base;hasGeneral|=drawKinds[i]==General;
#ifdef QLIE_PROFILE_WHJ6
   snowDraws[i]=Whj6SnowDraw::classify(draw,frame.sources[i]);
   hasSnow|=snowDraws[i].part==Whj6SnowDraw::Video||snowDraws[i].part==Whj6SnowDraw::Fog;
#endif
  }
  offsetWords.fill(0);
  count.fill(0);
  if(!hasBase&&!hasGeneral&&!hasSnow){
   count[Motion]=unsigned(tiles.size());scene.writeTileList(words);
   return;
  }
  for(size_t i=0;i<tiles.size();++i){
   const auto& h=tiles[i];
   const unsigned x=h[0]&65535u,y=h[0]>>16,tile=y/Tile*Columns+x/Tile;
   auto first=scene.workIndices.begin()+h[1];
   Kind kind=Motion;
#ifdef QLIE_PROFILE_WHJ6
   if(hasSnow&&h[2]&&snowTile(scene.workIndices.data()+h[1],h[2],h[3],x,y))kind=Snow;
   else
#endif
   if(h[2]==1&&drawKinds[*first]==Base&&frame.covered[tile]&&frame.tiles[tile].size()==1)kind=Base;
   else if(hasGeneral&&std::any_of(first,first+h[2],[this](uint32_t id){return drawKinds[id]==General;}))kind=General;
   tileKinds[i]=kind;
   ++count[kind];
  }
  if(std::any_of(count.begin(),count.end(),[&](unsigned n){return n==tiles.size();})){
   scene.writeTileList(words);
   return;
  }
  uint32_t end=0;
  std::array<uint32_t*,KindCount> heads{};
  for(unsigned kind=0;kind<KindCount;++kind){
   if(!count[kind])continue;
   offsetWords[kind]=(end+AlignmentWords-1)&~(AlignmentWords-1);
   heads[kind]=words+offsetWords[kind];
   end=offsetWords[kind]+4*count[kind];
  }
  for(size_t i=0;i<tiles.size();++i){
   auto h=tiles[i];Kind kind=tileKinds[i];
   h[1]+=end-offsetWords[kind];
   heads[kind]=std::copy(h.begin(),h.end(),heads[kind]);
  }
  std::copy(scene.workIndices.begin(),scene.workIndices.end(),words+end);
 }
};
}
