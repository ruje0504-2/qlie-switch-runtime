// SPDX-License-Identifier: GPL-2.0-or-later
vec2 bitmapPosition(Draw p,ivec2 q){
 precise vec2 d=vec2(q)+.5-p.p1.xy;
 return vec2(divide(p.p0.w*d.x,p.p1.z),divide(p.p0.x*d.y,p.p1.z));
}

float horizontalTap(Draw p,int row,ivec2 cols,float fx){
 uint address=p.colors.z+uint(row)*p.meta.x;
 precise float value=float(source[address+uint(cols.x)]&255u);
 if(fx!=0.0)value=value*(1.0-fx)+float(source[address+uint(cols.y)]&255u)*fx;
 return value;
}

// Keep the original bilinear order while sampling one channel at a time.
// Each invocation is independent, including when several raster batches run.
uint fogGray(Draw p,ivec2 q){
 precise vec2 f=bitmapPosition(p,q)-.5;
 ivec2 i=ivec2(floor(f)),last=ivec2(p.meta.xy)-1;
 f-=vec2(i);
 ivec2 lo=clamp(i,ivec2(0),last),hi=clamp(i+1,ivec2(0),last);
 ivec2 cols=ivec2(lo.x,hi.x);
 precise float value=horizontalTap(p,lo.y,cols,f.x);
 if(f.y!=0.0)value=value*(1.0-f.y)+horizontalTap(p,hi.y,cols,f.x)*f.y;
 return uint(floor(value+.5));
}
