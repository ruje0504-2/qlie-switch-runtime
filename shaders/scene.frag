// SPDX-License-Identifier: GPL-2.0-or-later
#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_fragment_shader_interlock : require
layout(pixel_interlock_ordered) in;
#include "raster_types.glsl"
layout(set=0,binding=0,std430) readonly buffer Source { uint source[]; };
layout(set=0,binding=1,std430) coherent buffer Destination { uint destination[]; };
layout(set=0,binding=2,std430) readonly buffer Commands { Draw draws[]; };
layout(set=0,binding=3,std430) readonly buffer Work { uint words[]; };
layout(set=0,binding=4) uniform usamplerBuffer packedTexels;
layout(push_constant) uniform Parameters { vec4 p0,p1,p2,p3;ivec4 rect;uvec4 meta,colors; } frame;
layout(location=0) flat in uint drawId;
layout(location=1) in vec2 texcoord;
#define NATIVE_SCENE
#include "raster_pixels.glsl"
void main(){
 ivec2 q=ivec2(gl_FragCoord.xy);
 bool paint=!gl_HelperInvocation&&all(greaterThanEqual(q,ivec2(0)))&&all(lessThan(q,ivec2(1280,720)));
 uint index=uint(q.y)*1280u+uint(q.x),checkpoint=0u,op=0u,rule=0u;
 Draw p=draws[drawId];uvec4 s=uvec4(0);
 if(paint){
  uint tile=uint(q.y/16)*80u+uint(q.x/16);
  uint at=frame.meta.z+2u*tile;
  paint=drawId>=words[at]&&all(greaterThanEqual(q,p.rect.xy))&&all(lessThan(q,p.rect.zw));
  checkpoint=words[at+1u];
 }
 if(paint){
  op=p.meta.z>>20;
  if(op==0u){
   s=tint(p,(p.meta.z&524288u)!=0u?pixel(p,q-ivec2(p.p1.xy)):samplePixel(p,texcoord));
   paint=s.a!=0u;
  }else if(op==1u){s=pixel(p,q-ivec2(p.p1.xy));paint=s.a!=0u;}
  else if(op==3u||op==4u){
   s=unpack(loadTexel(p.colors.z+index));
   if(op==3u&&(p.meta.z&131072u)!=0u)rule=ruleAlpha(p,uvec2(q));
  }
 }
 // Only destination-dependent arithmetic is serialized at an overlapping pixel.
 beginInvocationInterlockARB();
 if(paint){
  uvec4 color=s;
  if(op!=4u&&!(op==0u&&s.a==255u&&((p.meta.z>>8)&255u)<2u)){
   uvec4 d=unpack(destination[index]);
   if(op==0u)color=composite((p.meta.z>>8)&255u,s,d);
   else if(op==1u)color=glyphOver(s,d);
   else if(op==2u)color=uvec4(d.rgb*p.meta.x/p.meta.y,d.a);
   else color=fadePixel(p,s,d,rule);
  }
  uint result=pack(color);
  destination[index]=result;
  if(drawId<checkpoint)destination[921600u+index]=result;
 }
 endInvocationInterlockARB();
}
