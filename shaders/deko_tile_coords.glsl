// SPDX-License-Identifier: GPL-2.0-or-later
// For a scaled bitmap, u is constant down a column and v across a row.
// For a vertical projected edge, its signed area and its perspective weight
// are constant down a column. Keep the same precision and division order.
const uint AxisAligned=1u,EdgeBC=2u,EdgeCA=3u,EdgeAB=4u;
shared vec2 tileCoords[16];

uint tileCoordMode(Draw p,ivec2 origin){
 if((p.meta.z>>20)!=0u||(p.meta.z&524288u)!=0u)return 0u;
 if(any(greaterThan(p.rect.xy,origin))||any(lessThan(p.rect.zw,origin+16)))return 0u;
 if((p.meta.z&65536u)==0u)return p.p0.y==0.0&&p.p0.z==0.0?AxisAligned:0u;
 if(p.p1.x==p.p2.x)return EdgeBC;
 if(p.p2.x==p.p0.x)return EdgeCA;
 if(p.p0.x==p.p1.x)return EdgeAB;
 return 0u;
}

void prepareTileCoord(Draw p,ivec2 origin,uint mode){
 uint lane=gl_LocalInvocationID.x;
 precise vec2 point=vec2(origin+ivec2(int(lane)))+0.5;
 if(mode==AxisAligned){
  precise vec2 delta=point-p.p1.xy;
  tileCoords[lane]=vec2(divide(p.p0.w*delta.x,p.p1.z),divide(p.p0.x*delta.y,p.p1.z));
 }else{
  vec2 a=mode==EdgeBC?p.p1.xy:mode==EdgeCA?p.p2.xy:p.p0.xy;
  vec2 b=mode==EdgeBC?p.p2.xy:mode==EdgeCA?p.p0.xy:p.p1.xy;
  float depth=mode==EdgeBC?p.p3.x:mode==EdgeCA?p.p3.y:p.p3.z;
  precise float e=edge(a,b,point);
  tileCoords[lane]=vec2(e,divide(divide(e,p.p3.w),depth));
 }
}
