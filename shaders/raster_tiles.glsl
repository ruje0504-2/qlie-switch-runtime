// SPDX-License-Identifier: GPL-2.0-or-later
float edge(vec2 a,vec2 b,vec2 q){precise float e=(b.x-a.x)*(q.y-a.y)-(b.y-a.y)*(q.x-a.x);return e;}
bool included(vec2 a,vec2 b){return b.y<a.y||(b.y==a.y&&b.x>a.x);}
#ifdef DEKO_MOTION_KERNEL
#include "deko_tile_coords.glsl"
#endif
// Apply one paint operation to a pixel. The caller owns its final and cached
// prefix colors, including when an operation covers only part of the tile.
void applyDraw(Draw p,ivec2 q,uint index,inout uvec4 color
#ifdef DEKO_MOTION_KERNEL
 ,uint coordMode
#endif
){
 if(any(lessThan(q,p.rect.xy))||any(greaterThanEqual(q,p.rect.zw)))return;
 uint operation=p.meta.z>>20;
 if(operation==4u){color=unpack(source[p.colors.z+index]);return;}
#ifndef DEKO_MOTION_KERNEL
 if(operation==2u){color.rgb=RASTER_DIV(color.rgb*p.meta.x,p.meta.y);return;}
#endif
 if(operation==3u){
  uvec4 old=unpack(source[p.colors.z+index]);
  uint a=(p.meta.z&131072u)!=0u?ruleAlpha(p,uvec2(q)):0u;
  color=fadePixel(p,old,color,a);
  return;
 }
#ifndef DEKO_MOTION_KERNEL
 if(operation==1u){
  uvec4 s=pixel(p,q-ivec2(p.p1.xy));if(s.a==0u)return;
  color=glyphOver(s,color);return;
 }
#endif
 uvec4 s;
 if((p.meta.z&524288u)!=0u){
  s=pixel(p,q-ivec2(p.p1.xy));
 }else{
 precise vec2 uv;
 if((p.meta.z&65536u)==0u){
#ifdef DEKO_MOTION_KERNEL
  if(coordMode==AxisAligned)uv=vec2(tileCoords[gl_LocalInvocationID.x].x,tileCoords[gl_LocalInvocationID.y].y);
  else
#ifdef DEKO_WHJ6_MOTION
  if(coordMode==SecondAxisAligned)uv=vec2(secondTileCoords[gl_LocalInvocationID.x].x,secondTileCoords[gl_LocalInvocationID.y].y);
  else
#endif
#endif
  {precise vec2 d=vec2(q)+.5-p.p1.xy;uv=vec2(divide(p.p0.w*d.x-p.p0.y*d.y,p.p1.z),divide(-p.p0.z*d.x+p.p0.x*d.y,p.p1.z));}
  if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(p.meta.xy))))return;
 }
 else{
  vec2 a=p.p0.xy,b=p.p1.xy,c=p.p2.xy,point=vec2(q)+.5;
#ifdef DEKO_MOTION_KERNEL
  vec2 column=coordMode>=EdgeBC?tileCoords[gl_LocalInvocationID.x]:vec2(0);
  precise vec3 e=vec3(coordMode==EdgeBC?column.x:edge(b,c,point),
                      coordMode==EdgeCA?column.x:edge(c,a,point),
                      coordMode==EdgeAB?column.x:edge(a,b,point));
#else
  precise vec3 e=vec3(edge(b,c,point),edge(c,a,point),edge(a,b,point));
#endif
  if(any(lessThan(e,vec3(0)))||(e.x==0&&!included(b,c))||(e.y==0&&!included(c,a))||(e.z==0&&!included(a,b)))return;
#ifdef DEKO_MOTION_KERNEL
  precise vec3 t=vec3(coordMode==EdgeBC?column.y:divide(divide(e.x,p.p3.w),p.p3.x),
                      coordMode==EdgeCA?column.y:divide(divide(e.y,p.p3.w),p.p3.y),
                      coordMode==EdgeAB?column.y:divide(divide(e.z,p.p3.w),p.p3.z));
#else
  precise vec3 t=vec3(divide(divide(e.x,p.p3.w),p.p3.x),divide(divide(e.y,p.p3.w),p.p3.y),divide(divide(e.z,p.p3.w),p.p3.z));
#endif
  precise float inverse=t.x+t.y+t.z;
  precise float u=t.x*p.p0.z+t.y*p.p1.z+t.z*p.p2.z;
  precise float v=t.x*p.p0.w+t.y*p.p1.w+t.z*p.p2.w;uv=vec2(divide(u,inverse),divide(v,inverse));
 }
 s=samplePixel(p,uv);
 }
 s=tint(p,s);if(s.a!=0u)color=composite((p.meta.z>>8)&255u,s,color);
}

void main(){
 uint header=gl_WorkGroupID.x*4u;
 uint origin=tiles[header],first=tiles[header+1u],count=tiles[header+2u],control=tiles[header+3u];
 ivec2 q=ivec2(origin&65535u,origin>>16)+ivec2(gl_LocalInvocationID.xy);
 uint index=uint(q.y)*frame.meta.x+uint(q.x);
 uint prefixIndex=frame.meta.x*frame.meta.y+index;
 uvec4 color;
 if((control&0x80000000u)!=0u)color=unpack(destination[prefixIndex]);
 else color=unpack(frame.meta.w!=0u?frame.colors.x:destination[index]);
 uint saveAfter=control&0x7fffffffu;
#ifdef DEKO_MOTION_KERNEL
 // Every invocation in this workgroup sees the same first draw and origin.
 // Shared coordinates are prepared before the loop, including for prefix replays.
 ivec2 tileOrigin=ivec2(origin&65535u,origin>>16);
 uint coordMode=0u;
 if(count!=0u){
  Draw base=draws[tiles[first]];
  coordMode=tileCoordMode(base,tileOrigin);
  if(coordMode!=0u&&gl_LocalInvocationID.y==0u)prepareTileCoord(base,tileOrigin,coordMode);
 }
#ifdef DEKO_WHJ6_MOTION
 // The moving panorama and the video snow layer both cover the canvas.
 uint secondCoordMode=0u;
 if(count>1u){
  Draw second=draws[tiles[first+1u]];
  if(tileCoordMode(second,tileOrigin)==AxisAligned){
   secondCoordMode=SecondAxisAligned;
   if(gl_LocalInvocationID.y==0u){
    uint lane=gl_LocalInvocationID.x;
    precise vec2 point=vec2(tileOrigin+ivec2(int(lane)))+0.5;
    secondTileCoords[lane]=axisTileCoord(second,point);
   }
  }
 }
 if(coordMode!=0u||secondCoordMode!=0u)barrier();
#else
 if(coordMode!=0u)barrier();
#endif
#endif
 for(uint item=0;item<count;++item){
#ifdef DEKO_MOTION_KERNEL
#ifdef DEKO_WHJ6_MOTION
  applyDraw(draws[tiles[first+item]],q,index,color,item==0u?coordMode:item==1u?secondCoordMode:0u);
#else
  applyDraw(draws[tiles[first+item]],q,index,color,item==0u?coordMode:0u);
#endif
#else
  applyDraw(draws[tiles[first+item]],q,index,color);
#endif
  if(item+1u==saveAfter)destination[prefixIndex]=pack(color);
 }
#ifdef DEKO_RASTER
 uint result=pack(color);
 destination[index]=result;
 if(frame.meta.z!=0u)imageStore(presentImage,q,uvec4(result,0u,0u,0u));
#else
 destination[index]=pack(color);
#endif
}
