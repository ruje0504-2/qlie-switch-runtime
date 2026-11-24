// SPDX-License-Identifier: GPL-2.0-or-later
float edge(vec2 a,vec2 b,vec2 q){precise float e=(b.x-a.x)*(q.y-a.y)-(b.y-a.y)*(q.x-a.x);return e;}
bool included(vec2 a,vec2 b){return b.y<a.y||(b.y==a.y&&b.x>a.x);}
// Apply one paint operation to a pixel. The caller owns its final and cached
// prefix colors, including when an operation covers only part of the tile.
void applyDraw(Draw p,ivec2 q,uint index,inout uvec4 color){
 if(any(lessThan(q,p.rect.xy))||any(greaterThanEqual(q,p.rect.zw)))return;
 uint operation=p.meta.z>>20;
 if(operation==4u){color=unpack(source[p.colors.z+index]);return;}
 if(operation==2u){color.rgb=RASTER_DIV(color.rgb*p.meta.x,p.meta.y);return;}
 if(operation==3u){
  uvec4 old=unpack(source[p.colors.z+index]);
  uint a=(p.meta.z&131072u)!=0u?ruleAlpha(p,uvec2(q)):0u;
  color=fadePixel(p,old,color,a);
  return;
 }
 if(operation==1u){
  uvec4 s=pixel(p,q-ivec2(p.p1.xy));if(s.a==0u)return;
  color=glyphOver(s,color);return;
 }
 uvec4 s;
 if((p.meta.z&524288u)!=0u){
  s=pixel(p,q-ivec2(p.p1.xy));
 }else{
 precise vec2 uv;
 if((p.meta.z&65536u)==0u){precise vec2 d=vec2(q)+.5-p.p1.xy;uv=vec2(divide(p.p0.w*d.x-p.p0.y*d.y,p.p1.z),divide(-p.p0.z*d.x+p.p0.x*d.y,p.p1.z));if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(p.meta.xy))))return;}
 else{
  vec2 a=p.p0.xy,b=p.p1.xy,c=p.p2.xy,point=vec2(q)+.5;
  precise vec3 e=vec3(edge(b,c,point),edge(c,a,point),edge(a,b,point));
  if(any(lessThan(e,vec3(0)))||(e.x==0&&!included(b,c))||(e.y==0&&!included(c,a))||(e.z==0&&!included(a,b)))return;
  precise vec3 t=vec3(divide(divide(e.x,p.p3.w),p.p3.x),divide(divide(e.y,p.p3.w),p.p3.y),divide(divide(e.z,p.p3.w),p.p3.z));precise float inverse=t.x+t.y+t.z;
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
 for(uint item=0;item<count;++item){
  applyDraw(draws[tiles[first+item]],q,index,color);
  if(item+1u==saveAfter)destination[prefixIndex]=pack(color);
 }
 destination[index]=pack(color);
}
