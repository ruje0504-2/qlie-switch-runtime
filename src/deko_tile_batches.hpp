// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "raster_scene.hpp"

namespace whj2::gpu {
// The motion shader implements unmasked normal/subtractive sprites, projected
// triangles, and the original two transition operations. WHJ6 also uses it for
// screen, lighten, additive, and hard-light blending.
inline bool motionDraw(const DrawData& draw){
 const uint32_t flags=draw.image.meta[2];
 const unsigned op=flags>>20,blend=(flags>>8)&255u;
 bool supportedBlend=blend<2||blend==8;
#ifdef QLIE_PROFILE_WHJ6
 supportedBlend|=blend==2||blend==5||blend==7||blend==13;
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
 enum Part : uint8_t { None,Panorama,Video,Flash };
 Part part=None;
 AxisBounds bounds{};

 static Whj6SnowDraw classify(const DrawData& draw,const DrawSources& sources){
  const auto& p=draw.image;
  constexpr uint32_t allowed=0xff00u|524288u;
  if(p.meta[2]&~allowed||p.p[0][0]<=0.f||p.p[0][3]<=0.f||
     p.p[0][1]!=0.f||p.p[0][2]!=0.f)return {};
  const unsigned blend=(p.meta[2]>>8)&255u;
  if(blend==7&&coversCanvasWithSolid(p))return {Flash,{}};
  if(!sources.opaque||p.meta[3]!=255||p.colors[0]!=0||p.colors[1]!=0xffffff)return {};
  if(blend==0)return {Panorama,axisBounds(p)};
  if(blend==2&&p.meta[0]==1280&&p.meta[1]==720)return {Video,axisBounds(p)};
  return {};
 }
};
inline bool axisBitmap(const Push& p){
 constexpr uint32_t allowed=0xffffu|524288u;
 return !(p.meta[2]&~allowed)&&p.p[0][1]==0.f&&p.p[0][2]==0.f&&p.p[0][0]>0.f&&p.p[0][3]>0.f;
}
inline bool fogDraw(const DrawData& draw,const DrawSources& sources){
 const auto& p=draw.image;
 // At this scale a tile's linear filter visits at most 17 source rows.
 return sources.opaque&&sources.monochrome&&axisBitmap(p)&&((p.meta[2]>>8)&255u)==2&&
        p.p[0][3]>=1.0625f&&p.colors[0]==0&&p.colors[1]==0xffffff;
}
#endif

class DekoTileBatches {
public:
 enum Kind : uint8_t { Motion,Base,General,
#ifdef QLIE_PROFILE_WHJ6
  Snow,Fog,HardLight,
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
 std::vector<AxisBounds> fogDraws;
 std::vector<uint8_t> fogOverlays;
 std::vector<AxisBounds> hardLightDraws,alignedBases;
 bool snowTile(const uint32_t* ids,unsigned layers,uint32_t control,unsigned x,unsigned y)const{
  if(control||layers>3)return false;
  const auto& base=snowDraws[ids[0]];
  const auto& snow=snowDraws[ids[1]];
  return base.part==Whj6SnowDraw::Panorama&&snow.part==Whj6SnowDraw::Video&&
         base.bounds.covers(x,y,Tile,Tile)&&snow.bounds.covers(x,y,Tile,Tile)&&
         (layers==2||snowDraws[ids[2]].part==Whj6SnowDraw::Flash);
 }
 bool fogTile(const uint32_t* ids,unsigned layers,uint32_t control,unsigned x,unsigned y)const{
  // Only replay the moving cloud and normal sprites over the cached scene.
  return control==0x80000000u&&fogDraws[ids[0]].covers(x,y,Tile,Tile)&&
         std::all_of(ids+1,ids+layers,[this](uint32_t id){return fogOverlays[id]!=0;});
 }
 bool hardLightTile(const uint32_t* ids,unsigned layers,uint32_t control,unsigned x,unsigned y)const{
  if(layers==1&&control==0x80000000u)return hardLightDraws[ids[0]].covers(x,y,Tile,Tile);
  return layers==2&&control<=1&&alignedBases[ids[0]].covers(x,y,Tile,Tile)&&
         hardLightDraws[ids[1]].covers(x,y,Tile,Tile);
 }
#endif
public:
 void write(const Scene& scene,uint32_t* words){
  const auto& frame=*scene.current;
  const auto& tiles=scene.workTiles;
  drawKinds.resize(frame.commands.size());
  bool hasBase=false,hasGeneral=false,hasSnow=false,hasFog=false,hasHardLight=false;
#ifdef QLIE_PROFILE_WHJ6
  snowDraws.resize(frame.commands.size());
  fogDraws.resize(frame.commands.size());
  fogOverlays.resize(frame.commands.size());
  hardLightDraws.resize(frame.commands.size());
  alignedBases.resize(frame.commands.size());
#endif
  for(size_t i=0;i<frame.commands.size();++i){
   const auto& draw=frame.commands[i];
   drawKinds[i]=opaqueBaseDraw(draw,frame.sources[i])?Base:motionDraw(draw)?Motion:General;
   hasBase|=drawKinds[i]==Base;hasGeneral|=drawKinds[i]==General;
#ifdef QLIE_PROFILE_WHJ6
   snowDraws[i]=Whj6SnowDraw::classify(draw,frame.sources[i]);
   hasSnow|=snowDraws[i].part==Whj6SnowDraw::Video;
   const bool fog=fogDraw(draw,frame.sources[i]);
   fogDraws[i]=fog?axisBounds(draw.image):AxisBounds{};
   hasFog|=fog;
   fogOverlays[i]=axisBitmap(draw.image)&&((draw.image.meta[2]>>8)&255u)<2;
   const auto& p=draw.image;
   const bool hardLight=frame.sources[i].monochrome&&axisBitmap(p)&&((p.meta[2]>>8)&255u)==13&&
                        p.colors[0]==0&&p.colors[1]==0xffffff;
   hardLightDraws[i]=hardLight?axisBounds(p):AxisBounds{};
   hasHardLight|=hardLight;
   alignedBases[i]=drawKinds[i]==Base&&(p.meta[2]&524288u)?axisBounds(p):AxisBounds{};
#endif
  }
  offsetWords.fill(0);
  count.fill(0);
  if(!hasBase&&!hasGeneral&&!hasSnow&&!hasFog&&!hasHardLight){
   count[Motion]=unsigned(tiles.size());scene.writeTileList(words);
   return;
  }
  for(size_t i=0;i<tiles.size();++i){
   const auto& h=tiles[i];
   const unsigned x=h[0]&65535u,y=h[0]>>16,tile=y/Tile*Columns+x/Tile;
   auto first=scene.workIndices.begin()+h[1];
   Kind kind=Motion;
#ifdef QLIE_PROFILE_WHJ6
   if(hasFog&&h[2]&&fogTile(scene.workIndices.data()+h[1],h[2],h[3],x,y))kind=Fog;
   else if(hasSnow&&h[2]>=2&&snowTile(scene.workIndices.data()+h[1],h[2],h[3],x,y))kind=Snow;
   else if(hasHardLight&&h[2]&&hardLightTile(scene.workIndices.data()+h[1],h[2],h[3],x,y))kind=HardLight;
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
