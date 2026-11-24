// SPDX-License-Identifier: GPL-2.0-or-later
#version 450
#extension GL_GOOGLE_include_directive : require
#include "raster_types.glsl"
layout(set=0,binding=2,std430) readonly buffer Commands { Draw draws[]; };
layout(set=0,binding=3,std430) readonly buffer Work { uint words[]; };
out gl_PerVertex {vec4 gl_Position;float gl_ClipDistance[4];};
layout(location=0) flat out uint drawId;
layout(location=1) out vec2 texcoord;
const vec2 corner[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));
void main(){
 uint entry=uint(gl_InstanceIndex)*3u;drawId=words[entry];
 uint lo=words[entry+1u],hi=words[entry+2u];
 Draw p=draws[drawId];
 vec2 xy;float w=1;
 if((p.meta.z>>20)!=0u){
  xy=mix(vec2(p.rect.xy),vec2(p.rect.zw),corner[gl_VertexIndex]);
  texcoord=vec2(0);
 }else if((p.meta.z&65536u)!=0u){
  // The second triangle of this six-vertex instance is degenerate.
  int k=min(gl_VertexIndex,2);
  vec4 v=k==0?p.p0:(k==1?p.p1:p.p2);
  xy=v.xy;texcoord=v.zw;w=p.p3[k];
 }else{
  texcoord=corner[gl_VertexIndex]*vec2(p.meta.xy);
  precise float x=p.p0.x*texcoord.x+p.p0.y*texcoord.y+p.p1.x;
  precise float y=p.p0.z*texcoord.x+p.p0.w*texcoord.y+p.p1.y;
  xy=vec2(x,y);
 }
 vec2 low=vec2(lo&65535u,lo>>16),high=vec2(hi&65535u,hi>>16);
 gl_ClipDistance[0]=(xy.x-low.x)*w;gl_ClipDistance[1]=(high.x-xy.x)*w;
 gl_ClipDistance[2]=(xy.y-low.y)*w;gl_ClipDistance[3]=(high.y-xy.y)*w;
 gl_Position=vec4((xy/vec2(640,360)-1.0)*w,0.5*w,w);
}
